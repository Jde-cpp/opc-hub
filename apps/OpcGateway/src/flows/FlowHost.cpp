#include "FlowHost.h"
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
#include "FlowFeed.h"
#include "FlowNodes.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };
	constexpr uint DefaultQueue{ 1000 };
	constexpr Duration DefaultLive{ 200ms };
	constexpr uint MaxKey{ 256 };//flow_contexts' node_id and name.
	constexpr uint MinMemory{ 256*1024 };//a sandbox's:  the runtime and a node, with room for a body.
	constexpr uint MinStack{ 32*1024 };
#ifdef _WIN32
	constexpr uint MaxStack{ 512*1024 };//under the executor's threads' 1 MB.
#else
	constexpr uint MaxStack{ 4*1024*1024 };//under the executor's threads' 8 MiB.
#endif
	using Flows::ENodeType;

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

	//Resumed off the flow's strand, which has its first messages to take.
	struct FlowHost::StartAwait final : AnyVoidAwait{
		StartAwait( sp<Run> run, SRCE )ι:AnyVoidAwait{ sl }, _run{ move(run) }{}
	protected:
		α Suspend()ι->void override{
			auto run = _run;//the resume may end this await.
			run->Flow.Start( [this]{ boost::asio::post( *Executor(), [this]{ Resume(); } ); } );
		}
	private:
		sp<Run> _run;
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

	struct FlowHost::EndAwait final : AnyAwait<RunEnd>{
		EndAwait( sp<ReplayRun> replay, SRCE )ι:AnyAwait<RunEnd>{ sl }, _replay{ move(replay) }{}
	protected:
		α Suspend()ι->void override{
			auto replay = _replay;//the resume may end this await.
			{
				lg _{ replay->Mutex };
				if( !replay->End ){
					replay->Waiting.push_back( [this, replay]{ Resume( RunEnd{*replay->End} ); } );
					return;
				}
			}
			Resume( RunEnd{*replay->End} );
		}
	private:
		sp<ReplayRun> _replay;
	};
	//The flow's trace written now, a replay's records with it.
	struct FlushAwait final : AnyVoidAwait{
		FlushAwait( sp<Flows::TraceStore> store, string slug, SRCE )ι:AnyVoidAwait{ sl }, _store{ move(store) }, _slug{ move(slug) }{}
	protected:
		α Suspend()ι->void override{
			auto store = _store;//the resume may end this await.
			store->Flush( _slug, [this]{ Resume(); } );
		}
	private:
		sp<Flows::TraceStore> _store;
		string _slug;
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

	//`/gateway/flows/function`:  a key that does not read, or is under what a sandbox needs, keeps its default;  a stack over
	//what the executor's threads have is the most they leave a body.
	Ω functionLimits()ι->Flows::FunctionLimits{
		Flows::FunctionLimits y;
		let function = Settings::FindObject( "/gateway/flows/function" );
		let size = [&]( sv key, uint& value, uint least, uint most ){
			let v = function ? function->if_contains( key ) : nullptr;
			if( !v )
				return;
			boost::system::error_code ec;
			let read = v->is_string() ? Flows::ToBytes( v->get_string() ) : optional<uint>{ v->to_number<uint>(ec) };
			if( read && !ec && *read>=least && *read<=most )
				value = *read;
			else if( read && !ec && *read>most ){
				value = most;
				WARNT( ELogTags::Settings, "'/gateway/flows/function/{}' of {} is over the {} bytes the executor's threads leave it.  Using {}.", key, serialize(*v), most, most );
			}
			else
				WARNT( ELogTags::Settings, "'/gateway/flows/function/{}' of {} is not a size of {} bytes or more, as \"16MiB\" is.  Using {}.", key, serialize(*v), least, value );
		};
		size( "memory", y.Memory, MinMemory, std::numeric_limits<uint>::max() );
		size( "stack", y.Stack, MinStack, MaxStack );
		if( let timeout = Settings::FindDuration("/gateway/flows/function/timeout"); timeout && *timeout>Duration::zero() )
			y.Timeout = *timeout;
		else if( timeout )
			WARNT( ELogTags::Settings, "'/gateway/flows/function/timeout' is not above zero.  Using {} ms.", duration_cast<milliseconds>(y.Timeout).count() );
		return y;
	}

	//The trace's block (spec *Trace*):  a key that does not read, or reads as nothing, keeps its default with a Warning.  The
	//days' zone is the historian's, /gateway/hist/timeZone, UTC without one, so a flow's files sit beside its groups' days.
	Ω tracePath()ι->fs::path{ return Settings::FindPath( "/gateway/flows/path" ).value_or( Process::AppDataFolder()/"logs"/"flows"/"opc-gateway" ); }
	Ω traceSettings()ι->Flows::TraceSettings{
		Flows::TraceSettings y;
		y.Root = tracePath();
		let duration = [&]( sv key, Duration& value ){
			let path = Ƒ( "/gateway/flows/{}", key );
			let read = Settings::FindDuration( path );
			if( read && *read>Duration::zero() )
				value = *read;
			else if( read || Settings::FindString(path) )
				WARNT( ELogTags::Settings, "'{}' is not a duration above zero.  Using {}.", path, Chrono::ToString(value) );
		};
		duration( "delay", y.Delay );
		duration( "retention", y.Retention );
		if( let v = Json::FindValue(Settings::Value(), "/gateway/flows/maxSize"); v ){
			boost::system::error_code ec;
			let read = v->is_string() ? Flows::ToBytes( v->get_string() ) : optional<uint>{ v->to_number<uint>(ec) };
			if( read && !ec && *read )
				y.MaxSize = *read;
			else
				WARNT( ELogTags::Settings, "'/gateway/flows/maxSize' of {} is not a size, as \"10GiB\" is.  Using {}.", serialize(*v), y.MaxSize );
		}
		if( let limit = Settings::FindNumber<uint>("/gateway/flows/valueLimit"); limit )
			y.ValueLimit = *limit;
		if( let level = Settings::FindString("/gateway/flows/trace"); level ){
			if( let parsed = Flows::ToTraceLevel(*level); parsed )
				y.Level = *parsed;
			else
				WARNT( ELogTags::Settings, "'/gateway/flows/trace' of '{}' is not off, counts, errors or hops.  Using {}.", *level, ToString(y.Level) );
		}
		if( let hist = Settings::FindObject("/gateway/hist"); hist )
			y.Zone = &Json::FindTimeZone( *hist, "timeZone", *std::chrono::locate_zone("UTC") );
		else
			y.Zone = std::chrono::locate_zone( "UTC" );
		return y;
	}

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
		auto live = Settings::FindDuration( "/gateway/flows/live" ).value_or( DefaultLive );
		if( live<=Duration::zero() ){
			WARNT( ELogTags::Settings, "'/gateway/flows/live' of {} is not allowed:  it must be positive.  Using {}.", Chrono::ToString(live), Chrono::ToString(DefaultLive) );
			live = DefaultLive;
		}
		let function = functionLimits();
		let env = Settings::FindObject( "/gateway/flows/env" );
		let trace = traceSettings();
		bool observe;
		sp<Flows::TraceStore> store;
		{
			lg _{ _mutex };
			_stopped = false;
			_configured = flows!=nullptr;
			_clock = clock ? move( clock ) : Hist::SystemClock();
			_ids = ms<Flows::MessageIds>( _clock->Now() );
			_tracePath = tracePath();
			if( _configured )
				store = _store = ms<Flows::TraceStore>( trace, _clock );
			_readLimit = Settings::FindNumber<uint>( "/gateway/flows/readLimit" ).value_or( Hist::Settings::DefaultReadLimit );
			_live = live;
			_valueLimit = trace.ValueLimit;
			_feeds.clear();
			_queue = queue;
			_function = function;
			_env = env ? *env : jobject{};
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
		store->Start();
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
		flat_map<FlowPK,sp<FlowFeed>> feeds;
		optional<uint> observer;
		bool configured;
		{
			lg _{ _mutex };
			_stopped = true;
			configured = _configured;
			feeds = std::exchange( _feeds, {} );
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
		for( let& [_,feed] : feeds )
			feed->Ended( EHttpStatus::ServiceUnavailable, "The gateway's flows have stopped." );
		if( !configured )
			return;
		{//each replay's cancel, and its row, which its end writes behind the trace's flush:  before the store stops.
			vector<sp<Flows::Run>> replays;
			{
				lg _{ _mutex };
				for( let& [_,replay] : _replays )
					replays.push_back( replay->Run );
			}
			for( let& replay : replays )
				replay->Cancel();
			std::unique_lock l{ _mutex };
			if( !_replayEnded.wait_for(l, 10s, [&]{ return _replays.empty(); }) )
				WARN( "{} replays had not ended after 10 s.", _replays.size() );
		}
		sp<Flows::TraceStore> store;
		{
			lg _{ _mutex };
			store = move( _store );
		}
		if( store )//the flows' last records, now that each has stopped;  before the context, which waits on no file.
			store->Stop();
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
	α FlowHost::ReplayOf( FlowPK id )Ι->optional<FlowRunPK>{
		lg _{ _mutex };
		let p = _replays.find( id );
		return p==_replays.end() ? nullopt : optional{ p->second->Id };
	}
	α FlowHost::Feed( FlowPK id )Ι->sp<FlowFeed>{
		lg _{ _mutex };
		let p = _feeds.find( id );
		return p==_feeds.end() ? nullptr : p->second;
	}
	α FlowHost::Tell( FlowPK id )ι->void{
		sp<FlowFeed> feed;
		sp<Run> live;
		optional<FlowRow> row;
		{
			lg _{ _mutex };
			let held = _feeds.find( id );
			if( held==_feeds.end() )
				return;
			feed = held->second;
			if( let p = _flows.find(id); p!=_flows.end() ){
				live = p->second.Live;
				row = p->second.Row;
			}
			else
				_feeds.erase( held );
		}
		if( live && live->Flow.Running() ){
			vector<string> nodes;
			for( let& node : live->Graph.Nodes )
				nodes.push_back( node.Id );
			feed->Started( {live->Serial, sp<Flows::Flow>{live, &live->Flow}, move(nodes), live->Version} );
		}
		else if( !row )
			feed->Ended( EHttpStatus::NotFound, Ƒ("Flow {} not found:  it is purged.", id) );
		else{
			let fault = live ? live->Flow.Fault() : nullopt;
			feed->Ended( EHttpStatus::Conflict, Ƒ("Flow '{}' is not running{}.", row->Name, fault ? Ƒ(":  {}", fault->Message) : row->Deleted ? ":  it is deleted" : row->Enabled ? "" : ":  it is disabled") );
		}
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
			try{//a run with no status is one a gateway stopped under, with no word:  none runs now.
				co_await Any( DS()->Execute(FlowSql::EndRuns(Time()->Now(), "failed:  the gateway stopped before the run ended")) );
			}
			catch( const std::exception& e ){
				WARN( "The runs a gateway stopped under could not be ended:  {}", e.what() );
			}
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
					if( let read = Flows::Graph::Read(Json::ParseValue(graph.TakeString(1)), true, nullopt); read )
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
		auto run = ms<Run>( move(graph), Flows::Flow::Config{.Name=row.Slug, .Clock=Time()}, version, deployer, Resolved{} );
		run->Flow.Stop( move(fault) );//which logs it, an Error.
		{
			lg _{ _mutex };
			if( auto p = _flows.find(row.Id); p!=_flows.end() && !_stopped ){
				p->second.Live = move( run );
				if( retry && !p->second.Retry )
					p->second.Retry = _clock->Schedule( RetryInterval, [this, id=row.Id]{ Retry( id ); } );
			}
		}
		Tell( row.Id );
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
			graph = Flows::Graph::Read( Json::ParseValue(version.Graph), true, Function() );
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
		Resolved spelled;
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
	α FlowHost::Begin( FlowRow row, Flows::Graph graph, uint32 version, UserPK deployer, Resolved spelled )ι->AnyVoidTask{
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
		Flows::Flow::Config config{ .Name=row.Slug };
		Loaded( id, row.Slug, move(rows), config );
		let serial = ++_serials;
		{
			lg _{ _mutex };
			config.Clock = _clock;
			config.Ids = _ids;
			config.Global = _global;
			config.Queue = _queue;
			config.Function = _function;
			config.Env = _env;
			auto& held = _feeds[id];
			if( !held )
				held = ms<FlowFeed>( _clock, _live, _valueLimit );
			config.Active = [feed=held, serial]{ feed->OnActive( serial ); };
			if( _store )
				config.Trace = _store->Trace( row.Slug, row.Trace, _clock, [feed=held, serial]( const Flows::TraceRecord& record ){ feed->OnRecord( serial, record ); } );
		}
		config.ContextChanged = [this, id]( sv node, sv key, const jvalue* value ){ ContextChanged( id, node, key, value ); };
		config.Host = ms<FlowNodes>( *this, FlowRun{id, row.Slug, deployer, spelled.Groups, spelled.Nodes} );
		auto run = ms<Run>( move(graph), move(config), version, deployer, move(spelled) );
		run->Trace = row.Trace;
		run->Serial = serial;
		co_await StartAwait{ run };
		let started = run->Flow.Running();
		let fault = started ? nullopt : run->Flow.Fault();
		let faulted = fault ? run->Graph.IndexOf( fault->NodeId ) : nullopt;
		let retry = !started && ( !faulted || run->Graph.Nodes[*faulted].Type!=ENodeType::Function );
		bool kept;
		{
			lg _{ _mutex };
			auto p = _flows.find( id );
			kept = p!=_flows.end() && !_stopped;
			if( kept ){
				p->second.Live = run;
				//A node that could not be made or started:  the deploy read its props, so it failed on what is around it, a
				//calendar not yet held, a member not yet resolved, which may come back.  Not a Function node, whose code or
				//On Start failed, which only a deploy changes, as Node-RED's runs once.
				if( retry && !p->second.Retry )
					p->second.Retry = _clock->Schedule( RetryInterval, [this, id]{ Retry( id ); } );
			}
		}
		if( !kept ){
			run->Flow.Stop();
			co_return;
		}
		if( started )
			INFO( "[{}]Flow '{}' started from version {}.", row.Slug, row.Name, version );
		Tell( id );
	}

	α FlowHost::Loaded( FlowPK flow, str slug, vector<FlowContextRow>&& rows, Flows::Flow::Config& config )Ι->void{
		ContextValues values;
		for( auto& context : rows )
			values[{move(context.Node), move(context.Name)}] = move( context.Value );
		Unwritten( flow, values );
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
				WARN( "[{}]The context's '{}' does not read, and is left out:  {}", slug, name, e.what() );
			}
		}
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
						else if( let p = await.Spelled.Nodes.find(node.Id); p!=await.Spelled.Nodes.end() )
							spelled = p->second;
						else{
							spelled = co_await SpellAwait{ ExNodeId{ex}, connection, sl };
							await.Spelled.Nodes.emplace( node.Id, spelled );
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
							await.Spelled.Groups.insert_or_assign( node.Id, groups.front().Id );//whose records a replay reads in the node's place.
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
						if( let p = await.Spelled.Groups.find(node.Id); p!=await.Spelled.Groups.end() )//a re-check:  the group the run reads, whatever its slug names now.
							key = DB::Key{ (uint)p->second };
						else if( named && named->is_string() && !named->get_string().empty() )
							key = DB::Key{ string{named->get_string()} };
						else if( let id = Json::FindNumber<uint32>(node.Props, "group"); id )
							key = DB::Key{ (uint)*id };
						REFUSE_IF( !key, "`group` is required, a historian group's slug or id." );
						let label = key->IsPK() ? std::to_string( key->PK() ) : key->NK();
						auto groups = co_await Any( SelectHistGroups({.Key=*key}) );
						REFUSE_IF( groups.empty() || !HistHost::Instance().Find(groups.front().Id), "Group '{}' isn't one this gateway historizes.", label );
						if( let allowed = acl->Check("gateway", "histGroups", groups.front().Guid, Access::ERights::Read, await.Deployer, sl); !allowed )
							throw Exception{ sl, ExceptionArgs{allowed.error().HttpStatus()}, "Group '{}' needs Read:  {}", groups.front().Name, allowed.error().what() };
						await.Spelled.Groups.insert_or_assign( node.Id, groups.front().Id );
					}
					else if( node.Type==ENodeType::Connection ){
						let slug = Json::FindDefaultSV( node.Props, "connection" );
						REFUSE_IF( slug.empty(), "`connection` is required, a connection's slug." );
						auto connections = co_await Any( ServerCnnctnAwait{DB::Key{string{slug}}} );
						REFUSE_IF( connections.empty(), "'{}' is no connection of this gateway.", slug );
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
			auto graph = Flows::Graph::Read( json, true, Function() );
			if( !graph )
				throw refused( {move(graph.error())}, sl );
			for( let& node : graph->Nodes ){
				if( node.Id.size()>MaxKey )
					throw refused( {{node.Id, Ƒ("Its id is {} characters, over the {} a context row holds.", node.Id.size(), MaxKey)}}, sl );
				string fault;//a hosted node's props, which the library leaves to its host.
				try{
					FlowNodes::Check( node, sl );
				}
				catch( const std::exception& e ){
					fault = e.what();
				}
				if( fault.size() )
					throw refused( {{node.Id, move(fault)}}, sl );
			}
			Resolved spelled;
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
			Tell( row.Id );
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
						if( let graph = versions.empty() ? std::unexpected{ Flows::Fault{} } : Flows::Graph::Read( Json::ParseValue(versions.front().Graph), true, nullopt ); graph )
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

	α FlowHost::Replaying( ReplayAwait& await )ι->VoidTask{
		let sl = await.Source();
		try{
			let by = await.By.UserPK();
			auto lock = co_await _edits.Lock();
			auto rows = co_await Any( SelectFlows({.Key=DB::Key{(uint)await.Flow}, .IncludeDeleted=true}) );
			AppClient()->Acl()->Test( "gateway", "flows", rows.empty() ? string{} : rows.front().Guid, Access::ERights::Update, by, sl );//the table's for a flow that isn't there, before a caller learns of an id.
			TestEnabled( sl );
			if( rows.empty() )
				throw Exception{ sl, ExceptionArgs{EHttpStatus::NotFound}, "Flow {} not found.", await.Flow };
			auto row = move( rows.front() );
			REFUSE_IF( row.Deleted, "Flow '{}' is deleted:  restore it before it is replayed.", row.Name );
			REFUSE_IF( !row.Version, "Flow '{}' has no version to replay:  deploy it first.", row.Name );
			REFUSE_IF( await.To>Time()->Now(), "to, {}, is after now:  a replay reads the history there is, the same records each time.", ToIsoString(await.To) );
			if( let under = ReplayOf(row.Id); under )
				throw Exception{ sl, ExceptionArgs{EHttpStatus::Conflict}, "Flow '{}' has run {} under way:  one run at a time, which cancelFlowRun ends.", row.Name, *under };
			auto versions = co_await Any( SelectFlowVersion(row.Id, *row.Version) );
			REFUSE_IF( versions.empty(), "Version {} has no flow_versions row.", *row.Version );
			auto& version = versions.front();
			std::expected<Flows::Graph,Flows::Fault> graph{ std::unexpect };
			try{
				graph = Flows::Graph::Read( Json::ParseValue(version.Graph), true, Function() );
			}
			catch( const std::exception& e ){
				graph = std::unexpected{ Flows::Fault{{}, Ƒ("Version {}'s graph does not read:  {}", *row.Version, e.what())} };
			}
			if( !graph )
				throw refused( {move(graph.error())}, sl );
			Resolved spelled;
			if( let refusal = co_await Admit(row.Id, *graph, version.Deployer, nullptr, false, spelled, sl); refusal )
				throw refused( *refusal, sl );
			auto nodes = ms<FlowNodes>( *this, FlowRun{row.Id, row.Slug, version.Deployer, spelled.Groups, spelled.Nodes}, FlowReplay{await.From, await.To, await.Commit, ReadLimit()} );
			let acl = AppClient()->Acl();
			flat_set<HistGroupPK> read;
			for( let& node : graph->Nodes ){//a source's refusals before the row, which a refusal doesn't write:  its stream's, a node no group holds by its name, and a group the caller may not read.
				if( node.Type!=ENodeType::OpcIn && node.Type!=ENodeType::GroupIn )
					continue;
				try{
					nodes->Stream( node );
				}
				catch( const std::exception& e ){
					throw refused( {{node.Id, e.what()}}, sl );
				}
				if( let group = spelled.Groups.find(node.Id); group!=spelled.Groups.end() && read.insert(group->second).second ){
					auto groups = co_await Any( SelectHistGroups({.Key=DB::Key{(uint)group->second}}) );
					let allowed = groups.empty() ? std::expected<void,Access::AccessException>{} : acl->Check( "gateway", "histGroups", groups.front().Guid, Access::ERights::Read, by, sl );
					if( !allowed )
						throw refused( {{node.Id, Ƒ("A replay reads group '{}', which needs Read for who runs it:  {}", groups.front().Name, allowed.error().what())}, allowed.error().HttpStatus()}, sl );
				}
			}
			auto contexts = co_await Any( SelectFlowContexts(row.Id) );
			let writer = HistHost::Instance().Writer( by );
			let started = Time()->Now();
			let mode = await.Commit ? "commit"sv : "dry"sv;
			let run = co_await Any( DS()->InsertSeq<FlowRunPK>(FlowSql::InsertRun(row.Id, *row.Version, await.From, await.To, mode, started, by, writer.UserName)) );

			auto clock = ms<Flows::StreamClock>( await.From );
			Flows::Flow::Config config{ .Name=row.Slug };
			config.Run = run;
			Loaded( row.Id, row.Slug, move(contexts), config );
			sp<Flows::TraceStore> store;
			sp<Flows::GlobalStore> global;
			{
				lg _{ _mutex };
				config.Queue = _queue;
				config.Function = _function;
				config.Env = _env;
				store = _store;
				global = _global;
			}
			jobject scratch;//the gateway's scope as it stands, the run's own from here.
			for( let& key : global ? global->Keys() : vector<string>{} )
				scratch[key] = global->Get( key );
			config.Global = ms<Flows::GlobalStore>( move(scratch) );
			if( store )
				config.Trace = store->Trace( row.Slug, std::max(row.Trace.value_or(store->Settings().Level), Flows::ETraceLevel::Errors), clock, {}, run );
			auto replay = ms<ReplayRun>();
			replay->Id = run;
			replay->Flow = row.Id;
			replay->Slug = row.Slug;
			replay->Name = row.Name;
			replay->Run = ms<Flows::Run>( move(*graph), move(config), Flows::Run::Config{
				.Clock=clock, .Host=move(nodes), .From=await.From, .To=await.To,
				.Ended=[this, replay]( Flows::ERunEnd end, string why ){ Ending( replay, end, move(why) ); },
				.Pace=[store, slug=row.Slug]( absl::AnyInvocable<void( string )> next ){
					if( !store || store->Buffered(slug)<ReplayBuffer )
						next( {} );
					else
						store->Flush( slug, [store, slug, next=std::move(next)]()mutable{//a trace that can't be written fails the run, which would otherwise flush at each record.
							let failing = store->Failing( slug );
							next( failing ? Ƒ("The trace could not be written:  {}", *failing) : string{} );
						} );
				}
			} );
			bool stopped;
			{
				lg _{ _mutex };
				stopped = _stopped;
				if( !stopped )
					_replays.emplace( row.Id, replay );
			}
			if( stopped ){//the gateway's end came meanwhile:  the run's end writes its row, cancelled.
				replay->Run->Cancel();
				TestEnabled( sl );
			}
			INFO( "[{}]Run {} of flow '{}', version {}, started by '{}':  {} to {}, {}.", row.Slug, run, row.Name, *row.Version, writer.UserName, ToIsoString(await.From), ToIsoString(await.To), mode );
			replay->Run->Start();//one that cannot is told by its end, as one that fails later is.
			lock.unlock();
			await.Resume( Replayed{run, *row.Version, started, writer.UserName} );
		}
		catch( runtime_error& e ){
			await.ResumeExp( move(e) );
		}
	}
	α FlowHost::Ending( sp<ReplayRun> replay, Flows::ERunEnd end, string why )ι->VoidTask{
		let records = replay->Run->Delivered(), pages = replay->Run->Pages();
		if( let store = Store(); store ){
			co_await FlushAwait{ store, replay->Slug };
			if( let failing = store->Failing(replay->Slug); failing && end!=Flows::ERunEnd::Failed ){//its records are not on file.
				end = Flows::ERunEnd::Failed;
				why = Ƒ( "The trace could not be written:  {}", *failing );
			}
		}
		auto status = end==Flows::ERunEnd::Failed ? Ƒ( "failed:  {}", why ) : string{ ToString(end) };
		status.resize( Flows::CutJson(status, 2048).size() );//the column's, on a character's boundary.
		let finished = Time()->Now();
		string failed;
		try{
			co_await Any( DS()->Execute(FlowSql::EndRun(replay->Id, finished, status)) );
		}
		catch( const std::exception& e ){
			failed = e.what();
		}
		if( failed.size() )
			ERR( "[{}]Run {}'s row could not be written, and says it is under way until the gateway starts again:  {}", replay->Slug, replay->Id, failed );
		LOG( end==Flows::ERunEnd::Failed ? ELogLevel::Warning : ELogLevel::Information, ELogTags::Flow, "[{}]Run {} of flow '{}' {}, after {} records in {} pages.", replay->Slug, replay->Id, replay->Name, status, records, pages );
		{
			lg _{ _mutex };
			if( let p = _replays.find(replay->Flow); p!=_replays.end() && p->second==replay )
				_replays.erase( p );
		}
		_replayEnded.notify_all();
		vector<absl::AnyInvocable<void()>> waiting;
		{
			lg _{ replay->Mutex };
			replay->End = RunEnd{ move(status), finished };
			waiting = std::move( replay->Waiting );
		}
		for( auto& resume : waiting )
			resume();
	}
	α FlowHost::Cancelling( CancelAwait& await )ι->VoidTask{
		let sl = await.Source();
		try{
			auto runs = co_await Any( SelectFlowRun(await.Run) );
			let state = runs.empty() ? nullopt : Find( runs.front().Flow );
			AppClient()->Acl()->Test( "gateway", "flows", state ? state->Row.Guid : string{}, Access::ERights::Update, await.By, sl );//the table's for a run that isn't there, before a caller learns of an id.
			TestEnabled( sl );
			if( runs.empty() )
				throw Exception{ sl, ExceptionArgs{EHttpStatus::NotFound}, "Run {} not found.", await.Run };
			sp<ReplayRun> replay;
			{
				lg _{ _mutex };
				if( let p = _replays.find(runs.front().Flow); p!=_replays.end() && p->second->Id==await.Run )
					replay = p->second;
			}
			if( !replay )
				throw Exception{ sl, ExceptionArgs{EHttpStatus::Conflict}, "Run {} is not under way{}.", await.Run, runs.front().Status ? Ƒ(":  it {}", *runs.front().Status) : string{} };
			replay->Run->Cancel();
			auto ended = co_await EndAwait{ replay, sl };
			await.Resume( move(ended) );
		}
		catch( runtime_error& e ){
			await.ResumeExp( move(e) );
		}
	}

	α FlowHost::Link( FlowPK from, UserPK deployer, sv name, const Flows::Message& m, SL sl )ε->uint{
		struct Receiver final{ string Guid; string Name; sp<Run> Live; };
		vector<Receiver> to;//those running, which a flow that is not would take nothing from, nor be asked about.
		{
			const string key{ name };
			lg _{ _mutex };
			for( let& [id,entry] : _flows ){
				if( id!=from && !entry.Row.Deleted && entry.Live && entry.Live->Flow.Running() && entry.Links.contains(key) )
					to.emplace_back( entry.Row.Guid, entry.Row.Name, entry.Live );
			}
		}
		let acl = AppClient()->Acl();
		for( let& receiver : to ){
			if( let allowed = acl->Check("gateway", "flows", receiver.Guid, Access::ERights::Update, deployer, sl); !allowed )
				throw Exception{ sl, ExceptionArgs{allowed.error().HttpStatus()}, "A link to flow '{}' needs Update on it, as an inject into it does:  {}", receiver.Name, allowed.error().what() };
		}
		uint taken{};
		for( let& receiver : to ){
			if( receiver.Live->Flow.Link(name, m) )
				++taken;
		}
		return taken;
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
		bool retrace{};//a running flow whose row's trace level changed, which its run's trace was made at.
		{
			lg _{ _mutex };
			auto& entry = _flows[row.Id];
			entry.Row = row;
			live = entry.Live;
			retrace = wanted && live && live->Flow.Running() && live->Trace!=row.Trace;
			if( !wanted || retrace ){
				entry.Live = nullptr;
				if( auto timer = std::exchange(entry.Retry, 0); timer )
					_clock->Cancel( timer );
			}
		}
		if( retrace ){
			co_await HaltAwait{ live };
			INFO( "[{}]Flow '{}' restarts at trace level {}.", row.Slug, row.Name, row.Trace ? ToString(*row.Trace) : "the gateway's" );
			co_await Launch( move(row) );
		}
		else if( wanted && !live )
			co_await Launch( move(row) );
		else if( !wanted && live && live->Flow.Running() ){
			co_await HaltAwait{ live };
			INFO( "[{}]Flow '{}' stopped:  it is {}.", row.Slug, row.Name, row.Deleted ? "deleted" : "disabled" );
			Tell( row.Id );
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
		Tell( id );
		sp<ReplayRun> replay;
		{
			lg _{ _mutex };
			if( let p = _replays.find(id); p!=_replays.end() )
				replay = p->second;
		}
		if( replay ){//its row goes with the flow's, and its records with the directory.
			replay->Run->Cancel();
			co_await EndAwait{ replay };
		}
		co_await WriteContext();
	}
	α FlowHost::Purged( FlowRow row )ι->AnyVoidTask{
		sp<Flows::TraceStore> store;
		fs::path root;
		{
			auto lock = co_await _edits.Lock();
			sp<Run> live;
			{
				lg _{ _mutex };
				store = _store;
				root = _tracePath;
				if( auto p = _flows.find(row.Id); p!=_flows.end() ){//Changed's, for an edit of the row while the statement ran.
					live = move( p->second.Live );
					if( auto timer = std::exchange(p->second.Retry, 0); timer )
						_clock->Cancel( timer );
					_flows.erase( p );
				}
			}
			if( live && live->Flow.Running() )
				co_await HaltAwait{ live };
			Tell( row.Id );
			Forget( row.Id );
		}
		//Its trace directory;  the slug is held to a file name's characters at its create (FlowMAwait), and the store follows no other out of the directory.
		if( store )
			store->Purge( row.Slug );
		else if( !root.empty() )//the store stopped, or there was none:  a flow made later with the slug is not to read this one's trace.
			Flows::TraceStore::Remove( root, row.Slug );
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
		FlowNodes::GroupChanged( group );
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
					if( refusal && !refusal->Unknown ){
						co_await HaltAwait{ run, move(refusal->Fault) };//which logs it, an Error.
						Tell( row.Id );
					}
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