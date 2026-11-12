#include <jde/tests/Capture.h>
#include <jde/opc/openProtocol/logTags.h>
#include "Rig.h"

//The session against the controller's side of the codec (docs/open-protocol/plan.md *Phase 2*):  the step-down and the
//revisions it settles, both ways of acknowledging, the sequence numbers, the keep-alive, the lost link and its backoff.
namespace Jde::Opc::OpenProtocol::Tests{
	Ω millis( steady_clock::duration d )ι->_int{ return duration_cast<milliseconds>( d ).count(); }

	//What each request starts at with no ceiling:  the codec's last revision of what it asks for (plan review #11).
	TEST( SessionTests, FirstRevisions ){
		Rig rig;
		ASSERT_TRUE( rig.Start() );
		EXPECT_EQ( Revisions(rig.Got(Mid0001::Id)), (vector<uint16_t>{6}) );
		EXPECT_EQ( rig.Revision(Mid0001::Id), 6 ) << "settled by MID 0002";
		struct First{ uint16_t Mid; uint16_t Revision; };
		for( let& first : {First{60, 7}, First{70, 3}, First{51, 2}, First{14, 3}, First{34, 6}, First{38, 2}, First{64, 7}, First{18, 1}, First{80, 1}} )
			EXPECT_EQ( rig.Revision(first.Mid), first.Revision ) << first.Mid;
	}

	//§5.2.1 and Table 23:  a controller with MID 0002 to revision 3 refuses a start above it.
	TEST( SessionTests, StartStepsDown ){
		auto script = Scripted();
		script.Revisions = { {Mid0001::Id, 3} };
		Rig rig{ move(script) };
		ASSERT_TRUE( rig.Start() );
		EXPECT_EQ( Revisions(rig.Got(Mid0001::Id)), (vector<uint16_t>{6, 5, 4, 3}) );
		let connection = rig.Told.Connections()[0];
		EXPECT_EQ( connection.Revision, 3 );
		EXPECT_TRUE( connection.Identity==CaseOf<Mid0002>(3) );
		EXPECT_FALSE( connection.LinkLevel ) << "revision 3 says nothing of sequence numbers";
		EXPECT_EQ( rig.Revision(Mid0001::Id), 3 );
		EXPECT_TRUE( rig.Told.Losses().empty() );
		EXPECT_FALSE( rig.Station.ToolLockAsked() ) << "the session asks no revision that carries the tool lock";
	}

	//A controller may answer in the revision it has and say nothing (§1.4):  MID 0002 is read by its own header, and that
	//is the revision settled.
	TEST( SessionTests, StartAcknowledgeReadByItsHeader ){
		auto script = Scripted();
		script.Revisions = { {Mid0001::Id, 3} };
		script.Lowers = { Mid0001::Id };
		Rig rig{ move(script) };
		ASSERT_TRUE( rig.Start() );
		EXPECT_EQ( Revisions(rig.Got(Mid0001::Id)), (vector<uint16_t>{6}) );
		let connection = rig.Told.Connections()[0];
		EXPECT_EQ( connection.Revision, 3 );
		EXPECT_TRUE( connection.Identity==CaseOf<Mid0002>(3) );
		EXPECT_EQ( rig.Revision(Mid0001::Id), 3 );
	}

	TEST( SessionTests, CeilingsBoundWhatIsAsked ){
		auto settings = Quick();
		settings.Revisions = { {Mid0001::Id, 4}, {Mid0061::Id, 3}, {Mid0064::Id, 2} };
		Rig rig{ Scripted(), move(settings) };
		ASSERT_TRUE( rig.Start() );
		EXPECT_EQ( Revisions(rig.Got(Mid0001::Id)), (vector<uint16_t>{4}) );
		EXPECT_EQ( rig.Revision(Mid0060::Id), 3 ) << "MID 0061's ceiling is MID 0060's";
		EXPECT_EQ( rig.Revision(Mid0070::Id), 3 ) << "the codec's last, with no ceiling";
		let subscribed = rig.Ask( Mid0060{} );
		ASSERT_TRUE( subscribed ) << Why( subscribed );
		EXPECT_EQ( Revisions(rig.Got(Mid0060::Id)), (vector<uint16_t>{3}) );
		rig.Station.Push( CaseOf<Mid0061>() );
		ASSERT_TRUE( WaitUntil([&]{ return rig.Told.Pushes().size()==1; }) );
		EXPECT_EQ( rig.Told.Pushes()[0].Head.Revision, 3 );
		let old = rig.Ask( Mid0064{} );
		ASSERT_TRUE( old ) << Why( old );
		EXPECT_EQ( old->Head.Revision, 2 );
		EXPECT_EQ( Revisions(rig.Got(Mid0064::Id)), (vector<uint16_t>{2}) );
	}

	//A subscription steps down on the controller's code 74 and a request on its 76, and the revision each settles on is
	//where the next of its MID starts.
	TEST( SessionTests, RequestsStepDown ){
		auto script = Unnumbered();
		script.Revisions = { {Mid0060::Id, 5}, {Mid0064::Id, 2} };
		Rig rig{ move(script) };
		ASSERT_TRUE( rig.Start() );
		EXPECT_EQ( rig.Revision(Mid0060::Id), 7 );
		let subscribed = rig.Ask( Mid0060{} );
		ASSERT_TRUE( subscribed ) << Why( subscribed );
		EXPECT_TRUE( subscribed->Data==Body{Mid0005{.Mid=60}} );
		EXPECT_EQ( Revisions(rig.Got(Mid0060::Id)), (vector<uint16_t>{7, 6, 5}) );
		EXPECT_EQ( rig.Revision(Mid0060::Id), 5 );
		rig.Station.Push( CaseOf<Mid0061>() );
		ASSERT_TRUE( WaitUntil([&]{ return rig.Told.Pushes().size()==1; }) );
		let pushed = rig.Told.Pushes()[0];
		EXPECT_EQ( pushed.Head.Revision, 5 );
		EXPECT_TRUE( As<Mid0061>(pushed)==CaseOf<Mid0061>(5) );

		let old = rig.Ask( Mid0064{.TighteningId=0} );
		ASSERT_TRUE( old ) << Why( old );
		EXPECT_EQ( old->Head.Revision, 2 );
		EXPECT_EQ( As<Mid0065>(*old).TighteningId, CaseOf<Mid0061>().TighteningId );
		EXPECT_EQ( Revisions(rig.Got(Mid0064::Id)), (vector<uint16_t>{7, 6, 5, 4, 3, 2}) );
		EXPECT_EQ( rig.Revision(Mid0064::Id), 2 );
		ASSERT_TRUE( rig.Ask(Mid0064{.TighteningId=0}) );
		EXPECT_EQ( Revisions(rig.Got(Mid0064::Id)), (vector<uint16_t>{7, 6, 5, 4, 3, 2, 2}) ) << "the settled revision first from then on";
	}

	//Table 23's 74, 76 and 97 each step down, to revision 1 and then a refusal;  another code is the answer at once.
	TEST( SessionTests, StepsDownOnTheRevisionCodesAlone ){
		for( uint16_t code : {74, 76, 97, 9} ){
			auto script = Scripted();
			script.Refuses = { {Mid0060::Id, code} };
			Rig rig{ move(script) };
			ASSERT_TRUE( rig.Start() );
			let reply = rig.Ask( Mid0060{} );
			ASSERT_FALSE( reply ) << code;
			EXPECT_EQ( reply.error().Kind, EFailure::Refused );
			EXPECT_EQ( reply.error().Mid, 60 );
			EXPECT_EQ( reply.error().Code, code );
			EXPECT_EQ( rig.Got(Mid0060::Id).size(), code==9 ? 1u : 7u ) << code;
			if( code==9 )
				EXPECT_EQ( reply.error().What, "MID 0004 to MID 0060:  code 9, Last tightening result subscription already exists." );
			EXPECT_TRUE( rig.Told.Losses().empty() ) << "a refused request is no lost link";
			let time = rig.Ask( Mid0080{} );
			EXPECT_TRUE( time ) << Why( time );
		}
	}

	//Review #4:  a subscription accepted at 7 and pushed at 3 is read by each push's own header.
	TEST( SessionTests, PushesReadByTheirHeader ){
		auto script = Scripted();
		script.Revisions = { {Mid0060::Id, 3} };
		script.Lowers = { Mid0060::Id };
		Rig rig{ move(script) };
		ASSERT_TRUE( rig.Start() );
		ASSERT_TRUE( rig.Ask(Mid0060{}) );
		EXPECT_EQ( Revisions(rig.Got(Mid0060::Id)), (vector<uint16_t>{7}) );
		EXPECT_EQ( rig.Revision(Mid0060::Id), 7 ) << "what the controller accepted, until a push says otherwise";
		rig.Station.Push( CaseOf<Mid0061>() );
		ASSERT_TRUE( WaitUntil([&]{ return rig.Told.Pushes().size()==1; }) );
		let pushed = rig.Told.Pushes()[0];
		EXPECT_EQ( pushed.Head.Revision, 3 );
		EXPECT_TRUE( As<Mid0061>(pushed)==CaseOf<Mid0061>(3) );
		EXPECT_EQ( rig.Revision(Mid0060::Id), 3 );
	}

	//§3.2.2:  MID 0002 at revision 6 says the controller numbers its messages, so each message is numbered from 1 and
	//acknowledged by MID 9997 carrying the number after it, and no per-topic MID acknowledges a push.
	TEST( SessionTests, LinkLevelWhereRevision6SaysSo ){
		Rig rig;
		ASSERT_TRUE( rig.Start() );
		EXPECT_TRUE( rig.Told.Connections()[0].LinkLevel );
		EXPECT_EQ( rig.Told.Connections()[0].Revision, 6 );
		ASSERT_TRUE( rig.Ask(Mid0060{}) );
		rig.Station.Push( CaseOf<Mid0061>() );
		ASSERT_TRUE( WaitUntil([&]{ return rig.Told.Pushes().size()==1; }) );
		ASSERT_TRUE( rig.Ask(Mid0014{}) );
		ASSERT_TRUE( WaitUntil([&]{ return rig.Got(Mid9997::Id).size()==3; }) );
		EXPECT_EQ( Sequences(rig.Got(Mid0001::Id)), (vector<uint16_t>{0}) ) << "the numbers start after MID 0002";
		EXPECT_EQ( Sequences(rig.Got(Mid0060::Id)), (vector<uint16_t>{1}) );
		EXPECT_EQ( Sequences(rig.Got(Mid0014::Id)), (vector<uint16_t>{2}) );
		let acks = rig.Got( Mid9997::Id );
		EXPECT_EQ( Sequences(acks), (vector<uint16_t>{2, 3, 4}) ) << "the number after the acknowledged message's";
		EXPECT_EQ( As<Mid9997>(acks[0]).Mid, 5 );
		EXPECT_EQ( As<Mid9997>(acks[1]).Mid, 61 );
		EXPECT_EQ( As<Mid9997>(acks[2]).Mid, 5 );
		EXPECT_TRUE( rig.Got(Mid0062::Id).empty() );
		EXPECT_TRUE( rig.Told.Losses().empty() );
	}

	//The numbers run 1 to 99 and around, each way.
	TEST( SessionTests, SequenceNumbersWrap ){
		Rig rig;
		ASSERT_TRUE( rig.Start() );
		for( uint i=0; i<101; ++i ){
			let time = rig.Ask( Mid0080{} );
			ASSERT_TRUE( time ) << i << ":  " << Why( time );
		}
		ASSERT_TRUE( WaitUntil([&]{ return rig.Got(Mid9997::Id).size()==101; }) );
		let asked = Sequences( rig.Got(Mid0080::Id) ), acks = Sequences( rig.Got(Mid9997::Id) );
		EXPECT_EQ( asked[98], 99 );
		EXPECT_EQ( asked[99], 1 );
		EXPECT_EQ( asked[100], 2 );
		EXPECT_EQ( acks[97], 99 );
		EXPECT_EQ( acks[98], 1 ) << "the number after 99";
		EXPECT_EQ( acks[99], 2 );
		EXPECT_TRUE( rig.Told.Losses().empty() );
	}

	//Spec *Open questions* #7:  application acknowledgement where the controller does not number its messages, whether
	//revision 6 says so or MID 0002 came at a revision that says nothing.
	TEST( SessionTests, ApplicationAcksOtherwise ){
		auto revision5 = Scripted();
		revision5.Revisions = { {Mid0001::Id, 5} };
		for( let& script : {Unnumbered(), revision5} ){
			Rig rig{ script };
			ASSERT_TRUE( rig.Start() );
			EXPECT_FALSE( rig.Told.Connections()[0].LinkLevel );
			ASSERT_TRUE( rig.Ask(Mid0060{}) );
			rig.Station.Push( CaseOf<Mid0061>() );
			ASSERT_TRUE( WaitUntil([&]{ return rig.Told.Pushes().size()==1 && rig.Got(Mid0062::Id).size()==1; }) );
			rig.Station.SendRaw( Encode({.Mid=Mid0061::Id, .Revision=Mid0061::MaxRevision, .Sequence=5}, CaseOf<Mid0061>()).value() );
			ASSERT_TRUE( WaitUntil([&]{ return rig.Told.Pushes().size()==2 && rig.Got(Mid0062::Id).size()==2; }) ) << "a number in the header is no link level where the link is not";
			EXPECT_TRUE( rig.Got(Mid9997::Id).empty() );
			for( let& frame : rig.Station.Frames() )
				EXPECT_EQ( frame.Head.Sequence, 0 ) << "MID " << frame.Head.Mid;
		}
	}

	//Each subscription's push is acknowledged by its own MID:  0016, 0036, 0053, 0072, 0075 and 0077.
	TEST( SessionTests, EachTopicsAcknowledgement ){
		Rig rig{ Unnumbered() };
		ASSERT_TRUE( rig.Start() );
		rig.Station.Raise( CaseOf<Mid0071>() );//no one subscribes yet:  the alarm is active for MID 0076.
		for( let& subscribe : {Body{Mid0014{}}, Body{Mid0034{}}, Body{Mid0051{}}, Body{Mid0070{}}} ){
			let subscribed = rig.Ask( subscribe );
			ASSERT_TRUE( subscribed ) << Why( subscribed );
		}
		ASSERT_TRUE( WaitUntil([&]{ return rig.Told.Pushes().size()==1; }) ) << "the alarm's status, as its subscription was accepted";
		rig.Station.Select( CaseOf<Mid0015>() );
		rig.Station.Select( CaseOf<Mid0035>() );
		rig.Station.Select( CaseOf<Mid0052>() );
		rig.Station.AcknowledgeAlarm();
		rig.Station.Raise( CaseOf<Mid0071>() );
		ASSERT_TRUE( WaitUntil([&]{ return rig.Told.Pushes().size()==6 && rig.Got(Mid0072::Id).size()==1; }) );
		vector<uint16_t> pushed;
		for( let& m : rig.Told.Pushes() )
			pushed.push_back( m.Head.Mid );
		EXPECT_EQ( pushed, (vector<uint16_t>{76, 15, 35, 52, 74, 71}) );
		for( uint16_t ack : {16, 36, 53, 72, 75, 77} )
			EXPECT_EQ( rig.Got(ack).size(), 1u ) << ack;
		let pushes = rig.Told.Pushes();
		EXPECT_EQ( As<Mid0076>(pushes[0]).ErrorCode, CaseOf<Mid0071>().ErrorCode );
		EXPECT_TRUE( As<Mid0015>(pushes[1])==CaseOf<Mid0015>() );
		EXPECT_TRUE( As<Mid0035>(pushes[2])==CaseOf<Mid0035>() );
		EXPECT_TRUE( As<Mid0052>(pushes[3])==CaseOf<Mid0052>() );
		EXPECT_TRUE( As<Mid0071>(pushes[5])==CaseOf<Mid0071>() );
		EXPECT_EQ( pushes[4].Head.Revision, 2 ) << "MID 0074's own last revision, under a subscription at 3";
	}

	//Spec *Results*:  a push reaches the callback on the strand with its acknowledgement already on the wire, so what the
	//callback does never holds the controller.
	TEST( SessionTests, PushAcknowledgedBeforeItsCallback ){
		for( let numbered : {false, true} ){
			Rig* rig{};
			atomic<bool> onStrand{}, acknowledged{};
			Rig made{ numbered ? Scripted() : Unnumbered(), Quick(), [&]( const Message& ){
				onStrand = rig->Ran.running_in_this_thread();
				acknowledged = WaitUntil( [&]{ return numbered ? rig->Got(Mid9997::Id).size()==2 : rig->Got(Mid0062::Id).size()==1; }, 5s );
			} };
			rig = &made;
			ASSERT_TRUE( rig->Start() );
			ASSERT_TRUE( rig->Ask(Mid0060{}) );
			rig->Station.Push( CaseOf<Mid0061>() );
			ASSERT_TRUE( WaitUntil([&]{ return rig->Told.Pushes().size()==1; }) );
			EXPECT_TRUE( onStrand ) << numbered;
			EXPECT_TRUE( acknowledged ) << numbered << ":  the controller had its acknowledgement while the callback ran";
		}
	}

	//Review #5:  the number last acknowledged, again, is a retransmission:  the same MID 9997 again, and nothing to the caller.
	TEST( SessionTests, RetransmissionAcknowledgedAgainAndDropped ){
		Rig rig;
		ASSERT_TRUE( rig.Start() );
		ASSERT_TRUE( rig.Ask(Mid0060{}) );
		rig.Station.Push( Rundown(10) );
		ASSERT_TRUE( WaitUntil([&]{ return rig.Got(Mid9997::Id).size()==2; }) );
		rig.Station.Retransmit();
		ASSERT_TRUE( WaitUntil([&]{ return rig.Got(Mid9997::Id).size()==3; }) );
		let acks = rig.Got( Mid9997::Id );
		EXPECT_EQ( acks[2].Frame, acks[1].Frame ) << "the acknowledgement it had the first time";
		rig.Station.Push( Rundown(11) );
		ASSERT_TRUE( WaitUntil([&]{ return rig.Told.Pushes().size()==2; }) ) << "the next number is still the one expected";
		let pushes = rig.Told.Pushes();
		EXPECT_EQ( As<Mid0061>(pushes[0]).TighteningId, 10u );
		EXPECT_EQ( As<Mid0061>(pushes[1]).TighteningId, 11u );
		EXPECT_TRUE( rig.Told.Losses().empty() );
	}

	//Any other number out of order is answered by MID 9998 carrying the one expected, then a disconnect and a reconnect.
	TEST( SessionTests, OutOfOrderAnswered9998AndReconnected ){
		Rig rig;
		ASSERT_TRUE( rig.Start() );
		ASSERT_TRUE( rig.Ask(Mid0060{}) );
		rig.Station.SkipSequence();
		rig.Station.Push( CaseOf<Mid0061>() );
		ASSERT_TRUE( WaitUntil([&]{ return rig.Told.Connections().size()==2; }) );
		let rejected = rig.Got( Mid9998::Id );
		ASSERT_EQ( rejected.size(), 1u );
		EXPECT_TRUE( As<Mid9998>(rejected[0])==(Mid9998{.Mid=61, .Code=3}) );
		EXPECT_EQ( rejected[0].Head.Sequence, 2 ) << "MID 0005 came under 1, so 2 was expected, and the result came under 3";
		let losses = rig.Told.Losses();
		ASSERT_EQ( losses.size(), 1u );
		EXPECT_EQ( losses[0].Kind, EFailure::Sequence );
		EXPECT_EQ( losses[0].Mid, 61 );
		EXPECT_EQ( losses[0].What, "MID 0061 came under sequence number 3 where 2 was expected." );
		EXPECT_TRUE( rig.Told.Pushes().empty() ) << "what came out of order is not taken";
		EXPECT_EQ( Sequences(rig.Got(Mid0001::Id)), (vector<uint16_t>{0, 0}) );
		let again = rig.Ask( Mid0060{} );
		ASSERT_TRUE( again ) << Why( again );
		EXPECT_EQ( Sequences(rig.Got(Mid0060::Id)), (vector<uint16_t>{1, 1}) ) << "from 1 again on the new connection";
	}

	//§5.28.1:  MID 9999 after keepAlive without a message either way holds a controller that drops a silent client.
	TEST( SessionTests, KeepAliveHoldsTheLink ){
		for( let numbered : {false, true} ){
			auto script = numbered ? Scripted() : Unnumbered();
			script.DropSilent = 400ms;
			auto settings = Quick();
			settings.KeepAlive = 80ms;
			Rig rig{ move(script), move(settings) };
			ASSERT_TRUE( rig.Start() );
			std::this_thread::sleep_for( 900ms );
			EXPECT_TRUE( rig.Told.Losses().empty() ) << numbered;
			EXPECT_EQ( rig.Station.Clients(), 1u ) << numbered;
			EXPECT_GE( rig.Got(Mid9999::Id).size(), 4u ) << numbered;
		}
	}
	TEST( SessionTests, SilentLinkDroppedWithoutIt ){
		auto script = Scripted();
		script.DropSilent = 200ms;
		Rig rig{ move(script) };
		ASSERT_TRUE( rig.Start() );
		ASSERT_TRUE( WaitUntil([&]{ return !rig.Told.Losses().empty(); }) );
		EXPECT_EQ( rig.Told.Losses()[0].Kind, EFailure::Socket );
		EXPECT_EQ( rig.Told.Losses()[0].What, "The controller closed the connection." );
		EXPECT_TRUE( rig.Got(Mid9999::Id).empty() ) << "the keep-alive is 5 s off";
		EXPECT_TRUE( WaitUntil([&]{ return rig.Told.Connections().size()==2; }) ) << "and it connects again";
	}

	//§3.3.5:  a reply not here within replyTimeout is a lost link, to the request and to the caller.
	TEST( SessionTests, ReplyPastTheTimeoutIsALostLink ){
		auto settings = Quick();
		settings.ReplyTimeout = 200ms;
		Rig rig{ Scripted(), move(settings) };
		ASSERT_TRUE( rig.Start() );
		rig.Station.Delay( 2s );
		let reply = rig.Ask( Mid0060{} );
		ASSERT_FALSE( reply );
		EXPECT_EQ( reply.error().Kind, EFailure::Timeout );
		EXPECT_EQ( reply.error().Mid, 60 );
		EXPECT_EQ( reply.error().What, "MID 0060 had no answer within 200 ms." );
		ASSERT_TRUE( WaitUntil([&]{ return rig.Told.Connections().size()==2; }) );
		let losses = rig.Told.Losses();
		ASSERT_EQ( losses.size(), 1u );
		EXPECT_EQ( losses[0].Kind, EFailure::Timeout );
		let subscribed = rig.Ask( Mid0060{} );
		EXPECT_TRUE( subscribed ) << Why( subscribed );
	}

	//What was asked when the link goes is told why:  the request that was out by the loss itself, one behind it that it was never sent.
	TEST( SessionTests, LostLinkAnswersWhatWasAsked ){
		Rig rig{ Unnumbered() };
		ASSERT_TRUE( rig.Start() );
		rig.Station.Delay( 2s );
		mutex answersMutex;
		vector<Failure> answers;
		for( let& request : {Body{Mid0060{}}, Body{Mid0014{}}} ){
			rig.Client.Request( request, [&]( Session::Reply&& reply ){
				lg _{ answersMutex };
				answers.push_back( reply ? Failure{EFailure::Value, 0, 0, "answered"} : reply.error() );
			} );
		}
		ASSERT_TRUE( WaitUntil([&]{ return rig.Got(Mid0060::Id).size()==1; }) );
		rig.Station.Drop();
		ASSERT_TRUE( WaitUntil([&]{ lg _{ answersMutex }; return answers.size()==2; }) );
		lg _{ answersMutex };
		EXPECT_EQ( answers[0].Kind, EFailure::Socket );
		EXPECT_EQ( answers[0].What, "The controller closed the connection." );
		EXPECT_EQ( answers[1].Kind, EFailure::NotConnected );
		EXPECT_EQ( answers[1].Mid, 14 );
		EXPECT_TRUE( rig.Got(Mid0014::Id).empty() );
	}

	//The wait between attempts doubles from reconnectMin to reconnectMax, and starts over once MID 0002 answers.
	TEST( SessionTests, BackoffDoubles ){
		Logging::Capture down{ LogTag, ELogLevel::Debug };
		let attempts = [&]{ return down.Find( []( const Logging::Entry& e ){ return e.Message().contains(" down, the next attempt in "); } ); };
		auto settings = Quick();
		settings.ReplyTimeout = 100ms;
		settings.ReconnectMin = 50ms;
		settings.ReconnectMax = 200ms;
		Rig rig{ Scripted(), move(settings) };
		rig.Station.Silence();
		rig.Client.Start();
		ASSERT_TRUE( WaitUntil([&]{ return rig.Told.Losses().size()>=4; }) );
		let losses = rig.Told.Losses();
		for( uint i=0; i<4; ++i ){
			EXPECT_EQ( losses[i].Kind, EFailure::Timeout ) << i;
			EXPECT_EQ( losses[i].Mid, 1 ) << i;
		}
		let waits = attempts();
		ASSERT_GE( waits.size(), 4u );
		let waited = [&]( uint i, sv wait ){ return waits[i].Message().contains( Ƒ(" down, the next attempt in {} ms:  MID 0001 had no answer within 100 ms.", wait) ); };
		EXPECT_TRUE( waited(0, "50") ) << waits[0].Message();
		EXPECT_TRUE( waited(1, "100") ) << waits[1].Message();
		EXPECT_TRUE( waited(2, "200") ) << waits[2].Message();
		EXPECT_TRUE( waited(3, "200") ) << waits[3].Message();
		//Each start is the timeout and the wait after the one before, less what a loaded box delays the first's arrival.
		let starts = rig.Got( Mid0001::Id );
		ASSERT_GE( starts.size(), 4u );
		EXPECT_GE( millis(starts[1].At-starts[0].At), 120 );
		EXPECT_GE( millis(starts[2].At-starts[1].At), 170 );
		EXPECT_GE( millis(starts[3].At-starts[2].At), 270 );

		rig.Station.Silence( false );
		ASSERT_TRUE( WaitUntil([&]{ return !rig.Told.Connections().empty(); }) );
		let before = attempts().size();
		rig.Station.Drop();
		ASSERT_TRUE( WaitUntil([&]{ return attempts().size()>before; }) );
		let next = attempts()[before].Message();
		EXPECT_TRUE( next.ends_with(" down, the next attempt in 50 ms:  The controller closed the connection.") ) << next;
	}

	TEST( SessionTests, NothingListening ){
		boost::asio::io_context context;
		boost::asio::ip::tcp::acceptor held{ context, {boost::asio::ip::address_v4::loopback(), 0} };
		let port = held.local_endpoint().port();
		held.close();
		Heard told;
		Session session{ boost::asio::make_strand(*Executor()), "127.0.0.1", port, Quick(), told.Events() };
		let early = Ask( session, Mid0060{} );
		ASSERT_FALSE( early );
		EXPECT_EQ( early.error().Kind, EFailure::NotConnected );
		session.Start();
		ASSERT_TRUE( WaitUntil([&]{ return told.Losses().size()>=2; }) ) << "each attempt that fails is told";
		EXPECT_EQ( told.Losses()[0].Kind, EFailure::Socket );
		EXPECT_TRUE( told.Losses()[0].What.contains("refused") ) << told.Losses()[0].What;
		EXPECT_TRUE( told.Connections().empty() );
		EXPECT_FALSE( Ask(session, Mid0060{}) );
		EXPECT_TRUE( Stop(session) );
		let count = told.Losses().size();
		std::this_thread::sleep_for( 300ms );
		EXPECT_EQ( told.Losses().size(), count ) << "no attempt after the stop";
	}

	TEST( SessionTests, HostNameResolved ){
		Controller station{ *Executor(), 0, Scripted() };
		Heard told;
		Session session{ boost::asio::make_strand(*Executor()), "localhost", station.Port(), Quick(), told.Events() };
		session.Start();
		EXPECT_TRUE( WaitUntil([&]{ return !told.Connections().empty(); }) );
		EXPECT_TRUE( Stop(session) );
	}

	//A frame the codec refuses is logged and dropped, with no disconnect of its own:  a data field that is not its table's,
	//bytes whose length does not read, and a MID with no type.
	TEST( SessionTests, MalformedFrameRefused ){
		Rig rig{ Unnumbered() };
		ASSERT_TRUE( rig.Start() );
		ASSERT_TRUE( rig.Ask(Mid0060{}) );
		Logging::Capture warnings{ LogTag, ELogLevel::Warning };
		rig.Station.SendRaw( Frame({.Mid=61}, "01000x") );
		rig.Station.SendRaw( "00x40061001         0100\0"s );
		rig.Station.SendRaw( Frame({.Mid=900}, {}) );
		rig.Station.Push( CaseOf<Mid0061>() );
		ASSERT_TRUE( WaitUntil([&]{ return rig.Told.Pushes().size()==1; }) ) << "the frame after them is read";
		EXPECT_TRUE( rig.Told.Losses().empty() );
		EXPECT_EQ( rig.Told.Connections().size(), 1u );
		let name = Ƒ( "(127.0.0.1:{})", rig.Station.Port() );
		let lines = warnings.Find( [&]( const Logging::Entry& e ){ return e.Message().starts_with( name ); } );
		ASSERT_EQ( lines.size(), 3u );
		EXPECT_TRUE( lines[0].Message().starts_with(name+" refused a frame:  MID 0061 revision 1, parameter 01 at byte 21:") ) << lines[0].Message();
		EXPECT_EQ( lines[1].Message(), name+" '00x40061001         ' begins no frame:  dropped through the next NUL." );
		EXPECT_EQ( lines[2].Message(), name+" refused a frame:  MID 0900 is not one the codec reads." );
		ASSERT_TRUE( WaitUntil([&]{ return rig.Got(Mid0062::Id).size()==2; }) ) << "a result's header is acknowledged, whatever its data field";
	}
	//At link level a frame is acknowledged by its header, before its data field is read.
	TEST( SessionTests, MalformedFrameAcknowledgedAtLinkLevel ){
		Rig rig;
		ASSERT_TRUE( rig.Start() );
		ASSERT_TRUE( rig.Ask(Mid0060{}) );
		rig.Station.SendRaw( Frame({.Mid=61, .Sequence=2}, "01000x") );
		rig.Station.SkipSequence();//the station's own count, past the frame it did not send.
		rig.Station.Push( CaseOf<Mid0061>() );
		ASSERT_TRUE( WaitUntil([&]{ return rig.Told.Pushes().size()==1 && rig.Got(Mid9997::Id).size()==3; }) );
		let acks = rig.Got( Mid9997::Id );
		EXPECT_EQ( Sequences(acks), (vector<uint16_t>{2, 3, 4}) );
		EXPECT_EQ( As<Mid9997>(acks[1]).Mid, 61 );
		EXPECT_TRUE( rig.Told.Losses().empty() );
	}

	//§3.2.1:  one request out at a time, each behind the answer before it.
	TEST( SessionTests, RequestsGoOneAtATime ){
		Rig rig{ Unnumbered() };
		ASSERT_TRUE( rig.Start() );
		rig.Station.Delay( 300ms, 2 );
		atomic<uint> answered{};
		for( let& request : {Body{Mid0014{}}, Body{Mid0034{}}, Body{Mid0051{}}} )
			rig.Client.Request( request, [&]( Session::Reply&& reply ){ answered += reply ? 1 : 100; } );
		ASSERT_TRUE( WaitUntil([&]{ return answered>=3; }) );
		EXPECT_EQ( answered, 3u );
		let frames = rig.Station.Frames();
		let first = Of( frames, Mid0014::Id ), second = Of( frames, Mid0034::Id ), third = Of( frames, Mid0051::Id );
		ASSERT_TRUE( first.size()==1 && second.size()==1 && third.size()==1 );
		EXPECT_GE( millis(second[0].At-first[0].At), 295 ) << "behind the first's answer, which the station held";
		EXPECT_GE( millis(third[0].At-second[0].At), 295 ) << "behind the second's, held too";
	}

	TEST( SessionTests, RequestTheCodecRefuses ){
		Rig rig;
		ASSERT_TRUE( rig.Start() );
		let reply = rig.Ask( Mid0018{.Pset=1000} );
		ASSERT_FALSE( reply );
		EXPECT_EQ( reply.error().Kind, EFailure::Value );
		EXPECT_EQ( reply.error().Mid, 18 );
		let selected = rig.Ask( Mid0018{.Pset=999} );
		EXPECT_TRUE( selected ) << Why( selected );
		EXPECT_EQ( rig.Got(Mid0018::Id).size(), 1u ) << "what is refused is not sent";
		EXPECT_EQ( Sequences(rig.Got(Mid0018::Id)), (vector<uint16_t>{1}) ) << "and takes no sequence number";
	}

	//MID 0003 on a clean close, at either level, behind the request out, which keeps its answer (§3.2.1), then the
	//socket's close;  what was asked and not sent is told so.
	TEST( SessionTests, StopSendsCommunicationStop ){
		for( let numbered : {false, true} ){
			Rig rig{ numbered ? Scripted() : Unnumbered() };
			ASSERT_TRUE( rig.Start() );
			rig.Station.Delay( 500ms );
			optional<Session::Reply> out, queued;
			atomic<uint> told{};
			rig.Client.Request( Mid0060{}, [&]( Session::Reply&& reply ){ out = move( reply ); ++told; } );
			rig.Client.Request( Mid0014{}, [&]( Session::Reply&& reply ){ queued = move( reply ); ++told; } );
			ASSERT_TRUE( WaitUntil([&]{ return rig.Got(Mid0060::Id).size()==1; }) );
			EXPECT_TRUE( rig.Stop() );
			EXPECT_EQ( told, 2u );
			ASSERT_TRUE( out && queued ) << numbered;
			EXPECT_TRUE( *out ) << Why( *out );
			ASSERT_FALSE( *queued );
			EXPECT_EQ( queued->error().Kind, EFailure::Stopped );
			EXPECT_TRUE( rig.Got(Mid0014::Id).empty() ) << "what was queued is not sent";
			let stops = rig.Got( Mid0003::Id );
			ASSERT_EQ( stops.size(), 1u ) << numbered;
			EXPECT_GE( millis(stops[0].At-rig.Got(Mid0060::Id)[0].At), 495 ) << "behind the answer the station held";
			EXPECT_TRUE( WaitUntil([&]{ return rig.Station.Clients()==0; }) ) << "the socket closed";
			EXPECT_TRUE( rig.Told.Losses().empty() ) << "a stop is no loss";
			let after = rig.Ask( Mid0060{} );
			ASSERT_FALSE( after );
			EXPECT_EQ( after.error().Kind, EFailure::Stopped );
		}
	}
	//A request out with no answer within replyTimeout of the stop is told Stopped, and the socket closes with no MID 0003.
	TEST( SessionTests, StopWaitsTheReplyTimeoutAtMost ){
		auto settings = Quick();
		settings.ReplyTimeout = 1s;
		Rig rig{ Unnumbered(), move(settings) };
		ASSERT_TRUE( rig.Start() );
		rig.Station.Delay( 4s );
		optional<Session::Reply> out;
		rig.Client.Request( Mid0060{}, [&]( Session::Reply&& reply ){ out = move( reply ); } );
		ASSERT_TRUE( WaitUntil([&]{ return rig.Got(Mid0060::Id).size()==1; }) );
		let stopping = steady_clock::now();
		EXPECT_TRUE( rig.Stop() );
		EXPECT_LT( millis(steady_clock::now()-stopping), 2500 ) << "not the held reply's 4 s";
		ASSERT_TRUE( out );
		ASSERT_FALSE( *out );
		EXPECT_EQ( out->error().Kind, EFailure::Stopped );
		EXPECT_TRUE( rig.Got(Mid0003::Id).empty() );
		EXPECT_TRUE( WaitUntil([&]{ return rig.Station.Clients()==0; }) ) << "the socket closed";
		EXPECT_TRUE( rig.Told.Losses().empty() );
	}

	//A session let go without a stop closes its socket, with no MID 0003 and no word to its caller.
	TEST( SessionTests, DestroyedWithoutAStop ){
		Controller station{ *Executor(), 0, Scripted() };
		Heard told;
		{
			Session session{ boost::asio::make_strand(*Executor()), "127.0.0.1", station.Port(), Quick(), told.Events() };
			session.Start();
			ASSERT_TRUE( WaitUntil([&]{ return !told.Connections().empty(); }) );
			EXPECT_EQ( station.Clients(), 1u );
		}
		EXPECT_TRUE( WaitUntil([&]{ return station.Clients()==0; }) );
		EXPECT_TRUE( Of(station.Frames(), Mid0003::Id).empty() );
		EXPECT_TRUE( told.Losses().empty() );
	}

	TEST( SessionTests, FramesLoggedAtTraceWhenAsked ){
		Logging::Capture frames{ LogTag, ELogLevel::Trace };
		let logged = [&]{ return frames.Find( []( const Logging::Entry& e ){ return e.Level==ELogLevel::Trace; } ); };
		{
			auto settings = Quick();
			settings.Log = true;
			Rig rig{ Unnumbered(), move(settings) };
			ASSERT_TRUE( rig.Start() );
			EXPECT_TRUE( rig.Stop() );
			let lines = logged();
			let name = Ƒ( "(127.0.0.1:{})", rig.Station.Port() );
			ASSERT_EQ( lines.size(), 4u ) << "MID 0001 and 0003 out, MID 0002 and 0005 in";
			EXPECT_EQ( lines[0].Message(), name+" > '00200001006001010000'" );
			EXPECT_TRUE( lines[1].Message().starts_with(name+" < '02210002006") ) << lines[1].Message();
			EXPECT_EQ( lines[2].Message(), name+" > '00200003001001010000'" );
			EXPECT_EQ( lines[3].Message(), name+" < '002400050010010100000003'" );
		}
		frames.Restart();
		Rig rig{ Unnumbered() };
		ASSERT_TRUE( rig.Start() );
		EXPECT_TRUE( rig.Stop() );
		EXPECT_TRUE( logged().empty() ) << "`log` is off";
	}
}