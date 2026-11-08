#include <absl/cleanup/cleanup.h>
#include <jde/opc/proto/opc.Common.h>
#include <jde/opc/server/Condition.h>
#include "utils/GatewayClientSocket.h"
#include "utils/helpers.h"
#include "utils/Pumps.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	using Web::Client::ClientSocketAwait;
	//The alarms' half of the gateway (clients spec *Alarms and conditions*, #426), against the embedded OpcServer's pumpManual.overspeed:
	//the condition raised through `call` on its Raise, retained on an events subscription with the condition's fields - the ConditionId
	//among them, under the operand's own spelling - replayed by `refreshConditions` to the suite's own subscription between RefreshStart
	//and RefreshEnd, then acknowledged and confirmed through `call` on the condition node, each a transition the subscription shows, and a
	//refusal answered as the call's status.
	struct AlarmTests : ::testing::Test{
		Ω SetUpTestSuite()->void{ GetConnection( OpcServerSlug ); }
	protected:
		//Each test starts from a condition that is inactive and not retained, whatever the one before left - through the gateway, as the
		//view would leave one.
		α SetUp()->void override{
			if( !Server::FindUAServer() )
				GTEST_SKIP() << "Needs the embedded OpcServer.";
			_condition = NodeId{ PumpsNs(), Overspeed };
			auto& condition = Condition();
			if( condition.Active() )
				ASSERT_EQ( RaiseStatus(false, 1, "reset"), UA_STATUSCODE_GOOD );
			if( condition.Retain() ){
				Comment( UA_NS0ID_ACKNOWLEDGEABLECONDITIONTYPE_ACKNOWLEDGE, Str::ToHex(condition.LastEventId()), "reset" );
				Comment( UA_NS0ID_ACKNOWLEDGEABLECONDITIONTYPE_CONFIRM, Str::ToHex(condition.LastEventId()), "reset" );
			}
			ASSERT_FALSE( condition.Retain() ) << "the condition could not be reset";
		}
		Ω Condition()ε->Server::Condition&{ auto y = Server::GetUAServer().Conditions().Find( NodeId{PumpsNs(), Overspeed} ); THROW_IF( !y, "The condition was not added." ); return *y; }
		//the call's answer, whatever its status:  only a failure before the call throws.
		Ω Call( const NodeId& object, const NodeId& method, jarray args )ε->jobject{
			const jobject vars{ {"opc", OpcServerSlug}, {"object", object.ToJson()}, {"method", method.ToJson()}, {"args", move(args)} };
			return Socket().QuerySync( "call( opc: $opc, object: $object, method: $method, args: $args ){ statusCode outputArguments }", vars ).as_object();
		}
		Ω Status( const jobject& result )ε->StatusCode{ return result.at( "statusCode" ).to_number<StatusCode>(); }
		//overspeed.Raise( active, severity, message ):  the status, and the EventId it answered as the json spells a ByteString, hex.
		α Raise( bool active, uint16 severity, sv message )ε->std::pair<StatusCode,string>{
			let y = Call( _condition, NodeId{PumpsNs(), OverspeedRaise}, {active, severity, message} );
			let sc = Status( y );
			return { sc, sc ? string{} : string{y.at("outputArguments").as_array().at(0).as_string()} };
		}
		α RaiseStatus( bool active, uint16 severity, sv message )ε->StatusCode{ return Raise( active, severity, message ).first; }
		//Acknowledge, Confirm or AddComment, the type's method `method` on the condition with `eventId` and `comment`:  the status.
		α Comment( UA_UInt32 method, str eventId, sv comment )ε->StatusCode{
			return Status( Call(_condition, NodeId{method}, {eventId, jobject{{"text", comment}}}) );
		}
		Ω RefreshConditions( const NodeId& node )ε->jobject{
			return Socket().QuerySync( "refreshConditions( opc: $opc, node: $node ){ statusCode }", {{"opc", OpcServerSlug}, {"node", node.ToJson()}} ).as_object();
		}
		NodeId _condition;
	};

	//An events subscription's listener:  each EventValues as it came.
	struct AlarmListener final : IListener{
		α OnData( string, NodeId, const Proto::Value& )ι->void override{}
		α OnEvent( const FromServer::EventValues& event )ι->void override{ lg _{ Mutex }; Events.push_back( event ); ++Count; }
		α At( uint i )ι->FromServer::EventValues{ lg _{ Mutex }; return Events.at( i ); }
		α WaitFor( uint count, Duration timeout=10s )ι->bool{ return WaitUntil( [&]{ return Count>=count; }, timeout ); }
		atomic<uint> Count{};
		mutex Mutex; vector<FromServer::EventValues> Events;
	};
	//The condition's fields beside the base eight, in the order the view asks them:  the ConditionId under the operand's own spelling,
	//then the state the view reads.
	const vector<string> ConditionSelect{ "i=2782#NodeId", "ConditionName", "BranchId", "Retain", "EnabledState", "EnabledState/Id", "ActiveState", "ActiveState/Id", "AckedState", "AckedState/Id", "ConfirmedState", "ConfirmedState/Id", "Comment" };
	enum EField : int{ IEventId, IEventType, ISourceNode, ISourceName, ITime, IReceiveTime, IMessage, ISeverity, IConditionId, IConditionName, IBranchId, IRetain, IEnabledText, IEnabled, IActiveText, IActive, IAckedText, IAcked, IConfirmedText, IConfirmed, IComment };
	Ω subscribeEvents( const NodeId& node, sp<IListener> listener )ε->FromServer::SubscriptionAck{
		return BlockAwait<ClientSocketAwait<FromServer::SubscriptionAck>,FromServer::SubscriptionAck>( Socket().SubscribeEvents(OpcServerSlug, node, ConditionSelect, move(listener)) );
	}
	Ω eventType( const FromServer::EventValues& e )ι->NodeId{ return ProtoUtils::ToNodeId( e.fields(IEventType).node() ); }
	Ω eventId( const FromServer::EventValues& e )ι->string{ return Str::ToHex( e.fields(IEventId).byte_string() ); }

	//The condition raised through `call` reaches the Server object's events subscription with the ConditionId and its state - active,
	//unacknowledged, retained;  `refreshConditions` on the node replays it, EventId kept, between RefreshStart and RefreshEnd, and is
	//refused for a node the client has no events item on;  Acknowledge then Confirm through `call` are transitions the subscription
	//shows with the comment, a second Acknowledge answered Bad_ConditionBranchAlreadyAcked as the call's status;  the raise cleared, the
	//condition is retained no more.
	TEST_F( AlarmTests, ARaisedConditionIsReplayedAcknowledgedAndConfirmed ){
		const NodeId server{ UA_NS0ID_SERVER };
		auto listener = ms<AlarmListener>();
		FromServer::SubscriptionAck ack;
		ASSERT_NO_THROW( ack = subscribeEvents(server, listener) );
		ASSERT_EQ( ack.results_size(), 1 );
		ASSERT_EQ( ack.results(0).status_code(), UA_STATUSCODE_GOOD ) << UAException::Message( ack.results(0).status_code() );
		absl::Cleanup end = [&]{ BlockAwait<ClientSocketAwait<FromServer::UnsubscribeAck>,FromServer::UnsubscribeAck>( Socket().UnsubscribeEvents(OpcServerSlug, server) ); };

		auto [status, raised] = Raise( true, 700, "too fast" );
		ASSERT_EQ( status, UA_STATUSCODE_GOOD ) << UAException::Message( status );
		ASSERT_TRUE( listener->WaitFor(1) ) << "the raise never reached the subscription";
		let event = listener->At( 0 );
		ASSERT_EQ( event.fields_size(), (int)ConditionSelect.size()+8 );
		EXPECT_EQ( eventId(event), raised );
		EXPECT_EQ( eventType(event), NodeId{UA_NS0ID_ALARMCONDITIONTYPE} );
		EXPECT_EQ( ProtoUtils::ToNodeId(event.fields(ISourceNode).node()), NodeId(PumpsNs(), PumpManual) );
		EXPECT_EQ( ProtoUtils::ToNodeId(event.fields(IConditionId).node()), _condition ) << "the ConditionId under the standard clause";
		EXPECT_EQ( event.fields(IMessage).localized_text().text(), "too fast" );
		EXPECT_EQ( event.fields(ISeverity).uint16(), 700u );
		EXPECT_TRUE( event.fields(IRetain).boolean() );
		EXPECT_TRUE( event.fields(IActive).boolean() );
		EXPECT_FALSE( event.fields(IAcked).boolean() );
		EXPECT_FALSE( event.fields(IConfirmed).boolean() );
		EXPECT_FALSE( event.fields(IConditionName).string_value().empty() );
		let& branch = event.fields( IBranchId );
		EXPECT_TRUE( !branch.has_node() || ProtoUtils::ToNodeId(branch.node())==NodeId{} ) << "one branch, so no BranchId";
		EXPECT_EQ( event.fields(IEnabledText).localized_text().text(), "Enabled" );
		EXPECT_TRUE( event.fields(IEnabled).boolean() );
		EXPECT_EQ( event.fields(IActiveText).localized_text().text(), "Active" );
		EXPECT_EQ( event.fields(IAckedText).localized_text().text(), "Unacknowledged" );
		EXPECT_EQ( event.fields(IConfirmedText).localized_text().text(), "Unconfirmed" );

		try{
			RefreshConditions( NodeId{PumpsNs(), PumpManual} );
			ADD_FAILURE() << "a refresh for a node with no events item of this client's was not refused";
		}
		catch( const Exception& e ){
			EXPECT_TRUE( string{e.what()}.contains("No events subscription") ) << e.what();
		}
		jobject refreshed;
		ASSERT_NO_THROW( refreshed = RefreshConditions(server) );
		EXPECT_EQ( Status(refreshed), UA_STATUSCODE_GOOD ) << serialize( refreshed );
		ASSERT_TRUE( listener->WaitFor(4) ) << "RefreshStart, the condition and RefreshEnd never came:  " << listener->Count;
		EXPECT_EQ( eventType(listener->At(1)), NodeId{UA_NS0ID_REFRESHSTARTEVENTTYPE} );
		let replayed = listener->At( 2 );
		EXPECT_EQ( eventType(replayed), NodeId{UA_NS0ID_ALARMCONDITIONTYPE} );
		EXPECT_EQ( eventId(replayed), raised ) << "the replay carries the transition's EventId";
		EXPECT_EQ( ProtoUtils::ToNodeId(replayed.fields(IConditionId).node()), _condition );
		EXPECT_TRUE( replayed.fields(IRetain).boolean() );
		EXPECT_EQ( replayed.fields(IMessage).localized_text().text(), "too fast" );
		EXPECT_EQ( eventType(listener->At(3)), NodeId{UA_NS0ID_REFRESHENDEVENTTYPE} );

		ASSERT_EQ( Comment(UA_NS0ID_ACKNOWLEDGEABLECONDITIONTYPE_ACKNOWLEDGE, raised, "seen"), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( listener->WaitFor(5) ) << "the acknowledge never reached the subscription";
		let acked = listener->At( 4 );
		EXPECT_NE( eventId(acked), raised ) << "a transition is an event of its own";
		EXPECT_TRUE( acked.fields(IAcked).boolean() );
		EXPECT_EQ( acked.fields(IAckedText).localized_text().text(), "Acknowledged" );
		EXPECT_FALSE( acked.fields(IConfirmed).boolean() );
		EXPECT_TRUE( acked.fields(IRetain).boolean() );
		EXPECT_EQ( acked.fields(IComment).localized_text().text(), "seen" );
		EXPECT_EQ( Comment(UA_NS0ID_ACKNOWLEDGEABLECONDITIONTYPE_ACKNOWLEDGE, eventId(acked), "again"), UA_STATUSCODE_BADCONDITIONBRANCHALREADYACKED ) << "a refusal is the call's status";
		EXPECT_EQ( Comment(UA_NS0ID_ACKNOWLEDGEABLECONDITIONTYPE_ACKNOWLEDGE, raised, "stale"), UA_STATUSCODE_BADEVENTIDUNKNOWN );

		ASSERT_EQ( Comment(UA_NS0ID_ACKNOWLEDGEABLECONDITIONTYPE_CONFIRM, eventId(acked), "dealt with"), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( listener->WaitFor(6) ) << "the confirm never reached the subscription";
		let confirmed = listener->At( 5 );
		EXPECT_TRUE( confirmed.fields(IAcked).boolean() );
		EXPECT_TRUE( confirmed.fields(IConfirmed).boolean() );
		EXPECT_EQ( confirmed.fields(IConfirmedText).localized_text().text(), "Confirmed" );
		EXPECT_TRUE( confirmed.fields(IRetain).boolean() ) << "still active";
		EXPECT_EQ( confirmed.fields(IComment).localized_text().text(), "dealt with" );

		ASSERT_EQ( RaiseStatus(false, 700, "back to normal"), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( listener->WaitFor(7) ) << "the clear never reached the subscription";
		let cleared = listener->At( 6 );
		EXPECT_FALSE( cleared.fields(IActive).boolean() );
		EXPECT_EQ( cleared.fields(IActiveText).localized_text().text(), "Inactive" );
		EXPECT_FALSE( cleared.fields(IRetain).boolean() ) << "inactive, acknowledged and confirmed";
		ASSERT_NO_THROW( refreshed = RefreshConditions(server) );
		EXPECT_EQ( Status(refreshed), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( listener->WaitFor(9) ) << "RefreshStart and RefreshEnd";
		EXPECT_EQ( eventType(listener->At(7)), NodeId{UA_NS0ID_REFRESHSTARTEVENTTYPE} );
		EXPECT_EQ( eventType(listener->At(8)), NodeId{UA_NS0ID_REFRESHENDEVENTTYPE} ) << "nothing replayed between";
	}

	//An item whose last listener left stays a moment for its delete (DeleteMonitoring):  a replay would reach nothing, so it is refused.
	TEST_F( AlarmTests, ARefreshIsRefusedOnceTheLastListenerLeft ){
		const NodeId pump{ PumpsNs(), PumpManual };
		auto listener = ms<AlarmListener>();
		FromServer::SubscriptionAck ack;
		ASSERT_NO_THROW( ack = subscribeEvents(pump, listener) );
		ASSERT_EQ( ack.results(0).status_code(), UA_STATUSCODE_GOOD ) << UAException::Message( ack.results(0).status_code() );
		ASSERT_NO_THROW( RefreshConditions(pump) );
		ASSERT_NO_THROW( (BlockAwait<ClientSocketAwait<FromServer::UnsubscribeAck>,FromServer::UnsubscribeAck>( Socket().UnsubscribeEvents(OpcServerSlug, pump) )) );
		try{
			RefreshConditions( pump );
			ADD_FAILURE() << "a refresh for an item no listener holds was not refused";
		}
		catch( const Exception& e ){
			EXPECT_TRUE( string{e.what()}.contains("No events subscription") ) << e.what();
		}
	}

	//The ConditionId clause and the view's state fields through the events path alone, on the pump's own item, where no bracket comes
	//(ConditionTests):  the replay reaches it as the condition alone.
	TEST_F( AlarmTests, ALowerNotifierGetsTheReplayWithoutTheBracket ){
		const NodeId pump{ PumpsNs(), PumpManual };
		auto listener = ms<AlarmListener>();
		FromServer::SubscriptionAck ack;
		ASSERT_NO_THROW( ack = subscribeEvents(pump, listener) );
		ASSERT_EQ( ack.results(0).status_code(), UA_STATUSCODE_GOOD ) << UAException::Message( ack.results(0).status_code() );
		absl::Cleanup end = [&]{ BlockAwait<ClientSocketAwait<FromServer::UnsubscribeAck>,FromServer::UnsubscribeAck>( Socket().UnsubscribeEvents(OpcServerSlug, pump) ); };
		auto [status, raised] = Raise( true, 300, "lower" );
		ASSERT_EQ( status, UA_STATUSCODE_GOOD ) << UAException::Message( status );
		ASSERT_TRUE( listener->WaitFor(1) );
		jobject refreshed;
		ASSERT_NO_THROW( refreshed = RefreshConditions(pump) );
		EXPECT_EQ( Status(refreshed), UA_STATUSCODE_GOOD ) << serialize( refreshed );
		ASSERT_TRUE( listener->WaitFor(2) ) << "the replay never reached the pump's item";
		let replayed = listener->At( 1 );
		EXPECT_EQ( eventType(replayed), NodeId{UA_NS0ID_ALARMCONDITIONTYPE} ) << "no RefreshStart on a lower notifier";
		EXPECT_EQ( eventId(replayed), raised );
		EXPECT_EQ( ProtoUtils::ToNodeId(replayed.fields(IConditionId).node()), _condition );
		std::this_thread::sleep_for( 500ms );
		EXPECT_EQ( listener->Count.load(), 2u ) << "and no RefreshEnd";
		ASSERT_EQ( RaiseStatus(false, 300, "cleared"), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( listener->WaitFor(3) );
		ASSERT_EQ( Comment(UA_NS0ID_ACKNOWLEDGEABLECONDITIONTYPE_ACKNOWLEDGE, eventId(listener->At(2)), "ok"), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( listener->WaitFor(4) );
		ASSERT_EQ( Comment(UA_NS0ID_ACKNOWLEDGEABLECONDITIONTYPE_CONFIRM, eventId(listener->At(3)), "ok"), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( listener->WaitFor(5) );
		EXPECT_FALSE( listener->At(4).fields(IRetain).boolean() );
	}
}