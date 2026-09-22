#include <jde/fwk/process/execution.h>
#include <jde/fwk/utils/Stopwatch.h>
#include "../src/GatewayAppClient.h"
#include "../src/async/ConnectAwait.h"
#include "../src/async/Subscriptions.h"
#include "../src/async/UAStrandAwait.h"
#include "../src/UAClient.h"
#include "../src/types/UAClientException.h"
#include "utils/GatewayClientSocket.h"
#include "../src/types/proto/opc.FromServer.h"
#include "../src/types/proto/opc.Common.h"
#include "utils/ITest.h"
#include "../src/auth/OpcServerSession.h"
#include <jde/opc/ServerTrust.h>

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	using Jde::Web::Client::ClientSocketAwait;
	struct SubscribeTests : ITest{
		struct Listener final : IListener{
			Listener( SubscribeTests* tests )ι:_tests{ tests }{}
			α OnData( string opcId, NodeId nodeId, const vector<FromServer::Value>& values )ι->void override{
				TRACE( "OnData: opcId: '{}', nodeId: {}, valueCount: {}. 0={}", opcId, nodeId.ToString(), values.size(), values.size() ? std::to_string(values[0].of_case()) : "n/a" );
				ASSERT( values.size()==1 );
				if( values.size()==1 ){
					try{//ι: AsNumber throws for a non-finite or out-of-range reading (review3 #13), and this override may not let one out.
						auto v = FromServer::ToValue( values[0] );
						_tests->_value = v.AsNumber<uint>();
						TRACE( "Value updated to {}.", _tests->_value.load() );
					}
					catch( const std::exception& e ){
						WARN( "OnData: could not convert value for {}: {}", nodeId.ToString(), e.what() );
					}
				}
			}
		private:
			SubscribeTests* _tests;
		};
		Ω SetUpTestCase()ε->void{
			ITest::SetUpTestCase();
			optional<ssl::context> ctx;
			_session = ms<GatewayClientSocket>( Executor(), ctx );
			BlockVoidAwait( _session->RunSession("localhost", GatewayPort()) );
			BlockAwait<ClientSocketAwait<uint32>,uint>( _session->Connect(AppClient()->SessionId()) );
		}
		α SetUp()ι->void{
			_listener = ms<Listener>( this );
		}
	protected:
		//The blocks nearly every test below is built from - the same five to seven lines, copied across seven tests before
		//(subscription-disconnect L6).  Each polls on its own Stopwatch and throws on a timeout (or on an ack that did not do
		//what it said), so a caller wraps it in ASSERT_NO_THROW and streams what the silence would have meant.  Defined under
		//`read`, which they use.
		α Subscribe( const NodeId& nodeId )ε->void;//the subscribe round trip alone - for a re-subscribe that must not wait for a push.
		α SubscribeAndPush( const NodeId& nodeId, Duration timeout=6s )ε->void;//subscribe and wait for the initial push: the subscription is live when this returns.
		α WriteAndPush( const NodeId& nodeId, Duration timeout=10s )ε->uint;//write the next value through the gateway, check what it echoes back, and wait for the data change it must trigger; returns the value written.
		α UnsubscribeAndDrain( const NodeId& nodeId, uint remaining=0, Duration timeout=10s )ε->void;//unsubscribe and wait until only `remaining` items are left on the client - the delete runs a DeleteMonitoring timer behind the ack.
		atomic<uint> _value;
		sp<Listener> _listener;
		static sp<GatewayClientSocket> _session;
	};
	sp<GatewayClientSocket> SubscribeTests::_session;

	Ω read( sp<UAClient> client, NodeId nodeId )ε->uint{
		auto v = BlockTAwait<flat_map<NodeId, Value>>( ReadValueAwait{{nodeId}, client} ).at( nodeId );
		THROW_IFX( v.status, UAClientException(v.status, client->Handle()) );
		auto j = v.ToJson();
		TRACET( ELogTags::Test, "Initial value: {}.", serialize(j) );
		if( j.is_object() && j.as_object().contains("value") )
			j = j.as_object().at("value");
		let expected = v.ToJson().to_number<uint>();
		return expected;
	}

	//The server's half of a session loss:  deleteSingle fires deleteSubscriptionCallback, which clears the cached response exactly
	//as a dropped session does.  UA calls are strand-only, and UAStrandAwait+BlockTAwait is what the atomic_flag/PostUA pairs here
	//were re-implementing - with the status thrown away, so a delete that did not happen left the rest of the test proving nothing
	//(subscription-disconnect L7).
	Ω deleteSubscription( const sp<UAClient>& client, SubscriptionId subscriptionId )ε->void{
		let sc = BlockTAwait<StatusCode>( UAStrandAwait<StatusCode>{client, [client, subscriptionId]{ return UA_Client_Subscriptions_deleteSingle( client->UAPointer(), subscriptionId ); }} );
		THROW_IFX( sc, UAClientException(sc, client->Handle()) );
	}

	α SubscribeTests::Subscribe( const NodeId& nodeId )ε->void{
		BlockAwait<ClientSocketAwait<FromServer::SubscriptionAck>,FromServer::SubscriptionAck>( _session->Subscribe(OpcServerSlug, {nodeId}, _listener) );
	}
	α SubscribeTests::SubscribeAndPush( const NodeId& nodeId, Duration timeout )ε->void{
		let expected = read( _client, nodeId );//the server's value now - the initial push is what brings _value to it.
		Subscribe( nodeId );
		Stopwatch sw;
		while( _value!=expected )
			sw.CheckTimeout( timeout, 1ms );
	}
	α SubscribeTests::WriteAndPush( const NodeId& nodeId, Duration timeout )ε->uint{
		string q = "updateVariable( opc: $opc, id: $id, value: $value ){ value }";
		let newValue = _value + 1;
		const jobject vars{ {"opc", OpcServerSlug}, {"id", nodeId.ToJson()}, {"value", newValue} };
		let json = BlockAwait<ClientSocketAwait<jvalue>,jvalue>( _session->Query(move(q), vars, true) );
		TRACE( "write result: {}", serialize(json) );
		THROW_IF( json.as_object().at("value").to_number<uint>()!=newValue, "the write echoed back {}, not the {} it was given", serialize(json), newValue );
		Stopwatch sw;
		while( _value!=newValue )
			sw.CheckTimeout( timeout, 1ms );
		return newValue;
	}
	α SubscribeTests::UnsubscribeAndDrain( const NodeId& nodeId, uint remaining, Duration timeout )ε->void{
		let ack = BlockAwait<ClientSocketAwait<FromServer::UnsubscribeAck>,FromServer::UnsubscribeAck>( _session->Unsubscribe(OpcServerSlug, {nodeId}) );
		THROW_IF( ack.successes_size()!=1, "the unsubscribe dropped {} node(s), not the 1 it was given", ack.successes_size() );
		Stopwatch sw;//the item goes a DeleteMonitoring timer (1s) after the ack, so poll rather than fixed-sleep.
		while( _client->MonitoredNodes().Count()!=remaining )
			sw.CheckTimeout( timeout, 1ms );
	}

	TEST_F( SubscribeTests, Basic ){
		const NodeId nodeId{ 4, 6017 };
		ASSERT_NO_THROW( SubscribeAndPush(nodeId) );
		ASSERT_NO_THROW( WriteAndPush(nodeId, 6s) );
		ASSERT_NO_THROW( UnsubscribeAndDrain(nodeId) );
		TRACE( "-------------------------------------------------------------" );
		//teardown costs the gateway's 1s subscription wait + a 500ms poll tick, so poll rather than fixed-sleep.
		Stopwatch sw;
		while( _client->Processing() )
			ASSERT_NO_THROW( sw.CheckTimeout(6s, 1ms) );
	}

	//Unsubscribe looked the client up by the credential cached at connect, so anything that emptied the cache between
	//Subscribe and Unsubscribe - a logout, or an anonymous session, which is never cached - stranded the subscription as
	//"Client not found" (soak-findings #5).  It now derives the credential the way Subscribe did (SessionCredential).
	TEST_F( SubscribeTests, UnsubscribeSurvivesCredentialCacheLoss ){
		const NodeId nodeId{ 4, 6017 };
		ASSERT_NO_THROW( SubscribeAndPush(nodeId) );//the initial push lands first - the listener writes into this fixture, so nothing may still be in flight when the test returns.
		let cached = GetCredential( AppClient()->SessionId(), OpcServerSlug );
		ASSERT_TRUE( cached );//the suite's shared state: later tests connect through this credential, so it goes back below.
		Logout( AppClient()->SessionId() );//drops the web session's cached credentials; the UA client itself stays connected.
		auto result = BlockAwait<ClientSocketAwait<FromServer::UnsubscribeAck>,FromServer::UnsubscribeAck>( _session->Unsubscribe(OpcServerSlug, {nodeId}) );
		EXPECT_EQ( result.successes_size(), 1 );
		AddSession( AppClient()->SessionId(), OpcServerSlug, *cached );//or the next connect derives the fallback credential and builds a second client beside _client.
		Stopwatch sw;//as Basic: the monitored item must be gone before the test returns.
		while( _client->Processing() )
			ASSERT_NO_THROW( sw.CheckTimeout(6s, 1ms) );
	}

	//A session the server drops takes its subscription and every monitored item with it, and open62541 rebuilds neither - it
	//re-creates the session only.  So writes recovered and pushes never did: 12 minutes of acked writes with no data change and
	//nothing logged after DeleteSubscriptionCallback (soak-findings #8, the 09-14 24-h run).  UAClient::Resubscribe puts the
	//items back, and StateCallback calls it on an activation whose session token is not the one last recorded - a *new* session,
	//not the same one re-activated on a fresh channel (RecordSession).
	//Neither half can be staged whole in-process, so each is pinned on its own: the trigger by the token checks below, and the
	//rebuild by driving Resubscribe() after a deleteSingle - the loss minus the session churn, since the delete is what makes the
	//old monitored item stop pushing.  Without the fix the wait for the written value below times out no matter how long it runs.
	TEST_F( SubscribeTests, ResubscribesAfterSubscriptionLoss ){
		const NodeId nodeId{ 4, 6017 };
		let before = _client->MonitoredNodes().Count();
		ASSERT_NO_THROW( SubscribeAndPush(nodeId) );//the initial push - the subscription is live from here.
		ASSERT_EQ( _client->MonitoredNodes().Count(), before+1 );

		//The trigger's half of the fix: StateCallback rebuilds only when the session's authentication token changed, so an
		//unreadable token would silently disable it, and a token that looked new on every activation would duplicate every
		//monitored item on a plain channel renew.  The loss itself cannot be staged in-process (it needs a server that drops
		//the session), so pin what can be: the token reads, the same session does not read as a new one, and recording it
		//again leaves the baseline intact.  `RecordSession()==false` alone says none of that - it is also what an unreadable
		//token returns, and that call would have replaced the baseline with the empty string the check below rejects.
		let token = _client->SessionToken();//what the activation recorded - RecordSession compares against this and then overwrites it.
		ASSERT_FALSE( token.empty() ) << "no session token: UAClient::RecordSession could never see a change";
		ASSERT_EQ( _client->ReadSessionToken(), token ) << "the live session's token is unreadable or does not match the recorded one - before RecordSession can act on it";
		ASSERT_FALSE( _client->RecordSession() ) << "the same session must not read as a new one - that would rebuild live monitored items";
		ASSERT_EQ( _client->SessionToken(), token ) << "RecordSession replaced the baseline with a token it could not read - the rebuild is off from here on";

		let subscriptionId = _client->SubscriptionId();
		ASSERT_TRUE( subscriptionId );
		ASSERT_NO_THROW( deleteSubscription(_client, subscriptionId) );//the loss, minus the session churn.
		ASSERT_FALSE( _client->CreatedSubscriptionResponse() ) << "the delete callback should have cleared the cached subscription";

		_client->Resubscribe();
		Stopwatch sw;//the rebuild is a subscribe + a create-monitored-items round trip on the strand.
		while( _client->MonitoredNodes().Count()!=before+1 )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 1ms) );

		//The point of the fix: a value written after the loss reaches the same listener again.
		ASSERT_NO_THROW( WriteAndPush(nodeId) ) << "no data change after the subscription was rebuilt";
		//And the rebuilt item is the one this session owns - an unsubscribe still finds it.
		ASSERT_NO_THROW( UnsubscribeAndDrain(nodeId, before) );
	}

	//soak-findings #11: the last monitored item going deletes the whole subscription, but a second later (DeleteMonitoring's
	//timer).  A subscribe arriving inside that window used to build its items on the id about to be deleted - the ack came
	//back successful, the items existed client-side, and nothing ever pushed.  Re-subscribing immediately after an
	//unsubscribe lands inside the window every time, which is what this does; the assertion is simply that pushes work.
	TEST_F( SubscribeTests, SubscribeSurvivesSubscriptionDelete ){
		const NodeId nodeId{ 4, 6017 };
		ASSERT_NO_THROW( SubscribeAndPush(nodeId) );

		//The window opens when the item is actually gone, not at the unsubscribe:  DeleteMonitoring's timer erases the item a
		//second later and submits the subscription delete in the same pass.  The drain to zero is that pass having run, so the
		//delete is in flight from here and a subscribe landing now is the race.  (Re-subscribing before this instead keeps the
		//item count non-zero, and the pass then deletes nothing - which is why that ordering proves nothing.)
		ASSERT_NO_THROW( UnsubscribeAndDrain(nodeId) );
		Subscribe( nodeId );//into the window, and no wait for a push: the write below is the one that has to arrive.

		//Past the 1s delete timer, so a subscription deleted out from under these items shows up here as silence.
		ASSERT_NO_THROW( WriteAndPush(nodeId) ) << "no data change - the re-subscribe's items went onto a deleted subscription";
		ASSERT_NO_THROW( UnsubscribeAndDrain(nodeId) );
	}

	//soak-findings #10, the other half of a lost connection: a failing `run_iterate` does not just lose the session, it
	//deregisters the whole client ("the next request will reconnect") and `~UAClient` takes `UAMonitoringNodes` - every
	//session's monitored nodes - with it.  A later write then builds a *fresh* client, so writes recovered and pushes never
	//did, with nothing logged.  `ConnectionLost` is that path's last step, so calling it under a live subscription is the loss
	//itself, minus the dead socket; what must happen is that the nodes are parked, a reconnect brings a new client up, and
	//the items are re-created on it without the subscriber lifting a finger.
	TEST_F( SubscribeTests, ResubscribesAfterClientLoss ){
		const NodeId nodeId{ 4, 6017 };
		ASSERT_NO_THROW( SubscribeAndPush(nodeId) );//the initial push - the subscription is live from here.
		let lost = _client->Handle();
		let monitoredLost = _client->MonitoredNodes().Count();//what the loss has to park:  it is on no client until the rebuild puts it back.
		let [clientsBefore, monitoredBefore, parkedBefore] = UAClient::StatusCounts();

		UAClient::ConnectionLost( sp<UAClient>{_client} );

		{//subscription-disconnect L1:  status walked only _clients, so an outage read as zero monitored items with nothing to say the
			//subscriptions were still owed.  ConnectionLost parks them before it returns, so this needs no wait.
			let [clientsParked, monitoredParked, parked] = UAClient::StatusCounts();
			EXPECT_EQ( clientsParked, clientsBefore-1 ) << "the lost client is still in the counts";
			EXPECT_EQ( monitoredParked, monitoredBefore-monitoredLost ) << "the lost client's items are still counted as monitored";
			EXPECT_EQ( parked, parkedBefore+monitoredLost ) << "the nodes the loss parked are in neither count";
		}

		sp<UAClient> revived;//the reconnect waits a second before its first attempt, then connects, subscribes and re-creates the items.
		Stopwatch sw;
		while( !revived ){
			for( let& client : UAClient::LiveClients() ){//Try, as expectNothingRevived does:  this walks every live client, and MonitoredNodes() would build each of them a UAMonitoringNodes it never asked for.
				let nodes = client->Handle()!=lost ? client->TryMonitoredNodes() : nullptr;
				if( client->Slug()==OpcServerSlug && nodes && nodes->Count() )
					revived = client;
			}
			ASSERT_NO_THROW( sw.CheckTimeout(30s, 10ms) ) << "no replacement client came up with the monitored node restored";
		}
		_client = revived;//the suite shares it: later tests must not reach for the deregistered one.

		sw.Reset();//and the parked count gives them back:  the rebuild erases its entry once the create has answered, just after the item lands on the client.
		while( std::get<2>(UAClient::StatusCounts())!=parkedBefore )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 10ms) ) << "the restored nodes are still counted as parked";

		//The point again: a value written after the client was replaced reaches the same listener, which never re-subscribed.
		ASSERT_NO_THROW( WriteAndPush(nodeId) ) << "no data change after the client was replaced";
		ASSERT_NO_THROW( UnsubscribeAndDrain(nodeId) );
	}

	//subscription-disconnect #2: the rebuild is itself a round trip, and a client that dies during it - a server still coming
	//up drops the channel a second time - took the unfinished part with it.  By then the nodes had left _pending, the dying
	//client's stashPending finds only creates that completed, and the rebuild's catch just logged:  no entry, no chain, and
	//the listener never heard from the node again.  Resubscribe() takes the nodes out and posts the rebuild; removing the
	//client right behind it lands the loss inside the rebuild's first round trip (the subscribe), so nothing has completed
	//and only the rebuild itself can hand the nodes back.
	TEST_F( SubscribeTests, ResubscribesAfterClientLossDuringRebuild ){
		const NodeId nodeId{ 4, 6017 };
		ASSERT_NO_THROW( SubscribeAndPush(nodeId) );//the initial push - the subscription is live from here.
		let lost = _client->Handle();

		_client->Resubscribe();
		UAClient::ConnectionLost( sp<UAClient>{_client} );

		sp<UAClient> revived;//parked by the failed rebuild, so the reconnect's second-long wait, then a connect, subscribe and create.
		Stopwatch sw;
		while( !revived ){
			for( let& client : UAClient::LiveClients() ){//Try, as above:  MonitoredNodes() would build one for every live client this walk touches.
				let nodes = client->Handle()!=lost ? client->TryMonitoredNodes() : nullptr;
				if( client->Slug()==OpcServerSlug && nodes && nodes->Count() )
					revived = client;
			}
			ASSERT_NO_THROW( sw.CheckTimeout(30s, 10ms) ) << "no replacement client came up with the monitored node restored";
		}
		_client = revived;//the suite shares it: later tests must not reach for the deregistered one.

		ASSERT_NO_THROW( WriteAndPush(nodeId) ) << "no data change after the rebuild's client was lost";
		ASSERT_NO_THROW( UnsubscribeAndDrain(nodeId) );
	}

	//reviews/m2-closing.md #2: the test above tells the gateway its client died; in an outage nothing does - the rebuild's own
	//refused submission is the first it hears of it.  The rebuild decided park-or-drop on `Connected`, which only ConnectionLost
	//clears, and the create's submission never reached it (UAε, where every other submission has UACε), so a create refused by a
	//channel that had just gone was read as the server's refusal:  nothing parked, no chain, and by the time the processing loop
	//deregistered the client the rebuild had already let the listener go.  One strand turn, so no run_iterate gets to tell the
	//gateway first:  the listener goes into a rebuild posted behind this turn, the subscription is put back so that rebuild goes
	//straight to its create, and the channel is closed under it - `Connected` still true when the create is refused.
	TEST_F( SubscribeTests, ResubscribesWhenTheRebuildsCreateIsRefused ){
		const NodeId nodeId{ 4, 6017 };
		ASSERT_NO_THROW( SubscribeAndPush(nodeId) );//the initial push - the subscription is live from here.
		let lost = _client->Handle();

		StatusCode sc{};
		ASSERT_NO_THROW( sc = BlockTAwait<StatusCode>(UAStrandAwait<StatusCode>{ _client, [client=_client]{
			auto subscription = client->CreatedSubscriptionResponse();
			client->Resubscribe();//clears the cached subscription, and posts the rebuild...
			client->SetCreatedSubscriptionResponse( move(subscription) );//...which now finds one, and skips the subscribe's round trip.
			return UA_Client_disconnectSecureChannelAsync( client->UAPointer() );
		}}) );
		ASSERT_EQ( sc, UA_STATUSCODE_GOOD );

		sp<UAClient> revived;//parked by the refused rebuild, so the reconnect's second-long wait, then a connect, subscribe and create.
		Stopwatch sw;
		while( !revived ){
			for( let& client : UAClient::LiveClients() ){//Try, as above:  MonitoredNodes() would build one for every live client this walk touches.
				let nodes = client->Handle()!=lost ? client->TryMonitoredNodes() : nullptr;
				if( client->Slug()==OpcServerSlug && nodes && nodes->Count() )
					revived = client;
			}
			ASSERT_NO_THROW( sw.CheckTimeout(30s, 10ms) ) << "no replacement client came up with the monitored node restored - the refused rebuild dropped its listener";
		}
		_client = revived;//the suite shares it: later tests must not reach for the deregistered one.

		ASSERT_NO_THROW( WriteAndPush(nodeId) ) << "no data change after the rebuild's create was refused";
		ASSERT_NO_THROW( UnsubscribeAndDrain(nodeId) );
	}

	//subscription-disconnect #3: a reconnect chain waited out its backoff on a timer nothing could cancel.  A pending timer is live
	//asio work, so stopping the gateway during an outage waited out the rest of the delay (up to 15s), and a chain that woke
	//mid-shutdown built a client nothing stopped.  UAClient::Shutdown now starts with StopReconnects, which cancels the wait.  The
	//chain's first wait is a second long, so one still running half a second after StopReconnects was not cancelled.
	TEST_F( SubscribeTests, StopReconnectsEndsTheWait ){
		const NodeId nodeId{ 4, 6017 };
		ASSERT_NO_THROW( SubscribeAndPush(nodeId) );//the initial push - the subscription is live from here.

		let waiting = UAClient::ReconnectsWaiting();
		UAClient::ConnectionLost( sp<UAClient>{_client} );//parks the node and starts the chain, whose wait is registered before this returns.
		ASSERT_EQ( UAClient::ReconnectsWaiting(), waiting+1 );
		//The listener leaves first - from the parked entry and from the test socket - so nothing is left pointing at this fixture.
		auto dropped = BlockAwait<ClientSocketAwait<FromServer::UnsubscribeAck>,FromServer::UnsubscribeAck>( _session->Unsubscribe(OpcServerSlug, {nodeId}) );
		ASSERT_EQ( dropped.successes_size(), 1 );//the parked entry is the only place it is left - nothing to drain off a client here.

		Stopwatch sw;
		ASSERT_EQ( UAClient::StopReconnects(), 1u );
		while( UAClient::ReconnectsWaiting()!=waiting )
			ASSERT_NO_THROW( sw.CheckTimeout(500ms, 1ms) ) << "the reconnect's wait was not cancelled - it ran on toward its tick";

		Credential cred{ _jwt->Payload() }; cred.SetUserPK( _jwt->UserPK );//the suite shares _client, and nothing reconnects it now.
		_client = BlockTAwait<sp<UAClient>>( ConnectAwait{string{OpcServerSlug}, move(cred)} );
	}

	//subscription-disconnect #4: a rebuild held its listeners in its own frame, where neither unsubscribe could reach them, so a
	//session that closed - or dropped a node - while the rebuild was putting nodes back had them re-created for a listener nothing
	//would ever unsubscribe, pinning the monitored item for the client's life.  Resubscribe() hands the listener to a rebuild whose
	//first step is a subscribe round trip, so an unsubscribe right behind it lands before the rebuild reaches the listener, every
	//time.  The listener is the gateway side's own IDataChange, subscribed on the client directly, so the test can unsubscribe it.
	struct PushCount final : IDataChange{
		α SendDataChange( const ServerCnnctnNK&, const NodeId&, const Value& )ι->void override{ ++Pushes; }
		α to_string()Ι->string override{ return "SubscribeTests.PushCount"; }
		atomic<uint> Pushes;
	};
	Ω unsubscribeDuringRebuild( const sp<UAClient>& client, function<void(const sp<IDataChange>&)> unsubscribe )->void{
		const NodeId nodeId{ 4, 6017 };
		let before = client->MonitoredNodes().Count();
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( BlockVoidAwait(SubscribeAwait{client}) );
		ASSERT_NO_THROW( BlockTAwait<FromServer::SubscriptionAck>(DataChangeAwait{{nodeId}, listener, client}) );
		Stopwatch sw;
		while( !listener->Pushes )//the initial push - the item is live from here.
			ASSERT_NO_THROW( sw.CheckTimeout(6s, 1ms) );
		ASSERT_EQ( client->MonitoredNodes().Count(), before+1 );

		let subscriptionId = client->SubscriptionId();//the server's half of a session loss, as ResubscribesAfterSubscriptionLoss does, so the old item does not outlive the test there.
		ASSERT_TRUE( subscriptionId );
		ASSERT_NO_THROW( deleteSubscription(client, subscriptionId) );

		client->Resubscribe();
		unsubscribe( listener );
		let pushes = listener->Pushes.load();
		std::this_thread::sleep_for( 2s );//proving a negative:  without the fix the rebuild re-creates the item within milliseconds.
		EXPECT_EQ( client->MonitoredNodes().Count(), before ) << "the rebuild restored a node its listener had unsubscribed";
		EXPECT_EQ( listener->Pushes.load(), pushes ) << "the unsubscribed listener is still being sent data changes";
	}
	TEST_F( SubscribeTests, CloseDuringRebuildIsNotRestored ){
		unsubscribeDuringRebuild( _client, []( const sp<IDataChange>& listener ){ UAClient::Unsubscribe( listener ); } );//a websocket session's OnClose.
	}
	TEST_F( SubscribeTests, UnsubscribeDuringRebuildIsNotRestored ){
		unsubscribeDuringRebuild( _client, []( const sp<IDataChange>& listener ){
			//GatewaySocketSession::Unsubscribe's second step, reached because no live client holds the node for this listener.
			let dropped = UAClient::UnsubscribePending( OpcServerSlug, listener, {NodeId{4, 6017}} );
			EXPECT_EQ( dropped.size(), 1u ) << "a node the rebuild still held was reported as a failed unsubscribe";
		});
	}

	//subscription-disconnect #5: a client dying with a create in flight resumed that subscribe with an ack of zero results for its
	//nodes - stashPending's TakeForResubscribe had wiped the request GetResult answers from.  The web client reads an empty list
	//as success, the test client registers nothing and says so only in a log line, and the soak throws.  The request now survives
	//the take, so the ack has a result per node.  And the result has to be true:  Good exactly when the node was restored-able -
	//its create completed before the take, which parked it - and a failure otherwise.  GetResult used to ack Good for any node it
	//had no error for, a failed create included.  ConnectionLost is posted to the client's strand right behind the create's
	//submission, so it normally runs before the server's answer is read; either order has to satisfy that rule.
	TEST_F( SubscribeTests, InFlightCreateFailsPerNodeOnClientLoss ){
		const NodeId nodeId{ 4, 6017 };
		ASSERT_NO_THROW( BlockVoidAwait(SubscribeAwait{_client}) );//a subscription to create on - without one the create fails before it is submitted.
		struct Result{ std::atomic<bool> Done; FromServer::SubscriptionAck Ack; };
		auto result = ms<Result>();
		auto listener = ms<PushCount>();
		[]( sp<Result> r, sp<IDataChange> listener, sp<UAClient> client, NodeId node )->TAwait<FromServer::SubscriptionAck>::Task{
			r->Ack = co_await DataChangeAwait{ {node}, move(listener), move(client) };
			r->Done = true;
		}( result, listener, _client, nodeId );//eager:  its submission is queued on the strand before the next line runs.
		_client->PostUA( [client=_client]{ UAClient::ConnectionLost( sp<UAClient>{client} ); } );
		Stopwatch sw;
		while( !result->Done )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 1ms) ) << "the in-flight create never resumed";
		ASSERT_EQ( result->Ack.results_size(), 1 ) << "the subscribe's ack has no result for its node";
		let status = result->Ack.results(0).status_code();
		let parked = UAClient::UnsubscribePending( OpcServerSlug, listener, {nodeId} );//also takes it back out, so nothing is restored for later tests.
		EXPECT_EQ( status==0, parked.size()==1 ) << "status " << std::hex << status << (status ? ": failed, yet parked for the reconnect" : ": acked Good, yet not parked - the node is not monitored anywhere");
		UAClient::Unsubscribe( listener );

		Credential cred{ _jwt->Payload() }; cred.SetUserPK( _jwt->UserPK );//the suite shares _client.
		_client = BlockTAwait<sp<UAClient>>( ConnectAwait{string{OpcServerSlug}, move(cred)} );
	}

	//subscription-disconnect #6, ordering A: a subscribe decides to reuse the cached subscription (SubscribeAwait finds one) and only
	//later, on the strand, registers its create.  A delete pass landing between the two - another listener's last item going, a
	//second after it was unsubscribed - deleted that subscription and cleared the cache, and the create failed every node with
	//BadInternalError.  The create now builds a subscription of its own and carries on.  Deleting the subscription right after
	//SubscribeAwait, the way the pass does, is that window pinned open.
	TEST_F( SubscribeTests, CreateSurvivesSubscriptionDeletedAfterReuse ){
		const NodeId nodeId{ 4, 6017 };
		let before = _client->MonitoredNodes().Count();
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( BlockVoidAwait(SubscribeAwait{_client}) );
		let subscriptionId = _client->SubscriptionId();
		ASSERT_TRUE( subscriptionId );
		ASSERT_NO_THROW( deleteSubscription(_client, subscriptionId) );//deleteSubscriptionCallback clears the cache, as DeleteMonitoring's pass does.
		ASSERT_FALSE( _client->SubscriptionId() );

		FromServer::SubscriptionAck ack;
		ASSERT_NO_THROW( ack = BlockTAwait<FromServer::SubscriptionAck>(DataChangeAwait{{nodeId}, listener, _client}) );
		ASSERT_EQ( ack.results_size(), 1 );
		EXPECT_EQ( ack.results(0).status_code(), 0u ) << "the create failed because the subscription it was going to use had been deleted";
		Stopwatch sw;
		while( !listener->Pushes )//the item is live, on the subscription the create made for itself.
			ASSERT_NO_THROW( sw.CheckTimeout(6s, 1ms) );
		EXPECT_NE( _client->SubscriptionId(), subscriptionId );

		_client->MonitoredNodes().Unsubscribe( listener );
		sw.Reset();
		while( _client->MonitoredNodes().Count()!=before )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 1ms) );
	}

	//reviews/m3-closing.md #10:  the ack answers in NodeId order - GetResult walks the request's flat_set - never in the order the
	//client asked, and a result carried no node to match on, so the web client blamed a failure on whichever row sat at its index:
	//a healthy row was marked Bad, locked and its pushes dropped, while the node that failed stayed ticked with nothing said.  Each
	//result names its node now.  An unknown node the server refuses per item, asked for ahead of a good one:  the set puts it second.
	TEST_F( SubscribeTests, EachResultNamesItsNode ){
		const NodeId good{ 4, 6017 }, unknown{ 4, 999'999 };
		let before = _client->MonitoredNodes().Count();
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( BlockVoidAwait(SubscribeAwait{_client}) );
		FromServer::SubscriptionAck ack;
		ASSERT_NO_THROW( ack = BlockTAwait<FromServer::SubscriptionAck>(DataChangeAwait{{unknown, good}, listener, _client}) );
		_client->MonitoredNodes().Unsubscribe( listener );
		ASSERT_EQ( ack.results_size(), 2 );
		flat_map<NodeId,uint32> byNode;
		for( let& result : ack.results() ){
			EXPECT_TRUE( result.has_node() ) << "a result that does not say which node it answers";
			byNode.emplace( ProtoUtils::ToNodeId(result.node()), result.status_code() );
		}
		EXPECT_EQ( byNode.size(), 2u );
		EXPECT_EQ( byNode[good], (uint32)UA_STATUSCODE_GOOD );
		EXPECT_EQ( byNode[unknown], (uint32)UA_STATUSCODE_BADNODEIDUNKNOWN );
		Stopwatch sw;
		while( _client->MonitoredNodes().Count()!=before )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 1ms) );
	}

	//subscription-disconnect #6, ordering B: a delete pass whose items were already gone still deleted "the" subscription and
	//cleared the cache - by then a newer subscription - orphaning it on the server.  Two passes for one item give exactly that:  the
	//first deletes the item and its subscription, a subscribe in between makes a new one, and the second finds nothing of its own
	//left to delete.  It has to leave the new subscription alone.
	//Timing, not a pinned window:  each pass waits exactly a second, and the new subscription's round trip can take over half a
	//second (the processing loop polls every 500ms while only SubscriptionRequestId is queued).  So pass 2 is armed 0.85s after
	//pass 1 - before pass 1 removes the item it needs, and late enough that the new subscription is in place first.  A run that
	//misses either condition skips rather than passing on a scenario it did not stage.
	TEST_F( SubscribeTests, EmptyDeletePassKeepsNewSubscription ){
		const NodeId nodeId{ 4, 6017 };
		auto listener = ms<PushCount>();
		auto& nodes = _client->MonitoredNodes();
		ASSERT_NO_THROW( BlockVoidAwait(SubscribeAwait{_client}) );
		ASSERT_NO_THROW( BlockTAwait<FromServer::SubscriptionAck>(DataChangeAwait{{nodeId}, listener, _client}) );
		Stopwatch sw;
		while( !listener->Pushes )
			ASSERT_NO_THROW( sw.CheckTimeout(6s, 1ms) );
		let first = _client->SubscriptionId();

		(void)nodes.Unsubscribe( {nodeId}, listener );//pass 1, due in a second.
		std::this_thread::sleep_for( 850ms );
		if( _client->SubscriptionId()!=first )
			GTEST_SKIP() << "pass 1 fired before pass 2 could be armed";
		ASSERT_NO_THROW( BlockTAwait<FromServer::SubscriptionAck>(DataChangeAwait{{nodeId}, listener, _client}) );//back onto the same item - no create.
		let dropped = get<0>( nodes.Unsubscribe({nodeId}, listener) );//pass 2.
		let armed = steady_clock::now();
		if( dropped.size()!=1 )
			GTEST_SKIP() << "the item was gone before pass 2 could be armed";
		sw.Reset();
		while( _client->SubscriptionId() )//pass 1:  the item goes, and with it the subscription.
			ASSERT_NO_THROW( sw.CheckTimeout(2s, 1ms) );
		ASSERT_NO_THROW( BlockVoidAwait(SubscribeAwait{_client}) );//a new one, before pass 2.
		if( steady_clock::now()-armed>900ms )
			GTEST_SKIP() << "the new subscription was not in place well before pass 2";
		let second = _client->SubscriptionId();
		ASSERT_TRUE( second );
		ASSERT_NE( second, first );
		std::this_thread::sleep_for( 1s );//past pass 2.
		EXPECT_EQ( _client->SubscriptionId(), second ) << "a delete pass with nothing of its own left to delete dropped the newer subscription";
	}

	//subscription-disconnect #10: RemoveClient means "forget this client" to all but the connection-loss paths - test teardown, the
	//cert and trust suites, an idle client's shutdown - yet it had become "park its subscriptions and reconnect".  A suite that left
	//an item monitored had it revived a second after its teardown, with pushes landing in a fixture gtest had already deleted.
	//Parking is ConnectionLost's alone now; these pin that RemoveClient parks nothing, directly and through a rebuild.
	Ω expectNothingRevived( const sp<PushCount>& listener, uint pushes )->void{
		std::this_thread::sleep_for( 4s );//a revival is the reconnect's second-long wait, a connect, a subscribe, a create and an initial push.
		EXPECT_EQ( listener->Pushes.load(), pushes ) << "the removed client's listener was sent data again";
		for( let& client : UAClient::LiveClients() ){
			let nodes = client->TryMonitoredNodes();
			EXPECT_FALSE( client->Slug()==OpcServerSlug && nodes && nodes->Count() ) << "a client came back monitoring what the removed client monitored";
		}
	}
	TEST_F( SubscribeTests, RemoveClientForgetsSubscriptions ){
		const NodeId nodeId{ 4, 6017 };
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( BlockVoidAwait(SubscribeAwait{_client}) );
		ASSERT_NO_THROW( BlockTAwait<FromServer::SubscriptionAck>(DataChangeAwait{{nodeId}, listener, _client}) );
		Stopwatch sw;
		while( !listener->Pushes )
			ASSERT_NO_THROW( sw.CheckTimeout(6s, 1ms) );
		let waiting = UAClient::ReconnectsWaiting();

		UAClient::RemoveClient( sp<UAClient>{_client} );
		EXPECT_EQ( UAClient::ReconnectsWaiting(), waiting ) << "removing the client started a reconnect";
		EXPECT_TRUE( UAClient::UnsubscribePending(OpcServerSlug, listener, {nodeId}).empty() ) << "removing the client parked what it monitored";
		expectNothingRevived( listener, listener->Pushes.load() );

		UAClient::Unsubscribe( listener );//nothing of it may linger for later tests, whatever the outcome above.
		Credential cred{ _jwt->Payload() }; cred.SetUserPK( _jwt->UserPK );//the suite shares _client.
		_client = BlockTAwait<sp<UAClient>>( ConnectAwait{string{OpcServerSlug}, move(cred)} );
	}
	TEST_F( SubscribeTests, RemoveClientDuringRebuildParksNothing ){
		const NodeId nodeId{ 4, 6017 };
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( BlockVoidAwait(SubscribeAwait{_client}) );
		ASSERT_NO_THROW( BlockTAwait<FromServer::SubscriptionAck>(DataChangeAwait{{nodeId}, listener, _client}) );
		Stopwatch sw;
		while( !listener->Pushes )
			ASSERT_NO_THROW( sw.CheckTimeout(6s, 1ms) );
		let pushes = listener->Pushes.load();

		_client->Resubscribe();//the listener goes into a rebuild whose first step is a subscribe round trip...
		UAClient::RemoveClient( sp<UAClient>{_client} );//...so the removal lands inside it, and the rebuild finds its client gone.
		expectNothingRevived( listener, pushes );

		UAClient::Unsubscribe( listener );
		Credential cred{ _jwt->Payload() }; cred.SetUserPK( _jwt->UserPK );
		_client = BlockTAwait<sp<UAClient>>( ConnectAwait{string{OpcServerSlug}, move(cred)} );
	}

	//reviews/m2-closing.md #2, the status half.  A refused submission reaches RemoveIfDisconnected (UACε), which asked one
	//question - BadServerNotConnected? - and open62541 answers that only for a channel already down when the call starts.  One
	//that goes under the call comes back as BadConnectionClosed or BadSecureChannelClosed, and the client stayed registered and
	//`Connected` with its items on it.  Each now parks them and deregisters the client;  a refusal that says nothing about the
	//connection - the server's, or a local one - still leaves it alone.  On the strand, where UACε calls it.
	TEST_F( SubscribeTests, ARefusalThatMeansTheConnectionWentParks ){
		const NodeId nodeId{ 4, 6017 };
		for( let refusal : {UA_STATUSCODE_BADCONNECTIONCLOSED, UA_STATUSCODE_BADSECURECHANNELCLOSED} ){
			auto listener = ms<PushCount>();
			ASSERT_NO_THROW( BlockVoidAwait(SubscribeAwait{_client}) );
			ASSERT_NO_THROW( BlockTAwait<FromServer::SubscriptionAck>(DataChangeAwait{{nodeId}, listener, _client}) );
			Stopwatch sw;
			while( !listener->Pushes )//the initial push - the item is live from here.
				ASSERT_NO_THROW( sw.CheckTimeout(6s, 1ms) );

			auto refuse = [client=_client]( StatusCode sc ){
				return BlockTAwait<bool>( UAStrandAwait<bool>{client, [client, sc]{ UAClient::RemoveIfDisconnected( sc, client ); return client->Connected.load(); }} );
			};
			EXPECT_TRUE( refuse(UA_STATUSCODE_BADTOOMANYSUBSCRIPTIONS) ) << "the server's refusal took the client down";
			EXPECT_TRUE( refuse(UA_STATUSCODE_BADOUTOFMEMORY) ) << "a local refusal took the client down";
			EXPECT_FALSE( refuse(refusal) ) << std::hex << refusal << ": the client is still taken for connected";
			let parked = UAClient::UnsubscribePending( OpcServerSlug, listener, {nodeId} );//also takes it back out, so nothing is restored for later tests.
			EXPECT_EQ( parked.size(), 1u ) << std::hex << refusal << ": what the client monitored was not parked for the reconnect";
			UAClient::Unsubscribe( listener );

			Credential cred{ _jwt->Payload() }; cred.SetUserPK( _jwt->UserPK );//the suite shares _client.
			_client = BlockTAwait<sp<UAClient>>( ConnectAwait{string{OpcServerSlug}, move(cred)} );
		}
	}

	//reviews/m2-closing.md #5: the by-node unsubscribe asks the live clients - LiveClients(), which is `Connected` - and then
	//what is parked.  ConnectionLost cleared Connected *before* it parked, so for that moment a session's nodes were in neither
	//place:  the unsubscribe reported them failed, the park that followed kept them, and the reconnect restored them for a
	//listener that would never ask again.  Connected now goes false under the park's lock, after the park, so whoever sees the
	//client gone finds its nodes parked.  The unsubscriber here is those two steps at their worst timing - it spins on the
	//flag and looks in _pending the instant it drops - and a bystander's lookups contend for the park's lock, which is what
	//holds the old window open long enough to land in:  without the fix this misses in some rounds, never in all of them, so
	//a pass proves little and a failure is the defect.  With it, a miss is impossible rather than unlikely.
	TEST_F( SubscribeTests, AClientStaysLiveUntilItsNodesAreParked ){
		const NodeId nodeId{ 4, 6017 };
		const flat_set<NodeId> nodes{ nodeId };
		Credential cred{ _jwt->Payload() }; cred.SetUserPK( _jwt->UserPK );
		constexpr uint rounds{ 25 };
		uint missed{};
		{
			auto bystander = ms<PushCount>();//monitors nothing:  its lookups only hold the lock.
			std::jthread contention{ [&]( std::stop_token stop ){ while( !stop.stop_requested() ) UAClient::UnsubscribePending( OpcServerSlug, bystander, nodes ); } };
			for( uint i=0; i<rounds; ++i ){
				auto listener = ms<PushCount>();
				ASSERT_NO_THROW( BlockVoidAwait(SubscribeAwait{_client}) );
				ASSERT_NO_THROW( BlockTAwait<FromServer::SubscriptionAck>(DataChangeAwait{{nodeId}, listener, _client}) );
				ASSERT_EQ( _client->MonitoredNodes().Count(), 1u ) << "round " << i << ": the node is not on the client, so there is nothing to park";

				std::atomic<bool> spinning{};
				uint parked{};
				std::jthread unsubscriber{ [&, client=_client]{
					spinning = true;
					while( client->Connected ){}//step one:  not among the live clients...
					parked = UAClient::UnsubscribePending( OpcServerSlug, listener, nodes ).size();//...so, step two, it is parked.
				}};
				while( !spinning )
					std::this_thread::yield();
				UAClient::ConnectionLost( sp<UAClient>{_client} );
				unsubscriber.join();
				missed += parked ? 0 : 1;
				UAClient::Unsubscribe( listener );//a miss was parked behind the lookup - nothing of it may come back for later tests.
				_client = BlockTAwait<sp<UAClient>>( ConnectAwait{string{OpcServerSlug}, Credential{cred}} );//the suite shares _client.
			}
		}
		EXPECT_EQ( missed, 0u ) << "of " << rounds << " rounds: the client was no longer live and its node was not yet parked - in neither place";

		Stopwatch sw;//the rounds' reconnect chain finds nothing to restore and ends on its next tick;  later tests count the chains waiting.
		while( UAClient::ReconnectsWaiting() )//to none, not back to where it started:  an earlier test's chain can still have been waiting then.
			ASSERT_NO_THROW( sw.CheckTimeout(20s, 10ms) ) << "a reconnect chain is still waiting with nothing to restore";
	}

	//subscription-disconnect #11: a create that fails - refused outright, or failed by the server as a whole - never reaches
	//OnCreateResponse, the only thing that erased its _calls entry, so the entry and the listener it holds (a websocket session, in
	//production) stayed until that session closed or the client went.  GetResult, which every create resumes through, now erases it.
	//A client removed and fully disconnected still has its cached subscription id, so a create on it registers and is then refused.
	TEST_F( SubscribeTests, FailedCreateReleasesListener ){
		const NodeId nodeId{ 4, 6017 };
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( BlockVoidAwait(SubscribeAwait{_client}) );
		auto removed = _client;
		UAClient::RemoveClient( sp<UAClient>{_client} );
		std::this_thread::sleep_for( 1s );//past the disconnect, so the create below is refused rather than answered.
		ASSERT_TRUE( removed->SubscriptionId() ) << "no cached subscription - the create would not register, and there would be nothing to leak";

		FromServer::SubscriptionAck ack;
		ASSERT_NO_THROW( ack = BlockTAwait<FromServer::SubscriptionAck>(DataChangeAwait{{nodeId}, listener, removed}) );
		ASSERT_EQ( ack.results_size(), 1 );
		EXPECT_NE( ack.results(0).status_code(), 0u ) << "a create on a removed client succeeded";
		EXPECT_EQ( listener.use_count(), 1 ) << "the failed create's bookkeeping still holds its listener";

		removed = nullptr;
		Credential cred{ _jwt->Payload() }; cred.SetUserPK( _jwt->UserPK );//the suite shares _client.
		_client = BlockTAwait<sp<UAClient>>( ConnectAwait{string{OpcServerSlug}, move(cred)} );
	}

	//reviews/m2-closing.md #3: SubscribeAwait queues its handle under the client before it submits the create, and only the
	//create's response emptied that entry.  A refused submit has no response, so the handle stayed queued and the entry's key - an
	//sp<UAClient> - pinned the client for the life of the process;  and since being first in the queue is the only gate on
	//submitting, every later subscribe on that client queued behind a create that was never sent and was never resumed.  A client
	//removed and fully disconnected refuses the submit, as in FailedCreateReleasesListener;  its cached subscription is cleared so the
	//subscribe asks at all.  The second subscribe runs detached:  without the fix it never comes back, and a blocking wait would hang.
	TEST_F( SubscribeTests, RefusedSubscribeLeavesNothingQueued ){
		auto removed = _client;
		UAClient::RemoveClient( sp<UAClient>{_client} );
		std::this_thread::sleep_for( 1s );//past the disconnect, so the subscribes below are refused rather than answered.
		removed->SetCreatedSubscriptionResponse( nullptr );

		struct Result{ std::atomic<bool> Done; string Error; };
		auto subscribe = []( sp<UAClient> client )->sp<Result>{
			auto result = ms<Result>();
			[]( sp<Result> r, sp<UAClient> client )->VoidAwait::Task{
				try{
					co_await SubscribeAwait{ move(client) };
				}
				catch( const std::exception& e ){
					r->Error = e.what();
				}
				r->Done = true;
			}( result, move(client) );
			return result;
		};
		auto finished = []( const Result& r ){
			let deadline = steady_clock::now()+5s;
			while( !r.Done && steady_clock::now()<deadline )
				std::this_thread::sleep_for( 1ms );
			return r.Done.load();
		};
		let first = subscribe( removed );
		ASSERT_TRUE( finished(*first) ) << "the refused subscribe never resumed";
		EXPECT_FALSE( first->Error.empty() ) << "a subscribe on a removed client succeeded";
		let second = subscribe( removed );
		EXPECT_TRUE( finished(*second) ) << "queued behind the first subscribe's refused create, which nothing will ever answer";
		if( second->Done )
			EXPECT_FALSE( second->Error.empty() ) << "a subscribe on a removed client succeeded";

		wp<UAClient> weak{ removed };
		removed = nullptr; _client = nullptr;//the fixture's is the other reference this test can see.
		let deadline = steady_clock::now()+10s;
		while( !weak.expired() && steady_clock::now()<deadline )
			std::this_thread::sleep_for( 10ms );
		EXPECT_TRUE( weak.expired() ) << "the refused subscribe's queue entry still holds the client - " << weak.use_count() << " reference(s)";

		Credential cred{ _jwt->Payload() }; cred.SetUserPK( _jwt->UserPK );//the suite shares _client.
		_client = BlockTAwait<sp<UAClient>>( ConnectAwait{string{OpcServerSlug}, move(cred)} );
	}

	//subscription-disconnect #12: a delete pass that finds a create in flight keeps the subscription for it.  If that create then
	//failed for every node, the subscription was left with no items, so nothing would ever schedule the pass that retires it, and
	//SubscriptionRequestId stayed queued:  the processing loop never stopped and the client never idled out.  Now the pass stops
	//processing once nothing is monitored.  The create is for a node the server does not have, and the strand is held across the
	//pass, so the create is still in flight when the pass decides.
	TEST_F( SubscribeTests, KeptEmptySubscriptionStopsProcessing ){
		const NodeId nodeId{ 4, 6017 };
		const NodeId missing{ 4, 999'999 };//not on the server:  its create fails for every node.
		if( _client->MonitoredNodes().Count() )
			GTEST_SKIP() << "an earlier test left items monitored - the subscription would not end up empty";
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( BlockVoidAwait(SubscribeAwait{_client}) );
		ASSERT_NO_THROW( BlockTAwait<FromServer::SubscriptionAck>(DataChangeAwait{{nodeId}, listener, _client}) );
		Stopwatch sw;
		while( !listener->Pushes )
			ASSERT_NO_THROW( sw.CheckTimeout(6s, 1ms) );
		let subscriptionId = _client->SubscriptionId();

		(void)_client->MonitoredNodes().Unsubscribe( {nodeId}, listener );//the pass, due in a second.
		std::this_thread::sleep_for( 800ms );
		struct Result{ std::atomic<bool> Done; FromServer::SubscriptionAck Ack; };
		auto result = ms<Result>();
		[]( sp<Result> r, sp<IDataChange> l, sp<UAClient> c, NodeId n )->TAwait<FromServer::SubscriptionAck>::Task{
			r->Ack = co_await DataChangeAwait{ {n}, move(l), move(c) };
			r->Done = true;
		}( result, listener, _client, missing );//eager:  its submission is queued on the strand ahead of the hold below.
		_client->PostUA( []{ std::this_thread::sleep_for( 700ms ); } );//holds the strand past the pass, so its answer cannot be read first.
		sw.Reset();
		while( !result->Done )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 1ms) );
		ASSERT_EQ( result->Ack.results_size(), 1 );
		ASSERT_NE( result->Ack.results(0).status_code(), 0u ) << "the create for a missing node succeeded";
		if( _client->SubscriptionId()!=subscriptionId )
			GTEST_SKIP() << "the pass deleted the subscription instead of keeping it - the create was not in flight when it decided";
		EXPECT_EQ( _client->MonitoredNodes().Count(), 0u );

		sw.Reset();//the item delete's own round trip runs first; then nothing may keep the loop awake.
		while( _client->Processing() )
			ASSERT_NO_THROW( sw.CheckTimeout(5s, 10ms) ) << "the processing loop never stops - the empty kept subscription still holds it";
	}

	//subscription-disconnect #13: the reconnect chain retried every failure forever, permanent rejections included - a rotated
	//password, an untrusted certificate - so a subscriber's stale credential cost the server a real login every 15s for as long as
	//its websocket lived.  Now MaxRejections in a row end the chain and drop what it was restoring.  The rejection here is the
	//gateway's own:  with no trusted certificate directories, every reconnect refuses the server's certificate.
	TEST_F( SubscribeTests, ReconnectGivesUpOnRejection ){
		const NodeId nodeId{ 4, 6017 };
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( BlockVoidAwait(SubscribeAwait{_client}) );
		ASSERT_NO_THROW( BlockTAwait<FromServer::SubscriptionAck>(DataChangeAwait{{nodeId}, listener, _client}) );
		Stopwatch sw;
		while( !listener->Pushes )
			ASSERT_NO_THROW( sw.CheckTimeout(6s, 1ms) );
		let waiting = UAClient::ReconnectsWaiting();

		struct RestoreTrust{ ~RestoreTrust(){ ServerTrust::OverrideTrustedCertDirs( nullopt ); } } restoreTrust;//whatever the assertions below do - the rest of the suite needs the server trusted.
		ServerTrust::OverrideTrustedCertDirs( vector<fs::path>{ fs::temp_directory_path()/"jde-reconnect-no-anchors" } );
		UAClient::ConnectionLost( sp<UAClient>{_client} );//parks the node; every reconnect from here is refused.

		//Given up means no chain waiting, for longer than any wait it would take:  the chain waits 1s, 2s, 4s between attempts, and
		//retrying forever it is waiting nearly all the time.
		sw.Reset();
		auto quietSince = steady_clock::now();
		while( steady_clock::now()-quietSince<3s ){
			if( UAClient::ReconnectsWaiting()!=waiting )
				quietSince = steady_clock::now();
			ASSERT_NO_THROW( sw.CheckTimeout(30s, 10ms) ) << "the reconnect chain is still retrying a certificate the gateway refuses every time";
		}
		EXPECT_TRUE( UAClient::UnsubscribePending(OpcServerSlug, listener, {nodeId}).empty() ) << "the chain stopped but left the node parked";
		let errors = UAClient::ConnectErrors();
		let error = errors.find( OpcServerSlug );
		EXPECT_TRUE( error!=errors.end() && error->second.find("server certificate")!=string::npos ) << "the attempts did not fail on the certificate - the chain ended for some other reason";

		UAClient::Unsubscribe( listener );
		ServerTrust::OverrideTrustedCertDirs( nullopt );
		Credential cred{ _jwt->Payload() }; cred.SetUserPK( _jwt->UserPK );//the suite shares _client.
		_client = BlockTAwait<sp<UAClient>>( ConnectAwait{string{OpcServerSlug}, move(cred)} );
	}

	//subscription-disconnect #15: MonitoredItemsRequest only finds nodes already monitored, so two creates for one node in flight at
	//once - a rebuild restoring it while its session subscribes again - made two monitored items:  every change pushed twice, and an
	//unsubscribe by node removed only one.  The second create to answer now joins the first's item and deletes its own.  Two creates
	//queued back to back on the strand are both submitted before either answer can be read.
	TEST_F( SubscribeTests, ConcurrentCreatesShareOneItem ){
		const NodeId nodeId{ 4, 6017 };
		if( _client->MonitoredNodes().Count() )
			GTEST_SKIP() << "an earlier test left items monitored";
		ASSERT_NO_THROW( BlockVoidAwait(SubscribeAwait{_client}) );
		auto listener = ms<PushCount>();
		struct Result{ std::atomic<bool> Done; FromServer::SubscriptionAck Ack; };
		auto create = []( sp<Result> r, sp<IDataChange> l, sp<UAClient> c, NodeId n )->TAwait<FromServer::SubscriptionAck>::Task{
			r->Ack = co_await DataChangeAwait{ {n}, move(l), move(c) };
			r->Done = true;
		};
		auto first = ms<Result>(), second = ms<Result>();
		create( first, listener, _client, nodeId );//eager:  each queues its submission on the strand before the next line runs.
		create( second, listener, _client, nodeId );
		Stopwatch sw;
		while( !first->Done || !second->Done )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 1ms) );
		for( let& r : {first, second} ){
			ASSERT_EQ( r->Ack.results_size(), 1 );
			EXPECT_EQ( r->Ack.results(0).status_code(), 0u );
		}
		EXPECT_EQ( _client->MonitoredNodes().Count(), 1u ) << "two monitored items for one node";

		(void)_client->MonitoredNodes().Unsubscribe( {nodeId}, listener );//one unsubscribe by node has to leave nothing.
		sw.Reset();
		while( _client->MonitoredNodes().Count() )
			ASSERT_NO_THROW( sw.CheckTimeout(5s, 10ms) ) << "an item for the node is still monitored after its only listener unsubscribed";
	}

	//reviews/m2-closing.md #6: the by-node unsubscribe looked for the node among the monitored items alone, and a node is not
	//there until its create answers - so one unsubscribed inside its own subscribe's round trip (a view opened and closed at
	//once) was found nowhere, reported a failure, and attached when the answer came:  pushed to a listener that would never
	//ask again, the item never retired.  The strand is held right behind the create's submission, so the unsubscribe lands
	//while it is out, every time;  `holding` says the submission has run.
	struct HeldCreate final{
		struct Result{ std::atomic<bool> Done; FromServer::SubscriptionAck Ack; };
		HeldCreate( sp<UAClient> client, NodeId node, sp<IDataChange> listener ):_result{ ms<Result>() }{
			[]( sp<Result> r, sp<IDataChange> l, sp<UAClient> c, NodeId n )->TAwait<FromServer::SubscriptionAck>::Task{
				r->Ack = co_await DataChangeAwait{ {n}, move(l), move(c) };
				r->Done = true;
			}( _result, move(listener), client, move(node) );//eager:  its submission is queued on the strand ahead of the hold.
			client->PostUA( [holding=_holding]{ *holding = true; std::this_thread::sleep_for( 700ms ); } );//the answer cannot be read until this returns.
			while( !*_holding )
				std::this_thread::yield();
		}
		α Ack()ε->const FromServer::SubscriptionAck&{
			Stopwatch sw;
			while( !_result->Done )
				sw.CheckTimeout( 10s, 1ms );
			return _result->Ack;
		}
	private:
		sp<Result> _result;
		sp<std::atomic<bool>> _holding{ ms<std::atomic<bool>>() };
	};
	TEST_F( SubscribeTests, UnsubscribeReachesACreateStillOut ){
		const NodeId nodeId{ 4, 6017 };
		if( _client->MonitoredNodes().Count() )
			GTEST_SKIP() << "an earlier test left items monitored";
		ASSERT_NO_THROW( BlockVoidAwait(SubscribeAwait{_client}) );
		auto listener = ms<PushCount>();
		HeldCreate create{ _client, nodeId, listener };
		if( _client->MonitoredNodes().Count() )
			GTEST_SKIP() << "the create answered before the strand was held - nothing was in flight to unsubscribe";

		auto [dropped, failed] = _client->MonitoredNodes().Unsubscribe( {nodeId}, listener );
		EXPECT_EQ( dropped.size(), 1u ) << "the node's create was out, and the unsubscribe found it nowhere";
		EXPECT_TRUE( failed.empty() );
		auto [again, repeated] = _client->MonitoredNodes().Unsubscribe( {nodeId}, listener );//nothing left to drop - as for an item.
		EXPECT_TRUE( again.empty() );

		FromServer::SubscriptionAck ack;
		ASSERT_NO_THROW( ack = create.Ack() );
		ASSERT_EQ( ack.results_size(), 1 );
		EXPECT_EQ( ack.results(0).status_code(), UA_STATUSCODE_BADREQUESTCANCELLEDBYCLIENT ) << "the subscribe's ack still says the node is subscribed";
		std::this_thread::sleep_for( 1500ms );//an item the answer attached would have pushed its initial value by now.
		EXPECT_EQ( listener->Pushes.load(), 0u ) << "the unsubscribed listener is being sent data changes";
		EXPECT_EQ( _client->MonitoredNodes().Count(), 0u ) << "the answer attached the node its listener had unsubscribed";
		EXPECT_EQ( listener.use_count(), 1 ) << "the client still holds the listener";
		(void)_client->MonitoredNodes().Unsubscribe( {nodeId}, listener );//nothing of it may linger for later tests, whatever the outcome above.
	}

	//And a rebuild's create, where both of the by-node unsubscribe's steps now find the node:  the create drops it, and the rebuild
	//has to forget it as well (GatewaySocketSession::Unsubscribe asks _pending/_rebuilds for every node, not just what is left) -
	//or it counts the node as refused, and parks it again should the client die.  The rebuild goes straight to its create, as in
	//ResubscribesWhenTheRebuildsCreateIsRefused, and the strand is held behind it.
	TEST_F( SubscribeTests, UnsubscribeReachesARebuildsCreateStillOut ){
		const NodeId nodeId{ 4, 6017 };
		if( _client->MonitoredNodes().Count() )
			GTEST_SKIP() << "an earlier test left items monitored";
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( BlockVoidAwait(SubscribeAwait{_client}) );
		ASSERT_NO_THROW( BlockTAwait<FromServer::SubscriptionAck>(DataChangeAwait{{nodeId}, listener, _client}) );
		Stopwatch sw;
		while( !listener->Pushes )//the initial push - the item is live from here.
			ASSERT_NO_THROW( sw.CheckTimeout(6s, 1ms) );
		let parkedBefore = std::get<2>( UAClient::StatusCounts() );

		auto holding = ms<std::atomic<bool>>();
		_client->PostUA( [client=_client, holding]{
			auto subscription = client->CreatedSubscriptionResponse();
			client->Resubscribe();//the listener goes into a rebuild, posted behind this turn...
			client->SetCreatedSubscriptionResponse( move(subscription) );//...which finds a subscription and goes straight to its create...
			client->PostStrand( [holding]{ *holding = true; std::this_thread::sleep_for( 700ms ); } );//...and this, behind it, keeps the answer unread.
		});
		while( !*holding )
			std::this_thread::yield();
		auto restoreClient = [&]{//the item the rebuild took off this client is still on the server's subscription, which this test kept - so the client goes.
			UAClient::Unsubscribe( listener );
			UAClient::RemoveClient( sp<UAClient>{_client} );
			Credential cred{ _jwt->Payload() }; cred.SetUserPK( _jwt->UserPK );//the suite shares _client.
			_client = BlockTAwait<sp<UAClient>>( ConnectAwait{string{OpcServerSlug}, move(cred)} );
		};
		if( _client->MonitoredNodes().Count() ){
			restoreClient();
			GTEST_SKIP() << "the rebuild's create answered before the strand was held - nothing was in flight to unsubscribe";
		}

		auto [dropped, failed] = _client->MonitoredNodes().Unsubscribe( {nodeId}, listener );//GatewaySocketSession::Unsubscribe's first step...
		let forgotten = UAClient::UnsubscribePending( OpcServerSlug, listener, {nodeId} );//...and its second.
		EXPECT_EQ( dropped.size(), 1u ) << "the rebuild's create was out, and the unsubscribe found the node on no client";
		EXPECT_EQ( forgotten.size(), 1u ) << "the rebuild no longer listed a node it had not restored yet";

		let pushes = listener->Pushes.load();
		std::this_thread::sleep_for( 2s );//the hold, the answer, and an initial push had the node come back.
		EXPECT_EQ( _client->MonitoredNodes().Count(), 0u ) << "the rebuild restored a node its listener had unsubscribed";
		EXPECT_EQ( listener->Pushes.load(), pushes ) << "the unsubscribed listener is still being sent data changes";
		EXPECT_EQ( std::get<2>(UAClient::StatusCounts()), parkedBefore ) << "the rebuild still counts the node as owed";

		restoreClient();
	}

	//A tab that closes or reloads sends no Unsubscribe frame.  The gateway's session *is* the Subscription's IDataChange,
	//so unless OnClose unsubscribes it the sp to the dead session, the UAClient it pins and the server-side monitored
	//items all survive every reload (review3 #4).
	TEST_F( SubscribeTests, CloseUnsubscribes ){
		const NodeId nodeId{ 4, 6017 };
		let before = _client->MonitoredNodes().Count();
		optional<ssl::context> ctx;
		auto session = ms<GatewayClientSocket>( Executor(), ctx );
		BlockVoidAwait( session->RunSession("localhost", GatewayPort()) );
		BlockAwait<ClientSocketAwait<uint32>,uint>( session->Connect(AppClient()->SessionId()) );
		BlockAwait<ClientSocketAwait<FromServer::SubscriptionAck>,FromServer::SubscriptionAck>( session->Subscribe(OpcServerSlug, {nodeId}, _listener) );
		ASSERT_EQ( _client->MonitoredNodes().Count(), before+1 );

		BlockVoidAwait( session->Close(false, SRCE_CUR) );
		Stopwatch sw;//the item goes away a DeleteMonitoring timer (1s) after the close, so poll rather than fixed-sleep.
		while( _client->MonitoredNodes().Count()!=before )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 1ms) );
	}

	//reviews/m2-closing.md #15 - the harness's own leak, and the soak is scored by this harness.  The test client registers a
	//subscribe's record before it writes the request, and only the ack ever took it out:  a request that failed - answered
	//with an error, or on a socket that had gone - left its record, listener and all, for the life of the process, and
	//subscription-disconnect #7's per-cycle re-subscribe makes that one a second for the length of an outage.  Both ways a
	//request fails, and the unsubscribe's record with the subscribe's.
	TEST_F( SubscribeTests, AFailedRequestLeavesNoRecord ){
		const NodeId nodeId{ 4, 6017 };
		let before = GatewayClientSocket::PendingSubscriptionRecords();
		//lambdas:  the awaits' template commas do not survive a gtest macro.
		auto subscribe = [&]( GatewayClientSocket& socket, ServerCnnctnNK slug ){ BlockAwait<ClientSocketAwait<FromServer::SubscriptionAck>,FromServer::SubscriptionAck>( socket.Subscribe(move(slug), {nodeId}, _listener) ); };
		auto unsubscribe = [&]( GatewayClientSocket& socket ){ BlockAwait<ClientSocketAwait<FromServer::UnsubscribeAck>,FromServer::UnsubscribeAck>( socket.Unsubscribe(OpcServerSlug, {nodeId}) ); };

		//answered:  the gateway has no such connection, and says so over a working socket.
		EXPECT_THROW( subscribe(*_session, "m2Closing15NoSuchConnection"), GatewayErrorResponse );
		EXPECT_EQ( GatewayClientSocket::PendingSubscriptionRecords(), before ) << "a subscribe the gateway answered with an error left its record";

		//a dead socket:  the write finds the stream gone and every pending task is failed - CloseTasks, which is also where
		//a timeout and a close mid-request end up.
		optional<ssl::context> ctx;
		auto session = ms<GatewayClientSocket>( Executor(), ctx );
		BlockVoidAwait( session->RunSession("localhost", GatewayPort()) );
		BlockAwait<ClientSocketAwait<uint32>,uint>( session->Connect(AppClient()->SessionId()) );
		BlockVoidAwait( session->Close(false, SRCE_CUR) );
		EXPECT_ANY_THROW( subscribe(*session, string{OpcServerSlug}) );
		EXPECT_EQ( GatewayClientSocket::PendingSubscriptionRecords(), before ) << "a subscribe on a dead socket left its record";
		EXPECT_ANY_THROW( unsubscribe(*session) );
		EXPECT_EQ( GatewayClientSocket::PendingSubscriptionRecords(), before ) << "an unsubscribe on a dead socket left its record";
	}

	//Sessions racing to connect to the same slug coalesce in ConnectAwait::_requests, and a Create() failure fans the
	//one exception out to all of them.  The fan-out "cloned" with e.Move(), which moves the payload *out of* e, so every
	//waiter after the first - the last, which takes e itself, included - got the format string with its arguments gone
	//("Could not find connection:  '{}'") instead of the real message (review3 #5).
	//NB this only bites when Create() actually suspends before it throws, i.e. when ServerCnnctnAwait's SelectAsync is
	//async.  Under the ctest sqlite config it completes inline, Create runs to completion inside the first Suspend(), and
	//the fan-out only ever sees one handle - so a green run here is not by itself evidence of coverage.  Verified against
	//the bug by forcing a suspension into Create (a 200ms Any(DurationTimer)), which makes the fan-out see all three.
	TEST( ConnectCoalesceTests, EveryWaiterGetsTheMessage ){
		constexpr uint count{ 3 };
		struct Results{ atomic<uint> Done{}; std::array<string,count> What; };
		auto results = ms<Results>();
		auto connect = []( sp<Results> r, uint i )ι->TAwait<sp<UAClient>>::Task{
			try{
				co_await ConnectAwait{ "claudeNoSuchOpcSlug"s, Credential{} };
				r->What[i] = "<no exception>";
			}
			catch( runtime_error& e ){
				r->What[i] = e.what();
			}
			++r->Done;
		};
		for( uint i=0; i<count; ++i )
			connect( results, i );
		Stopwatch sw;
		while( results->Done<count )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 1ms) );
		for( uint i=0; i<count; ++i )
			TRACET( ELogTags::Test, "waiter {}: '{}'", i, results->What[i] );//not TRACE: _tags here is ITest's, and this is not a fixture test.
		for( uint i=0; i<count; ++i ){
			ASSERT_FALSE( results->What[i].empty() ) << "waiter " << i << " got a message-less exception.";
			ASSERT_EQ( results->What[i], results->What[0] ) << "waiter " << i << " got a different exception than waiter 0.";
		}
	}
}