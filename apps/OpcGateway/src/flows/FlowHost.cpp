#include "FlowHost.h"
#include <condition_variable>
#include <jde/fwk/co/AllAwait.h>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/fwk/exceptions/refuse.h>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/settings.h>
#include <jde/access/AccessListener.h>
#include <jde/access/Authorize.h>
#include "../async/ConnectAwait.h"
#include "../GatewayAppClient.h"
#include "../hist/HistHost.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };
	constexpr uint DefaultQueue{ 1000 };
	constexpr uint MaxKey{ 256 };//flow_contexts' node_id and name.
	using Flows::ENodeType;

	//Until the sources and sinks are built (docs/flows/plan.md, phase 3), what a hosted node is made as:  a source that
	//emits nothing, and a sink whose message is its error, but a link to another flow, which ends the message.
	struct PendingSource final : Flows::ISource{
		α Start()ε->void override{ SetStatus( {"grey", "ring", "Not built yet:  it emits nothing."} ); }
	};
	struct PendingSink final : Flows::ISink{
		α Start()ε->void override{
			SetStatus( {"grey", "ring", Def().Type==ENodeType::LinkOut ? "Not built yet:  nothing reaches the other flow." : "Not built yet:  a message it takes is its error."} );
		}
		α Receive( Flows::Message& )ε->void override{
			THROW_IF( Def().Type!=ENodeType::LinkOut, "A '{}' node is not built yet.", Flows::ToString(Def().Type) );
		}
	};
	struct FlowNodes final : Flows::IHost{
		α Source( const Flows::NodeDef& )ε->up<Flows::ISource> override{ return mu<PendingSource>(); }
		α Sink( const Flows::NodeDef& )ε->up<Flows::ISink> override{ return mu<PendingSink>(); }
	};

	struct FlowHost::HaltAwait final : AnyVoidAwait{
		HaltAwait( sp<Run> run, optional<Flows::Fault> why={}, SRCE )ι:AnyVoidAwait{ sl }, _run{ move(run) }, _why{ move(why) }{}
	protected:
		α Suspend()ι->void override{
			auto run = _run;//the resume may end this await, and the run with it, inside Stop.
			if( _why )
				run->Flow.Stop( move(*_why), [this]{ Resume(); } );
			else
				run->Flow.Stop( [this]{ Resume(); } );
		}
	private:
		sp<Run> _run;
		optional<Flows::Fault> _why;
	};

	struct FlowHost::SetAwait final : AnyAwait<bool>{
		SetAwait( sp<Run> run, sv node, sv key, jvalue value, SRCE )ι:AnyAwait<bool>{ sl }, _run{ move(run) }, _node{ node }, _key{ key }, _value{ move(value) }{}
	protected:
		α Suspend()ι->void override{
			auto run = _run;//the resume may end this await.
			run->Flow.SetContext( _node, _key, move(_value), [this]( bool set ){ Resume( move(set) ); } );
		}
	private:
		sp<Run> _run;
		sv _node, _key;
		jvalue _value;
	};

	Ω drops( const vector<Flows::NodeCounts>& counts )ι->uint{
		uint y{};
		for( let& node : counts )
			y += node.Drops;
		return y;
	}
	//A refusal as its exception:  `Node '<id>':  <why>`, the id for the editor to mark.
	Ω refused( const FlowRefusal& refusal, SL sl )ι->Exception{
		let& fault = refusal.Fault;
		return Exception{ sl, ExceptionArgs{refusal.Status}, "{}{}", fault.NodeId.empty() ? string{} : Ƒ("Node '{}':  ", fault.NodeId), fault.Message };
	}
	Ω unstarted( sv why )ι->Flows::Fault{ return { {}, Ƒ("It could not be started:  {}", why) }; }

	α FlowHost::Instance()ι->FlowHost&{
		static FlowHost instance;
		return instance;
	}
	α FlowHost::Start( sp<Hist::IClock> clock )ε->void{
		let flows = Settings::FindObject( "/gateway/flows" );
		auto queue = Settings::FindNumber<uint>( "/gateway/flows/queue" ).value_or( DefaultQueue );
		if( !queue ){
			WARNT( ELogTags::Settings, "'/gateway/flows/queue' of 0 is not allowed:  a node's input holds a message at least.  Using {}.", DefaultQueue );
			queue = DefaultQueue;
		}
		bool observe;
		{
			lg _{ _mutex };
			_stopped = false;
			_configured = flows!=nullptr;
			_clock = clock ? move( clock ) : Hist::SystemClock();
			_ids = ms<Flows::MessageIds>( _clock->Now() );
			_nodes = ms<FlowNodes>();
			_path = Settings::FindPath( "/gateway/flows/path" ).value_or( Process::AppDataFolder()/"logs"/"flows"/"opc-gateway" );
			_queue = queue;
			_global = nullptr;
			_flows.clear();
			observe = _configured && !_observer;
			if( observe )
				_observer = 0;
		}
		if( !flows ){
			INFO( "No '/gateway/flows':  this gateway runs no flows." );
			return;
		}
		if( observe ){
			let handle = Access::AccessListener::Observe( []{ Instance().Recheck(); } );
			lg _{ _mutex };
			_observer = handle;
		}
		struct LoadAwait final : AnyVoidAwait{
			LoadAwait( FlowHost& host )ι:_host{ host }{}
			α Suspend()ι->void override{ _host.Loading( *this ); }
			FlowHost& _host;
		};
		BlockAwait( LoadAwait{*this} );
	}
	α FlowHost::Stop()ι->void{
		vector<sp<Run>> runs;
		optional<uint> observer;
		bool configured;
		{
			lg _{ _mutex };
			_stopped = true;
			configured = _configured;
			observer = std::exchange( _observer, nullopt );
			for( auto&& [_,entry] : _flows ){
				if( auto timer = std::exchange(entry.Retry, 0); timer )
					_clock->Cancel( timer );
				if( entry.Live )
					runs.push_back( move(entry.Live) );
			}
		}
		if( observer )
			Access::AccessListener::Unobserve( *observer );
		struct Wait final{ std::mutex Mutex; std::condition_variable Done; uint Left; };
		auto wait = ms<Wait>();
		wait->Left = runs.size();
		for( auto& run : runs ){
			run->Flow.Stop( [wait]{
				{
					lg _{ wait->Mutex };
					--wait->Left;
				}
				wait->Done.notify_all();
			} );
		}
		{
			std::unique_lock l{ wait->Mutex };
			if( !wait->Done.wait_for(l, 10s, [&]{ return !wait->Left; }) )
				WARN( "{} flows had not stopped after 10 s.", wait->Left );
		}
		if( !configured )
			return;
		struct FlushAwait final : AnyVoidAwait{
			FlushAwait( FlowHost& host )ι:_host{ host }{}
			α Suspend()ι->void override{ _host.Flushing( this ); }
			FlowHost& _host;
		};
		try{
			BlockAwait( FlushAwait{*this} );
		}
		catch( const std::exception& )
		{}
	}
	α FlowHost::Enabled()Ι->bool{
		lg _{ _mutex };
		return _configured && !_stopped;
	}
	α FlowHost::TestEnabled( SL sl )Ε->void{
		bool configured, stopped;
		{
			lg _{ _mutex };
			configured = _configured;
			stopped = _stopped;
		}
		const ExceptionArgs args{ EHttpStatus::ServiceUnavailable };
		if( stopped )
			throw Exception{ sl, args, "The gateway's flows have stopped." };
		if( !configured )
			throw Exception{ sl, args, "This gateway runs no flows:  it has no /gateway/flows block." };
	}
	α FlowHost::Find( FlowPK id )Ι->optional<State>{
		sp<Run> live;
		State y;
		{
			lg _{ _mutex };
			let p = _flows.find( id );
			if( p==_flows.end() )
				return nullopt;
			y.Row = p->second.Row;
			live = p->second.Live;
		}
		if( live ){
			y.Running = live->Flow.Running();
			y.Fault = live->Flow.Fault();
		}
		return y;
	}
	α FlowHost::Status( FlowPK id, sv node )Ι->Flows::NodeStatus{
		sp<Run> live;
		{
			lg _{ _mutex };
			if( let p = _flows.find(id); p!=_flows.end() )
				live = p->second.Live;
		}
		return live ? live->Flow.Status( node ) : Flows::NodeStatus{};
	}
	α FlowHost::Counts( FlowPK id )Ι->vector<Flows::NodeCounts>{
		sp<Run> live;
		{
			lg _{ _mutex };
			if( let p = _flows.find(id); p!=_flows.end() )
				live = p->second.Live;
		}
		return live ? live->Flow.Counts() : vector<Flows::NodeCounts>{};
	}
	α FlowHost::Links( const Flows::Graph& graph )Ι->flat_set<string>{
		flat_set<string> y;
		for( let& node : graph.Nodes ){
			if( node.Type==ENodeType::LinkIn )
				y.emplace( Json::FindDefaultSV(node.Props, "name") );
		}
		return y;
	}
	α FlowHost::Receivers( sv link, FlowPK but )Ι->vector<FlowRow>{
		vector<FlowRow> y;
		lg _{ _mutex };
		for( let& [id,entry] : _flows ){
			if( id!=but && !entry.Row.Deleted && entry.Links.contains(string{link}) )
				y.push_back( entry.Row );
		}
		return y;
	}

	α FlowHost::Loading( AnyVoidAwait& await )ι->VoidTask{
		bool resumed{};
		try{
			auto lock = co_await _edits.Lock();
			auto [rows, contexts, graphs] = co_await All( SelectFlows({.IncludeDeleted=true}), SelectFlowContexts(nullopt), DS()->SelectAsync(FlowSql::CurrentGraphs()) );
			ContextValues values;
			for( auto& row : contexts )
				values[{string{}, move(row.Name)}] = move( row.Value );
			Unwritten( 0, values );
			jobject global;
			for( auto&& [key, text] : values ){
				try{
					if( text )
						global[key.second] = Json::ParseValue( *text );
				}
				catch( const std::exception& e ){
					WARN( "The global context's '{}' does not read, and is left out:  {}", key.second, e.what() );
				}
			}
			vector<FlowRow> enabled;
			flat_set<FlowPK> launching;
			for( let& row : rows ){
				if( !row.Deleted && row.Version && row.Enabled ){
					enabled.push_back( row );
					launching.emplace( row.Id );
				}
			}
			flat_map<FlowPK,flat_set<string>> links;//a link into a flow that is disabled, or deleted, still names it:  Launch reads the rest.
			for( auto& graph : graphs ){
				let id = graph.Get<uint32>( 0 );
				if( launching.contains(id) )
					continue;
				try{
					if( let read = Flows::Graph::Read(Json::ParseValue(graph.TakeString(1)), true); read )
						links.emplace( id, Links(*read) );
				}
				catch( const std::exception& )
				{}
			}
			{
				lg _{ _mutex };
				_global = ms<Flows::GlobalStore>( move(global), [this]( sv key, const jvalue* value ){ ContextChanged( nullopt, {}, key, value ); } );
				for( let& row : rows )
					_flows[row.Id].Row = row;
				for( auto&& [id, names] : links )
					_flows[id].Links = move( names );
			}
			resumed = true;
			await.Resume();//Start returns here:  the flows start behind it, under _edits still, so no edit of one comes between.
			uint started{};
			for( auto& row : enabled ){
				let id = row.Id;
				try{
					co_await Launch( move(row) );
				}
				catch( const std::exception& e ){
					ERR( "Flow {} could not be started:  {}", id, e.what() );
				}
				if( let state = Find(id); state && state->Running )
					++started;
			}
			INFO( "Flows:  {} of {} enabled flows started, of {} flows.", started, enabled.size(), rows.size() );
		}
		catch( runtime_error& e ){
			if( !resumed )
				await.ResumeExp( move(e) );
			else
				ERR( "The flows could not be started:  {}", e.what() );
		}
	}

	α FlowHost::Refused( const FlowRow& row, Flows::Graph graph, uint32 version, UserPK deployer, Flows::Fault fault, bool retry )ι->void{
		auto run = ms<Run>( move(graph), Flows::Flow::Config{.Name=row.Slug, .Clock=Time()}, version, deployer, Spellings{} );
		run->Flow.Stop( move(fault) );//which logs it, an Error.
		lg _{ _mutex };
		if( auto p = _flows.find(row.Id); p!=_flows.end() && !_stopped ){
			p->second.Live = move( run );
			if( retry && !p->second.Retry )
				p->second.Retry = _clock->Schedule( RetryInterval, [this, id=row.Id]{ Retry( id ); } );
		}
	}
	α FlowHost::Launch( FlowRow row )ι->AnyVoidTask{
		let id = row.Id;
		let number = row.Version.value_or( 0 );
		{
			lg _{ _mutex };
			if( auto p = _flows.find(id); p!=_flows.end() ){
				if( auto timer = std::exchange(p->second.Retry, 0); timer )
					_clock->Cancel( timer );
			}
		}
		vector<FlowVersionRow> versions;
		string failed;//a statement's error:  no co_await in a handler.
		try{
			versions = co_await Any( SelectFlowVersion(id, number) );
		}
		catch( const std::exception& e ){
			failed = e.what();
		}
		if( failed.size() ){
			Refused( row, {}, number, {}, unstarted(failed), true );
			co_return;
		}
		if( versions.empty() ){
			Refused( row, {}, number, {}, {{}, Ƒ("Version {} has no flow_versions row.", number)} );
			co_return;
		}
		auto& version = versions.front();
		std::expected<Flows::Graph,Flows::Fault> graph{ std::unexpect };
		try{
			graph = Flows::Graph::Read( Json::ParseValue(version.Graph), true );
		}
		catch( const std::exception& e ){
			graph = std::unexpected{ Flows::Fault{{}, Ƒ("Version {}'s graph does not read:  {}", number, e.what())} };
		}
		if( !graph ){
			Refused( row, {}, number, version.Deployer, move(graph.error()) );
			co_return;
		}
		{
			lg _{ _mutex };
			if( auto p = _flows.find(id); p!=_flows.end() )
				p->second.Links = Links( *graph );
		}
		Spellings spelled;
		optional<FlowRefusal> refusal;
		try{
			refusal = co_await Admit( id, *graph, version.Deployer, nullptr, false, spelled );
		}
		catch( const std::exception& e ){
			refusal = FlowRefusal{ {{}, Ƒ("It could not be checked:  {}", e.what())}, EHttpStatus::ServiceUnavailable, true };
		}
		if( refusal ){
			Refused( row, move(*graph), number, version.Deployer, refusal->Fault, refusal->Unknown );
			co_return;
		}
		co_await Begin( move(row), move(*graph), number, version.Deployer, move(spelled) );
	}
	α FlowHost::Retry( FlowPK id )ι->VoidTask{
		try{
			auto lock = co_await _edits.Lock();
			optional<FlowRow> row;
			{
				lg _{ _mutex };
				let p = _flows.find( id );
				if( _stopped || p==_flows.end() || (p->second.Live && p->second.Live->Flow.Running()) )
					co_return;
				p->second.Retry = 0;
				row = p->second.Row;
			}
			if( row->Enabled && !row->Deleted && row->Version )
				co_await Launch( move(*row) );
		}
		catch( const std::exception& e ){
			ERR( "Flow {} could not be started again:  {}", id, e.what() );
		}
	}
	α FlowHost::Begin( FlowRow row, Flows::Graph graph, uint32 version, UserPK deployer, Spellings spelled )ι->AnyVoidTask{
		let id = row.Id;
		co_await WriteContext();//what the run before this one left to write;  what still fails is laid over its row (Unwritten).
		vector<FlowContextRow> rows;
		string failed;
		try{
			rows = co_await Any( SelectFlowContexts(id) );
		}
		catch( const std::exception& e ){
			failed = e.what();
		}
		if( failed.size() ){
			Refused( row, move(graph), version, deployer, unstarted(failed), true );
			co_return;
		}
		ContextValues values;
		for( auto& context : rows )
			values[{move(context.Node), move(context.Name)}] = move( context.Value );
		Unwritten( id, values );
		Flows::Flow::Config config{ .Name=row.Slug };
		{
			lg _{ _mutex };
			config.Clock = _clock;
			config.Host = _nodes;
			config.Ids = _ids;
			config.Global = _global;
			config.Queue = _queue;
		}
		for( auto&& [key, text] : values ){
			let& [node, name] = key;
			try{
				if( !text )
					continue;
				auto value = Json::ParseValue( *text );
				if( node.empty() )
					config.Context[name] = move( value );
				else
					config.NodeContext[node][name] = move( value );
			}
			catch( const std::exception& e ){
				WARN( "[{}]The context's '{}' does not read, and is left out:  {}", row.Slug, name, e.what() );
			}
		}
		config.ContextChanged = [this, id]( sv node, sv key, const jvalue* value ){ ContextChanged( id, node, key, value ); };
		auto run = ms<Run>( move(graph), move(config), version, deployer, move(spelled) );
		let started = run->Flow.Start();
		bool kept;
		{
			lg _{ _mutex };
			auto p = _flows.find( id );
			kept = p!=_flows.end() && !_stopped;
			if( kept )
				p->second.Live = run;
		}
		if( !kept )
			run->Flow.Stop();
		else if( started )
			INFO( "[{}]Flow '{}' started from version {}.", row.Slug, row.Name, version );
	}

	α FlowHost::Admitting( AdmitAwait& await )ι->VoidTask{
		let sl = await.Source();
		try{
			let acl = AppClient()->Acl();
			let local = await.Deploying ? Links( await.Graph ) : flat_set<string>{};
			optional<FlowRefusal> refusal;
			for( let& node : await.Graph.Nodes ){
				try{
					if( node.Type==ENodeType::OpcIn || node.Type==ENodeType::OpcOut ){
						let slug = Json::FindDefaultSV( node.Props, "connection" );
						REFUSE_IF( slug.empty(), "`connection` is required, a connection's slug." );
						let named = node.Props.if_contains( "node" );
						REFUSE_IF( !named || named->is_null(), "`node` is required, the node's id." );
						let ex = ParseHistNode( *named, sl );
						auto connections = co_await Any( ServerCnnctnAwait{DB::Key{string{slug}}} );
						REFUSE_IF( connections.empty(), "'{}' is no connection of this gateway.", slug );
						let connection = connections.front().Id;
						string spelled;
						if( ex.namespaceUri.length )
							spelled = HistNodeText( ex );
						else if( let p = await.Spelled.find(node.Id); p!=await.Spelled.end() )
							spelled = p->second;
						else{
							spelled = co_await SpellAwait{ ExNodeId{ex}, connection, sl };
							await.Spelled.emplace( node.Id, spelled );
						}
						auto members = co_await Any( SelectHistGroupNodes({.Connection=connection, .Node=spelled}) );
						vector<HistGroupRow> groups;
						if( members.size() )
							groups = co_await Any( SelectHistGroups({.Key=DB::Key{(uint)members.front().Group}}) );//a deleted group holds nothing.
						let what = Ƒ( "'{}' on '{}'", ex.ToString(), slug );
						if( node.Type==ENodeType::OpcOut ){
							if( groups.empty() )
								throw Exception{ sl, ExceptionArgs{EHttpStatus::Forbidden}, "{} is in no group:  a flow writes a node under Update on the enforced access row of a group that holds it.", what };
							if( let allowed = acl->CheckEnforced("gateway", "histGroups", groups.front().Guid, Access::ERights::Update, await.Deployer, sl); !allowed )
								throw Exception{ sl, ExceptionArgs{allowed.error().HttpStatus()}, "A write to {} needs Update on group '{}':  {}", what, groups.front().Name, allowed.error().what() };
						}
						else if( groups.size() ){
							if( let allowed = acl->Check("gateway", "histGroups", groups.front().Guid, Access::ERights::Read, await.Deployer, sl); !allowed )
								throw Exception{ sl, ExceptionArgs{allowed.error().HttpStatus()}, "{} is group '{}'s, which needs Read:  {}", what, groups.front().Name, allowed.error().what() };
						}
						else if( await.Deploying ){//the server's own answer, under the deployer's session:  the read a page of theirs would make.
							REFUSE_IF( !await.Session, "{} is in no group, so its server admits it:  the deploy reads it once under the deployer's own session, and this deploy has none.", what );
							ConnectAwait connect{ string{slug}, await.Session->SessionId, await.Session->UserPK, sl };//an lvalue:  ConnectAwait is neither copied nor moved.
							auto client = co_await Any( connect );
							optional<NodeId> id{ NodeId{ex.nodeId} };
							if( ex.namespaceUri.length ){
								let namespaces = co_await Any( NamespacesAwait{client} );
								id = namespaces.ToNodeId( ex );
								REFUSE_IF( !id, "{} names a namespace its server doesn't have.", what );
							}
							ReadValueAwait read{ {*id}, client };
							let values = co_await Any( read );
							let value = values.find( *id );
							let status = value==values.end() ? (StatusCode)UA_STATUSCODE_BADNODEIDUNKNOWN : value->second.hasStatus ? value->second.status : (StatusCode)UA_STATUSCODE_GOOD;
							if( UA_StatusCode_isBad(status) )
								throw Exception{ sl, ExceptionArgs{EHttpStatus::Forbidden}, "{} is in no group, and its server refused the read under the deployer's own session:  {}.", what, UAException::Message(status) };
						}
					}
					else if( node.Type==ENodeType::GroupIn ){
						let named = node.Props.if_contains( "group" );
						optional<DB::Key> key;
						if( named && named->is_string() && !named->get_string().empty() )
							key = DB::Key{ string{named->get_string()} };
						else if( let id = Json::FindNumber<uint32>(node.Props, "group"); id )
							key = DB::Key{ (uint)*id };
						REFUSE_IF( !key, "`group` is required, a historian group's slug or id." );
						let label = key->IsPK() ? std::to_string( key->PK() ) : key->NK();
						auto groups = co_await Any( SelectHistGroups({.Key=*key}) );
						REFUSE_IF( groups.empty() || !HistHost::Instance().Find(groups.front().Id), "Group '{}' isn't one this gateway historizes.", label );
						if( let allowed = acl->Check("gateway", "histGroups", groups.front().Guid, Access::ERights::Read, await.Deployer, sl); !allowed )
							throw Exception{ sl, ExceptionArgs{allowed.error().HttpStatus()}, "Group '{}' needs Read:  {}", groups.front().Name, allowed.error().what() };
					}
					else if( node.Type==ENodeType::LinkOut && await.Deploying ){
						let name = Json::FindDefaultSV( node.Props, "name" );
						if( local.contains(string{name}) )
							continue;
						let receivers = Receivers( name, await.Flow );
						REFUSE_IF( receivers.empty(), "No `link in` of this flow or of another is named '{}'.", name );
						for( let& to : receivers ){
							if( let allowed = acl->Check("gateway", "flows", to.Guid, Access::ERights::Update, await.Deployer, sl); !allowed )
								throw Exception{ sl, ExceptionArgs{allowed.error().HttpStatus()}, "A link to flow '{}' needs Update on it, as an inject into it does:  {}", to.Name, allowed.error().what() };
						}
					}
				}
				catch( const std::exception& e ){
					using enum EHttpStatus;
					let known = dynamic_cast<const Exception*>( &e );
					let status = known ? known->HttpStatus() : InternalServerError;
					if( status==BadRequest || status==Forbidden || status==Unauthorized || status==NotFound )
						refusal = FlowRefusal{ {node.Id, e.what()}, status };
					else
						refusal = FlowRefusal{ {node.Id, Ƒ("It could not be checked:  {}", e.what())}, ServiceUnavailable, true };
				}
				if( refusal )
					break;
			}
			await.Resume( move(refusal) );
		}
		catch( runtime_error& e ){
			await.ResumeExp( move(e) );
		}
	}

	α FlowHost::Deploying( DeployAwait& await )ι->VoidTask{
		let sl = await.Source();
		try{
			let by = await.Deployer.UserPK();
			auto lock = co_await _edits.Lock();
			auto rows = co_await Any( SelectFlows({.Key=DB::Key{(uint)await.Flow}, .IncludeDeleted=true}) );
			//Asked by the row, whether the flow is hosted or not, and for a flow that isn't there by the table's, so a caller learns nothing of an id before the gate.
			AppClient()->Acl()->Test( "gateway", "flows", rows.empty() ? string{} : rows.front().Guid, Access::ERights::Update, by, sl );
			TestEnabled( sl );
			if( rows.empty() )
				throw Exception{ sl, ExceptionArgs{EHttpStatus::NotFound}, "Flow {} not found.", await.Flow };
			auto row = move( rows.front() );
			REFUSE_IF( row.Deleted, "Flow '{}' is deleted:  restore it before it is deployed.", row.Name );
			jvalue json;
			try{
				json = await.Graph.is_string() ? Json::ParseValue( sv{await.Graph.get_string()} ) : move( await.Graph );
			}
			catch( const std::exception& e ){
				REFUSE( "graph is a flow's graph, as json or its text:  {}", e.what() );
			}
			auto graph = Flows::Graph::Read( json, true );
			if( !graph )
				throw refused( {move(graph.error())}, sl );
			for( let& node : graph->Nodes ){
				if( node.Id.size()>MaxKey )
					throw refused( {{node.Id, Ƒ("Its id is {} characters, over the {} a context row holds.", node.Id.size(), MaxKey)}}, sl );
			}
			Spellings spelled;
			if( let refusal = co_await Admit(row.Id, *graph, by, dynamic_pointer_cast<Web::Server::SessionInfo>(await.Deployer.Session()), true, spelled, sl); refusal )
				throw refused( *refusal, sl );

			let last = co_await Any( DS()->SelectAsync(FlowSql::LastVersion(row.Id)) );
			let version = ( last.empty() || last.front().IsNull(0) ? 0 : last.front().Get<uint32>(0) )+1;
			let writer = HistHost::Instance().Writer( by );
			let now = Time()->Now();
			co_await Any( DS()->Execute(FlowSql::InsertVersion(row.Id, version, serialize(json), now, by, writer.UserName, await.Comment)) );
			co_await Any( DS()->Execute(FlowSql::SetVersion(row.Id, version)) );
			row.Version = version;
			sp<Run> live;
			{
				lg _{ _mutex };
				auto& entry = _flows[row.Id];
				entry.Row = row;
				entry.Links = Links( *graph );
				live = move( entry.Live );
				if( auto timer = std::exchange(entry.Retry, 0); timer )
					_clock->Cancel( timer );
			}
			Deployed y{ .Version=version, .Time=now, .UserName=writer.UserName, .Running=false, .Fault=nullopt, .Dropped=0 };
			if( live && live->Flow.Running() ){
				let before = drops( live->Flow.Counts() );
				co_await HaltAwait{ live };
				y.Dropped = drops( live->Flow.Counts() )-before;
			}
			if( row.Enabled ){
				co_await Begin( row, move(*graph), version, by, move(spelled) );//a start that fails leaves its Fault, which the answer carries, and is tried again.
				if( let state = Find(row.Id); state ){
					y.Running = state->Running;
					y.Fault = state->Fault;
				}
			}
			INFO( "[{}]Flow '{}' deployed as version {} by '{}'{}.", row.Slug, row.Name, version, writer.UserName, row.Enabled ? "" : ", and is disabled" );
			lock.unlock();
			await.Resume( move(y) );
		}
		catch( runtime_error& e ){
			await.ResumeExp( move(e) );
		}
	}
	α FlowHost::Inject( FlowPK id, sv node, const jobject* message, SL sl )ε->void{
		TestEnabled( sl );
		sp<Run> live; string name;
		{
			lg _{ _mutex };
			let p = _flows.find( id );
			if( p==_flows.end() || p->second.Row.Deleted )
				throw Exception{ sl, ExceptionArgs{EHttpStatus::NotFound}, "Flow {} not found.", id };
			live = p->second.Live;
			name = p->second.Row.Name;
		}
		if( !live || !live->Flow.Running() ){
			let fault = live ? live->Flow.Fault() : nullopt;
			throw Exception{ sl, ExceptionArgs{EHttpStatus::Conflict}, "Flow '{}' is not running{}.", name, fault ? Ƒ(":  {}", fault->Message) : string{} };
		}
		try{
			live->Flow.Inject( node, message );
		}
		catch( const std::exception& e ){
			REFUSE( "{}", e.what() );
		}
	}
	α FlowHost::SetContext( optional<FlowPK> flow, string node, string key, jvalue value, SL sl )ι->AnyVoidTask{
		TestEnabled( sl );
		REFUSE_IF( key.empty() || key.size()>MaxKey, "name is the key, of 1 to {} characters.", MaxKey );
		REFUSE_IF( !flow && node.size(), "node '{}' is a flow's:  it needs flow.", node );
		auto lock = co_await _edits.Lock();
		bool set{};
		if( !flow ){
			let global = Global();
			if( !global )
				throw Exception{ sl, ExceptionArgs{EHttpStatus::ServiceUnavailable}, "The gateway's flows have not loaded." };
			global->Set( key, jvalue{value} );
			set = true;
		}
		else{
			FlowRow row; sp<Run> live;
			{
				lg _{ _mutex };
				let p = _flows.find( *flow );
				if( p==_flows.end() )
					throw Exception{ sl, ExceptionArgs{EHttpStatus::NotFound}, "Flow {} not found.", *flow };
				row = p->second.Row;
				live = p->second.Live;
			}
			REFUSE_IF( row.Deleted, "Flow '{}' is deleted:  restore it before its context is set.", row.Name );
			if( node.size() ){
				bool known{};
				if( live )
					known = live->Graph.IndexOf( node ).has_value();
				else if( row.Version ){
					auto versions = co_await Any( SelectFlowVersion(*flow, *row.Version) );
					try{
						if( let graph = versions.empty() ? std::unexpected{ Flows::Fault{} } : Flows::Graph::Read( Json::ParseValue(versions.front().Graph), true ); graph )
							known = graph->IndexOf( node ).has_value();
					}
					catch( const std::exception& )
					{}
				}
				REFUSE_IF( !known, "'{}' is no node of flow '{}'s current version.", node, row.Name );
			}
			if( live && live->Flow.Running() )
				set = co_await SetAwait{ live, node, key, value, sl };
		}
		if( !set )//no run to tell the host of it.
			ContextChanged( flow, node, key, value.is_null() ? nullptr : &value );
		co_await WriteContext();
	}

	α FlowHost::Changed( DB::Key key )ι->AnyVoidTask{
		if( !Enabled() )
			co_return;
		auto lock = co_await _edits.Lock();
		auto rows = co_await Any( SelectFlows({.Key=key, .IncludeDeleted=true}) );
		if( rows.empty() )
			co_return;
		auto& row = rows.front();
		let wanted = row.Enabled && !row.Deleted && row.Version;
		sp<Run> live;
		{
			lg _{ _mutex };
			auto& entry = _flows[row.Id];
			entry.Row = row;
			live = entry.Live;
			if( !wanted ){
				entry.Live = nullptr;
				if( auto timer = std::exchange(entry.Retry, 0); timer )
					_clock->Cancel( timer );
			}
		}
		if( wanted && !live )
			co_await Launch( move(row) );
		else if( !wanted && live && live->Flow.Running() ){
			co_await HaltAwait{ live };
			INFO( "[{}]Flow '{}' stopped:  it is {}.", row.Slug, row.Name, row.Deleted ? "deleted" : "disabled" );
		}
	}
	α FlowHost::Purging( FlowPK id, optional<FlowRow>& row )ι->AnyVoidTask{
		auto lock = co_await _edits.Lock();
		auto rows = co_await Any( SelectFlows({.Key=DB::Key{(uint)id}, .IncludeDeleted=true}) );
		if( rows.empty() )
			co_return;
		row = move( rows.front() );
		sp<Run> live;
		{
			lg _{ _mutex };
			if( auto p = _flows.find(id); p!=_flows.end() ){
				live = move( p->second.Live );
				if( auto timer = std::exchange(p->second.Retry, 0); timer )
					_clock->Cancel( timer );
				_flows.erase( p );
			}
		}
		if( live && live->Flow.Running() )
			co_await HaltAwait{ live };
		co_await WriteContext();
	}
	α FlowHost::Purged( FlowRow row )ι->AnyVoidTask{
		fs::path path;
		{
			auto lock = co_await _edits.Lock();
			sp<Run> live;
			{
				lg _{ _mutex };
				path = _path;
				if( auto p = _flows.find(row.Id); p!=_flows.end() ){//Changed's, for an edit of the row while the statement ran.
					live = move( p->second.Live );
					if( auto timer = std::exchange(p->second.Retry, 0); timer )
						_clock->Cancel( timer );
					_flows.erase( p );
				}
			}
			if( live && live->Flow.Running() )
				co_await HaltAwait{ live };
			Forget( row.Id );
		}
		//The slug is held to a file name's characters at its create (FlowMAwait);  a row written another way is not followed out of the directory.
		if( !path.empty() && !row.Slug.empty() && row.Slug.find_first_of("/\\")==string::npos && row.Slug!="." && row.Slug!=".." ){
			std::error_code ec;
			if( fs::remove_all(path/row.Slug, ec); ec )
				WARN( "[{}]The flow's trace directory wasn't removed with it:  {}", row.Slug, ec.message() );
		}
		//Its own access row and the grants on it, through the AppServer, best effort, as a group's are purged (HistHost::GroupPurged).
		try{
			co_await Any( *AppClient()->Query<jvalue>("purgeResource( schemaName:\"gateway\", slug:\"flows\", criteria:$criteria )", {{"criteria", row.Guid}}) );
			INFO( "[{}]The flow's access row purged with it.", row.Slug );
		}
		catch( const runtime_error& e ){
			if( let p = dynamic_cast<const Exception*>(&e); !p || p->HttpStatus()!=EHttpStatus::NotFound )
				WARN( "[{}]The flow's access row wasn't purged with it, and stays, inert:  {}", row.Slug, e.what() );
		}
	}

	α FlowHost::GroupChanged( HistGroupPK group )ι->void{
		TRACE( "Group {} changed:  the running flows are admitted again.", group );
		Recheck();
	}
	α FlowHost::Recheck()ι->void{
		{
			lg _{ _mutex };
			if( !_configured || _stopped )
				return;
			_recheck = true;
			if( _rechecking )
				return;
			_rechecking = true;
		}
		boost::asio::post( *Executor(), [this]{ Rechecking(); } );
	}
	α FlowHost::Rechecking()ι->VoidTask{
		for( ;; ){
			{
				lg _{ _mutex };
				if( !_recheck || _stopped ){
					_rechecking = false;
					co_return;
				}
				_recheck = false;
			}
			try{
				auto lock = co_await _edits.Lock();
				vector<std::pair<FlowRow,sp<Run>>> running;
				{
					lg _{ _mutex };
					for( let& [_,entry] : _flows ){
						if( entry.Live && entry.Live->Flow.Running() )
							running.emplace_back( entry.Row, entry.Live );
					}
				}
				for( auto& [row, run] : running ){
					auto refusal = co_await Admit( row.Id, run->Graph, run->Deployer, nullptr, false, run->Spelled );
					if( refusal && !refusal->Unknown )
						co_await HaltAwait{ run, move(refusal->Fault) };//which logs it, an Error.
					else if( refusal )
						WARN( "[{}]Flow '{}' runs on unchecked:  node '{}':  {}", row.Slug, row.Name, refusal->Fault.NodeId, refusal->Fault.Message );
				}
			}
			catch( const std::exception& e ){
				ERR( "The running flows could not be admitted again:  {}", e.what() );
			}
			++_rechecks;
		}
	}

	α FlowHost::ContextChanged( optional<FlowPK> flow, sv node, sv key, const jvalue* value )ι->void{
		if( key.size()>MaxKey ){
			WARN( "A context key of {} characters is not kept past a restart:  a row holds {}.", key.size(), MaxKey );
			return;
		}
		bool arm;
		{
			lg _{ _contextMutex };
			_pending.insert_or_assign( ContextKey{flow.value_or(0), string{node}, string{key}}, value ? optional<string>{ serialize(*value) } : nullopt );
			arm = !std::exchange( _contextArmed, true );
		}
		if( arm )
			Time()->Schedule( ContextDelay, [this]{ Flush(); } );
	}
	α FlowHost::Flush()ι->void{ Flushing( nullptr ); }
	α FlowHost::Flushing( AnyVoidAwait* await )ι->VoidTask{
		try{
			auto lock = co_await _edits.Lock();
			co_await WriteContext();
		}
		catch( const std::exception& e ){
			ERR( "The flows' context could not be written:  {}", e.what() );
		}
		if( await )
			await->Resume();
	}
	α FlowHost::WriteContext()ι->AnyVoidTask{
		flat_map<ContextKey,optional<string>> pending;
		{
			lg _{ _contextMutex };
			pending = std::exchange( _pending, {} );
			_contextArmed = false;
		}
		vector<ContextKey> written;
		vector<tuple<ContextKey,optional<string>,string>> failed;//the key, its value and why.
		for( auto&& [contextKey, value] : pending ){
			let& [flow, node, key] = contextKey;
			let scope = flow ? optional<FlowPK>{ flow } : nullopt;
			string error;
			try{
				co_await Any( DS()->Execute(FlowSql::DeleteContext(scope, node, key)) );
				if( value )
					co_await Any( DS()->Execute(FlowSql::InsertContext(scope, node, key, *value)) );
			}
			catch( const std::exception& e ){
				error = e.what();
			}
			if( error.empty() )
				written.push_back( contextKey );
			else
				failed.emplace_back( contextKey, move(value), move(error) );
		}
		let named = []( const ContextKey& key ){ return Ƒ( "'{}' of flow {}{}", get<2>(key), get<0>(key), get<1>(key).empty() ? string{} : Ƒ(", node '{}'", get<1>(key)) ); };
		vector<string> dropped;
		uint left{}; string first;
		bool arm{};
		{
			lg _{ _contextMutex };
			for( let& key : written )
				_contextFailures.erase( key );
			for( auto& [key, value, error] : failed ){
				if( written.size() && ++_contextFailures[key]>=ContextTries ){
					_contextFailures.erase( key );
					dropped.push_back( Ƒ("{}:  {}", named(key), error) );
					continue;
				}
				if( !left++ )
					first = Ƒ( "{} first:  {}", named(key), error );
				_pending.try_emplace( key, move(value) );//a value set since stands.
			}
			if( left )
				arm = !std::exchange( _contextArmed, true );
		}
		for( let& why : dropped )
			ERR( "A context value failed {} writes in which others were written, and is dropped:  {}", ContextTries, why );
		if( left )
			WARN( "{} of the flows' context values could not be written, and are tried again, {}", left, first );
		if( arm )
			Time()->Schedule( RetryInterval, [this]{ Flush(); } );
	}
	α FlowHost::Unwritten( FlowPK flow, ContextValues& values )Ι->void{
		lg _{ _contextMutex };
		for( auto p = _pending.lower_bound( ContextKey{flow, string{}, string{}} ); p!=_pending.end() && get<0>(p->first)==flow; ++p )
			values[{get<1>(p->first), get<2>(p->first)}] = p->second;
	}
	α FlowHost::Forget( FlowPK flow )ι->void{
		lg _{ _contextMutex };
		erase_if( _pending, [flow]( let& entry ){ return get<0>( entry.first )==flow; } );
		erase_if( _contextFailures, [flow]( let& entry ){ return get<0>( entry.first )==flow; } );
	}
}