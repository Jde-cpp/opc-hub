#include "FlowNodes.h"
#include <deque>
#include <jde/fwk/settings.h>
#include <jde/fwk/str.h>
#include <jde/web/client/http/ClientHttpAwait.h>
#include <jde/web/client/http/ClientHttpResException.h>
#include <jde/calendar/client/Calendars.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/flows/Allow.h>
#include <jde/flows/Props.h>
#include <jde/flows/Template.h>
#include "../async/ValueTypesAwait.h"
#include "../async/WriteAwait.h"
#include "../hist/HistHost.h"
#include "../UAClient.h"
#include "FlowHost.h"
#include "FlowServer.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ ELogTags::Flow };
	using Flows::ENodeType; using Flows::Message; using Flows::NodeDef; using Flows::NodeStatus;
	namespace http = boost::beast::http;

	//A node's way back to itself from work off its strand - a coroutine, a listener, a timer of another's:  the node until
	//its Stop, then nothing, so what outlives the run reaches no node.
	template<class TNode> struct Tether final{
		explicit Tether( TNode& node )ι:_node{ &node }{}
		α Cut()ι->void{ lg _{ _mutex }; _node = nullptr; }
		//`f` with the node, here, under the tether's lock:  for what a node offers any thread.
		α With( auto&& f )ι->void{
			lg _{ _mutex };
			if( _node )
				f( *_node );
		}
		//`f` with the node, on its strand:  dropped once the flow has stopped.
		α Post( absl::AnyInvocable<void( TNode& )> f )ι->void{
			lg _{ _mutex };
			if( _node )
				_node->OnStrand( [node=_node, f=std::move(f)]()mutable{ f( *node ); } );
		}
	private:
		std::mutex _mutex;
		TNode* _node;
	};

	namespace Prop = Flows::Prop;

	//The connection an `opc in`, an `opc out` and a `connection` node name, by its slug.
	Ω connection( const jobject& props )ε->ServerCnnctnNK{
		auto slug = Prop::FindText( props, "connection" );
		THROW_IF( !slug, "`connection` is required, a connection's slug." );
		return ServerCnnctnNK{ move(*slug) };
	}
	//A node of a connection, as an `opc in` and an `opc out` name it.
	struct OpcTarget final{ ServerCnnctnNK Slug; ExNodeId Node; string Text; };
	Ω target( const jobject& props, SL sl )ε->OpcTarget{
		auto slug = connection( props );
		let named = Prop::Find( props, "node" );
		THROW_IF( !named, "`node` is required, the node's id." );
		auto ex = ParseHistNode( *named, sl );
		auto text = ex.ToString();
		return { move(slug), move(ex), move(text) };
	}
	//The node by the index the connection's server has for it:  through the server's namespaces for one named by its URI.
	struct ResolveAwait final : AnyAwait<NodeId>{
		ResolveAwait( const OpcTarget& target, sp<UAClient> client, SRCE )ι:AnyAwait<NodeId>{ sl }, _target{ target }, _client{ move(client) }{}
		α await_ready()ι->bool override{
			if( _target.Node.namespaceUri.length )
				return false;
			_result = NodeId{ _target.Node.nodeId };
			return true;
		}
	protected:
		α Suspend()ι->void override{ Execute(); }
	private:
		α Execute()ι->VoidTask{
			try{
				let namespaces = co_await Any( NamespacesAwait{_client} );
				auto id = namespaces.ToNodeId( _target.Node );
				THROW_IF( !id, "'{}' names a namespace '{}' doesn't have.", _target.Text, _target.Slug );
				Resume( move(*id) );
			}
			catch( runtime_error& e ){
				ResumeExp( move(e) );
			}
		}
		const OpcTarget& _target;
		sp<UAClient> _client;
	};

	//`opc in`.
	constexpr std::array<sv,3> _triggers{ "status", "statusValue", "statusValueTimestamp" };
	constexpr std::array<sv,3> _deadbands{ "none", "absolute", "percent" };
	Ω parameters( const jobject& props )ε->MonitoringParameters{
		MonitoringParameters y;
		if( let sampling = Prop::FindNumber(props, "samplingInterval") )//ms:  0 the server's fastest, negative the publishing interval.
			y.SamplingInterval = *sampling;
		if( let queue = Prop::FindNumber(props, "queueSize") ){
			THROW_IF( *queue<0 || *queue>std::numeric_limits<uint32_t>::max() || *queue!=std::floor(*queue), "`queueSize` must be a whole number." );
			y.QueueSize = (uint32)*queue;
		}
		y.DiscardOldest = Prop::Flag( props, "discardOldest", true );
		if( let filter = Prop::Find(props, "filter") ){
			THROW_IF( !filter->is_object(), "`filter` must be an object of trigger, deadbandType and deadbandValue." );
			let& o = filter->get_object();
			DataChangeFilter f;
			f.Trigger = (UA_DataChangeTrigger)Prop::Choice( o, "trigger", _triggers, UA_DATACHANGETRIGGER_STATUSVALUE );
			f.DeadbandType = (UA_DeadbandType)Prop::Choice( o, "deadbandType", _deadbands, UA_DEADBANDTYPE_NONE );
			f.DeadbandValue = Prop::FindNumber( o, "deadbandValue" ).value_or( 0 );
			THROW_IF( f.DeadbandValue<0, "`deadbandValue` must not be negative." );
			y.Filter = f;
		}
		return y;
	}
	struct OpcIn;
	//What the node's item delivers to (IDataChange), the client's until the node's Stop unsubscribes it.
	struct OpcListener final : IDataChange{
		OpcListener( OpcIn& node, OpcTarget target, MonitoringParameters parameters, string label )ι:
			Node{ node }, Target{ move(target) }, Parameters{ move(parameters) }, Collector{ HistHost::Instance().Collector(Target.Slug) }, Label{ move(label) }
		{}
		α SendDataChange( const ServerCnnctnNK&, const NodeId&, const Value& value, const ITypes* types )ι->void override;
		α ConnectionLost( const ServerCnnctnNK&, const flat_set<NodeId>&, TimePoint )ι->void override;
		α ConnectionRestored( const ServerCnnctnNK&, const flat_set<NodeId>& )ι->void override;
		α ItemParameters( const ServerCnnctnNK&, const NodeId&, Duration publishingInterval )Ι->MonitoringParameters override{
			auto y = Parameters;
			if( y.SamplingInterval<0 )
				y.SamplingInterval = std::chrono::duration<double,std::milli>{ publishingInterval }.count();
			return y;
		}
		α ItemModified( const ServerCnnctnNK&, const NodeId&, const MonitoredItemModifyResult& result, const optional<AskReverted>& )ι->void override{
			if( result.Status )
				WARN( "[{}]'{}' refused the item's parameters for '{}':  {}.", Label, Target.Slug, Target.Text, UAException::Message(result.Status) );
		}
		α to_string()Ι->string override{ return Ƒ( "Flow({})", Label ); }
		Tether<OpcIn> Node;
		const OpcTarget Target;
		const MonitoringParameters Parameters;
		const Credential Collector;
		const string Label;//the flow's slug and the node's id.
		std::atomic<bool> Closed{}, Fed{};//Fed:  a value came, which a read of an item that existed would repeat.
	};
	struct OpcIn final : Flows::ISource{
		OpcIn( const NodeDef& def, str flow, SL sl )ε:
			_listener{ ms<OpcListener>(*this, target(def.Props, sl), parameters(def.Props), Ƒ("{}.{}", flow, def.Id)) }
		{}
		~OpcIn(){ Close(); }
		α Start()ε->void override{
			SetStatus( {"yellow", "ring", "subscribing"} );
			_started = true;
			Subscribing( _listener );
		}
		α Stop()ι->void override{ Close(); }
		α OnStrand( absl::AnyInvocable<void()> f )ι->void{ Post( std::move(f) ); }
		//Any thread, under the tether:  a value, or the item's loss, as a message.
		α Deliver( const Value& value, const ITypes* types )ι->void{
			let& to = _listener->Target;
			auto m = Make();
			m.SetDataChange( to.Slug, to.Text, value, types );
			m.Json["topic"] = to.Text;
			Emit( move(m) );
		}
		//The strand's.
		α Subscribed( string failure )ι->void{
			if( failure.empty() ){
				Restored();
				return;
			}
			SetStatus( {"red", "ring", failure} );
			Schedule( FlowNodes::RetryInterval, [this]{ Subscribing( _listener ); } );
		}
		//The client parks the item and puts it back with the connection.  One it has not put back within the collector's
		//AbandonAfter, a rebuild the server refused or a reconnect it rejected, is asked for again here, as the collector asks.
		α Lost()ι->void{
			SetStatus( {"red", "ring", "connection lost"} );
			if( !_abandon )
				_abandon = Schedule( HistGroupHost::AbandonAfter, [this]{
					_abandon = 0;
					UAClient::Unsubscribe( _listener );
					_listener->Fed = false;//the item it joins again sends no first value either.
					Subscribing( _listener );
				} );
		}
		α Restored()ι->void{
			if( auto timer = std::exchange(_abandon, 0); timer )
				Cancel( timer );
			SetStatus( {"green", "dot", "subscribed"} );
		}
	private:
		α Close()ι->void{
			_listener->Node.Cut();
			if( !_listener->Closed.exchange(true) && _started )
				UAClient::Unsubscribe( _listener );
		}
		//The item, on the connection's collector client, as a group's collector asks for its own (HistGroupHost::Subscribing):
		//one that fails is asked for again RetryInterval on.  An item the client had already sends no first value, so the
		//node's value is read once in its place:  a read that fails leaves the node subscribed, its next change its first.
		Ω Subscribing( sp<OpcListener> listener )ι->VoidTask{
			string failure;
			try{
				let& to = listener->Target;
				ConnectAwait connect{ string{to.Slug}, listener->Collector };//an lvalue:  ConnectAwait is neither copied nor moved.
				auto client = co_await Any( connect );
				let id = co_await ResolveAwait{ to, client };
				co_await Any( SubscribeAwait{client} );
				let monitored = client->TryMonitoredNodes();
				let existing = monitored && !monitored->Handles( {id} ).empty();
				auto ack = co_await Any( DataChangeAwait{{id}, listener, client} );
				if( listener->Closed ){//the Stop came while this was out, before the create registered the listener:  its unsubscribe found nothing to drop.
					UAClient::Unsubscribe( listener );
					co_return;
				}
				if( let status = ack.results_size() ? ack.results(0).status_code() : (StatusCode)UA_STATUSCODE_BADUNEXPECTEDERROR; status )
					THROW( "'{}' refused an item for '{}':  {}.", to.Slug, to.Text, UAException::Message(status) );
				if( existing ){
					try{
						auto values = co_await Any( ReadValueAwait{{id}, client, UA_TIMESTAMPSTORETURN_BOTH} );
						if( let p = values.find(id); p!=values.end() && !listener->Fed.exchange(true) )
							listener->Node.With( [&]( OpcIn& node ){ node.Deliver( p->second, client->Types().get() ); } );
					}
					catch( const std::exception& e ){
						WARN( "[{}]'{}' on '{}' has no first value, its read having failed:  its next change is its first.  {}", listener->Label, to.Text, to.Slug, e.what() );
					}
				}
			}
			catch( const std::exception& e ){
				failure = e.what();
			}
			if( failure.size() )
				WARN( "[{}]Could not subscribe to '{}' on '{}' - trying again in {}s:  {}", listener->Label, listener->Target.Text, listener->Target.Slug, duration_cast<std::chrono::seconds>(FlowNodes::RetryInterval).count(), failure );
			listener->Node.Post( [failure=move(failure)]( OpcIn& node )mutable{ node.Subscribed( move(failure) ); } );
		}
		sp<OpcListener> _listener;
		bool _started{};
		TimerId _abandon{};
	};
	α OpcListener::SendDataChange( const ServerCnnctnNK&, const NodeId&, const Value& value, const ITypes* types )ι->void{
		Fed = true;
		Node.With( [&]( OpcIn& node ){ node.Deliver( value, types ); } );
	}
	α OpcListener::ConnectionLost( const ServerCnnctnNK&, const flat_set<NodeId>&, TimePoint )ι->void{
		Node.With( []( OpcIn& node ){ node.Deliver( Value{(StatusCode)UA_STATUSCODE_BADNOCOMMUNICATION}, nullptr ); } );
		Node.Post( []( OpcIn& node ){ node.Lost(); } );
	}
	α OpcListener::ConnectionRestored( const ServerCnnctnNK&, const flat_set<NodeId>& )ι->void{
		Node.Post( []( OpcIn& node ){ node.Restored(); } );
	}

	//`group in`.
	struct GroupIn;
	//What the group delivers to (Hist::ISubscriber), on the thread that stored the record:  the records wait here until the
	//strand takes them, heartbeats, which repeat a value, left out.  More than `limit` of them, and they go:  the node
	//subscribes again from its mark, and reads them from the group.
	struct GroupFeed final : Hist::ISubscriber{
		GroupFeed( sp<Tether<GroupIn>> node, uint limit )ι:_node{ move(node) }, _limit{ limit }{}
		α OnRecords( vector<Hist::Proto::HistoryRecord>&& records )ι->void override;
		α OnEnded( Hist::ESubscriptionEnd why )ι->void override;
		//Up to `count`, oldest first, and whether the feed fell behind.  With neither, the next record wakes the node.
		α Take( uint count, bool& behind )ι->vector<Hist::Proto::HistoryRecord>{
			lg _{ _mutex };
			behind = _behind;
			vector<Hist::Proto::HistoryRecord> y;
			while( y.size()<count && !_records.empty() ){
				y.push_back( move(_records.front()) );
				_records.pop_front();
			}
			if( y.empty() && !behind )
				_awake = false;
			return y;
		}
	private:
		sp<Tether<GroupIn>> _node;
		const uint _limit;
		std::mutex _mutex;
		std::deque<Hist::Proto::HistoryRecord> _records;
		bool _behind{};
		bool _awake{ true };//the node is taking, its snapshot first.
	};
	static std::mutex _groupInsMutex;
	static vector<std::pair<HistGroupPK,wp<Tether<GroupIn>>>> _groupIns;//each running `group in`, for GroupChanged.

	//`group in`'s props, as the node and a deploy's check read them.
	struct GroupInProps final{
		GroupInProps( const jobject& props, SL sl )ε:
			Modifications{ Prop::Flag(props, "modifications") },
			Lateness{ Prop::FindWait(props, "lateness", true) }{
			let from = Prop::Text( props, "from", "resume" );
			if( from=="now" )
				Now = true;
			else if( from!="resume" ){
				try{
					Start = Chrono::ToTimePoint( from, sl );
				}
				catch( const std::exception& ){
					THROW( "`from` must be resume, now or a time, as 2026-03-09T06:00:00Z, not '{}'.", from );
				}
			}
			if( let members = Prop::Find(props, "members") ){
				THROW_IF( !members->is_array(), "`members` must be a list of the group's nodes." );
				for( let& member : members->get_array() )
					Wanted.push_back( ParseHistNode(member, sl) );
			}
		}
		const bool Modifications;
		const optional<Duration> Lateness;//none, the group's heartbeat gap.
		optional<TimePoint> Start;//`from`, a time.
		bool Now{};//`from`:  now, whatever the mark.
		vector<ExNodeId> Wanted;//`members`:  none, every member.
	};
	//A group's member as a `group in`'s message names it:  its node, and its path, the message's `topic`, else the node.
	struct GroupMember final{ string Node; string Path; };
	using GroupMembers = flat_map<Hist::NodeIndex,GroupMember>;
	//The members a `group in` takes:  `members`, else every one, a node that is none of the group's refused.
	Ω groupMembers( HistGroupHost& host, const GroupInProps& props )ε->GroupMembers{
		GroupMembers y;
		let all = host.HistMembers();
		let add = [&]( const HistMember& member ){
			let text = member.Node.ToString();
			y.emplace( member.Index, GroupMember{text, member.Attributes.Path ? HistPathText(*member.Attributes.Path) : text} );
		};
		if( props.Wanted.empty() )
			std::ranges::for_each( all, add );
		for( let& node : props.Wanted ){
			let index = host.Index( node );
			let member = index ? std::ranges::find( all, *index, &HistMember::Index ) : all.end();
			THROW_IF( member==all.end(), "'{}' is no member of its group.", node.ToString() );
			add( *member );
		}
		return y;
	}
	//The member a record is a message of:  none for a heartbeat, which repeats a value, and for a node not taken.
	Ω recorded( const GroupMembers& members, const Hist::Proto::DataValue& value )ι->const GroupMember*{
		if( value.has_heartbeat() )
			return nullptr;
		let member = members.find( value.node_index() );
		return member==members.end() ? nullptr : &member->second;
	}
	//The record as its member's message, `m`'s data change and topic, live or in a replay:  false for a value that does
	//not read, which is logged.
	Ω dataChange( Message& m, sv slug, const GroupMember& member, const Hist::Proto::DataValue& value, const ITypes* types )ι->bool{
		try{
			m.SetDataChange( slug, member.Node, Hist::ToUA(value), types );
		}
		catch( const std::exception& e ){
			WARN( "A record of '{}' does not read, and is left out:  {}", member.Node, e.what() );
			return false;
		}
		m.Json["topic"] = member.Path;
		return true;
	}

	constexpr std::array<sv,5> _updateTypes{ "unspecified", "insert", "replace", "update", "delete" };
	struct GroupIn final : Flows::ISource{
		GroupIn( const NodeDef& def, HistGroupPK group, SL sl )ε:
			_group{ group },
			_props{ def.Props, sl },
			_self{ ms<Tether<GroupIn>>(*this) }{
			THROW_IF( !HistHost::Instance().Find(group), "Its group isn't one this gateway historizes." );
		}
		~GroupIn(){ _self->Cut(); }
		//A subscribe that fails, a member that does not resolve yet, leaves the node's status saying so, and it is asked for
		//again RetryInterval on, and at the group's next change.
		α Start()ε->void override{
			let host = HistHost::Instance().Find( _group );
			THROW_IF( !host, "Its group isn't one this gateway historizes." );
			if( let marks = _props.Now ? nullptr : Context().Get("marks"); marks && marks->is_object() ){
				for( let& mark : marks->get_object() ){
					if( let index = Str::TryTo<Hist::NodeIndex>(sv{mark.key()}); index && mark.value().is_int64() ){
						let ticks = (UA_DateTime)mark.value().get_int64();
						_high[*index] = ticks;
						_mark = std::max( _mark.value_or(ticks), ticks );
					}
				}
			}
			_lateness = _props.Lateness.value_or( host->Columns().Thresholds().MaxTimeInterval );
			_floor = _props.Start ? UADateTime{ *_props.Start }.UA() : _props.Now || !_mark ? UADateTime{ Now() }.UA() : std::numeric_limits<UA_DateTime>::min();
			_page = std::clamp<uint>( QueueLimit()/4, 1, 1000 );
			{
				lg _{ _groupInsMutex };
				_groupIns.emplace_back( _group, _self );
			}
			try{
				Subscribe( *host );
			}
			catch( const std::exception& e ){
				Retry( e.what() );
				return;
			}
			try{
				Pump();
			}
			catch( const std::exception& )//its status says it, and it is read again RetryInterval on.
			{}
		}
		α Stop()ι->void override{
			_self->Cut();
			{
				lg _{ _groupInsMutex };
				erase_if( _groupIns, [this]( let& entry ){ let p = entry.second.lock(); return !p || p==_self; } );
			}
			if( _subscription )
				_subscription->End();
		}
		α OnStrand( absl::AnyInvocable<void()> f )ι->void{ Post( std::move(f) ); }
		//Each the strand's.
		//What waits, a page of it:  the snapshot's, then the records stored since, and then nothing until the feed wakes this
		//again.  The page after goes once this one is through the flow (Idle), so a replay stays within the queues, and the
		//mark is saved then, when the flow has finished every message it sent.
		α Pump()ε->void{
			if( !_subscription )
				return;
			string failure;
			try{
				if( _paging ){
					let page = _subscription->Next( _page );
					let types = UAClient::FindTypes( _slug );
					for( let& value : page.Values )
						Deliver( value.Value, types.get(), value.Bound );
					_paging = !page.Continuation.empty();
				}
				else{
					bool behind;
					let records = _feed->Take( _page, behind );
					if( behind ){
						let host = HistHost::Instance().Find( _group );
						THROW_IF( !host, "Its group is no longer one this gateway historizes." );
						Subscribe( *host );
					}
					else if( records.empty() )
						return;
					let types = UAClient::FindTypes( _slug );
					for( let& record : records ){
						if( record.has_value() )
							Deliver( record.value(), types.get() );
						else if( record.has_modification() && _props.Modifications )
							Modified( record.modification(), types.get() );
					}
				}
			}
			catch( const std::exception& e ){
				failure = e.what();
			}
			if( failure.empty() )
				Idle( [this]{ Save(); Pump(); } );
			else{//its files could not be read, or the group has gone:  from the mark again, RetryInterval on.
				Retry( failure );
				THROW( "{}", failure );
			}
		}
		α Ended( Hist::ESubscriptionEnd why )ι->void{
			_subscription = nullptr;
			SetStatus( {"red", "ring", why==Hist::ESubscriptionEnd::Removed ? "Its group was removed." : "The historian stopped."} );
		}
		//The group's members changed, or a read failed:  subscribed again from the mark, when it is every member this takes
		//and they are not those it has, when it has no subscription, or `always`.
		α Resubscribe( bool always )ε->void{
			let host = HistHost::Instance().Find( _group );
			if( !host )
				return;
			if( !always && _subscription ){
				if( _props.Wanted.size() )
					return;
				let members = host->Members();
				if( members.size()==_members.size() && std::ranges::all_of(members, [this]( let& m ){ return _members.contains( m.first ); }) )
					return;
			}
			try{
				Subscribe( *host );
			}
			catch( const std::exception& e ){
				Retry( e.what() );
				return;
			}
			if( auto timer = std::exchange(_retry, 0); timer )
				Cancel( timer );
			Pump();
		}
	private:
		α Retry( string why )ι->void{
			SetStatus( {"red", "ring", move(why)} );
			if( !_retry )
				_retry = Schedule( FlowNodes::RetryInterval, [this]{ _retry = 0; Resubscribe( true ); } );
		}
		//Registered with the group from the mark less the lateness, or from where the node starts, its snapshot to page first.
		α Subscribe( HistGroupHost& host )ε->void{
			if( _subscription )
				_subscription->End();
			_subscription = nullptr;
			_slug = host.Slug();
			_members.clear();
			_members = groupMembers( host, _props );
			vector<Hist::NodeIndex> nodes;
			for( let& [index, _] : _members )
				nodes.push_back( index );
			if( nodes.empty() ){
				SetStatus( {"grey", "ring", "Its group has no members."} );
				return;
			}
			let lateness = (UA_DateTime)( duration_cast<std::chrono::nanoseconds>(_lateness).count()/100 );
			_from = _mark ? std::max( _floor, *_mark-lateness ) : _floor;
			_feed = ms<GroupFeed>( _self, QueueLimit() );
			_subscription = host.Group()->Subscribe( _feed, move(nodes), _from );
			_paging = true;
			SetStatus( {"green", "dot", Ƒ("from {}", ToIsoString(UADateTime{_from}.Time()))} );
		}
		//A record as a message, but a heartbeat, which repeats a value, a snapshot's bound from before its time, the value
		//the node had then, and one at or before the last of its node the flow finished, which a restart's lookback reads again.
		α Deliver( const Hist::Proto::DataValue& value, const ITypes* types, bool bound=false )ι->void{
			let member = recorded( _members, value );
			if( !member )
				return;
			let ticks = value.has_source_ts() ? optional<UA_DateTime>{ value.source_ts() } : value.has_server_ts() ? optional<UA_DateTime>{ value.server_ts() } : nullopt;
			if( bound && (!ticks || *ticks<_from) )
				return;
			let high = _high.find( value.node_index() );
			if( ticks && high!=_high.end() && *ticks<=high->second )
				return;
			auto m = Make();
			if( !dataChange(m, _slug, *member, value, types) )
				return;
			Send( move(m) );
			if( ticks ){
				_high[value.node_index()] = *ticks;
				_mark = std::max( _mark.value_or(*ticks), *ticks );
				_dirty = true;
			}
		}
		//An edit of the group's history, a message of its own:  the value it left, none for a delete, and what it was.
		α Modified( const Hist::Proto::Modification& edit, const ITypes* types )ι->void{
			let member = _members.find( edit.node_index() );
			if( member==_members.end() )
				return;
			auto m = Make();
			try{
				if( edit.has_new_value() )
					m.SetDataChange( _slug, member->second.Node, Hist::ToUA(edit.new_value()), types );
				else
					m.Json["node"] = jobject{ {"connection", _slug}, {"id", member->second.Node} };
				jobject modification{ {"type", _updateTypes[std::min<uint>((uint)edit.update_type(), _updateTypes.size()-1)]}, {"time", ToIsoString(UADateTime{(UA_DateTime)edit.ts()}.Time())}, {"userName", edit.user_name()} };
				if( edit.has_original() )
					modification["original"] = Flows::ToPayload( Hist::ToUA(edit.original()), types );
				m.Json["modification"] = move( modification );
			}
			catch( const std::exception& e ){
				WARN( "An edit of '{}' does not read, and is left out:  {}", member->second.Node, e.what() );
				return;
			}
			m.Json["sourceTime"] = ToIsoString( UADateTime{(UA_DateTime)edit.target_source_ts()}.Time() );
			m.Json["topic"] = member->second.Path;
			Send( move(m) );
		}
		//The mark, as the context keeps it, once the flow has finished what was sent up to it (Idle):  written a second after
		//the first change not yet written, the host's rule.
		α Save()ι->void{
			if( !std::exchange(_dirty, false) )
				return;
			jobject marks;
			for( let& [index, ticks] : _high )
				marks[std::to_string(index)] = ticks;
			Context().Set( "mark", jvalue{ToIsoString(UADateTime{*_mark}.Time())} );
			Context().Set( "marks", move(marks) );
		}

		const HistGroupPK _group;
		const GroupInProps _props;
		sp<Tether<GroupIn>> _self;
		//The strand's.
		Duration _lateness{};//the props', else the group's heartbeat gap.
		ServerCnnctnNK _slug;
		GroupMembers _members;
		sp<GroupFeed> _feed;
		up<Hist::Subscription> _subscription;
		bool _paging{};
		uint _page{ 1 };
		UA_DateTime _floor{};//no subscription starts before it.
		UA_DateTime _from{};//where the subscription it has starts.
		optional<UA_DateTime> _mark;//the latest time of a record the node has sent.
		flat_map<Hist::NodeIndex,UA_DateTime> _high;//each node's.
		bool _dirty{};
		TimerId _retry{};
	};
	α GroupFeed::OnRecords( vector<Hist::Proto::HistoryRecord>&& records )ι->void{
		std::erase_if( records, []( let& r ){ return r.has_value() && r.value().has_heartbeat(); } );
		if( records.empty() )
			return;
		{
			lg _{ _mutex };
			if( _behind )
				return;
			if( _records.size()+records.size()>_limit ){
				_records.clear();
				_behind = true;
			}
			else
				std::ranges::move( records, std::back_inserter(_records) );
			if( std::exchange(_awake, true) )
				return;
		}
		_node->Post( []( GroupIn& node ){ node.Pump(); } );
	}
	α GroupFeed::OnEnded( Hist::ESubscriptionEnd why )ι->void{
		_node->Post( [why]( GroupIn& node ){ node.Ended( why ); } );
	}

	//`timer`.
	constexpr std::array<sv,7> _weekdays{ "sun", "mon", "tue", "wed", "thu", "fri", "sat" };
	struct Timer final : Flows::ISource{
		Timer( const NodeDef& def )ε:
			_every{ Prop::FindWait(def.Props, "every") }{
			let at = Prop::Find( def.Props, "at" );
			THROW_IF( _every && at, "A timer takes `every` or `at`, not both." );
			THROW_IF( !_every && !at, "A timer takes `every`, an ISO 8601 duration, or `at`, local times." );
			THROW_IF( _every && *_every<1ms, "`every` must be a millisecond at least." );
			if( !at )
				return;
			THROW_IF( !at->is_object(), "`at` must be an object of times, days and zone." );
			let& o = at->get_object();
			let times = Prop::Find( o, "times" );
			THROW_IF( !times || !times->is_array() || times->get_array().empty(), "`at.times` must be a list of local times, as 06:00." );
			for( let& time : times->get_array() ){
				let parts = time.is_string() ? Str::Split( sv{time.get_string()}, ':' ) : vector<sv>{};
				let h = parts.size()>=2 ? Str::TryTo<uint>( string{parts[0]} ) : nullopt;
				let m = parts.size()>=2 ? Str::TryTo<uint>( string{parts[1]} ) : nullopt;
				let s = parts.size()==3 ? Str::TryTo<uint>( string{parts[2]} ) : optional<uint>{ 0 };
				THROW_IF( parts.size()>3 || !h || !m || !s || *h>23 || *m>59 || *s>59, "`at.times` holds {}, which is not a time of day, as 06:00 or 14:30:15.", serialize(time) );
				_times.push_back( std::chrono::hours{*h}+std::chrono::minutes{*m}+std::chrono::seconds{*s} );
			}
			std::ranges::sort( _times );
			if( let days = Prop::Find(o, "days") ){
				THROW_IF( !days->is_array(), "`at.days` must be a list of weekdays, as mon." );
				_days.fill( false );
				for( let& day : days->get_array() ){
					let p = day.is_string() ? std::ranges::find( _weekdays, sv{day.get_string()} ) : _weekdays.end();
					THROW_IF( p==_weekdays.end(), "`at.days` holds {}, which is not one of {}.", serialize(day), Str::Join(_weekdays, ", ") );
					_days[p-_weekdays.begin()] = true;
				}
				THROW_IF( std::ranges::none_of(_days, std::identity{}), "`at.days` names no day." );
			}
			if( let zone = Prop::FindText(o, "zone") ){
				try{
					_zone = std::chrono::locate_zone( *zone );
				}
				catch( const std::exception& ){
					THROW( "`at.zone` is '{}', which is no time zone the gateway knows.", *zone );
				}
			}
		}
		α Start()ε->void override{ Arm( Now() ); }
		//The first tick after `t`:  `every`'s next multiple from the epoch, so one that divides an hour falls on it, or `at`'s
		//next time of day on one of its days, in its zone.
		α Next( TimePoint t )Ι->TimePoint{
			if( _every ){
				let since = t.time_since_epoch();
				return TimePoint{ duration_cast<Duration>(*_every)*(since/duration_cast<Duration>(*_every)+1) };
			}
			using namespace std::chrono;
			let today = floor<days>( _zone->to_local(t) );
			for( int d=0; d<=14; ++d ){
				let day = today+days{ d };
				if( !_days[weekday{day}.c_encoding()] )
					continue;
				for( let time : _times ){
					if( let tick = _zone->to_sys(day+time, choose::earliest); tick>t )
						return time_point_cast<Duration>( tick );
				}
			}
			return t+24h*365;//unreachable:  a day of the week is within a week.
		}
	private:
		α Arm( TimePoint after )ι->void{
			let due = Next( after );
			Schedule( due, [this, due]{
				auto m = Make();
				m.Json["topic"] = Def().Label();
				m.Json["payload"] = ToIsoString( due );
				Send( move(m) );
				Arm( std::max(due, Now()) );
			} );
		}
		optional<Duration> _every;
		vector<std::chrono::seconds> _times;
		std::array<bool,7> _days{ true, true, true, true, true, true, true };
		const std::chrono::time_zone* _zone{ std::chrono::current_zone() };
	};

	//`calendar`.
	constexpr std::array<sv,4> _boundaries{ "shiftStart", "shiftEnd", "breakStart", "breakEnd" };
	Ω schedule( const jobject& props )ε->sp<const Calendar::Schedule>{
		let named = Prop::Find( props, "calendar" );
		THROW_IF( !named || (!named->is_string() && !named->is_number()), "`calendar` is required, a calendar's slug or id." );
		auto y = named->is_string() ? Calendar::Client::Calendars::Find( sv{named->get_string()} ) : Calendar::Client::Calendars::Find( named->to_number<uint32>() );
		THROW_IF( !y, "Calendar {} is no calendar the gateway holds:  the AppServer has none by that name, it is deleted, or the gateway may not read the calendars.", serialize(*named) );
		return y;
	}
	Ω boundaries( const jobject& props )ε->std::array<bool,4>{
		std::array<bool,4> y{ true, true, true, true };
		if( let named = Prop::Find(props, "boundaries") ){
			THROW_IF( !named->is_array(), "`boundaries` must be a list of {}.", Str::Join(_boundaries, ", ") );
			y.fill( false );
			for( let& name : named->get_array() ){
				let p = name.is_string() ? std::ranges::find( _boundaries, sv{name.get_string()} ) : _boundaries.end();
				THROW_IF( p==_boundaries.end(), "`boundaries` holds {}, which is not one of {}.", serialize(name), Str::Join(_boundaries, ", ") );
				y[p-_boundaries.begin()] = true;
			}
		}
		return y;
	}
	struct CalendarIn final : Flows::ISource{
		CalendarIn( const NodeDef& def )ε:
			_id{ schedule(def.Props)->Id() },
			_wanted{ boundaries(def.Props) },
			_self{ ms<Tether<CalendarIn>>(*this) }
		{}
		~CalendarIn(){ Close(); }
		α Start()ε->void override{
			THROW_IF( !Calendar::Client::Calendars::Find(_id), "Its calendar is no longer one the gateway holds." );
			_listening = Calendar::Client::Calendars::Listen( [self=_self, id=_id]( const flat_set<uint32>& changed ){
				if( changed.contains(id) )
					self->Post( []( CalendarIn& node ){ node.Arm( node.Now() ); } );
			} );
			Arm( Now() );
		}
		α Stop()ι->void override{ Close(); }
		α OnStrand( absl::AnyInvocable<void()> f )ι->void{ Post( std::move(f) ); }
		//The strand's:  the calendar's next boundary after `t`, as its snapshot now has it.
		α Arm( TimePoint t )ι->void{
			if( auto timer = std::exchange(_timer, 0); timer )
				Cancel( timer );
			let calendar = Calendar::Client::Calendars::Find( _id );
			if( !calendar ){
				SetStatus( {"red", "ring", "Its calendar is gone."} );
				return;
			}
			let next = calendar->Next( t );
			if( !next ){
				SetStatus( {"grey", "ring", "Nothing is planned."} );
				return;
			}
			SetStatus( {"green", "dot", Ƒ("next {}", ToIsoString(*next))} );
			_timer = Schedule( *next, [this, due=*next]{
				_timer = 0;
				Tick( due );
				Arm( std::max(due, Now()) );
			} );
		}
	private:
		α Close()ι->void{
			_self->Cut();
			if( let handle = std::exchange(_listening, nullopt) )
				Calendar::Client::Calendars::StopListen( *handle );
		}
		//Each boundary that falls at `t`, of those the node takes, a message with its occurrence.
		α Tick( TimePoint t )ι->void{
			let calendar = Calendar::Client::Calendars::Find( _id );
			if( !calendar )
				return;
			let emit = [&]( uint boundary, const Calendar::Occurrence& occurrence ){
				if( !_wanted[boundary] )
					return;
				auto m = Make();
				m.Json["topic"] = calendar->Slug();
				m.Json["payload"] = _boundaries[boundary];
				m.Json["occurrence"] = Calendar::ToJson( occurrence );
				Send( move(m) );
			};
			for( let& occurrence : calendar->Occurrences(t-1s, t+1s) ){
				if( occurrence.End==t )
					emit( 1, occurrence );
				for( let& rest : occurrence.Breaks ){
					if( rest.End==t )
						emit( 3, occurrence );
					if( rest.Start==t )
						emit( 2, occurrence );
				}
				if( occurrence.Start==t )
					emit( 0, occurrence );
			}
		}
		const uint32 _id;
		const std::array<bool,4> _wanted;
		sp<Tether<CalendarIn>> _self;
		optional<uint> _listening;
		TimerId _timer{};
	};

	//`connection`.
	struct ConnectionIn final : Flows::ISource{
		ConnectionIn( const NodeDef& def )ε:
			_slug{ connection(def.Props) },
			_self{ ms<Tether<ConnectionIn>>(*this) }
		{}
		~ConnectionIn(){ Close(); }
		α Start()ε->void override{
			//Heard on the thread that made the change, which only posts:  a loss when the slug's last session went, and the return
			//at the next activation, in the order they came.
			_observer = UAClient::ObserveConnections( [self=_self, slug=_slug]( const ConnectionChange& change ){
				using enum ConnectionChange::EKind;
				if( change.Slug!=slug || change.Kind==Failed || (change.Kind!=Activated && !change.Down) )
					return;
				self->Post( [connected=change.Kind==Activated]( ConnectionIn& node ){ node.Changed( connected ); } );
			} );
			_connected = UAClient::HasSession( _slug );
			Show();
		}
		α Stop()ι->void override{ Close(); }
		α OnStrand( absl::AnyInvocable<void()> f )ι->void{ Post( std::move(f) ); }
		α Changed( bool connected )ι->void{
			if( connected==_connected )
				return;
			_connected = connected;
			let now = Now();
			auto m = Make();
			m.Json["topic"] = _slug;
			m.Json["payload"] = jobject{ {"connected", connected}, {"since", ToIsoString(now)} };
			Send( move(m) );
			Show();
		}
	private:
		α Show()ι->void{ SetStatus( _connected ? NodeStatus{"green", "dot", "connected"} : NodeStatus{"red", "ring", "not connected"} ); }
		α Close()ι->void{
			_self->Cut();
			if( let handle = std::exchange(_observer, nullopt) )
				UAClient::UnobserveConnections( *handle );
		}
		const ServerCnnctnNK _slug;
		sp<Tether<ConnectionIn>> _self;
		optional<uint> _observer;
		bool _connected{};
	};

	//`opc out`.
	struct OpcOut final : Flows::ISink{
		OpcOut( const NodeDef& def, str flow, bool captured, SL sl )ε:
			_target{ target(def.Props, sl) },
			_property{ Prop::Text(def.Props, "property", "payload") },
			_collector{ HistHost::Instance().Collector(_target.Slug) },
			_label{ Ƒ("{}.{}", flow, def.Id) },
			_captured{ captured },
			_self{ ms<Tether<OpcOut>>(*this) }
		{}
		~OpcOut(){ _self->Cut(); }
		α Stop()ι->void override{ _self->Cut(); }
		α OnStrand( absl::AnyInvocable<void()> f )ι->void{ Post( std::move(f) ); }
		//One write is out at a time, so a node's values reach its server in the order they came:  the rest wait here, the
		//oldest dropped past the flow's queue.  A replay's writes none:  the value is its capture.
		α Receive( Message& m )ε->void override{
			let value = _property.Find( m.Json );
			THROW_IF( !value, "The message has no `{}` to write.", _property.Text() );
			if( _captured ){
				Capture( m.Id(), *value );
				return;
			}
			Hold();
			_waiting.push_back( move(m) );
			if( _waiting.size()>QueueLimit()+1 ){
				let dropped = _waiting[1].Id();//the first is out.
				_waiting.erase( _waiting.begin()+1 );
				Drop( dropped );
			}
			if( _waiting.size()==1 )
				Write();
		}
		α Answered( StatusCode status, string error )ι->void{
			auto m = move( _waiting.front() );
			_waiting.pop_front();
			let id = m.Id();
			if( error.empty() ){
				SetStatus( {"green", "dot", UAException::Message(status)} );
				m.Json["status"] = status;
				Send( move(m) );
				Done( id );
			}
			else{
				SetStatus( {"red", "ring", error} );
				Fail( move(m), move(error) );
			}
			if( !_waiting.empty() )
				Write();
		}
	private:
		α Write()ι->void{ Writing( _self, _target, _collector, _label, *_property.Find(_waiting.front().Json) ); }
		//The value toward the node's DataType, as updateVariable converts it, written on the connection's collector client and
		//logged with its answer:  a flow driving a PLC under the gateway's credential (docs/servers/spec.md *Writes*).
		Ω Writing( sp<Tether<OpcOut>> node, OpcTarget target, Credential collector, string label, jvalue value )ι->VoidTask{
			StatusCode status{ UA_STATUSCODE_GOOD };
			string error;
			try{
				ConnectAwait connect{ string{target.Slug}, collector };//an lvalue:  ConnectAwait is neither copied nor moved.
				auto client = co_await Any( connect );
				let id = co_await ResolveAwait{ target, client };
				let resolved = co_await ValueTypesAwait{ {id}, client };
				if( let sc = resolved.Statuses[0]; UA_StatusCode_isBad(sc) )
					throw UAException{ sc, "Could not read the node's DataType." };
				WriteAwait write{ id, Value{value, resolved.Types[0]}, client };
				co_await Any( write );
			}
			catch( const std::exception& e ){
				error = e.what();
				let ua = dynamic_cast<const UAException*>( &e );
				status = ua && ua->Code() ? (StatusCode)ua->Code() : (StatusCode)UA_STATUSCODE_BADUNEXPECTEDERROR;
			}
			INFO( "[{}]wrote {} to '{}' on '{}':  {}{}", label, value.is_double() ? Ƒ("{}", value.get_double()) : serialize(value), target.Text, target.Slug, UAException::Message(status), error.empty() ? string{} : Ƒ(" - {}", error) );
			node->Post( [status, error=move(error)]( OpcOut& out )mutable{ out.Answered( status, move(error) ); } );
		}
		const OpcTarget _target;
		const Flows::Path _property;
		const Credential _collector;
		const string _label;
		const bool _captured;
		sp<Tether<OpcOut>> _self;
		std::deque<Message> _waiting;//the first is out, the rest held behind it.
	};

	//`log`.
	constexpr std::array<sv,5> _levels{ "Trace", "Debug", "Information", "Warning", "Error" };//the `debug` node's.
	struct LogOut final : Flows::ISink{
		LogOut( const NodeDef& def, str flow, bool captured=false )ε:
			_template{ Prop::Text(def.Props, "template", "{{payload}}") },
			_flow{ flow },
			_level{ (ELogLevel)Prop::Choice(def.Props, "level", _levels, (uint)ELogLevel::Information) },
			_captured{ captured }
		{}
		α Receive( Message& m )ε->void override{
			let line = _template.Render( m, FlowContext(), Global(), false );
			if( _captured )//a replay's:  the line, in its trace and not the log.
				Capture( m.Id(), jvalue{line} );
			else
				LOG( _level, _tags, "[{}.{}]{}", _flow, Def().Label(), line );
		}
	private:
		Flows::Mustache _template;
		const string _flow;
		const ELogLevel _level;
		const bool _captured;
	};

	//`http request`.
	struct HttpSend final{ string Host; string Target; PortType Port; bool Ssl; http::verb Verb; string Body; string ContentType; };
	constexpr std::array<sv,2> _outputs{ "text", "json" };
	Ω allowed( sv host )ι->bool{ return Flows::HostAllowed( host, Settings::FindStringArray("/gateway/flows/http/allow") ); }
	//A url as its request is sent:  http or https, the host without an IPv6 literal's brackets, the scheme's port without one.
	//One with a user is refused:  the host the list is asked about is the whole of what the request connects to.
	Ω parseUrl( sv url )ε->HttpSend{
		let parts = Str::ParseUrl( url );
		let scheme = Str::ToLower( string{parts.Scheme} );
		THROW_IF( scheme!="http" && scheme!="https", "'{}' is not an http or https url.", url );
		HttpSend y{ .Host=string{parts.Host}, .Target=parts.Path.empty() ? string{"/"} : string{parts.Path}, .Port=PortType(scheme=="https" ? 443 : 80), .Ssl=scheme=="https", .Verb=http::verb::get, .Body={}, .ContentType={} };
		if( y.Host.size()>1 && y.Host.front()=='[' && y.Host.back()==']' )
			y.Host = y.Host.substr( 1, y.Host.size()-2 );
		THROW_IF( y.Host.empty(), "'{}' names no host.", url );
		THROW_IF( y.Host.contains('@'), "'{}' carries a user, which a flow's request does not send.", url );
		if( parts.Port.size() ){
			let port = Str::TryTo<PortType>( string{parts.Port} );
			THROW_IF( !port, "'{}' has a port that is not one.", url );
			y.Port = *port;
		}
		return y;
	}
	Ω verb( sv method )ε->http::verb{
		let y = http::string_to_verb( Str::ToUpper(string{method}) );
		THROW_IF( y==http::verb::unknown, "'{}' is not an http method.", method );
		return y;
	}
	struct HttpOut final : Flows::ISink{
		HttpOut( const NodeDef& def, bool captured=false )ε:
			_contentType{ Prop::FindText(def.Props, "contentType") },
			_json{ Prop::Choice(def.Props, "output", _outputs, 0)==1 },
			_captured{ captured },
			_self{ ms<Tether<HttpOut>>(*this) }{
			if( let url = Prop::FindText(def.Props, "url") )
				_url = parseUrl( *url );
			if( let method = Prop::FindText(def.Props, "method") )
				_verb = verb( *method );
		}
		~HttpOut(){ _self->Cut(); }
		α Stop()ι->void override{ _self->Cut(); }
		α OnStrand( absl::AnyInvocable<void()> f )ι->void{ Post( std::move(f) ); }
		α Url()Ι->const optional<HttpSend>&{ return _url; }
		//The props' url, method and content type, else the message's `url` and `method`, with its payload the body of a
		//method that has one.  The host is checked here, at the send, whatever a deploy saw.  MaxRequests are out at once:
		//the rest wait here, the oldest dropped past the flow's queue.  A replay's sends none:  the request is its capture.
		α Receive( Message& m )ε->void override{
			let named = Json::FindSV( m.Json, "url" );
			THROW_IF( !_url && !named, "The node has no `url`, and the message none." );
			auto send = _url ? *_url : parseUrl( *named );
			THROW_IF( !allowed(send.Host), "'{}' is not a host /gateway/flows/http/allow lets a flow reach.", send.Host );
			let method = Json::FindSV( m.Json, "method" );
			send.Verb = _verb ? *_verb : method ? verb( *method ) : http::verb::get;
			if( send.Verb!=http::verb::get && send.Verb!=http::verb::head && send.Verb!=http::verb::delete_ ){
				let& payload = m.Payload();
				send.Body = payload.is_string() ? string{ payload.get_string() } : payload.is_null() ? string{} : serialize( payload );
				send.ContentType = _contentType ? *_contentType : payload.is_string() ? "text/plain" : "application/json";
			}
			if( _captured ){
				let port = send.Port==(send.Ssl ? 443 : 80) ? string{} : Ƒ( ":{}", send.Port );
				jobject request{ {"method", http::to_string(send.Verb)}, {"url", Ƒ("{}://{}{}{}", send.Ssl ? "https" : "http", send.Host.contains(':') ? Ƒ("[{}]", send.Host) : send.Host, port, send.Target)} };
				if( send.ContentType.size() ){
					request["contentType"] = send.ContentType;
					request["body"] = send.Body;
				}
				Capture( m.Id(), request );
				return;
			}
			Hold();
			_waiting.emplace_back( move(m), move(send) );
			if( _waiting.size()>QueueLimit() ){
				let dropped = _waiting.front().first.Id();
				_waiting.pop_front();
				Drop( dropped );
			}
			Next();
		}
		α Answered( Message&& m, string error )ι->void{
			--_out;
			let id = m.Id();
			if( error.size() ){
				SetStatus( {"red", "ring", error} );
				Fail( move(m), move(error) );
			}
			else{
				SetStatus( {"green", "dot", Ƒ("{}", Json::FindNumber<uint>(m.Json, "statusCode").value_or(0))} );
				Send( move(m) );
				Done( id );
			}
			Next();
		}
	private:
		α Next()ι->void{
			while( _out<FlowNodes::MaxRequests && !_waiting.empty() ){
				auto [m, send] = move( _waiting.front() );
				_waiting.pop_front();
				++_out;
				Requesting( _self, move(m), move(send), _json );
			}
		}
		//The request, and each redirect it is answered with, to a host the list allows:  the await follows none itself, so
		//every hop is checked as the first was.  One to a host outside the list ends the request with the redirect as its answer.
		Ω Requesting( sp<Tether<HttpOut>> node, Message m, HttpSend send, bool json )ι->VoidTask{
			using Web::Client::ClientHttpAwait; using Web::Client::HttpAwaitArgs;
			string error;
			try{
				Web::Client::ClientHttpRes res;
				for( uint hop=0;; ++hop ){
					try{
						res = co_await Any( ClientHttpAwait{send.Host, send.Target, send.Body, send.Port, HttpAwaitArgs{.ContentType=send.ContentType, .Verb=send.Verb, .IsSsl=send.Ssl, .AllowRedirects=false}} );
					}
					catch( const Web::Client::ClientHttpResException& e ){
						if( !e.Res().IsRedirect() )
							throw;
						res = e.Res();
					}
					if( !res.IsRedirect() )
						break;
					let status = (uint)res.Status();
					let location = res[http::field::location];
					THROW_IF( hop>=FlowNodes::MaxRedirects, "({}) It was redirected {} times, the last to '{}'.", status, hop+1, location );
					auto [host, to, port, ssl] = res.RedirectVariables();
					if( host.size() ){
						if( host.size()>1 && host.front()=='[' && host.back()==']' )
							host = host.substr( 1, host.size()-2 );
						THROW_IF( !allowed(host), "({}) It was redirected to '{}', whose host /gateway/flows/http/allow does not let a flow reach.", status, location );
						send.Host = move( host );
						send.Port = port;
						send.Ssl = ssl;
					}
					send.Target = move( to );
					if( status<307 && send.Verb!=http::verb::get && send.Verb!=http::verb::head ){//as a browser follows a 301, 302 or 303.
						send.Verb = http::verb::get;
						send.Body.clear();
						send.ContentType.clear();
					}
				}
				m.Json["statusCode"] = (uint)res.Status();
				jobject headers;
				for( let& field : res.Headers() )
					headers[field.name_string()] = field.value();
				m.Json["headers"] = move( headers );
				m.Json["payload"] = json && res.Body().size() ? parse( res.Body() ) : jvalue{ res.Body() };
			}
			catch( const std::exception& e ){
				error = e.what();
			}
			node->Post( [m=move(m), error=move(error)]( HttpOut& out )mutable{ out.Answered( move(m), move(error) ); } );
		}
		optional<HttpSend> _url;
		optional<http::verb> _verb;
		const optional<string> _contentType;
		const bool _json;
		const bool _captured;
		sp<Tether<HttpOut>> _self;
		std::deque<std::pair<Message,HttpSend>> _waiting;
		uint _out{};//requests out.
	};

	//A `link out` whose `link in` another flow holds.
	struct LinkOut final : Flows::ISink{
		LinkOut( const NodeDef& def, FlowHost& host, const FlowRun& run, bool captured )ι:
			_name{ Json::FindDefaultSV(def.Props, "name") }, _host{ host }, _flow{ run.Flow }, _deployer{ run.Deployer }, _captured{ captured }
		{}
		α Receive( Message& m )ε->void override{
			if( _captured ){//a replay's reaches no running flow:  the message is its capture.
				Capture( m.Id(), m.Json );
				return;
			}
			let taken = _host.Link( _flow, _deployer, _name, m );
			SetStatus( taken ? NodeStatus{"green", "dot", Ƒ("{} {}", taken, taken==1 ? "flow" : "flows")} : NodeStatus{"grey", "ring", "No running flow holds its `link in`."} );
		}
	private:
		const string _name;
		FlowHost& _host;
		const FlowPK _flow;
		const UserPK _deployer;
		const bool _captured;
	};

	//A replay's records of a group (spec *Replay*):  its members' over the range, through the group's raw read, a page of
	//`readLimit` a call, the edits applied, each as its node sends it live.  A heartbeat, which repeats a value, is none,
	//so a page of them answers no record.
	struct GroupStream final : Flows::IStream{
		GroupStream( sp<Opc::Hist::Group> group, ServerCnnctnNK slug, GroupMembers members, const FlowReplay& range )ι:
			_group{ move(group) }, _slug{ move(slug) }, _members{ move(members) }, _last{ range.From }, _done{ _members.empty() }{
			for( let& member : _members )
				_request.Nodes.push_back( member.first );
			_request.Start = UADateTime{ range.From }.UA();
			_request.End = UADateTime{ range.To }.UA();
			_request.Limit = range.ReadLimit;
		}
		α Next()ε->optional<vector<Flows::StreamRecord>> override{
			if( _done )
				return nullopt;
			let page = _group->Read( _request );
			_request.Continuation = page.Continuation;
			_done = page.Continuation.empty();
			let types = UAClient::FindTypes( _slug );
			vector<Flows::StreamRecord> y;
			for( let& read : page.Values ){
				let& value = read.Value;
				let member = recorded( _members, value );
				Message m;
				if( !member || !dataChange(m, _slug, *member, value, types.get()) )
					continue;
				if( value.has_source_ts() || value.has_server_ts() )
					_last = UADateTime{ (UA_DateTime)(value.has_source_ts() ? value.source_ts() : value.server_ts()) }.Time();
				y.emplace_back( _last, move(m.Json) );
			}
			return y;
		}
	private:
		const sp<Opc::Hist::Group> _group;
		const ServerCnnctnNK _slug;
		const GroupMembers _members;
		Hist::ReadRequest _request;
		TimePoint _last;//the time of the last record, which one with none takes.
		bool _done;
	};

	//`variable`.
	//The message as the variable's value (spec *Derived variables*):  its `payload` read toward the type, its `status` or
	//Good, its `sourceTime` or `_time` the source time, else `now`, the flow's clock's, which is the server time.  A
	//payload of none is a status alone, which a Bad data change's message is, over the type's zero, since the server takes
	//a number no other way;  with a Good one there is nothing to write.
	Ω variableValue( const FlowVariable& variable, const Message& m, TimePoint now )ε->Value{
		let& payload = m.Payload();
		let status = Json::FindNumber<StatusCode>( m.Json, "status" ).value_or( UA_STATUSCODE_GOOD );
		THROW_IF( payload.is_null() && !UA_StatusCode_isBad(status), "The message has no `payload` to write." );
		Value y = payload.is_null() ? FlowServer::Zero( variable, status ) : Value{ payload, variable.Type };
		THROW_IF( !payload.is_null() && y.IsScalar()==variable.Array, "`payload` is {}, and variable '{}' is {}.", variable.Array ? "no array" : "an array", variable.Name, variable.TypeName() );
		y.status = status;
		y.hasStatus = status!=UA_STATUSCODE_GOOD;
		auto source = Json::FindTimePoint( m.Json, "sourceTime" );
		if( !source )
			source = Json::FindTimePoint( m.Json, "_time" );
		y.sourceTimestamp = UADateTime{ source.value_or(now) }.UA();
		y.hasSourceTimestamp = true;
		y.serverTimestamp = UADateTime{ now }.UA();
		y.hasServerTimestamp = true;
		return y;
	}
	struct VariableOut final : Flows::ISink{
		//Live, a replay's with `captured`, and a committed one's with its `commits`, the group that holds the variable and its
		//member there.
		VariableOut( const NodeDef& def, str flow, bool captured, sp<FlowCommits> commits, sp<HistGroupHost> group, Hist::NodeIndex member, SL sl )ε:
			_variable{ def.Props, sl }, _flow{ flow }, _captured{ captured }, _commits{ move(commits) }, _group{ move(group) }, _member{ member }
		{}
		α Start()ε->void override{
			if( _captured )
				return;
			_server = FlowServer::Instance();
			THROW_IF( !_server, "The flow server is not running, so variable '{}' has no node:  the gateway's log says why.", _variable.Name );
			if( !_target )
				_target = ms<const FlowServer::Target>( _flow, _variable.Name );
			SetStatus( {"grey", "ring", _variable.TypeName()} );
		}
		α Stop()ι->void override{ _server = nullptr; }
		α Receive( Message& m )ε->void override{
			auto value = variableValue( _variable, m, Now() );
			if( _captured ){//a replay's:  the value, in its trace, and for a committed run in its group's history too.
				Capture( m.Id(), m.Payload() );
				if( _commits )
					_commits->Add( _group, _member, _variable.Name, move(value) );
			}
			else
				_server->Write( _target, move(value) );
		}
	private:
		const FlowVariable _variable;
		const string _flow;
		const bool _captured;
		const sp<FlowCommits> _commits;
		const sp<HistGroupHost> _group;
		const Hist::NodeIndex _member;
		sp<FlowServer> _server;
		sp<const FlowServer::Target> _target;//made at the first start:  each value's node id.
	};

	FlowCommits::FlowCommits( Hist::Writer by, TimePoint from, TimePoint to )ι:
		_by{ move(by) }, _from{ UADateTime{from}.UA() }, _to{ UADateTime{to}.UA() }
	{}
	α FlowCommits::Hold( sp<HistGroupHost> group, Hist::NodeIndex node, sv variable )ι->void{
		lg _{ _mutex };
		let id = group->Id();
		_held.try_emplace( std::pair{id, node}, Held{move(group), string{variable}, _from-1} );
	}
	α FlowCommits::Fail( uint count, sv variable, sv why )ι->void{
		if( _totals.First.empty() )
			_totals.First = Ƒ( "variable '{}':  {}", variable, why );
		_totals.Failed += count;
	}
	α FlowCommits::Add( sp<HistGroupHost> group, Hist::NodeIndex node, sv variable, Value&& value )ι->void{
		optional<Batch> batch;
		{
			lg _{ _mutex };
			if( let time = value.sourceTimestamp; time<_from || time>_to ){//a body's msg.sourceTime:  history the run doesn't own.
				Fail( 1, variable, Ƒ("its source time, {}, is outside the run's range", ToIsoString(UADateTime{time}.Time())) );
				return;
			}
			auto& pending = _pending[group->Id()];
			if( !pending.Group )
				pending.Group = move( group );
			auto& values = pending.Nodes[node];
			if( values.Variable.empty() )
				values.Variable = variable;
			values.Values.push_back( move(value) );
			++pending.Waiting;
			++_waiting;
			batch = Send();
		}
		Start( move(batch) );
	}
	α FlowCommits::Complete()ι->void{
		optional<Batch> batch;
		{
			lg _{ _mutex };
			if( _totals.Failed || _totals.Uncleared )//a run that fails clears no further than its values went.
				return;
			for( let& [key, held] : _held ){
				if( held.Cleared>=_to )
					continue;
				auto& pending = _pending[key.first];
				if( !pending.Group )
					pending.Group = held.Group;
				auto& through = pending.Nodes[key.second];
				if( through.Variable.empty() )
					through.Variable = held.Variable;
				through.Through = _to;
				++pending.Waiting;
				++_waiting;
			}
			batch = Send();
		}
		Start( move(batch) );
	}
	α FlowCommits::Send()ι->optional<Batch>{
		if( _out || _pending.empty() )
			return nullopt;
		auto p = _served ? _pending.upper_bound( *_served ) : _pending.begin();
		if( p==_pending.end() )
			p = _pending.begin();
		_served = p->first;
		_out = true;
		auto batch = move( p->second );
		_pending.erase( p );
		for( auto&& [node, values] : batch.Nodes ){//the range up to its last value cleared, from where the batch before left off.
			auto& held = _held.try_emplace( std::pair{*_served, node}, Held{batch.Group, values.Variable, _from-1} ).first->second;
			auto through = values.Through;
			for( let& v : values.Values )
				through = std::max( through, v.sourceTimestamp );
			if( through>held.Cleared ){
				values.Clear = std::pair{ held.Cleared+1, through };
				held.Cleared = through;
			}
		}
		return batch;
	}
	α FlowCommits::Start( optional<Batch> batch )ι->void{
		if( batch )
			boost::asio::post( *Executor(), [self=shared_from_this(), batch=move(*batch)]()mutable{ Editing( move(self), move(batch) ); } );
	}
	α FlowCommits::Editing( sp<FlowCommits> self, Batch batch )ι->VoidTask{
		struct Entry final{ string Variable; uint Count; bool Clear; };//Count:  the values of an update.
		vector<Entry> entries;
		vector<Hist::EditDetails> details;
		for( auto&& [node, values] : batch.Nodes ){//a node's delete before its values, as the group applies an edit's entries in order.
			if( values.Clear ){
				entries.push_back( {values.Variable, 0, true} );
				details.push_back( Hist::DeleteRaw{node, values.Clear->first, values.Clear->second} );
			}
			if( values.Values.size() ){
				entries.push_back( {values.Variable, values.Values.size(), false} );
				details.push_back( Hist::UpdateData{node, UA_PERFORMUPDATETYPE_UPDATE, move(values.Values)} );
			}
		}
		vector<Hist::EditResult> results;
		string thrown;
		if( details.size() ){
			try{
				results = co_await batch.Group->Group()->Edit( move(details), self->_by );
			}
			catch( const std::exception& e ){
				thrown = e.what();
			}
		}
		optional<Batch> next;
		vector<absl::AnyInvocable<void()>> told;
		{
			lg _{ self->_mutex };
			auto& totals = self->_totals;
			for( uint i=0; i<entries.size(); ++i ){
				let& entry = entries[i];
				let why = thrown.size() ? thrown : i>=results.size() ? string{ "its edit had no answer" } : string{};
				if( entry.Clear ){
					let status = why.empty() ? results[i].Status : (StatusCode)UA_STATUSCODE_GOOD;
					if( why.size() || (UA_StatusCode_isBad(status) && status!=UA_STATUSCODE_BADNODATA) ){//Bad_NoData:  a range with nothing in it.
						if( totals.First.empty() )
							totals.First = Ƒ( "variable '{}', whose history over the range could not be cleared:  {}", entry.Variable, why.size() ? why : UAException::Message(status) );
						++totals.Uncleared;
					}
				}
				else if( why.size() )
					self->Fail( entry.Count, entry.Variable, why );
				else if( let& result = results[i]; UA_StatusCode_isBad(result.Status) )
					self->Fail( entry.Count, entry.Variable, UAException::Message(result.Status) );
				else{
					for( uint v=0; v<entry.Count; ++v ){
						if( let sc = v<result.Results.size() ? result.Results[v] : (StatusCode)UA_STATUSCODE_BADUNEXPECTEDERROR; UA_StatusCode_isBad(sc) )
							self->Fail( 1, entry.Variable, UAException::Message(sc) );
						else
							++totals.Written;
					}
				}
			}
			self->_waiting -= batch.Waiting;
			self->_out = false;
			next = self->Send();
			told = self->Tell();
		}
		self->Start( move(next) );
		for( auto& f : told )
			f();
	}
	α FlowCommits::Tell()ι->vector<absl::AnyInvocable<void()>>{
		vector<absl::AnyInvocable<void()>> y;
		if( _waiting<=Buffer/2 ){
			for( auto& f : _eased )
				y.push_back( std::move(f) );
			_eased.clear();
		}
		if( !_waiting ){
			for( auto& f : _drained )
				y.push_back( std::move(f) );
			_drained.clear();
		}
		return y;
	}
	α FlowCommits::Drained( absl::AnyInvocable<void()> f )ι->void{
		{
			lg _{ _mutex };
			if( _waiting ){
				_drained.push_back( std::move(f) );
				return;
			}
		}
		f();
	}
	α FlowCommits::Eased( absl::AnyInvocable<void()> f )ι->void{
		{
			lg _{ _mutex };
			if( _waiting>Buffer/2 ){
				_eased.push_back( std::move(f) );
				return;
			}
		}
		f();
	}

	FlowNodes::FlowNodes( FlowHost& host, FlowRun run, optional<FlowReplay> replay )ι:
		_host{ host }, _run{ move(run) }, _replay{ move(replay) },
		_commits{ _replay && _replay->Commit ? ms<FlowCommits>(HistHost::Instance().Writer(_run.Deployer), _replay->From, _replay->To) : nullptr }
	{}
	α FlowNodes::Source( const NodeDef& node )ε->up<Flows::ISource>{
		let sl = SRCE_CUR;
		switch( node.Type ){
		case ENodeType::OpcIn: return mu<OpcIn>( node, _run.Slug, sl );
		case ENodeType::GroupIn:{
			let group = _run.Groups.find( node.Id );
			THROW_IF( group==_run.Groups.end(), "Its group isn't one this gateway historizes." );
			return mu<GroupIn>( node, group->second, sl );
		}
		case ENodeType::Timer: return mu<Timer>( node );
		case ENodeType::Calendar: return mu<CalendarIn>( node );
		case ENodeType::Connection: return mu<ConnectionIn>( node );
		default: THROW( "A '{}' is no source the gateway makes.", Flows::ToString(node.Type) );
		}
	}
	α FlowNodes::Sink( const NodeDef& node )ε->up<Flows::ISink>{
		let sl = SRCE_CUR;
		let captured = _replay.has_value();
		switch( node.Type ){
		case ENodeType::Variable:{
			sp<HistGroupHost> group; Hist::NodeIndex member{};
			if( _commits ){//the group the run's start found holding the variable, and its member there.
				let held = _run.Groups.find( node.Id );
				group = held==_run.Groups.end() ? nullptr : HistHost::Instance().Find( held->second );
				THROW_IF( !group, "Its variable is in no group this gateway historizes:  a committed run has no history to write it into." );
				let index = group->Index( ParseHistNode(FlowServer::NodeText(_run.Slug, Json::FindDefaultSV(node.Props, "name")), sl) );
				THROW_IF( !index, "Its variable is no member of group '{}'.", group->Name() );
				member = *index;
				_commits->Hold( group, member, Json::FindDefaultSV(node.Props, "name") );
			}
			return mu<VariableOut>( node, _run.Slug, captured, _commits, move(group), member, sl );
		}
		case ENodeType::OpcOut: return mu<OpcOut>( node, _run.Slug, captured, sl );
		case ENodeType::Log: return mu<LogOut>( node, _run.Slug, captured );
		case ENodeType::HttpRequest: return mu<HttpOut>( node, captured );
		case ENodeType::LinkOut: return mu<LinkOut>( node, _host, _run, captured );
		default: THROW( "A '{}' is no sink the gateway makes.", Flows::ToString(node.Type) );
		}
	}
	α FlowNodes::Stream( const NodeDef& node )ε->up<Flows::IStream>{
		let sl = SRCE_CUR;
		THROW_IF( !_replay, "A '{}' has its records in a replay alone.", Flows::ToString(node.Type) );
		let held = _run.Groups.find( node.Id );
		let host = held==_run.Groups.end() ? nullptr : HistHost::Instance().Find( held->second );
		GroupMembers members;
		if( node.Type==ENodeType::OpcIn ){
			let to = target( node.Props, sl );
			THROW_IF( held==_run.Groups.end(), "'{}' on '{}' is in no group:  a replay has no records of it.", to.Text, to.Slug );
			THROW_IF( !host, "Its group isn't one this gateway historizes." );
			let spelled = to.Node.namespaceUri.length ? _run.Nodes.end() : _run.Nodes.find( node.Id );//by its URI, which needs no server's namespaces.
			let index = host->Index( spelled==_run.Nodes.end() ? to.Node : ParseHistNode(spelled->second, sl) );
			THROW_IF( !index, "'{}' is no member of group '{}'.", to.Text, host->Name() );
			members.emplace( *index, GroupMember{to.Text, to.Text} );
		}
		else{
			THROW_IF( node.Type!=ENodeType::GroupIn, "A '{}' has no records.", Flows::ToString(node.Type) );
			THROW_IF( !host, "Its group isn't one this gateway historizes." );
			members = groupMembers( *host, GroupInProps{node.Props, sl} );
		}
		return mu<GroupStream>( host->Group(), host->Slug(), move(members), *_replay );
	}
	α FlowNodes::Check( const NodeDef& node, SL sl )ε->void{
		switch( node.Type ){
		case ENodeType::Variable: FlowVariable{ node.Props, sl }; break;
		case ENodeType::OpcIn:
			target( node.Props, sl );
			parameters( node.Props );
			break;
		case ENodeType::GroupIn: GroupInProps{ node.Props, sl }; break;
		case ENodeType::Timer: Timer{ node }; break;
		case ENodeType::Calendar:
			schedule( node.Props );
			boundaries( node.Props );
			break;
		case ENodeType::Connection: connection( node.Props ); break;
		case ENodeType::OpcOut:
			target( node.Props, sl );
			Flows::Path{ Prop::Text(node.Props, "property", "payload") };
			break;
		case ENodeType::Log: LogOut{ node, {} }; break;
		case ENodeType::HttpRequest:{
			HttpOut out{ node };
			if( let& url = out.Url(); url )
				THROW_IF( !allowed(url->Host), "`url`'s host '{}' is not one /gateway/flows/http/allow lets a flow reach.", url->Host );
			break;
		}
		default:
			break;
		}
	}
	α FlowNodes::GroupChanged( HistGroupPK group )ι->void{
		vector<sp<Tether<GroupIn>>> nodes;
		{
			lg _{ _groupInsMutex };
			for( let& [id, node] : _groupIns ){
				if( auto p = !group || id==group ? node.lock() : nullptr )//no group:  a member's purge, which names none.
					nodes.push_back( move(p) );
			}
		}
		for( let& node : nodes )
			node->Post( []( GroupIn& in ){ in.Resubscribe( false ); } );
	}
}