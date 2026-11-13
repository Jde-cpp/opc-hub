//Flows 6 (#452):  a flow's live activity over the gateway's socket (docs/flows/spec.md *Live*) - a FlowSubscribe's ack with
//the version, each node's status and its figures, a FlowActivity every `live` at most with each wire's count and last
//message, the status changes and the errors, the trace's records with `hops`, the ack of a run that takes the first's
//place, a FlowUnsubscribe, the refusals, the socket dropped and the subscription ended with its flow.  The cadence's cases
//are on a manual clock:  a feed's timer runs only when a test moves it.
#include <jde/opc/uatypes/DateTime.h>
#include "utils/FlowFixture.h"
#include "utils/GatewayClientSocket.h"
#include "../src/flows/FlowFeed.h"
#include "../src/types/proto/opc.FromServer.h"

namespace Jde::Opc::Gateway::Tests{
	using Web::Client::ClientSocketAwait;
	//What the socket delivered for one subscription after its first ack, in order.
	struct FlowHeard final : IFlowListener{
		α OnActivity( const FromServer::FlowActivity& activity )ι->void override{ lg _{ _mutex }; _pushes.push_back( activity ); }
		α OnAck( const FromServer::FlowSubscribeAck& ack )ι->void override{ lg _{ _mutex }; _acks.push_back( ack ); }
		α OnError( string what )ι->void override{ lg _{ _mutex }; _errors.push_back( move(what) ); }
		α Count()Ι->uint{ lg _{ _mutex }; return _pushes.size(); }
		α Push( uint i )Ι->FromServer::FlowActivity{ lg _{ _mutex }; return i<_pushes.size() ? _pushes[i] : FromServer::FlowActivity{}; }
		α Acks()Ι->vector<FromServer::FlowSubscribeAck>{ lg _{ _mutex }; return _acks; }
		α Errors()Ι->vector<string>{ lg _{ _mutex }; return _errors; }
	private:
		mutable std::mutex _mutex;
		vector<FromServer::FlowActivity> _pushes;
		vector<FromServer::FlowSubscribeAck> _acks;
		vector<string> _errors;
	};

	struct FlowSubscribeTests : FlowFixture{
		Ω Subscribe( GatewayClientSocket& socket, uint32 flow, bool hops, sp<FlowHeard> heard )ε->FromServer::FlowSubscribeAck{
			return BlockAwait<ClientSocketAwait<FromServer::FlowSubscribeAck>,FromServer::FlowSubscribeAck>( socket.FlowSubscribe(flow, hops, move(heard)) );
		}
		Ω Subscribe( uint32 flow, bool hops, sp<FlowHeard> heard )ε->FromServer::FlowSubscribeAck{ return Subscribe( Socket(), flow, hops, move(heard) ); }
		Ω Unsubscribe( RequestId subscription )ε->bool{ return BlockAwait<ClientSocketAwait<bool>,bool>( Socket().FlowUnsubscribe(subscription) ); }
		Ω Last()ι->RequestId{ return Socket().LastFlowSubscription(); }
		Ω Time( const google::protobuf::Timestamp& t )ι->TimePoint{ return UADateTime{ t }.Time(); }
		Ω WaitHeard( const FlowHeard& heard, uint count )ι->bool{ return WaitUntil( [&]{ return heard.Count()>=count; }, 15s ); }
		//A node of an ack or of a push, by its id:  an empty one for none.
		Ω Node( const FromServer::FlowSubscribeAck& ack, sv id )ι->FromServer::FlowNodeState{
			let p = find( ack.nodes(), id, &FromServer::FlowNodeState::node );
			return p==ack.nodes().end() ? FromServer::FlowNodeState{} : *p;
		}
		Ω Node( const FromServer::FlowActivity& push, sv id )ι->FromServer::FlowNodeActivity{
			let p = find( push.nodes(), id, &FromServer::FlowNodeActivity::node );
			return p==push.nodes().end() ? FromServer::FlowNodeActivity{} : *p;
		}
		Ω Wire( const FromServer::FlowActivity& push, sv node, sv to )ι->FromServer::FlowWire{
			for( let& wire : push.wires() ){
				if( wire.node()==node && wire.to()==to )
					return wire;
			}
			return {};
		}
		//Each record a push carries as `kind node`, in order.
		Ω Hops( const FromServer::FlowActivity& push )ι->vector<string>{
			vector<string> y;
			for( let& hop : push.hops() )
				y.push_back( Ƒ("{} {}", ToString((Flows::ERecordKind)hop.kind()), hop.node()) );
			return y;
		}
		Ω Json( str bytes )ε->jobject{ return parse( bytes ).as_object(); }
		//An inject through a Function node that says what it saw in its status, into a debug;  and one into a body that throws.
		Ω Seen()->jvalue{
			return Graph( {
				N( "in", "inject", "{}", {{"f"}} ),
				N( "f", "function", serialize(jobject{{"code", "node.status( {fill:'green', shape:'dot', text:'seen '+msg.n} );\nreturn msg;"}}), {{"d"}} ),
				N( "d", "debug" ),
				N( "bad", "inject", "{}", {{"boom"}} ),
				N( "boom", "function", serialize(jobject{{"code", "throw new Error( 'no good' );"}}) )
			} );
		}
		Ω Marked()->jvalue{ return Graph( {N("in", "inject", "{}", {{"mark"}}), N("mark", "change", R"({"rules":[{"t":"set","p":"seen","to":true}]})", {{"d"}}), N("d", "debug")} ); }
		Ω Send( uint32 flow, uint n, jvalue payload={} )ε->void{ Inject( flow, "in", jobject{{"payload", payload.is_null() ? jvalue{ n } : move(payload)}, {"n", n}} ); }
	};

	//The ack carries the version, and each node's status and figures since the flow started.  What the flow does next is pushed
	//when the clock reaches it, each wire's count and last message, a 64-bit integer of its json a Long, the status that changed
	//and the figures;  a second change `live` after the first at the soonest;  nothing while nothing changes;  an error with its
	//text and a json past valueLimit cut;  and nothing follows a FlowUnsubscribe.
	TEST_F( FlowSubscribeTests, TheAckThenEveryLiveTheCountsTheLastMessagesAndTheStatusChanges ){
		auto [clock, restart] = OnClock( Clock::now() );
		let flow = CreateFlow( "live" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		ASSERT_EQ( Deploy(flow, Seen()).at("running"), true );
		Send( flow, 1 );
		ASSERT_TRUE( Received(flow, "d", 1) );
		let feed = Flows().Feed( flow );
		ASSERT_TRUE( feed );
		EXPECT_EQ( feed->Subscribers(), 0u );
		let live = Flows().Live();
		EXPECT_EQ( live, Duration{200ms} );

		auto heard = ms<FlowHeard>();
		let ack = Subscribe( flow, false, heard );
		let subscription = Last();
		EXPECT_EQ( ack.version(), 1u );
		ASSERT_EQ( ack.nodes_size(), 5 );
		EXPECT_EQ( ack.nodes(0).node(), "in" );
		EXPECT_EQ( ack.nodes(4).node(), "boom" );
		ASSERT_EQ( Node(ack, "in").counts().sent_size(), 1 );
		EXPECT_EQ( Node(ack, "in").counts().sent(0), 1u );
		EXPECT_EQ( Node(ack, "f").counts().received(), 1u );
		EXPECT_EQ( Node(ack, "f").status().fill(), "green" );
		EXPECT_EQ( Node(ack, "f").status().shape(), "dot" );
		EXPECT_EQ( Node(ack, "f").status().text(), "seen 1" );
		EXPECT_EQ( Node(ack, "d").counts().received(), 1u );
		EXPECT_EQ( Node(ack, "d").status().text(), "" );
		EXPECT_EQ( Node(ack, "boom").counts().errors(), 0u );
		EXPECT_EQ( feed->Subscribers(), 1u );

		Send( flow, 2 );
		Send( flow, 3 );
		ASSERT_TRUE( Received(flow, "d", 3) );
		std::this_thread::sleep_for( 100ms );
		EXPECT_EQ( heard->Count(), 0u ) << "pushed by the feed's timer, on the flows' clock";
		clock->Advance( 0s );
		ASSERT_TRUE( WaitHeard(*heard, 1) );
		let first = heard->Push( 0 );
		ASSERT_EQ( first.wires_size(), 2 );
		let toF = Wire( first, "in", "f" ), toD = Wire( first, "f", "d" );
		EXPECT_EQ( toF.port(), 0u );
		EXPECT_EQ( toF.count(), 2u );
		EXPECT_EQ( toD.count(), 2u );
		EXPECT_EQ( Json(toF.json()).at("n"), 3 ) << "the last message down the wire";
		EXPECT_EQ( Json(toF.json()).at("_id"), Flows::ToLong(toF.msg_id()) ) << "a 64-bit integer of the json is the web's Long";
		EXPECT_EQ( toD.msg_id(), toF.msg_id() ) << "the message itself passes the node";
		EXPECT_FALSE( toF.cut() );
		EXPECT_EQ( Time(toF.time()), clock->Now() );
		EXPECT_EQ( Node(first, "in").counts().sent(0), 2u );
		EXPECT_FALSE( Node(first, "in").has_status() );
		EXPECT_EQ( Node(first, "f").counts().received(), 2u );
		EXPECT_EQ( Node(first, "f").status().text(), "seen 3" ) << "the status as it stands, where it changed";
		EXPECT_EQ( Node(first, "d").counts().received(), 2u );
		EXPECT_EQ( first.hops_size(), 0 );

		Send( flow, 4 );
		ASSERT_TRUE( Received(flow, "d", 4) );
		clock->Advance( live/2 );
		std::this_thread::sleep_for( 100ms );
		EXPECT_EQ( feed->Pushed(), 1u ) << "at most every `live`";
		clock->Advance( live/2 );
		ASSERT_TRUE( WaitHeard(*heard, 2) );
		EXPECT_EQ( Wire(heard->Push(1), "in", "f").count(), 1u ) << "since the push before";
		EXPECT_EQ( Node(heard->Push(1), "f").status().text(), "seen 4" );
		clock->Advance( live*2 );
		std::this_thread::sleep_for( 100ms );
		EXPECT_EQ( feed->Pushed(), 2u ) << "nothing changed, nothing pushed";

		//A json past valueLimit, and a node's error.
		Send( flow, 5, jvalue{string(5000, 'x')} );
		ASSERT_TRUE( Received(flow, "d", 5) );
		Inject( flow, "bad" );
		ASSERT_TRUE( WaitUntil([&]{ return Flows().Status( flow, "boom" ).Fill=="red"; }) );
		clock->Advance( live );
		ASSERT_TRUE( WaitHeard(*heard, 3) );
		let third = heard->Push( 2 );
		EXPECT_TRUE( Wire(third, "in", "f").cut() );
		EXPECT_LE( Wire(third, "in", "f").json().size(), 4096u ) << "cut as the trace cuts it";
		EXPECT_EQ( Node(third, "boom").counts().errors(), 1u );
		EXPECT_TRUE( Has(Node(third, "boom").error(), "no good") );
		EXPECT_EQ( Node(third, "boom").status().fill(), "red" );
		EXPECT_EQ( Node(third, "bad").counts().sent(0), 1u );

		//Unsubscribed:  out of the feed, and nothing the flow does reaches the socket.
		let strays = Socket().FlowStrays();
		EXPECT_TRUE( Unsubscribe(subscription) );
		EXPECT_EQ( feed->Subscribers(), 0u );
		Send( flow, 6 );
		ASSERT_TRUE( Received(flow, "d", 6) );
		clock->Advance( live*2 );
		std::this_thread::sleep_for( 200ms );
		EXPECT_EQ( feed->Pushed(), 3u );
		EXPECT_EQ( heard->Count(), 3u );
		EXPECT_EQ( Socket().FlowStrays(), strays );
		EXPECT_THROW( Unsubscribe(subscription), GatewayErrorResponse ) << "ended already";
		EXPECT_THROW( Unsubscribe(999'999), GatewayErrorResponse );
	}

	//With `hops` a push carries the records the trace made, in its order, a record's json as the wire's;  a subscription
	//without them gets the same push less the records.  A join pushes what was still to come to those already subscribed, so
	//its ack and what follows count nothing twice.  A restart at another trace level is a new ack, and `counts` makes no record.
	TEST_F( FlowSubscribeTests, HopsOnCarriesTheTracesRecords ){
		auto [clock, restart] = OnClock( Clock::now() );
		let flow = CreateFlow( "live-hops" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		ASSERT_EQ( Deploy(flow, Marked()).at("running"), true );
		let live = Flows().Live();
		auto hops = ms<FlowHeard>(), plain = ms<FlowHeard>();
		Subscribe( flow, true, hops );
		let first = Last();
		Inject( flow, "in", jobject{{"payload", 7}} );
		ASSERT_TRUE( Received(flow, "d", 1) );
		let ack = Subscribe( flow, false, plain );
		let second = Last();
		ASSERT_TRUE( WaitHeard(*hops, 1) ) << "a join pushes what was still to come to those subscribed before it";
		EXPECT_EQ( Node(ack, "d").counts().received(), 1u ) << "and its ack has it";
		let push = hops->Push( 0 );
		EXPECT_EQ( Hops(push), (vector<string>{"birth in", "hop mark", "hop d"}) );
		ASSERT_EQ( push.hops_size(), 3 );
		let& birth = push.hops( 0 ); let& mark = push.hops( 1 ); let& debug = push.hops( 2 );
		EXPECT_EQ( birth.kind(), FromServer::FlowRecordBirth );
		EXPECT_EQ( Json(birth.json()).at("payload"), 7 );
		EXPECT_EQ( mark.msg_id(), birth.msg_id() );
		EXPECT_EQ( mark.parent_id(), 0u );
		EXPECT_EQ( mark.run_id(), 0u );
		ASSERT_EQ( mark.ports_size(), 1 );
		EXPECT_EQ( Json(mark.json()).at("seen"), true ) << "the message where the node changed it";
		EXPECT_EQ( Json(mark.json()).at("_id"), Flows::ToLong(mark.msg_id()) ) << "its ids Longs";
		EXPECT_FALSE( mark.cut() );
		EXPECT_EQ( Time(mark.time()), clock->Now() );
		EXPECT_FALSE( debug.has_json() ) << "a hop that passed the message as it came";
		EXPECT_EQ( push.hops_dropped(), 0u );
		EXPECT_EQ( push.wires_size(), 2 );
		EXPECT_EQ( Wire(push, "mark", "d").msg_id(), birth.msg_id() );

		clock->Advance( live );
		std::this_thread::sleep_for( 100ms );
		EXPECT_EQ( plain->Count(), 0u ) << "what its ack counted is not pushed to it";
		EXPECT_EQ( hops->Count(), 1u );
		Inject( flow, "in", jobject{{"payload", 8}} );
		ASSERT_TRUE( Received(flow, "d", 2) );
		clock->Advance( live );
		ASSERT_TRUE( WaitHeard(*plain, 1) );
		ASSERT_TRUE( WaitHeard(*hops, 2) );
		EXPECT_EQ( plain->Push(0).hops_size(), 0 );
		EXPECT_EQ( Wire(plain->Push(0), "in", "mark").count(), 1u );
		EXPECT_EQ( Node(plain->Push(0), "d").counts().received(), 1u );
		EXPECT_EQ( Hops(hops->Push(1)), (vector<string>{"birth in", "hop mark", "hop d"}) );
		EXPECT_EQ( Wire(hops->Push(1), "in", "mark").count(), 1u );

		Mutate( "updateFlow( id:$id, trace:\"counts\" )", {{"id", flow}} );
		ASSERT_TRUE( WaitUntil([&]{ return hops->Acks().size()==1 && plain->Acks().size()==1; }) ) << "a restart is a new run, and its ack";
		EXPECT_EQ( hops->Acks()[0].version(), 1u );
		EXPECT_EQ( Node(hops->Acks()[0], "d").counts().received(), 0u ) << "the new run's figures";
		Inject( flow, "in", jobject{{"payload", 9}} );
		ASSERT_TRUE( Received(flow, "d", 1) );
		clock->Advance( live );
		ASSERT_TRUE( WaitHeard(*hops, 3) );
		EXPECT_EQ( hops->Push(2).hops_size(), 0 ) << "`counts` makes no record";
		EXPECT_EQ( Wire(hops->Push(2), "mark", "d").count(), 1u );
		EXPECT_TRUE( Unsubscribe(first) );
		EXPECT_TRUE( Unsubscribe(second) );
		EXPECT_EQ( Flows().Feed(flow)->Subscribers(), 0u );
	}

	//A deploy is answered by the new version's ack under the subscription's id, and the pushes go on from it;  the flow
	//disabled ends the subscription with an exception under its id, one that is not running refuses another, and a purge ends
	//it too.  On the system clock, as the gateway runs.
	TEST_F( FlowSubscribeTests, ARedeployIsANewAckAndAStopEndsIt ){
		let flow = CreateFlow( "live-redeploy" );
		bool purged{};
		absl::Cleanup purge = [&]{ if( !purged ) PurgeFlow( flow ); };
		ASSERT_EQ( Deploy(flow, Plain()).at("running"), true );
		let feed = Flows().Feed( flow );
		ASSERT_TRUE( feed );
		auto heard = ms<FlowHeard>();
		let ack = Subscribe( flow, false, heard );
		let subscription = Last();
		EXPECT_EQ( ack.version(), 1u );
		EXPECT_EQ( ack.nodes_size(), 2 );
		Inject( flow, "in", jobject{{"payload", 1}} );
		ASSERT_TRUE( WaitHeard(*heard, 1) );
		EXPECT_EQ( Wire(heard->Push(0), "in", "d").count(), 1u );

		ASSERT_EQ( Deploy(flow, Marked()).at("running"), true );
		ASSERT_TRUE( WaitUntil([&]{ return heard->Acks().size()==1; }) ) << "the new version's ack";
		let redeployed = heard->Acks()[0];
		EXPECT_EQ( redeployed.version(), 2u );
		ASSERT_EQ( redeployed.nodes_size(), 3 );
		EXPECT_EQ( redeployed.nodes(1).node(), "mark" );
		EXPECT_EQ( Node(redeployed, "d").counts().received(), 0u );
		EXPECT_EQ( feed->Subscribers(), 1u );
		let before = heard->Count();
		Inject( flow, "in", jobject{{"payload", 2}} );
		ASSERT_TRUE( WaitUntil([&]{ return heard->Count()>before && Wire( heard->Push(heard->Count()-1), "mark", "d" ).count()==1; }, 15s) ) << "the pushes go on, the new run's";

		Mutate( "updateFlow( id:$id, enabled:false )", {{"id", flow}} );
		ASSERT_TRUE( WaitUntil([&]{ return heard->Errors().size()==1; }, 15s) );
		EXPECT_TRUE( Has(heard->Errors()[0], "is not running") && Has(heard->Errors()[0], "disabled") );
		EXPECT_EQ( feed->Subscribers(), 0u );
		EXPECT_THROW( Unsubscribe(subscription), GatewayErrorResponse ) << "the id is free";
		let stopped = Refused( [&]{ Subscribe( flow, false, ms<FlowHeard>() ); } );
		EXPECT_EQ( stopped.Status, EHttpStatus::Conflict );
		EXPECT_TRUE( Has(stopped.What, "is not running") );

		Mutate( "updateFlow( id:$id, enabled:true )", {{"id", flow}} );
		ASSERT_TRUE( Running(flow) );
		auto again = ms<FlowHeard>();
		EXPECT_EQ( Subscribe(flow, true, again).version(), 2u );
		PurgeFlow( flow );
		purged = true;
		ASSERT_TRUE( WaitUntil([&]{ return again->Errors().size()==1; }, 15s) );
		EXPECT_TRUE( Has(again->Errors()[0], "purged") );
		EXPECT_EQ( Refused([&]{ Subscribe( flow, false, ms<FlowHeard>() ); }).Status, EHttpStatus::NotFound );
	}

	//The refusals - no flow, no id, one never deployed, a socket that bound no session, a caller without Read on the flow, an
	//id a subscription holds - and the socket dropped, which takes its subscription out of the feed.
	TEST_F( FlowSubscribeTests, TheRefusalsAndADroppedSocket ){
		let flow = CreateFlow( "live-refused" ), never = CreateFlow( "live-never" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); PurgeFlow( never ); };
		ASSERT_EQ( Deploy(flow, Plain()).at("running"), true );
		let feed = Flows().Feed( flow );
		ASSERT_TRUE( feed );
		let subscribe = [&]( uint32 id ){ return Refused( [&]{ Subscribe( id, false, ms<FlowHeard>() ); } ); };

		EXPECT_EQ( subscribe(999'999).Status, EHttpStatus::NotFound );
		EXPECT_EQ( subscribe(0).Status, EHttpStatus::BadRequest );
		let idle = subscribe( never );
		EXPECT_EQ( idle.Status, EHttpStatus::Conflict );
		EXPECT_TRUE( Has(idle.What, "is not running") );
		auto unbound = GatewayClientSocket::Open( "localhost", GatewayPort(), nullopt );
		EXPECT_THROW( Subscribe(*unbound, flow, false, ms<FlowHeard>()), GatewayErrorResponse );
		BlockAwait( unbound->Close(false, SRCE_CUR) );
		{//the flow's own row grants the suite's user no Read.
			auto enforced = EnforcedFlows();
			ScopeFlow( flow, ERights::Update );
			absl::Cleanup unscope = [&]{ try{ UnscopeFlow( flow ); }catch( const std::exception& e ){ ADD_FAILURE() << "the flow's resource wasn't opened again:  " << e.what(); } };
			EXPECT_EQ( subscribe(flow).Status, EHttpStatus::Forbidden );
		}
		EXPECT_EQ( feed->Subscribers(), 0u ) << "none joined past a refusal";

		auto dropped = GatewayClientSocket::Open( "localhost", GatewayPort(), AppClient()->SessionId() );
		EXPECT_EQ( Subscribe(*dropped, flow, true, ms<FlowHeard>()).version(), 1u );
		EXPECT_EQ( feed->Subscribers(), 1u );
		BlockAwait( dropped->Close(false, SRCE_CUR) );
		EXPECT_TRUE( WaitUntil([&]{ return feed->Subscribers()==0; }, 15s) ) << "the socket's close ends its subscriptions";

		auto heard = ms<FlowHeard>();
		Subscribe( flow, false, heard );
		let subscription = Last();
		let twice = Refused( [&]{ BlockAwait<ClientSocketAwait<FromServer::FlowSubscribeAck>,FromServer::FlowSubscribeAck>( Socket().FlowSubscribeAs(subscription, flow) ); } );
		EXPECT_EQ( twice.Status, EHttpStatus::BadRequest ) << "a live subscription's id names it alone";
		EXPECT_EQ( feed->Subscribers(), 1u ) << "the refusal left it as it was";
		Inject( flow, "in", jobject{{"payload", 1}} );
		EXPECT_TRUE( WaitHeard(*heard, 1) ) << "and it hears on";
		EXPECT_TRUE( Unsubscribe(subscription) );
	}

	//`/gateway/flows/live` is how often at most a feed pushes;  one that is not positive keeps the default.
	TEST_F( FlowSubscribeTests, LiveIsTheBlocksSetting ){
		absl::Cleanup settings = []{
			Settings::Set( "/gateway/flows/live", "PT0.2S" );
			Flows().Stop();
			Flows().Start();
		};
		Settings::Set( "/gateway/flows/live", "PT0.5S" );
		Flows().Stop();
		Flows().Start();
		EXPECT_EQ( Flows().Live(), Duration{500ms} );
		Settings::Set( "/gateway/flows/live", "PT0S" );
		Flows().Stop();
		Flows().Start();
		EXPECT_EQ( Flows().Live(), Duration{200ms} );
	}

	//An ack and a push are serialized once and framed by hand for each subscriber:  the bytes are the proto's own.  A push's
	//records are appended to its nodes and wires, which reads as the one message.
	TEST( FlowTransTests, FramedAsTheProtoSerializesIt ){
		FromServer::FlowActivity activity;
		auto& wire = *activity.add_wires();
		wire.set_node( "in" );
		wire.set_json( string(300, 'j') );
		FromServer::FlowSubscribeAck ack;
		ack.set_version( 3 );
		ack.add_nodes()->set_node( "in" );
		for( Jde::RequestId id : {0u, 1u, 300u, 0xffffffffu} ){
			FromServer::Transmission pushed, acked;
			auto& push = *pushed.add_messages();
			push.set_request_id( id );
			*push.mutable_flow_activity() = activity;
			EXPECT_EQ( FromServer::FlowActivityTrans(activity.SerializeAsString(), id), pushed.SerializeAsString() ) << id;
			auto& answer = *acked.add_messages();
			answer.set_request_id( id );
			*answer.mutable_flow_subscribe_ack() = ack;
			EXPECT_EQ( FromServer::FlowSubscribeAckTrans(ack.SerializeAsString(), id), acked.SerializeAsString() ) << id;
		}
		FromServer::FlowActivity hops;
		hops.add_hops()->set_node( "d" );
		hops.set_hops_dropped( 2 );
		FromServer::FlowActivity both;
		ASSERT_TRUE( both.ParseFromString(activity.SerializeAsString()+hops.SerializeAsString()) );
		EXPECT_EQ( both.wires_size(), 1 );
		EXPECT_EQ( both.hops_size(), 1 );
		EXPECT_EQ( both.hops_dropped(), 2u );
	}
}