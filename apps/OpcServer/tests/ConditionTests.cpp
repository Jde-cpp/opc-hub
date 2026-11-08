//Clients 12a (#425):  pumpManual.overspeed, the AlarmConditionType object the pumps nodeset declares and the server library drives
//(Condition.h), seen by open62541's synchronous client as HistoryTests sees the history - an events subscription with the condition's
//fields, its Raise, Acknowledge, Confirm and AddComment through Call, ConditionRefresh replaying to the calling session's subscription alone.
#include <jde/fwk/settings.h>
#include <jde/fwk/crypto/CryptoSettings.h>
#include <jde/db/meta/AppSchema.h>//GetSchema().Authorizer
#include <jde/opc/UAException.h>
#include <jde/opc/tests/UATestClient.h>
#include <jde/opc/uatypes/Value.h>
#include <jde/opc/server/UAServer.h>
#include "../src/access/OpcAuthorize.h"
#include "../src/nodesets.h"
#include "helpers.h"
#define let const auto

namespace Jde::Opc::Server::Tests{
	using namespace std::chrono;
	using Opc::Tests::UATestClient;
	namespace{
		constexpr UA_UInt32 PumpManual{ 5005 }, RpmManual{ 6054 }, Overspeed{ 5020 }, HighLimit{ 6106 }, Raise{ 6110 }, RaiseEventMethod{ 6058 };
		constexpr sv ConditionIdKey{ "i=2782#NodeId" };//how open62541 prints the standard ConditionId clause, ConditionType's NodeId with no path.
		//The clauses every item here selects, each delivered under the key the client prints it as, "/Retain", "/AckedState/Id".
		const vector<string> Clauses{ "EventId", "EventType", "SourceNode", "SourceName", "Time", "Message", "Severity", "Retain", "ActiveState/Id", "AckedState/Id", "ConfirmedState/Id", "Comment", "ConditionName" };
		using Event = flat_map<string,Value>;
		//An event item's notifications, filled as the client is pumped (UATestClient::Iterate) on the test's thread.
		struct Events final{
			vector<Event> Received;
			Ω Callback( UA_Client*, UA_UInt32, void*, UA_UInt32, void* context, const UA_KeyValueMap fields )ι->void{
				Event event;
				for( uint i=0; i<fields.mapSize; ++i ){
					UA_Variant v; UA_Variant_init( &v );
					UA_Variant_copy( &fields.map[i].value, &v );
					event.emplace( ToString(fields.map[i].key.name), Value{move(v)} );
				}
				static_cast<Events*>( context )->Received.push_back( move(event) );
			}
		};
		Ω eventType( const Event& e )ι->NodeId{ let p = e.find( "/EventType" ); return p!=e.end() && p->second.hasValue ? NodeId{ p->second.Get<UA_NodeId>(0) } : NodeId{}; }
		Ω node( const Event& e, sv key )ι->NodeId{ let p = e.find( string{key} ); return p!=e.end() && p->second.hasValue ? NodeId{ p->second.Get<UA_NodeId>(0) } : NodeId{}; }
		Ω boolean( const Event& e, sv key )ι->optional<bool>{ let p = e.find( string{key} ); return p!=e.end() && p->second.hasValue && UA_Variant_hasScalarType(&p->second.value, &UA_TYPES[UA_TYPES_BOOLEAN]) ? optional<bool>{ p->second.Get<UA_Boolean>(0) } : nullopt; }
		Ω text( const Event& e, sv key )ι->string{
			let p = e.find( string{key} );
			if( p==e.end() || !p->second.hasValue )
				return {};
			if( UA_Variant_hasScalarType(&p->second.value, &UA_TYPES[UA_TYPES_LOCALIZEDTEXT]) )
				return ToString( p->second.Get<UA_LocalizedText>(0).text );
			return UA_Variant_hasScalarType( &p->second.value, &UA_TYPES[UA_TYPES_STRING] ) ? ToString( p->second.Get<UA_String>(0) ) : string{};
		}
		Ω bytes( const Event& e, sv key )ι->string{ let p = e.find( string{key} ); return p!=e.end() && p->second.hasValue && UA_Variant_hasScalarType(&p->second.value, &UA_TYPES[UA_TYPES_BYTESTRING]) ? ToString( p->second.Get<UA_ByteString>(0) ) : string{}; }
		Ω severity( const Event& e )ι->optional<UA_UInt16>{ let p = e.find( "/Severity" ); return p!=e.end() && p->second.hasValue && UA_Variant_hasScalarType(&p->second.value, &UA_TYPES[UA_TYPES_UINT16]) ? optional<UA_UInt16>{ p->second.Get<UA_UInt16>(0) } : nullopt; }
		Ω eventTime( const Event& e )ι->optional<UA_DateTime>{ let p = e.find( "/Time" ); return p!=e.end() && p->second.hasValue && UA_Variant_hasScalarType(&p->second.value, &UA_TYPES[UA_TYPES_DATETIME]) ? optional<UA_DateTime>{ p->second.Get<UA_DateTime>(0) } : optional<UA_DateTime>{}; }
		Ω byteString( str bytes )ι->UA_ByteString{ return { bytes.size(), (UA_Byte*)bytes.data() }; }//a view.
		struct Called final{ UA_StatusCode Status; vector<Value> Outputs; };
	}

	struct ConditionTests : ::testing::Test{
	protected:
		//As opcServerStartup does, less the history and PubSub:  the nodeset, the pumps' bindings, the rights, and the listener.
		Ω SetUpTestCase()ε->void{
			Server::Initialize( GetSchemaPtr() );
			auto& ua = GetUAServer();
			LoadNodeset( ua, PumpsNodeset() );
			ConfigurePumps( ua );
			static_cast<OpcAuthorize&>( *GetSchema().Authorizer ).AssignRights( ua );
			PublishDataTypes( ua );
			ua.Run();
			_ns = NamespaceIndex( ua, "urn:jde:pumps" );
			_client = Connect();
		}
		Ω TearDownTestCase()ι->void{ _client.reset(); }
		//A session of this program's own user, as HistoryTests opens one:  an issued token with its session with the AppServer.
		Ω Connect()ε->up<UATestClient>{
			const Crypto::CryptoSettings ssl{ *Settings::FindObject("/opcServer/ssl") };
			auto y = mu<UATestClient>( Ƒ("opc.tcp://127.0.0.1:{}", Settings::FindNumber<PortType>("/opcServer/port").value_or(4940)), ssl, UATestClient::Issued(Ƒ("{:x}", AppClient()->SessionId())) );
			y->ConnectOrThrow();
			return y;
		}
		Ω Node( UA_UInt32 id )ι->NodeId{ return NodeId{ _ns, id }; }
		Ω Condition()ε->Server::Condition&{ auto y = GetUAServer().Conditions().Find( Node(Overspeed) ); THROW_IF( !y, "The condition was not added." ); return *y; }
		//Each test starts from a condition that is inactive, acknowledged and confirmed - Retain false - with the limit the nodeset's and the
		//input under it, whatever the one before left.  Through the client, so the condition is driven on the server's thread (Condition.h);
		//the reads between are of a server with nothing in flight, each call here being synchronous.
		α SetUp()ε->void override{
			ASSERT_EQ( WriteDouble(HighLimit, 1500), UA_STATUSCODE_GOOD );
			ASSERT_EQ( WriteDouble(RpmManual, 0), UA_STATUSCODE_GOOD );
			auto& condition = Condition();
			if( condition.Active() )
				ASSERT_EQ( RaiseCondition(*_client, false, 1, "reset").first, UA_STATUSCODE_GOOD );
			if( condition.Retain() ){
				Acknowledge( *_client, condition.LastEventId(), "reset" );
				Confirm( *_client, condition.LastEventId(), "reset" );
			}
			ASSERT_FALSE( condition.Retain() );
		}
		Ω WriteDouble( UA_UInt32 node, UA_Double value )ε->UA_StatusCode{ return _client->Write( Node(node), &value, UA_TYPES[UA_TYPES_DOUBLE] ); }
		//An event item on `node` with `Clauses` and the ConditionId clause, as a standard client selects it - ConditionType's NodeId with no
		//path - on `subscription`:  its notifications land in `into`.  The item's id.
		Ω MonitorEvents( UATestClient& client, UA_UInt32 subscription, const NodeId& node, Events& into )ε->UA_UInt32{
			UA_EventFilter filter; UA_EventFilter_init( &filter );
			filter.selectClausesSize = Clauses.size()+1;
			filter.selectClauses = (UA_SimpleAttributeOperand*)UA_Array_new( filter.selectClausesSize, &UA_TYPES[UA_TYPES_SIMPLEATTRIBUTEOPERAND] );
			for( uint i=0; i<Clauses.size(); ++i ){
				auto& clause = filter.selectClauses[i];
				clause.typeDefinitionId = UA_NODEID_NUMERIC( 0, UA_NS0ID_BASEEVENTTYPE );
				clause.attributeId = UA_ATTRIBUTEID_VALUE;
				let names = Str::Split( sv{Clauses[i]}, "/"sv );
				clause.browsePathSize = names.size();
				clause.browsePath = (UA_QualifiedName*)UA_Array_new( names.size(), &UA_TYPES[UA_TYPES_QUALIFIEDNAME] );
				for( uint j=0; j<names.size(); ++j )
					clause.browsePath[j] = UA_QUALIFIEDNAME_ALLOC( 0, string{names[j]}.c_str() );
			}
			auto& conditionId = filter.selectClauses[Clauses.size()];
			conditionId.typeDefinitionId = UA_NODEID_NUMERIC( 0, UA_NS0ID_CONDITIONTYPE );
			conditionId.attributeId = UA_ATTRIBUTEID_NODEID;
			auto item = UA_MonitoredItemCreateRequest_default( node );//a view of `node`:  not cleared.
			item.itemToMonitor.attributeId = UA_ATTRIBUTEID_EVENTNOTIFIER;
			item.requestedParameters.queueSize = 100;
			item.requestedParameters.filter.encoding = UA_EXTENSIONOBJECT_DECODED_NODELETE;
			item.requestedParameters.filter.content.decoded.type = &UA_TYPES[UA_TYPES_EVENTFILTER];
			item.requestedParameters.filter.content.decoded.data = &filter;
			auto result = UA_Client_MonitoredItems_createEvent( client, subscription, UA_TIMESTAMPSTORETURN_BOTH, item, &into, &Events::Callback, nullptr );
			let status = result.statusCode;
			let id = result.monitoredItemId;
			UA_MonitoredItemCreateResult_clear( &result );
			UA_EventFilter_clear( &filter );
			UAε( status );
			return id;
		}
		//Pumps `client` until `into` holds `count` events or `timeout` passes:  whether it does.
		Ω Wait( UATestClient& client, const Events& into, uint count, Duration timeout=10s )ι->bool{
			let deadline = steady_clock::now()+timeout;
			while( into.Received.size()<count && steady_clock::now()<deadline )
				client.Iterate( 10 );
			return into.Received.size()>=count;
		}
		//`method` called on `object` with `inputs`, views the call leaves as they are:  the call's status and its outputs.
		Ω Call( UATestClient& client, const NodeId& object, const NodeId& method, const vector<UA_Variant>& inputs )ι->Called{
			uint outputSize{}; UA_Variant* outputs{};
			Called y{ UA_Client_call(client, object, method, inputs.size(), inputs.data(), &outputSize, &outputs), {} };
			for( uint i=0; i<outputSize; ++i )
				y.Outputs.emplace_back( move(outputs[i]) );
			UA_Array_delete( outputs, outputSize, &UA_TYPES[UA_TYPES_VARIANT] );
			return y;
		}
		Ω Scalar( const void* p, uint type )ι->UA_Variant{ UA_Variant v; UA_Variant_init( &v ); UA_Variant_setScalar( &v, const_cast<void*>(p), &UA_TYPES[type] ); return v; }
		//overspeed.Raise( active, severity, message ):  the status, and the EventId it answered.
		Ω RaiseCondition( UATestClient& client, bool active, UA_UInt16 severity, str message )ι->std::pair<UA_StatusCode,string>{
			const UA_Boolean a{ active };
			const UA_String m = ToUV( message );
			let y = Call( client, Node(Overspeed), Node(Raise), {Scalar(&a, UA_TYPES_BOOLEAN), Scalar(&severity, UA_TYPES_UINT16), Scalar(&m, UA_TYPES_STRING)} );
			return { y.Status, y.Outputs.size()==1 && y.Outputs[0].hasValue ? ToString( y.Outputs[0].Get<UA_ByteString>(0) ) : string{} };
		}
		//Acknowledge, Confirm or AddComment, the type's method `method`, on overspeed with `eventId` and `comment`.
		Ω Comment( UATestClient& client, UA_UInt32 method, str eventId, str comment, sv locale={} )ι->UA_StatusCode{
			const UA_ByteString id = byteString( eventId );
			const UA_LocalizedText text{ locale.empty() ? UA_STRING_NULL : ToUV(locale), ToUV(comment) };
			return Call( client, Node(Overspeed), NodeId{method}, {Scalar(&id, UA_TYPES_BYTESTRING), Scalar(&text, UA_TYPES_LOCALIZEDTEXT)} ).Status;
		}
		Ω Acknowledge( UATestClient& client, str eventId, str comment )ι->UA_StatusCode{ return Comment( client, UA_NS0ID_ACKNOWLEDGEABLECONDITIONTYPE_ACKNOWLEDGE, eventId, comment ); }
		Ω Confirm( UATestClient& client, str eventId, str comment )ι->UA_StatusCode{ return Comment( client, UA_NS0ID_ACKNOWLEDGEABLECONDITIONTYPE_CONFIRM, eventId, comment ); }
		Ω AddComment( UATestClient& client, str eventId, str comment, sv locale={} )ι->UA_StatusCode{ return Comment( client, UA_NS0ID_CONDITIONTYPE_ADDCOMMENT, eventId, comment, locale ); }
		//ConditionRefresh( subscription ) on ConditionType.
		Ω ConditionRefresh( UATestClient& client, UA_UInt32 subscription )ι->UA_StatusCode{
			return Call( client, NodeId{UA_NS0ID_CONDITIONTYPE}, NodeId{UA_NS0ID_CONDITIONTYPE_CONDITIONREFRESH}, {Scalar(&subscription, UA_TYPES_UINT32)} ).Status;
		}
		//A subscription deleted at the end of its scope, so declared after the Events its items fill:  a failed ASSERT's early return leaves no
		//item on the shared client pointing at a dead Events.
		struct Subscription final{
			Subscription( UATestClient& client )ε:Client{ client }, Id{ client.Subscribe() }{}
			~Subscription(){ UA_Client_Subscriptions_deleteSingle( Client, Id ); }
			operator UA_UInt32()Ι{ return Id; }
			UATestClient& Client;
			UA_UInt32 Id;
		};
		//The condition's own variable under `path`, read from the server.
		Ω Variable( std::initializer_list<const char*> path )ε->Value{
			vector<UA_QualifiedName> names;
			for( let name : path )
				names.push_back( UA_QUALIFIEDNAME(0, (char*)name) );
			auto result = UA_Server_browseSimplifiedBrowsePath( GetUAServer().Ptr(), Node(Overspeed), names.size(), names.data() );
			THROW_IF( result.statusCode || !result.targetsSize, "overspeed has no {}.", *path.begin() );
			const NodeId target{ result.targets[0].targetId.nodeId };
			UA_BrowsePathResult_clear( &result );
			return ReadValue( GetUAServer(), target );
		}
		static inline up<UATestClient> _client;
		static inline NsIndex _ns{};
	};

	//Raise reaches an events subscription on the pump with the condition's fields:  AlarmConditionType from pumpManual, the EventId the call
	//answered, the severity and message given, Retain, ActiveState set and AckedState and ConfirmedState unset, the ConditionName and, under
	//the standard clause, the ConditionId;  and the instance's variables say the same.
	TEST_F( ConditionTests, RaiseIsRetainedOnAnEventsSubscription ){
		Events events;
		const Subscription subscription{ *_client };
		ASSERT_NO_THROW( MonitorEvents(*_client, subscription, Node(PumpManual), events) );
		let [status, eventId] = RaiseCondition( *_client, true, 700, "too fast" );
		ASSERT_EQ( status, UA_STATUSCODE_GOOD ) << UA_StatusCode_name( status );
		EXPECT_EQ( eventId.size(), 16u ) << "the EventId the server made";
		ASSERT_TRUE( Wait(*_client, events, 1) ) << "the event never reached the subscription";
		let& event = events.Received.front();
		EXPECT_EQ( bytes(event, "/EventId"), eventId );
		EXPECT_EQ( eventType(event), NodeId{UA_NS0ID_ALARMCONDITIONTYPE} );
		EXPECT_EQ( node(event, "/SourceNode"), Node(PumpManual) );
		EXPECT_EQ( node(event, ConditionIdKey), Node(Overspeed) ) << "the ConditionId under the standard clause";
		EXPECT_EQ( text(event, "/Message"), "too fast" );
		EXPECT_EQ( severity(event), optional<UA_UInt16>{700} );
		EXPECT_EQ( boolean(event, "/Retain"), optional{true} );
		EXPECT_EQ( boolean(event, "/ActiveState/Id"), optional{true} );
		EXPECT_EQ( boolean(event, "/AckedState/Id"), optional{false} );
		EXPECT_EQ( boolean(event, "/ConfirmedState/Id"), optional{false} );
		EXPECT_EQ( text(event, "/ConditionName"), "pumpManual.overspeed" );
		EXPECT_EQ( text(event, "/Comment"), "" );
		EXPECT_TRUE( event.contains("/Time") );
		EXPECT_EQ( ToString(Variable({"ActiveState"}).Get<UA_LocalizedText>(0).text), "Active" );
		EXPECT_EQ( ToString(Variable({"AckedState"}).Get<UA_LocalizedText>(0).text), "Unacknowledged" );
		EXPECT_TRUE( Variable({"Retain"}).Get<UA_Boolean>(0) );
		EXPECT_EQ( Variable({"Severity"}).Get<UA_UInt16>(0), 700 );
		EXPECT_EQ( ToString(Variable({"EnabledState"}).Get<UA_LocalizedText>(0).text), "Enabled" );
	}
	//Acknowledge then Confirm, each with the last event's EventId and a comment, are transitions seen on the subscription with the comment;  a
	//second Acknowledge is refused Bad_ConditionBranchAlreadyAcked, the first event's EventId, stale by then, Bad_EventIdUnknown, a Confirm
	//before the Acknowledge Bad_InvalidState, and a second Confirm Bad_ConditionBranchAlreadyConfirmed.  Retain holds while the condition is
	//active and drops once it is inactive, acknowledged and confirmed.
	TEST_F( ConditionTests, AcknowledgeThenConfirmAreTransitions ){
		Events events;
		const Subscription subscription{ *_client };
		ASSERT_NO_THROW( MonitorEvents(*_client, subscription, Node(PumpManual), events) );
		let [status, raised] = RaiseCondition( *_client, true, 600, "acknowledge me" );
		ASSERT_EQ( status, UA_STATUSCODE_GOOD ) << UA_StatusCode_name( status );
		ASSERT_TRUE( Wait(*_client, events, 1) );
		EXPECT_EQ( Confirm(*_client, raised, "early"), UA_STATUSCODE_BADINVALIDSTATE ) << "a confirm before the acknowledge";
		EXPECT_EQ( Acknowledge(*_client, "not an event id", "stale"), UA_STATUSCODE_BADEVENTIDUNKNOWN );
		ASSERT_EQ( Acknowledge(*_client, raised, "seen"), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( Wait(*_client, events, 2) ) << "the acknowledge's transition never arrived";
		let& acked = events.Received[1];
		EXPECT_EQ( boolean(acked, "/AckedState/Id"), optional{true} );
		EXPECT_EQ( boolean(acked, "/ActiveState/Id"), optional{true} );
		EXPECT_EQ( boolean(acked, "/Retain"), optional{true} ) << "still active";
		EXPECT_EQ( text(acked, "/Comment"), "seen" );
		EXPECT_EQ( text(acked, "/Message"), "acknowledge me" );
		let ackedId = bytes( acked, "/EventId" );
		EXPECT_NE( ackedId, raised ) << "a transition is an event of its own";
		EXPECT_EQ( Acknowledge(*_client, ackedId, "again"), UA_STATUSCODE_BADCONDITIONBRANCHALREADYACKED );
		EXPECT_EQ( Acknowledge(*_client, raised, "stale"), UA_STATUSCODE_BADEVENTIDUNKNOWN ) << "the first event's id is stale";
		ASSERT_EQ( Confirm(*_client, ackedId, "dealt with"), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( Wait(*_client, events, 3) ) << "the confirm's transition never arrived";
		let& confirmed = events.Received[2];
		EXPECT_EQ( boolean(confirmed, "/ConfirmedState/Id"), optional{true} );
		EXPECT_EQ( boolean(confirmed, "/AckedState/Id"), optional{true} );
		EXPECT_EQ( boolean(confirmed, "/Retain"), optional{true} ) << "still active";
		EXPECT_EQ( text(confirmed, "/Comment"), "dealt with" );
		EXPECT_EQ( Confirm(*_client, bytes(confirmed, "/EventId"), "again"), UA_STATUSCODE_BADCONDITIONBRANCHALREADYCONFIRMED );
		let [cleared, clearedId] = RaiseCondition( *_client, false, 600, "back to normal" );
		ASSERT_EQ( cleared, UA_STATUSCODE_GOOD );
		ASSERT_TRUE( Wait(*_client, events, 4) );
		let& inactive = events.Received[3];
		EXPECT_EQ( boolean(inactive, "/ActiveState/Id"), optional{false} );
		EXPECT_EQ( boolean(inactive, "/Retain"), optional{false} ) << "inactive, acknowledged and confirmed";
		EXPECT_EQ( text(inactive, "/Message"), "back to normal" );
		EXPECT_FALSE( Variable({"Retain"}).Get<UA_Boolean>(0) );
	}
	//AddComment is a transition that changes the comment alone, refused for a stale EventId;  the Enable and Disable the type gives every
	//instance answer that the condition is enabled for good.
	TEST_F( ConditionTests, AddCommentKeepsTheState ){
		Events events;
		const Subscription subscription{ *_client };
		ASSERT_NO_THROW( MonitorEvents(*_client, subscription, Node(PumpManual), events) );
		let [status, raised] = RaiseCondition( *_client, true, 300, "comment me" );
		ASSERT_EQ( status, UA_STATUSCODE_GOOD ) << UA_StatusCode_name( status );
		ASSERT_TRUE( Wait(*_client, events, 1) );
		EXPECT_EQ( AddComment(*_client, "stale", "nothing"), UA_STATUSCODE_BADEVENTIDUNKNOWN );
		ASSERT_EQ( AddComment(*_client, raised, "noted"), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( Wait(*_client, events, 2) );
		let& commented = events.Received[1];
		EXPECT_EQ( text(commented, "/Comment"), "noted" );
		EXPECT_EQ( boolean(commented, "/ActiveState/Id"), optional{true} );
		EXPECT_EQ( boolean(commented, "/AckedState/Id"), optional{false} );
		EXPECT_EQ( severity(commented), optional<UA_UInt16>{300} );
		EXPECT_EQ( ToString(Variable({"Comment"}).Get<UA_LocalizedText>(0).text), "noted" );
		EXPECT_EQ( Call(*_client, Node(Overspeed), NodeId{UA_NS0ID_CONDITIONTYPE_ENABLE}, {}).Status, UA_STATUSCODE_BADCONDITIONALREADYENABLED );
		EXPECT_EQ( Call(*_client, Node(Overspeed), NodeId{UA_NS0ID_CONDITIONTYPE_DISABLE}, {}).Status, UA_STATUSCODE_BADNOTSUPPORTED );
	}
	//ConditionRefresh on a fresh subscription replays the retained condition - the last transition's EventId and Time - between RefreshStart and
	//RefreshEnd on the Server object's item, to an item on the pump the condition alone, and to another session's subscription nothing;  a
	//subscription that isn't the caller's is refused Bad_SubscriptionIdInvalid, and a condition no longer retained is not replayed.
	TEST_F( ConditionTests, ConditionRefreshReplaysToItsOwnSubscription ){
		Events live;
		const Subscription liveSubscription{ *_client };
		ASSERT_NO_THROW( MonitorEvents(*_client, liveSubscription, Node(PumpManual), live) );
		let [status, raised] = RaiseCondition( *_client, true, 800, "retained" );
		ASSERT_EQ( status, UA_STATUSCODE_GOOD ) << UA_StatusCode_name( status );
		ASSERT_TRUE( Wait(*_client, live, 1) ) << "the raise never arrived";
		let raisedTime = eventTime( live.Received[0] );
		ASSERT_TRUE( raisedTime.has_value() );
		auto other = Connect();
		Events mine, pump, theirs;
		const Subscription subscription{ *_client };
		ASSERT_NO_THROW( MonitorEvents(*_client, subscription, NodeId{UA_NS0ID_SERVER}, mine) );
		ASSERT_NO_THROW( MonitorEvents(*_client, subscription, Node(PumpManual), pump) );
		const Subscription otherSubscription{ *other };
		ASSERT_NO_THROW( MonitorEvents(*other, otherSubscription, NodeId{UA_NS0ID_SERVER}, theirs) );
		EXPECT_EQ( ConditionRefresh(*_client, otherSubscription), UA_STATUSCODE_BADSUBSCRIPTIONIDINVALID ) << "another session's subscription";
		ASSERT_EQ( ConditionRefresh(*_client, subscription), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( Wait(*_client, mine, 3) ) << "RefreshStart, the condition and RefreshEnd";
		EXPECT_EQ( eventType(mine.Received[0]), NodeId{UA_NS0ID_REFRESHSTARTEVENTTYPE} );
		EXPECT_EQ( node(mine.Received[0], "/SourceNode"), NodeId{UA_NS0ID_SERVER} );
		let& replayed = mine.Received[1];
		EXPECT_EQ( eventType(replayed), NodeId{UA_NS0ID_ALARMCONDITIONTYPE} );
		EXPECT_EQ( bytes(replayed, "/EventId"), raised ) << "the replay carries the transition's EventId";
		EXPECT_EQ( eventTime(replayed), raisedTime ) << "and its Time";
		EXPECT_EQ( node(replayed, ConditionIdKey), Node(Overspeed) );
		EXPECT_EQ( boolean(replayed, "/Retain"), optional{true} );
		EXPECT_EQ( text(replayed, "/Message"), "retained" );
		EXPECT_EQ( severity(replayed), optional<UA_UInt16>{800} );
		EXPECT_EQ( eventType(mine.Received[2]), NodeId{UA_NS0ID_REFRESHENDEVENTTYPE} );
		ASSERT_TRUE( Wait(*_client, pump, 1) ) << "the pump's item gets the condition";
		EXPECT_EQ( bytes(pump.Received[0], "/EventId"), raised );
		EXPECT_EQ( pump.Received.size(), 1u ) << "and no bracket:  RefreshStart and RefreshEnd are the Server object's";
		other->Iterate( 500 );
		EXPECT_TRUE( theirs.Received.empty() ) << "the other session's subscription saw " << theirs.Received.size() << " of the replay";
		//Acknowledged and confirmed while inactive, the condition is not retained, so the next refresh replays nothing.
		ASSERT_EQ( RaiseCondition(*_client, false, 800, "cleared").first, UA_STATUSCODE_GOOD );
		ASSERT_EQ( Acknowledge(*_client, Condition().LastEventId(), "ok"), UA_STATUSCODE_GOOD );
		ASSERT_EQ( Confirm(*_client, Condition().LastEventId(), "ok"), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( Wait(*_client, mine, 6) ) << "the three transitions";
		ASSERT_EQ( ConditionRefresh(*_client, subscription), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( Wait(*_client, mine, 8) ) << "RefreshStart and RefreshEnd";
		EXPECT_EQ( eventType(mine.Received[6]), NodeId{UA_NS0ID_REFRESHSTARTEVENTTYPE} );
		EXPECT_EQ( eventType(mine.Received[7]), NodeId{UA_NS0ID_REFRESHENDEVENTTYPE} );
	}
	//The pump's motorRpm written over HighLimit raises the condition through the value callback, as the emulator's signals would, with the
	//input's name and the values in the message;  a write still over it raises nothing more, and one at or under the limit clears it.
	TEST_F( ConditionTests, TheRpmOverItsLimitRaisesTheCondition ){
		Events events;
		const Subscription subscription{ *_client };
		ASSERT_NO_THROW( MonitorEvents(*_client, subscription, Node(PumpManual), events) );
		let limit = _client->Read( Node(HighLimit) );
		ASSERT_TRUE( limit.hasValue );
		EXPECT_EQ( limit.Get<UA_Double>(0), 1500.0 ) << "the nodeset's limit";
		UA_Double rpm{ 1600 };
		ASSERT_EQ( _client->Write(Node(RpmManual), &rpm, UA_TYPES[UA_TYPES_DOUBLE]), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( Wait(*_client, events, 1) ) << "the write over the limit raised nothing";
		let& raised = events.Received[0];
		EXPECT_EQ( boolean(raised, "/ActiveState/Id"), optional{true} );
		EXPECT_EQ( severity(raised), optional<UA_UInt16>{700} );
		EXPECT_EQ( text(raised, "/Message"), "motorRpm at 1600 is over its limit of 1500." );
		rpm = 1700;
		ASSERT_EQ( _client->Write(Node(RpmManual), &rpm, UA_TYPES[UA_TYPES_DOUBLE]), UA_STATUSCODE_GOOD );
		rpm = 1500;
		ASSERT_EQ( _client->Write(Node(RpmManual), &rpm, UA_TYPES[UA_TYPES_DOUBLE]), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( Wait(*_client, events, 2) ) << "the write at the limit cleared nothing";
		let& cleared = events.Received[1];
		EXPECT_EQ( boolean(cleared, "/ActiveState/Id"), optional{false} );
		EXPECT_EQ( boolean(cleared, "/Retain"), optional{true} ) << "unacknowledged";
		EXPECT_EQ( text(cleared, "/Message"), "motorRpm at 1500 is within its limit of 1500." );
		_client->Iterate( 300 );
		EXPECT_EQ( events.Received.size(), 2u ) << "a write still over the limit is no transition";
	}
	//A Raise by hand holds against samples on the side of the limit they were already on, and yields only when the input next crosses;  a
	//crossing into the state the condition is already in raises nothing.
	TEST_F( ConditionTests, ARaiseHoldsUntilTheInputCrosses ){
		Events events;
		const Subscription subscription{ *_client };
		ASSERT_NO_THROW( MonitorEvents(*_client, subscription, Node(PumpManual), events) );
		ASSERT_EQ( WriteDouble(RpmManual, 1000), UA_STATUSCODE_GOOD );
		ASSERT_EQ( RaiseCondition(*_client, true, 900, "by hand").first, UA_STATUSCODE_GOOD );
		ASSERT_TRUE( Wait(*_client, events, 1) );
		ASSERT_EQ( WriteDouble(RpmManual, 1100), UA_STATUSCODE_GOOD );
		ASSERT_EQ( WriteDouble(RpmManual, 1600), UA_STATUSCODE_GOOD );
		_client->Iterate( 300 );
		EXPECT_EQ( events.Received.size(), 1u ) << "a sample under the limit undid the Raise, or a crossing re-raised an active alarm";
		EXPECT_TRUE( Variable({"ActiveState", "Id"}).Get<UA_Boolean>(0) );
		ASSERT_EQ( WriteDouble(RpmManual, 1400), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( Wait(*_client, events, 2) ) << "the crossing back under the limit cleared nothing";
		EXPECT_EQ( boolean(events.Received[1], "/ActiveState/Id"), optional{false} );
		ASSERT_EQ( WriteDouble(RpmManual, 1600), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( Wait(*_client, events, 3) );
		ASSERT_EQ( RaiseCondition(*_client, false, 900, "cleared by hand").first, UA_STATUSCODE_GOOD );
		ASSERT_TRUE( Wait(*_client, events, 4) );
		ASSERT_EQ( WriteDouble(RpmManual, 1700), UA_STATUSCODE_GOOD );
		_client->Iterate( 300 );
		EXPECT_EQ( events.Received.size(), 4u ) << "a sample over the limit undid the clearing";
		EXPECT_FALSE( Variable({"ActiveState", "Id"}).Get<UA_Boolean>(0) );
	}
	//A NaN and a Bad sample leave the state as it is, the Bad one's status in Quality;  the next good sample decides, and Quality follows it.
	TEST_F( ConditionTests, ABadOrNanSampleKeepsTheState ){
		Events events;
		const Subscription subscription{ *_client };
		ASSERT_NO_THROW( MonitorEvents(*_client, subscription, Node(PumpManual), events) );
		ASSERT_EQ( WriteDouble(RpmManual, 1600), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( Wait(*_client, events, 1) );
		ASSERT_EQ( WriteDouble(RpmManual, std::numeric_limits<UA_Double>::quiet_NaN()), UA_STATUSCODE_GOOD );
		UA_Double zero{};
		UA_DataValue bad; UA_DataValue_init( &bad );
		UA_Variant_setScalar( &bad.value, &zero, &UA_TYPES[UA_TYPES_DOUBLE] );
		bad.hasValue = true;
		bad.status = UA_STATUSCODE_BADSENSORFAILURE;
		bad.hasStatus = true;
		ASSERT_EQ( UA_Server_writeDataValue(GetUAServer().Ptr(), Node(RpmManual), bad), UA_STATUSCODE_GOOD );//a client's is refused:  no StatusWrite bit.
		_client->Iterate( 300 );
		EXPECT_EQ( events.Received.size(), 1u ) << "a sample that says nothing of the process moved the condition";
		EXPECT_TRUE( Variable({"ActiveState", "Id"}).Get<UA_Boolean>(0) );
		EXPECT_EQ( Variable({"Quality"}).Get<UA_StatusCode>(0), UA_STATUSCODE_BADSENSORFAILURE );
		ASSERT_EQ( WriteDouble(RpmManual, 1400), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( Wait(*_client, events, 2) ) << "the good sample under the limit cleared nothing";
		EXPECT_EQ( boolean(events.Received[1], "/ActiveState/Id"), optional{false} );
		EXPECT_EQ( Variable({"Quality"}).Get<UA_StatusCode>(0), UA_STATUSCODE_GOOD );
	}
	//The type's mandatory fields a standard client selects all answer:  the nodeset's class, never suppressed, Quality, the severity before the
	//last, and who last commented with when.
	TEST_F( ConditionTests, TheMandatoryFieldsHaveValues ){
		Events events;
		const Subscription subscription{ *_client };
		ASSERT_NO_THROW( MonitorEvents(*_client, subscription, Node(PumpManual), events) );
		ASSERT_EQ( RaiseCondition(*_client, true, 400, "first").first, UA_STATUSCODE_GOOD );
		let [status, raised] = RaiseCondition( *_client, true, 500, "second" );
		ASSERT_EQ( status, UA_STATUSCODE_GOOD );
		ASSERT_TRUE( Wait(*_client, events, 2) );
		ASSERT_EQ( AddComment(*_client, raised, "noted"), UA_STATUSCODE_GOOD );
		EXPECT_EQ( NodeId{Variable({"ConditionClassId"}).Get<UA_NodeId>(0)}, NodeId{UA_NS0ID_PROCESSCONDITIONCLASSTYPE} );
		EXPECT_EQ( ToString(Variable({"ConditionClassName"}).Get<UA_LocalizedText>(0).text), "ProcessConditionClassType" );
		EXPECT_FALSE( Variable({"SuppressedOrShelved"}).Get<UA_Boolean>(0) );
		EXPECT_EQ( Variable({"Quality"}).Get<UA_StatusCode>(0), UA_STATUSCODE_GOOD );
		EXPECT_GT( Variable({"Quality", "SourceTimestamp"}).Get<UA_DateTime>(0), 0 );
		EXPECT_EQ( Variable({"LastSeverity"}).Get<UA_UInt16>(0), 400 );
		EXPECT_GT( Variable({"LastSeverity", "SourceTimestamp"}).Get<UA_DateTime>(0), 0 );
		EXPECT_FALSE( ToString(Variable({"ClientUserId"}).Get<UA_String>(0)).empty() ) << "the commenter, the user the issued token resolved to";
		EXPECT_GT( Variable({"Comment", "SourceTimestamp"}).Get<UA_DateTime>(0), 0 );
	}
	//A comment with text and locale both empty leaves the last, which keeps its locale;  an empty text with a locale clears it (Part 9).
	TEST_F( ConditionTests, AnEmptyCommentKeepsTheLast ){
		Events events;
		const Subscription subscription{ *_client };
		ASSERT_NO_THROW( MonitorEvents(*_client, subscription, Node(PumpManual), events) );
		let [status, raised] = RaiseCondition( *_client, true, 300, "comment me" );
		ASSERT_EQ( status, UA_STATUSCODE_GOOD );
		ASSERT_TRUE( Wait(*_client, events, 1) );
		ASSERT_EQ( AddComment(*_client, raised, "bearing hot", "de"), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( Wait(*_client, events, 2) );
		EXPECT_EQ( ToString(Variable({"Comment"}).Get<UA_LocalizedText>(0).locale), "de" );
		ASSERT_EQ( Acknowledge(*_client, bytes(events.Received[1], "/EventId"), ""), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( Wait(*_client, events, 3) );
		EXPECT_EQ( text(events.Received[2], "/Comment"), "bearing hot" );
		EXPECT_EQ( ToString(Variable({"Comment"}).Get<UA_LocalizedText>(0).locale), "de" );
		ASSERT_EQ( AddComment(*_client, bytes(events.Received[2], "/EventId"), "", "en"), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( Wait(*_client, events, 4) );
		EXPECT_EQ( text(events.Received[3], "/Comment"), "" );
		EXPECT_EQ( ToString(Variable({"Comment"}).Get<UA_LocalizedText>(0).locale), "en" );
	}
	//Writing HighLimit compares the input again, with no write to the input.
	TEST_F( ConditionTests, WritingTheLimitReevaluates ){
		Events events;
		const Subscription subscription{ *_client };
		ASSERT_NO_THROW( MonitorEvents(*_client, subscription, Node(PumpManual), events) );
		ASSERT_EQ( WriteDouble(RpmManual, 1400), UA_STATUSCODE_GOOD );
		ASSERT_EQ( WriteDouble(HighLimit, 1300), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( Wait(*_client, events, 1) ) << "the limit lowered under the rpm raised nothing";
		EXPECT_EQ( boolean(events.Received[0], "/ActiveState/Id"), optional{true} );
		EXPECT_EQ( text(events.Received[0], "/Message"), "motorRpm at 1400 is over its limit of 1300." );
		ASSERT_EQ( WriteDouble(HighLimit, 1500), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( Wait(*_client, events, 2) ) << "the limit raised over the rpm cleared nothing";
		EXPECT_EQ( boolean(events.Received[1], "/ActiveState/Id"), optional{false} );
	}
	//The events' SourceName is the source's DisplayName when the event is raised, which a client with the WriteMask may have changed.
	TEST_F( ConditionTests, SourceNameFollowsARename ){
		Events events;
		const Subscription subscription{ *_client };
		ASSERT_NO_THROW( MonitorEvents(*_client, subscription, Node(PumpManual), events) );
		let ua = GetUAServer().Ptr();
		UA_LocalizedText was; UA_LocalizedText_init( &was );
		ASSERT_EQ( UA_Server_readDisplayName(ua, Node(PumpManual), &was), UA_STATUSCODE_GOOD );
		const UA_LocalizedText renamed{ was.locale, UA_STRING((char*)"renamed pump") };//in the node's locale:  open62541 keeps one text per locale and reads the first.
		ASSERT_EQ( UA_Server_writeDisplayName(ua, Node(PumpManual), renamed), UA_STATUSCODE_GOOD );
		let status = RaiseCondition( *_client, true, 200, "renamed" ).first;
		let arrived = Wait( *_client, events, 1 );
		EXPECT_EQ( UA_Server_writeDisplayName(ua, Node(PumpManual), was), UA_STATUSCODE_GOOD );
		UA_LocalizedText_clear( &was );
		ASSERT_EQ( status, UA_STATUSCODE_GOOD );
		ASSERT_TRUE( arrived );
		EXPECT_EQ( text(events.Received[0], "/SourceName"), "renamed pump" );
	}
	//Raise and raiseEvent refuse a severity outside Part 5's 1 to 1000, as their InputArguments say, and take its ends.
	TEST_F( ConditionTests, ASeverityOutOfRangeIsRefused ){
		for( const UA_UInt16 severity : {UA_UInt16{0}, UA_UInt16{1001}} ){
			EXPECT_EQ( RaiseCondition(*_client, true, severity, "out of range").first, UA_STATUSCODE_BADINVALIDARGUMENT ) << severity;
			const UA_String message = UA_STRING( (char*)"out of range" );
			EXPECT_EQ( Call(*_client, Node(PumpManual), Node(RaiseEventMethod), {Scalar(&message, UA_TYPES_STRING), Scalar(&severity, UA_TYPES_UINT16)}).Status, UA_STATUSCODE_BADINVALIDARGUMENT ) << severity;
		}
		EXPECT_EQ( RaiseCondition(*_client, true, 1, "the bottom").first, UA_STATUSCODE_GOOD );
		EXPECT_EQ( RaiseCondition(*_client, true, 1000, "the top").first, UA_STATUSCODE_GOOD );
	}
}