#include "FlowNodes.h"
#include <deque>
#include <boost/beast/http/verb.hpp>
#include <jde/fwk/settings.h>
#include <jde/fwk/str.h>
#include <jde/web/client/http/ClientHttpAwait.h>
#include <jde/web/client/http/ClientHttpResException.h>
#include <jde/calendar/client/Calendars.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/flows/Allow.h>
#include <jde/flows/Template.h>
#include "../async/ValueTypesAwait.h"
#include "../async/WriteAwait.h"
#include "../hist/HistHost.h"
#include "../UAClient.h"
#include "FlowHost.h"

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

	//A node's props, read to their types:  what each throws is the node's fault, so it names the key.
	Ω find( const jobject& props, sv key )ι->const jvalue*{
		let v = props.if_contains( key );
		return v && !v->is_null() ? v : nullptr;
	}
	Ω findText( const jobject& props, sv key )ε->optional<sv>{
		let v = find( props, key );
		THROW_IF( v && !v->is_string(), "`{}` must be text.", key );
		return v ? optional<sv>{ v->get_string() } : nullopt;
	}
	Ω flag( const jobject& props, sv key, bool dflt )ε->bool{
		let v = find( props, key );
		THROW_IF( v && !v->is_bool(), "`{}` must be true or false.", key );
		return v ? v->get_bool() : dflt;
	}
	Ω findNumber( const jobject& props, sv key )ε->optional<double>{
		let v = find( props, key );
		THROW_IF( v && !v->is_number(), "`{}` must be a number.", key );
		return v ? optional<double>{ v->to_number<double>() } : nullopt;
	}
	Ω findWait( const jobject& props, sv key )ε->optional<Duration>{
		let text = findText( props, key );
		if( !text )
			return nullopt;
		let y = Chrono::TryToDuration( *text );
		THROW_IF( !y || *y<Duration::zero(), "`{}` must be an ISO 8601 duration, as PT30S.", key );
		return *y;
	}
	Ω choice( const jobject& props, sv key, std::span<const sv> names, uint dflt )ε->uint{
		let text = findText( props, key );
		if( !text )
			return dflt;
		let p = std::ranges::find( names, *text );
		THROW_IF( p==names.end(), "`{}` must be one of {}, not '{}'.", key, Str::Join(names, ", "), *text );
		return p-names.begin();
	}

	//A node of a connection, as an `opc in` and an `opc out` name it.
	struct OpcTarget final{ ServerCnnctnNK Slug; ExNodeId Node; string Text; };
	Ω target( const jobject& props, SL sl )ε->OpcTarget{
		let slug = findText( props, "connection" );
		THROW_IF( !slug || slug->empty(), "`connection` is required, a connection's slug." );
		let named = find( props, "node" );
		THROW_IF( !named, "`node` is required, the node's id." );
		auto ex = ParseHistNode( *named, sl );
		auto text = ex.ToString();
		return { ServerCnnctnNK{*slug}, move(ex), move(text) };
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
		if( let sampling = findNumber(props, "samplingInterval") )//ms:  0 the server's fastest, negative the publishing interval.
			y.SamplingInterval = *sampling;
		if( let queue = findNumber(props, "queueSize") ){
			THROW_IF( *queue<0 || *queue>std::numeric_limits<uint32_t>::max() || *queue!=std::floor(*queue), "`queueSize` must be a whole number." );
			y.QueueSize = (uint32)*queue;
		}
		y.DiscardOldest = flag( props, "discardOldest", true );
		if( let filter = find(props, "filter") ){
			THROW_IF( !filter->is_object(), "`filter` must be an object of trigger, deadbandType and deadbandValue." );
			let& o = filter->get_object();
			DataChangeFilter f;
			f.Trigger = (UA_DataChangeTrigger)choice( o, "trigger", _triggers, UA_DATACHANGETRIGGER_STATUSVALUE );
			f.DeadbandType = (UA_DeadbandType)choice( o, "deadbandType", _deadbands, UA_DEADBANDTYPE_NONE );
			f.DeadbandValue = findNumber( o, "deadbandValue" ).value_or( 0 );
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
		//node's value is read once in its place.
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
					auto values = co_await Any( ReadValueAwait{{id}, client, UA_TIMESTAMPSTORETURN_BOTH} );
					if( let p = values.find(id); p!=values.end() && !listener->Fed.exchange(true) )
						listener->Node.With( [&]( OpcIn& node ){ node.Deliver( p->second, client->Types().get() ); } );
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
	//strand takes them.  More than `limit` of them, and they go:  the node subscribes again from its mark, and reads them
	//from the group.
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

	constexpr std::array<sv,5> _updateTypes{ "unspecified", "insert", "replace", "update", "delete" };
	struct GroupIn final : Flows::ISource{
		GroupIn( const NodeDef& def, HistGroupPK group, SL sl )ε:
			_group{ group },
			_modifications{ flag(def.Props, "modifications", false) },
			_lateness{ findWait(def.Props, "lateness") },
			_self{ ms<Tether<GroupIn>>(*this) }{
			let from = findText( def.Props, "from" ).value_or( "resume" );
			if( from=="now" )
				_now = true;
			else if( from!="resume" ){
				try{
					_start = Chrono::ToTimePoint( string{from}, sl );
				}
				catch( const std::exception& ){
					THROW( "`from` must be resume, now or a time, as 2026-03-09T06:00:00Z, not '{}'.", from );
				}
			}
			if( let members = find(def.Props, "members") ){
				THROW_IF( !members->is_array(), "`members` must be a list of the group's nodes." );
				for( let& member : members->get_array() )
					_wanted.push_back( ParseHistNode(member, sl) );
			}
			THROW_IF( !HistHost::Instance().Find(group), "Its group isn't one this gateway historizes." );
		}
		~GroupIn(){ _self->Cut(); }
		α Start()ε->void override{
			let host = HistHost::Instance().Find( _group );
			THROW_IF( !host, "Its group isn't one this gateway historizes." );
			if( let marks = _now ? nullptr : Context().Get("marks"); marks && marks->is_object() ){
				for( let& mark : marks->get_object() ){
					if( let index = Str::TryTo<Hist::NodeIndex>(sv{mark.key()}); index && mark.value().is_int64() ){
						let ticks = (UA_DateTime)mark.value().get_int64();
						_high[*index] = ticks;
						_mark = std::max( _mark.value_or(ticks), ticks );
					}
				}
			}
			if( !_lateness )
				_lateness = host->Columns().Thresholds().MaxTimeInterval;
			_floor = _start ? UADateTime{ *_start }.UA() : _now || !_mark ? UADateTime{ Now() }.UA() : std::numeric_limits<UA_DateTime>::min();
			_page = std::clamp<uint>( QueueLimit()/4, 1, 1000 );
			Subscribe( *host );
			{
				lg _{ _groupInsMutex };
				_groupIns.emplace_back( _group, _self );
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
		//again.  The page after goes once this one is through the flow (Idle), so a replay stays within the queues.
		α Pump()ε->void{
			if( !_subscription )
				return;
			string failure;
			try{
				if( _paging ){
					let page = _subscription->Next( _page );
					for( let& value : page.Values )
						Deliver( value.Value, value.Bound );
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
					for( let& record : records ){
						if( record.has_value() )
							Deliver( record.value() );
						else if( record.has_modification() && _modifications )
							Modified( record.modification() );
					}
				}
			}
			catch( const std::exception& e ){
				failure = e.what();
			}
			Save();
			if( failure.empty() )
				Idle( [this]{ Pump(); } );
			else{//its files could not be read, or the group has gone:  from the mark again, RetryInterval on.
				SetStatus( {"red", "ring", failure} );
				Schedule( FlowNodes::RetryInterval, [this]{ Resubscribe( true ); } );
				THROW( "{}", failure );
			}
		}
		α Ended( Hist::ESubscriptionEnd why )ι->void{
			_subscription = nullptr;
			SetStatus( {"red", "ring", why==Hist::ESubscriptionEnd::Removed ? "Its group was removed." : "The historian stopped."} );
		}
		//The group's members changed, or a read failed:  subscribed again from the mark, when it is every member this takes
		//and they are not those it has, or `always`.
		α Resubscribe( bool always )ε->void{
			let host = HistHost::Instance().Find( _group );
			if( !host )
				return;
			if( !always ){
				if( _wanted.size() )
					return;
				let members = host->Members();
				if( members.size()==_members.size() && std::ranges::all_of(members, [this]( let& m ){ return _members.contains( m.first ); }) )
					return;
			}
			try{
				Subscribe( *host );
			}
			catch( const std::exception& e ){
				SetStatus( {"red", "ring", e.what()} );
				Schedule( FlowNodes::RetryInterval, [this]{ Resubscribe( true ); } );
				return;
			}
			Pump();
		}
	private:
		struct Member final{ string Node; string Path; };
		//Registered with the group from the mark less the lateness, or from where the node starts, its snapshot to page first.
		α Subscribe( HistGroupHost& host )ε->void{
			if( _subscription )
				_subscription->End();
			_subscription = nullptr;
			_slug = host.Slug();
			_members.clear();
			vector<Hist::NodeIndex> nodes;
			let add = [&]( const HistMember& member ){
				nodes.push_back( member.Index );
				let text = member.Node.ToString();
				_members.emplace( member.Index, Member{text, member.Attributes.Path ? HistPathText(*member.Attributes.Path) : text} );
			};
			let members = host.HistMembers();
			if( _wanted.empty() )
				std::ranges::for_each( members, add );
			for( let& node : _wanted ){
				let index = host.Index( node );
				let member = index ? std::ranges::find( members, *index, &HistMember::Index ) : members.end();
				THROW_IF( member==members.end(), "'{}' is no member of its group.", node.ToString() );
				add( *member );
			}
			if( nodes.empty() ){
				SetStatus( {"grey", "ring", "Its group has no members."} );
				return;
			}
			let lateness = (UA_DateTime)( duration_cast<std::chrono::nanoseconds>(*_lateness).count()/100 );
			_from = _mark ? std::max( _floor, *_mark-lateness ) : _floor;
			_feed = ms<GroupFeed>( _self, QueueLimit() );
			_subscription = host.Group()->Subscribe( _feed, move(nodes), _from );
			_paging = true;
			SetStatus( {"green", "dot", Ƒ("from {}", ToIsoString(UADateTime{_from}.Time()))} );
		}
		//A record as a message, but a heartbeat, which repeats a value, a snapshot's bound from before its time, the value
		//the node had then, and one at or before the last of its node the flow finished, which a restart's lookback reads again.
		α Deliver( const Hist::Proto::DataValue& value, bool bound=false )ι->void{
			if( value.has_heartbeat() )
				return;
			let member = _members.find( value.node_index() );
			if( member==_members.end() )
				return;
			let ticks = value.has_source_ts() ? optional<UA_DateTime>{ value.source_ts() } : value.has_server_ts() ? optional<UA_DateTime>{ value.server_ts() } : nullopt;
			if( bound && (!ticks || *ticks<_from) )
				return;
			let high = _high.find( value.node_index() );
			if( ticks && high!=_high.end() && *ticks<=high->second )
				return;
			auto m = Make();
			try{
				let types = UAClient::FindTypes( _slug );
				m.SetDataChange( _slug, member->second.Node, Hist::ToUA(value), types.get() );
			}
			catch( const std::exception& e ){
				WARN( "A record of '{}' does not read, and is left out:  {}", member->second.Node, e.what() );
				return;
			}
			m.Json["topic"] = member->second.Path;
			Send( move(m) );
			if( ticks ){
				_high[value.node_index()] = *ticks;
				_mark = std::max( _mark.value_or(*ticks), *ticks );
				_dirty = true;
			}
		}
		//An edit of the group's history, a message of its own:  the value it left, none for a delete, and what it was.
		α Modified( const Hist::Proto::Modification& edit )ι->void{
			let member = _members.find( edit.node_index() );
			if( member==_members.end() )
				return;
			auto m = Make();
			try{
				let types = UAClient::FindTypes( _slug );
				if( edit.has_new_value() )
					m.SetDataChange( _slug, member->second.Node, Hist::ToUA(edit.new_value()), types.get() );
				else
					m.Json["node"] = jobject{ {"connection", _slug}, {"id", member->second.Node} };
				jobject modification{ {"type", _updateTypes[std::min<uint>((uint)edit.update_type(), _updateTypes.size()-1)]}, {"time", ToIsoString(UADateTime{(UA_DateTime)edit.ts()}.Time())}, {"userName", edit.user_name()} };
				if( edit.has_original() )
					modification["original"] = Flows::ToPayload( Hist::ToUA(edit.original()), types.get() );
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
		//The mark, as the context keeps it:  written a second after the first change not yet written, the host's rule.
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
		const bool _modifications;
		optional<Duration> _lateness;//the props', else the group's heartbeat gap.
		optional<TimePoint> _start;//`from`, a time.
		bool _now{};//`from`:  now, whatever the mark.
		vector<ExNodeId> _wanted;//`members`:  none, every member.
		sp<Tether<GroupIn>> _self;
		//The strand's.
		ServerCnnctnNK _slug;
		flat_map<Hist::NodeIndex,Member> _members;
		sp<GroupFeed> _feed;
		up<Hist::Subscription> _subscription;
		bool _paging{};
		uint _page{ 1 };
		UA_DateTime _floor{};//no subscription starts before it.
		UA_DateTime _from{};//where the subscription it has starts.
		optional<UA_DateTime> _mark;//the latest time of a record the flow finished.
		flat_map<Hist::NodeIndex,UA_DateTime> _high;//each node's.
		bool _dirty{};
	};
	α GroupFeed::OnRecords( vector<Hist::Proto::HistoryRecord>&& records )ι->void{
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
			_every{ findWait(def.Props, "every") }{
			let at = find( def.Props, "at" );
			THROW_IF( _every && at, "A timer takes `every` or `at`, not both." );
			THROW_IF( !_every && !at, "A timer takes `every`, an ISO 8601 duration, or `at`, local times." );
			THROW_IF( _every && *_every<1ms, "`every` must be a millisecond at least." );
			if( !at )
				return;
			THROW_IF( !at->is_object(), "`at` must be an object of times, days and zone." );
			let& o = at->get_object();
			let times = find( o, "times" );
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
			if( let days = find(o, "days") ){
				THROW_IF( !days->is_array(), "`at.days` must be a list of weekdays, as mon." );
				_days.fill( false );
				for( let& day : days->get_array() ){
					let p = day.is_string() ? std::ranges::find( _weekdays, sv{day.get_string()} ) : _weekdays.end();
					THROW_IF( p==_weekdays.end(), "`at.days` holds {}, which is not one of {}.", serialize(day), Str::Join(_weekdays, ", ") );
					_days[p-_weekdays.begin()] = true;
				}
				THROW_IF( std::ranges::none_of(_days, std::identity{}), "`at.days` names no day." );
			}
			if( let zone = findText(o, "zone") ){
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
		let named = find( props, "calendar" );
		THROW_IF( !named || (!named->is_string() && !named->is_number()), "`calendar` is required, a calendar's slug or id." );
		auto y = named->is_string() ? Calendar::Client::Calendars::Find( sv{named->get_string()} ) : Calendar::Client::Calendars::Find( named->to_number<uint32>() );
		THROW_IF( !y, "Calendar {} is no calendar the gateway holds:  the AppServer has none by that name, it is deleted, or the gateway may not read the calendars.", serialize(*named) );
		return y;
	}
	Ω boundaries( const jobject& props )ε->std::array<bool,4>{
		std::array<bool,4> y{ true, true, true, true };
		if( let named = find(props, "boundaries") ){
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
			_self{ ms<Tether<ConnectionIn>>(*this) }{
			let slug = findText( def.Props, "connection" );
			THROW_IF( !slug || slug->empty(), "`connection` is required, a connection's slug." );
			_slug = *slug;
		}
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
		ServerCnnctnNK _slug;
		sp<Tether<ConnectionIn>> _self;
		optional<uint> _observer;
		bool _connected{};
	};

	//`opc out`.
	struct OpcOut final : Flows::ISink{
		OpcOut( const NodeDef& def, str flow, SL sl )ε:
			_target{ target(def.Props, sl) },
			_property{ findText(def.Props, "property").value_or("payload") },
			_collector{ HistHost::Instance().Collector(_target.Slug) },
			_label{ Ƒ("{}.{}", flow, def.Id) },
			_self{ ms<Tether<OpcOut>>(*this) }
		{}
		~OpcOut(){ _self->Cut(); }
		α Stop()ι->void override{ _self->Cut(); }
		α OnStrand( absl::AnyInvocable<void()> f )ι->void{ Post( std::move(f) ); }
		//One write is out at a time, so a node's values reach its server in the order they came:  the rest wait here, the
		//oldest dropped past the flow's queue.
		α Receive( Message& m )ε->void override{
			THROW_IF( !_property.Find(m.Json), "The message has no `{}` to write.", _property.Text() );
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
		sp<Tether<OpcOut>> _self;
		std::deque<Message> _waiting;//the first is out, the rest held behind it.
	};

	//`log`.
	struct LogOut final : Flows::ISink{
		LogOut( const NodeDef& def, str flow )ε:
			_template{ findText(def.Props, "template").value_or("{{payload}}") },
			_flow{ flow }{
			if( let level = findText(def.Props, "level") ){
				let names = LogLevelStrings();
				let p = std::ranges::find_if( names, [&]( sv name ){ return Str::ToLower( string{name} )==Str::ToLower( string{*level} ); } );
				THROW_IF( p==names.end(), "`level` must be one of {}, not '{}'.", Str::Join(names, ", "), *level );
				_level = (ELogLevel)( p-names.begin() );
			}
		}
		α Receive( Message& m )ε->void override{
			LOG( _level, _tags, "[{}.{}]{}", _flow, Def().Label(), _template.Render(m, FlowContext(), Global(), false) );
		}
	private:
		Flows::Mustache _template;
		const string _flow;
		ELogLevel _level{ ELogLevel::Information };
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
		HttpOut( const NodeDef& def )ε:
			_json{ choice(def.Props, "output", _outputs, 0)==1 },
			_self{ ms<Tether<HttpOut>>(*this) }{
			if( let url = findText(def.Props, "url") )
				_url = parseUrl( *url );
			if( let method = findText(def.Props, "method") )
				_verb = verb( *method );
			if( let type = findText(def.Props, "contentType") )
				_contentType = *type;
		}
		~HttpOut(){ _self->Cut(); }
		α Stop()ι->void override{ _self->Cut(); }
		α OnStrand( absl::AnyInvocable<void()> f )ι->void{ Post( std::move(f) ); }
		Ω Url( const NodeDef& def )ε->optional<HttpSend>{ let url = findText( def.Props, "url" ); return url ? optional<HttpSend>{ parseUrl(*url) } : nullopt; }
		//The props' url, method and content type, else the message's `url` and `method`, with its payload the body of a
		//method that has one.  The host is checked here, at the send, whatever a deploy saw.
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
			Hold();
			Requesting( _self, move(m), move(send), _json );
		}
		α Answered( Message&& m, string error )ι->void{
			let id = m.Id();
			if( error.size() ){
				SetStatus( {"red", "ring", error} );
				Fail( move(m), move(error) );
				return;
			}
			SetStatus( {"green", "dot", Ƒ("{}", Json::FindNumber<uint>(m.Json, "statusCode").value_or(0))} );
			Send( move(m) );
			Done( id );
		}
	private:
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
		optional<string> _contentType;
		const bool _json;
		sp<Tether<HttpOut>> _self;
	};

	//A `link out` whose `link in` another flow holds.
	struct LinkOut final : Flows::ISink{
		LinkOut( const NodeDef& def, FlowHost& host, const FlowRun& run )ι:
			_name{ Json::FindDefaultSV(def.Props, "name") }, _host{ host }, _flow{ run.Flow }, _deployer{ run.Deployer }
		{}
		α Receive( Message& m )ε->void override{
			let taken = _host.Link( _flow, _deployer, _name, m );
			SetStatus( taken ? NodeStatus{"green", "dot", Ƒ("{} {}", taken, taken==1 ? "flow" : "flows")} : NodeStatus{"grey", "ring", "No running flow holds its `link in`."} );
		}
	private:
		const string _name;
		FlowHost& _host;
		const FlowPK _flow;
		const UserPK _deployer;
	};

	Ω unbuilt( const NodeDef& node )ε->void{
		THROW_IF( node.Type==ENodeType::Variable, "A `variable` node waits for the flow server, which is not built yet (docs/flows/plan.md, phase 8)." );
	}
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
		unbuilt( node );
		switch( node.Type ){
		case ENodeType::OpcOut: return mu<OpcOut>( node, _run.Slug, sl );
		case ENodeType::Log: return mu<LogOut>( node, _run.Slug );
		case ENodeType::HttpRequest: return mu<HttpOut>( node );
		case ENodeType::LinkOut: return mu<LinkOut>( node, _host, _run );
		default: THROW( "A '{}' is no sink the gateway makes.", Flows::ToString(node.Type) );
		}
	}
	α FlowNodes::Check( const NodeDef& node, SL sl )ε->void{
		unbuilt( node );
		switch( node.Type ){
		case ENodeType::OpcIn:
			target( node.Props, sl );
			parameters( node.Props );
			break;
		case ENodeType::GroupIn:{
			flag( node.Props, "modifications", false );
			findWait( node.Props, "lateness" );
			if( let from = findText(node.Props, "from"); from && *from!="resume" && *from!="now" ){
				try{
					Chrono::ToTimePoint( string{*from}, sl );
				}
				catch( const std::exception& ){
					THROW( "`from` must be resume, now or a time, as 2026-03-09T06:00:00Z, not '{}'.", *from );
				}
			}
			if( let members = find(node.Props, "members") ){
				THROW_IF( !members->is_array(), "`members` must be a list of the group's nodes." );
				for( let& member : members->get_array() )
					ParseHistNode( member, sl );
			}
			break;
		}
		case ENodeType::Timer: Timer{ node }; break;
		case ENodeType::Calendar:
			schedule( node.Props );
			boundaries( node.Props );
			break;
		case ENodeType::Connection:{
			let slug = findText( node.Props, "connection" );
			THROW_IF( !slug || slug->empty(), "`connection` is required, a connection's slug." );
			break;
		}
		case ENodeType::OpcOut:
			target( node.Props, sl );
			Flows::Path{ findText(node.Props, "property").value_or("payload") };
			break;
		case ENodeType::Log: LogOut{ node, {} }; break;
		case ENodeType::HttpRequest:{
			HttpOut{ node };
			if( let url = HttpOut::Url(node); url )
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