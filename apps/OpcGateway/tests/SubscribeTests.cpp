#include <open62541/client_subscriptions.h>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/fwk/process/execution.h>
#include <absl/cleanup/cleanup.h>
#include <jde/fwk/utils/Stopwatch.h>
#include <jde/tests/WaitUntil.h>
#include "../src/GatewayAppClient.h"
#include "../src/async/ConnectAwait.h"
#include "../src/async/Subscriptions.h"
#include "../src/async/MonitoredItems.h"
#include "../src/async/UAStrandAwait.h"
#include "../src/ql/GatewayQL.h"
#include "../src/ql/HistEventsQLAwait.h"
#include "../src/UAClient.h"
#include "../src/types/UAClientException.h"
#include "utils/GatewayClientSocket.h"
#include "utils/helpers.h"
#include "utils/Pumps.h"
#include "../src/types/proto/opc.FromClient.h"
#include "../src/types/proto/opc.FromServer.h"
#include <jde/opc/proto/opc.Common.h>
#include <jde/opc/uatypes/DateTime.h>
#include "utils/ITest.h"
#include "utils/Subscribing.h"
#include "../src/auth/OpcServerSession.h"
#include "../src/hist/HistGroupHost.h"
#include <jde/opc/ServerTrust.h>

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };
	using Jde::Web::Client::ClientSocketAwait;
	struct SubscribeTests : ITest{
		struct Listener final : IListener{
			Listener( SubscribeTests* tests )ι:_tests{ tests }{}
			α OnPush( const FromServer::NodeValues& push )ι->void override{
				_tests->_hasSource = push.has_source();
				_tests->_hasServer = push.has_server();
			}
			α OnData( string opcId, NodeId nodeId, const Proto::Value& value )ι->void override{
				TRACE( "OnData: opcId: '{}', nodeId: {}, member: {}.", opcId, nodeId.ToString(), (int)value.of_case() );
				let scalar = value.of_case()!=Proto::Value::OF_NOT_SET && !value.has_array();
				ASSERT( scalar );
				if( scalar ){
					try{//ι: AsNumber throws for a non-finite or out-of-range reading (review3 #13), and this override may not let one out.
						Opc::Value v{ ProtoUtils::ToVariant(value) };
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
		//A watch list's listener (#418):  the last value pushed, as a number, every one in order, the pushes counted, and the
		//MonitoredItemModified pushes with the last one's revised values and status.
		struct WatchListener final : IListener{
			α OnData( string, NodeId nodeId, const Proto::Value& value )ι->void override{
				try{
					let v = Opc::Value{ ProtoUtils::ToVariant(value) }.AsNumber<double>();
					Value = v;
					lg _{ Mutex };
					Values.push_back( v );
				}
				catch( const std::exception& e ){
					WARN( "OnData: could not convert value for {}: {}", nodeId.ToString(), e.what() );
				}
				++Pushes;
			}
			α OnModified( const FromServer::MonitoredItemModified& modified )ι->void override{
				lg _{ Mutex };
				RevisedSamplingInterval = modified.revised_sampling_interval();
				RevisedQueueSize = modified.revised_queue_size();
				Status = modified.status_code();
				++Modified;
			}
			atomic<double> Value{};
			atomic<uint> Pushes{}, Modified{};
			α Saw( double v )ι->bool{ lg _{ Mutex }; return std::ranges::find( Values, v )!=Values.end(); }
			mutex Mutex; double RevisedSamplingInterval{}; uint32 RevisedQueueSize{}; StatusCode Status{}; vector<double> Values;
		};
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
		//The watch tests' (#418):  a socket subscribe with the node's parameters, whose ack has to say Good;  a Double written
		//through the gateway, as WriteAndPush writes;  and the wait for a listener to hold a value.
		α Watch( const NodeId& nodeId, const MonitoringParameters& parameters, sp<WatchListener> listener )ε->FromServer::SubscriptionAck;
		α WriteDouble( const NodeId& nodeId, double value )ε->void;
		Ω WaitValue( const sp<WatchListener>& listener, double value, Duration timeout=5s )ε->void;
		atomic<uint> _value;
		atomic<bool> _hasSource, _hasServer;//the last push's timestamps
		sp<Listener> _listener;
	};

	//The server's half of a session loss:  deleteSingle fires deleteSubscriptionCallback, which clears the cached response exactly
	//as a dropped session does.  UA calls are strand-only, and UAStrandAwait+BlockAwait is what the atomic_flag/PostUA pairs here
	//were re-implementing - with the status thrown away, so a delete that did not happen left the rest of the test proving nothing
	//(subscription-disconnect L7).
	Ω deleteSubscription( const sp<UAClient>& client, SubscriptionId subscriptionId )ε->void{
		let sc = BlockAwait( UAStrandAwait<StatusCode>{client, [client, subscriptionId]{ return UA_Client_Subscriptions_deleteSingle( client->UAPointer(), subscriptionId ); }} );
		THROW_IFX( sc, UAClientException(sc, client->Handle()) );
	}

	α SubscribeTests::Subscribe( const NodeId& nodeId )ε->void{
		BlockAwait<ClientSocketAwait<FromServer::SubscriptionAck>,FromServer::SubscriptionAck>( Socket().Subscribe(OpcServerSlug, {nodeId}, _listener) );
	}
	α SubscribeTests::SubscribeAndPush( const NodeId& nodeId, Duration timeout )ε->void{
		auto now = ReadValue( _client, nodeId );//the server's value now - the initial push is what brings _value to it.
		THROW_IFX( now.status, UAClientException(now.status, _client->Handle()) );
		let expected = now.AsNumber<uint>();
		Subscribe( nodeId );
		Stopwatch sw;
		while( _value!=expected )
			sw.CheckTimeout( timeout, 1ms );
	}
	α SubscribeTests::WriteAndPush( const NodeId& nodeId, Duration timeout )ε->uint{
		string q = "updateVariable( opc: $opc, id: $id, value: $value ){ value }";
		let newValue = _value + 1;
		const jobject vars{ {"opc", OpcServerSlug}, {"id", nodeId.ToJson()}, {"value", newValue} };
		let json = Socket().QuerySync( move(q), vars );
		TRACE( "write result: {}", serialize(json) );
		THROW_IF( json.as_object().at("value").to_number<uint>()!=newValue, "the write echoed back {}, not the {} it was given", serialize(json), newValue );
		Stopwatch sw;
		while( _value!=newValue )
			sw.CheckTimeout( timeout, 1ms );
		return newValue;
	}
	α SubscribeTests::UnsubscribeAndDrain( const NodeId& nodeId, uint remaining, Duration timeout )ε->void{
		let ack = BlockAwait<ClientSocketAwait<FromServer::UnsubscribeAck>,FromServer::UnsubscribeAck>( Socket().Unsubscribe(OpcServerSlug, {nodeId}) );
		THROW_IF( ack.successes_size()!=1, "the unsubscribe dropped {} node(s), not the 1 it was given", ack.successes_size() );
		Stopwatch sw;//the item goes _itemDeleteDelay after the ack, so poll rather than fixed-sleep.
		while( _client->MonitoredNodes().Count()!=remaining )
			sw.CheckTimeout( timeout, 1ms );
	}
	α SubscribeTests::Watch( const NodeId& nodeId, const MonitoringParameters& parameters, sp<WatchListener> listener )ε->FromServer::SubscriptionAck{
		auto ack = BlockAwait<ClientSocketAwait<FromServer::SubscriptionAck>,FromServer::SubscriptionAck>( Socket().Subscribe(OpcServerSlug, {nodeId}, {parameters}, move(listener)) );
		THROW_IF( ack.results_size()!=1, "the ack answered {} node(s), not the 1 asked", ack.results_size() );
		THROW_IF( ack.results(0).status_code(), "'{}' refused the item:  {}", nodeId.ToString(), UAException::Message(ack.results(0).status_code()) );
		return ack;
	}
	α SubscribeTests::WriteDouble( const NodeId& nodeId, double value )ε->void{
		string q = "updateVariable( opc: $opc, id: $id, value: $value ){ value }";
		const jobject vars{ {"opc", OpcServerSlug}, {"id", nodeId.ToJson()}, {"value", value} };
		let json = Socket().QuerySync( move(q), vars );
		THROW_IF( json.as_object().at("value").to_number<double>()!=value, "the write echoed back {}, not the {} it was given", serialize(json), value );
	}
	α SubscribeTests::WaitValue( const sp<WatchListener>& listener, double value, Duration timeout )ε->void{
		Stopwatch sw;
		while( listener->Value!=value )
			sw.CheckTimeout( timeout, 1ms );
	}
	//Clients 8b (#422):  a structure variable's push carries its value as fields, `structure` - its members by name, the Range nested, the
	//array an array - the first push and a data change alike, and the listener's conversion back, by the connection's registry, gives
	//the value written.
	TEST_F( SubscribeTests, AStructurePushesAsFields ){
		struct StructureListener final : IListener{
			α OnData( string, NodeId, const Proto::Value& value )ι->void override{ lg _{ Mutex }; Values.push_back( value ); }
			α Last()Ι->optional<Proto::Value>{ lg _{ Mutex }; return Values.empty() ? nullopt : optional{Values.back()}; }
			mutable mutex Mutex; vector<Proto::Value> Values;
		};
		let member = []( const Proto::Structure& s, sv name )->const Proto::Value*{
			for( let& m : s.members() ){
				if( m.name()==name )
					return m.has_value() ? &m.value() : nullptr;
			}
			return nullptr;
		};
		let rpm = [&]( const Proto::Value& v )->optional<double>{ let m = v.has_structure() ? member( v.structure(), "Rpm" ) : nullptr; return m ? optional{m->double_value()} : nullopt; };
		const NodeId node{ PumpsNs(), TestSample };
		Socket().QuerySync( "node( opc: $opc, id: $id ){ value }", {{"opc", OpcServerSlug}, {"id", node.ToJson()}} );//the connect, and with it the registry's fetch.
		ASSERT_TRUE( WaitUntil([&]{ auto r = UAClient::FindTypes( OpcServerSlug ); return r && r->Find( NodeId{PumpsNs(), SampleType} ); }, 15s) ) << "the registry never held PumpSampleType";
		auto listener = ms<StructureListener>();
		let ack = BlockAwait<ClientSocketAwait<FromServer::SubscriptionAck>,FromServer::SubscriptionAck>( Socket().Subscribe(OpcServerSlug, {node}, listener) );
		ASSERT_EQ( ack.results_size(), 1 );
		ASSERT_EQ( ack.results(0).status_code(), UA_STATUSCODE_GOOD ) << UAException::Message( ack.results(0).status_code() );
		absl::Cleanup unsubscribe = [&]{ try{ UnsubscribeAndDrain( node ); }catch( const std::exception& e ){ ADD_FAILURE() << e.what(); } };
		ASSERT_TRUE( WaitUntil([&]{ return listener->Last().has_value(); }, 10s) ) << "the first push";
		EXPECT_TRUE( rpm(*listener->Last()) ) << "the first push, as fields";
		let written = SampleJson( 1515.5, "pushed", false );
		Socket().QuerySync( "updateVariable( opc: $opc, id: $id, value: $value ){ value }", {{"opc", OpcServerSlug}, {"id", node.ToJson()}, {"value", written}} );
		Proto::Value pushed;
		ASSERT_TRUE( WaitUntil([&]{ auto v = listener->Last(); if( v && rpm(*v)==1515.5 ) pushed = *v; return v && rpm(*v)==1515.5; }, 10s) ) << "the data change";
		let& s = pushed.structure();
		ASSERT_EQ( s.members_size(), 5 );
		EXPECT_EQ( ProtoUtils::ToNodeId(s.type_id()), (NodeId{PumpsNs(), SampleType}) );
		EXPECT_EQ( s.members(0).name(), "Rpm" );
		EXPECT_FALSE( member(s, "Running")->boolean() );
		EXPECT_EQ( member(s, "Label")->string_value(), "pushed" );
		ASSERT_TRUE( member(s, "Limits") && member(s, "Limits")->has_structure() ) << "a namespace-0 member, nested";
		EXPECT_EQ( member(member(s, "Limits")->structure(), "High")->double_value(), 3000 );
		ASSERT_TRUE( member(s, "History") && member(s, "History")->has_array() );
		EXPECT_EQ( member(s, "History")->array().values_size(), 3 );
		let registry = UAClient::FindTypes( OpcServerSlug );
		ASSERT_TRUE( registry );
		EXPECT_EQ( Opc::Value{ProtoUtils::ToVariant(pushed, registry.get())}.ToJson(registry.get()), jvalue{written} ) << "the push converted back, member by member";
	}

	TEST_F( SubscribeTests, Basic ){
		ASSERT_NO_THROW( SubscribeAndPush(Intensity) );
		ASSERT_NO_THROW( WriteAndPush(Intensity, 6s) );
		ASSERT_NO_THROW( UnsubscribeAndDrain(Intensity) );
		TRACE( "-------------------------------------------------------------" );
		//teardown costs the gateway's DeleteMonitoring wait (_itemDeleteDelay) and the delete's round trip, so poll rather than fixed-sleep.
		Stopwatch sw;
		while( _client->Processing() )
			ASSERT_NO_THROW( sw.CheckTimeout(6s, 1ms) );
	}

	//NodeValues gained the reading's timestamps for the history trend, but no push carried the server's:  the monitored items
	//asked for the source's alone (timestampsToReturn left at its init), and the read that sends the first push for neither
	//(UA_Client_readValueAttribute_async's own) - historian-web-trend #9.  The first push's source time is the node's, which one
	//never written may not have;  a write's data change has both.
	TEST_F( SubscribeTests, APushCarriesBothTimestamps ){
		ASSERT_NO_THROW( SubscribeAndPush(Intensity) );
		EXPECT_TRUE( _hasServer ) << "the first push";
		ASSERT_NO_THROW( WriteAndPush(Intensity, 6s) );
		EXPECT_TRUE( _hasSource ) << "a data change";
		EXPECT_TRUE( _hasServer ) << "a data change";
		ASSERT_NO_THROW( UnsubscribeAndDrain(Intensity) );
		Stopwatch sw;
		while( _client->Processing() )
			ASSERT_NO_THROW( sw.CheckTimeout(6s, 1ms) );
	}

	//The historian sizes an item's queue and the group's heartbeat hold from the interval the subscription runs at - the server's
	//revised one once it has answered, the request's until then (#526, from the #505 review's #1):  with a 500 ms constant, a server
	//revising to 2 s dropped about 14 of every 20 samples of a 100 ms node from a 6-deep queue.
	TEST_F( SubscribeTests, TheHistorianTakesTheRevisedPublishingInterval ){
		ASSERT_NO_THROW( SubscribeAndPush(Intensity) );
		absl::Cleanup unsubscribe = [this]{ try{ UnsubscribeAndDrain(Intensity); }catch( const std::exception& e ){ ADD_FAILURE() << e.what(); } };
		Opc::Hist::Thresholds sampled; sampled.MinTimeInterval = 100ms;
		EXPECT_EQ( _client->PublishingInterval(), _publishingInterval ) << "the OpcServer revises nothing, so the request's interval runs";
		EXPECT_EQ( HistGroupHost::Parameters(sampled, _client->PublishingInterval()).QueueSize, (uint32)std::ceil(std::chrono::duration<double,std::milli>(_publishingInterval).count()/100.)+1 );
		{
			RevisedInterval revised{ _client, 2000 };
			EXPECT_EQ( _client->PublishingInterval(), 2s );
			EXPECT_EQ( HistGroupHost::Parameters(sampled, _client->PublishingInterval()).QueueSize, 21u );
		}
		for( let ms : {std::numeric_limits<double>::infinity(), 1e300, 1e-7} ){
			RevisedInterval revised{ _client, ms };
			EXPECT_EQ( _client->PublishingInterval(), _publishingInterval ) << ms << " ms:  an answer no Duration holds, or one that rounds to none, is not taken";
		}
		EXPECT_EQ( HistGroupHost::Parameters(sampled, 24h).QueueSize, 864001u );
		EXPECT_EQ( HistGroupHost::Parameters(sampled, Duration::max()).QueueSize, std::numeric_limits<UA_UInt32>::max() ) << "the request's deepest queue, not a wrapped one";
	}

	//A read, a write or a history page to a subscribed client is pumped as it lands:  before #505 the processing loop slept the
	//subscription's publishing interval with only it outstanding, and a request that landed mid-sleep waited the sleep out - ten
	//reads took about 5 s at the 1 s this test used to swap in.  Since #526 the loop waits for the connection manager's events, so
	//the interval no longer bears on a request at all;  the ten reads under a second stay as the guard.
	TEST_F( SubscribeTests, ARequestEndsTheSubscriptionsSleep ){
		ASSERT_NO_THROW( SubscribeAndPush(Intensity) );
		let start = steady_clock::now();
		for( uint i=0; i<10; ++i ){
			StatusCode sc{};
			ASSERT_NO_THROW( sc = ReadValue(_client, Intensity).status ) << "read " << i;
			ASSERT_EQ( sc, UA_STATUSCODE_GOOD ) << "read " << i;
		}
		let elapsed = duration_cast<Duration>( steady_clock::now()-start );
		EXPECT_LT( elapsed, 1s ) << "10 reads took " << Chrono::ToString(elapsed) << " - each waited out a publishing interval";
		ASSERT_NO_THROW( UnsubscribeAndDrain(Intensity) );
		Stopwatch sw;//as Basic: the monitored item must be gone before the test returns.
		while( _client->Processing() )
			ASSERT_NO_THROW( sw.CheckTimeout(6s, 1ms) );
	}

	//Unsubscribe looked the client up by the credential cached at connect, so anything that emptied the cache between
	//Subscribe and Unsubscribe - a logout, or an anonymous session, which is never cached - stranded the subscription as
	//"Client not found" (soak-findings #5).  It now derives the credential the way Subscribe did (SessionCredential).
	TEST_F( SubscribeTests, UnsubscribeSurvivesCredentialCacheLoss ){
		ASSERT_NO_THROW( SubscribeAndPush(Intensity) );//the initial push lands first - the listener writes into this fixture, so nothing may still be in flight when the test returns.
		let cached = GetCredential( AppClient()->SessionId(), OpcServerSlug );
		ASSERT_TRUE( cached );//the suite's shared state: later tests connect through this credential, so it goes back below.
		Logout( AppClient()->SessionId() );//drops the web session's cached credentials; the UA client itself stays connected.
		auto result = BlockAwait<ClientSocketAwait<FromServer::UnsubscribeAck>,FromServer::UnsubscribeAck>( Socket().Unsubscribe(OpcServerSlug, {Intensity}) );
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
		let before = _client->MonitoredNodes().Count();
		ASSERT_NO_THROW( SubscribeAndPush(Intensity) );//the initial push - the subscription is live from here.
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
		ASSERT_NO_THROW( WriteAndPush(Intensity) ) << "no data change after the subscription was rebuilt";
		//And the rebuilt item is the one this session owns - an unsubscribe still finds it.
		ASSERT_NO_THROW( UnsubscribeAndDrain(Intensity, before) );
	}

	//soak-findings #11: the last monitored item going deletes the whole subscription, but _itemDeleteDelay later (DeleteMonitoring's
	//timer).  A subscribe arriving inside that window used to build its items on the id about to be deleted - the ack came
	//back successful, the items existed client-side, and nothing ever pushed.  Re-subscribing immediately after an
	//unsubscribe lands inside the window every time, which is what this does; the assertion is simply that pushes work.
	TEST_F( SubscribeTests, SubscribeSurvivesSubscriptionDelete ){
		ASSERT_NO_THROW( SubscribeAndPush(Intensity) );

		//The window opens when the item is actually gone, not at the unsubscribe:  DeleteMonitoring's timer erases the item
		//_itemDeleteDelay later and submits the subscription delete in the same pass.  The drain to zero is that pass having run, so the
		//delete is in flight from here and a subscribe landing now is the race.  (Re-subscribing before this instead keeps the
		//item count non-zero, and the pass then deletes nothing - which is why that ordering proves nothing.)
		ASSERT_NO_THROW( UnsubscribeAndDrain(Intensity) );
		Subscribe( Intensity );//into the window, and no wait for a push: the write below is the one that has to arrive.

		//Past the delete timer, so a subscription deleted out from under these items shows up here as silence.
		ASSERT_NO_THROW( WriteAndPush(Intensity) ) << "no data change - the re-subscribe's items went onto a deleted subscription";
		ASSERT_NO_THROW( UnsubscribeAndDrain(Intensity) );
	}

	//soak-findings #10, the other half of a lost connection: a failing `run_iterate` does not just lose the session, it
	//deregisters the whole client ("the next request will reconnect") and `~UAClient` takes `UAMonitoringNodes` - every
	//session's monitored nodes - with it.  A later write then builds a *fresh* client, so writes recovered and pushes never
	//did, with nothing logged.  `ConnectionLost` is that path's last step, so calling it under a live subscription is the loss
	//itself, minus the dead socket; what must happen is that the nodes are parked, a reconnect brings a new client up, and
	//the items are re-created on it without the subscriber lifting a finger.
	TEST_F( SubscribeTests, ResubscribesAfterClientLoss ){
		ASSERT_NO_THROW( SubscribeAndPush(Intensity) );//the initial push - the subscription is live from here.
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

		let revived = WaitRevived( lost, OpcServerSlug );//the reconnect's first wait, then a connect, subscribe and create.
		ASSERT_TRUE( revived ) << "no replacement client came up with the monitored node restored";
		Stopwatch sw;
		_client = revived;//the suite shares it: later tests must not reach for the deregistered one.

		sw.Reset();//and the parked count gives them back:  the rebuild erases its entry once the create has answered, just after the item lands on the client.
		while( std::get<2>(UAClient::StatusCounts())!=parkedBefore )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 10ms) ) << "the restored nodes are still counted as parked";

		//The point again: a value written after the client was replaced reaches the same listener, which never re-subscribed.
		ASSERT_NO_THROW( WriteAndPush(Intensity) ) << "no data change after the client was replaced";
		ASSERT_NO_THROW( UnsubscribeAndDrain(Intensity) );
	}

	//subscription-disconnect #2: the rebuild is itself a round trip, and a client that dies during it - a server still coming
	//up drops the channel a second time - took the unfinished part with it.  By then the nodes had left _pending, the dying
	//client's stashPending finds only creates that completed, and the rebuild's catch just logged:  no entry, no chain, and
	//the listener never heard from the node again.  Resubscribe() takes the nodes out and posts the rebuild; removing the
	//client right behind it lands the loss inside the rebuild's first round trip (the subscribe), so nothing has completed
	//and only the rebuild itself can hand the nodes back.
	TEST_F( SubscribeTests, ResubscribesAfterClientLossDuringRebuild ){
		ASSERT_NO_THROW( SubscribeAndPush(Intensity) );//the initial push - the subscription is live from here.
		let lost = _client->Handle();

		_client->Resubscribe();
		UAClient::ConnectionLost( sp<UAClient>{_client} );

		let revived = WaitRevived( lost, OpcServerSlug );//parked by the failed rebuild, so the reconnect's first wait, then a connect, subscribe and create.
		ASSERT_TRUE( revived ) << "no replacement client came up with the monitored node restored";
		_client = revived;//the suite shares it: later tests must not reach for the deregistered one.

		ASSERT_NO_THROW( WriteAndPush(Intensity) ) << "no data change after the rebuild's client was lost";
		ASSERT_NO_THROW( UnsubscribeAndDrain(Intensity) );
	}

	//reviews/m2-closing.md #2: the test above tells the gateway its client died; in an outage nothing does - the rebuild's own
	//refused submission is the first it hears of it.  The rebuild decided park-or-drop on `Connected`, which only ConnectionLost
	//clears, and the create's submission never reached it (UAε, where every other submission has UACε), so a create refused by a
	//channel that had just gone was read as the server's refusal:  nothing parked, no chain, and by the time the processing loop
	//deregistered the client the rebuild had already let the listener go.  One strand turn, so no run_iterate gets to tell the
	//gateway first:  the listener goes into a rebuild posted behind this turn, the subscription is put back so that rebuild goes
	//straight to its create, and the channel is closed under it - `Connected` still true when the create is refused.
	TEST_F( SubscribeTests, ResubscribesWhenTheRebuildsCreateIsRefused ){
		ASSERT_NO_THROW( SubscribeAndPush(Intensity) );//the initial push - the subscription is live from here.
		let lost = _client->Handle();

		StatusCode sc{};
		ASSERT_NO_THROW( sc = BlockAwait(UAStrandAwait<StatusCode>{ _client, [client=_client]{
			auto subscription = client->CreatedSubscriptionResponse();
			client->Resubscribe();//clears the cached subscription, and posts the rebuild...
			client->SetCreatedSubscriptionResponse( move(subscription) );//...which now finds one, and skips the subscribe's round trip.
			return UA_Client_disconnectSecureChannelAsync( client->UAPointer() );
		}}) );
		ASSERT_EQ( sc, UA_STATUSCODE_GOOD );

		let revived = WaitRevived( lost, OpcServerSlug );//parked by the refused rebuild, so the reconnect's first wait, then a connect, subscribe and create.
		ASSERT_TRUE( revived ) << "no replacement client came up with the monitored node restored - the refused rebuild dropped its listener";
		_client = revived;//the suite shares it: later tests must not reach for the deregistered one.

		ASSERT_NO_THROW( WriteAndPush(Intensity) ) << "no data change after the rebuild's create was refused";
		ASSERT_NO_THROW( UnsubscribeAndDrain(Intensity) );
	}

	//subscription-disconnect #3: a reconnect chain waited out its backoff on a timer nothing could cancel.  A pending timer is live
	//asio work, so stopping the gateway during an outage waited out the rest of the delay (up to 15s), and a chain that woke
	//mid-shutdown built a client nothing stopped.  UAClient::Shutdown now starts with StopReconnects, which cancels the wait.  The
	//chain's first wait is a second long, so one still running half a second after StopReconnects was not cancelled.
	TEST_F( SubscribeTests, StopReconnectsEndsTheWait ){
		const DurationOverride delay{ _reconnectDelay, 1s };//the shipped wait:  the suite's would end by itself inside the half second.
		ASSERT_NO_THROW( SubscribeAndPush(Intensity) );//the initial push - the subscription is live from here.

		let waiting = UAClient::ReconnectsWaiting();
		UAClient::ConnectionLost( sp<UAClient>{_client} );//parks the node and starts the chain, whose wait is registered before this returns.
		ASSERT_EQ( UAClient::ReconnectsWaiting(), waiting+1 );
		//The listener leaves first - from the parked entry and from the test socket - so nothing is left pointing at this fixture.
		auto dropped = BlockAwait<ClientSocketAwait<FromServer::UnsubscribeAck>,FromServer::UnsubscribeAck>( Socket().Unsubscribe(OpcServerSlug, {Intensity}) );
		ASSERT_EQ( dropped.successes_size(), 1 );//the parked entry is the only place it is left - nothing to drain off a client here.

		Stopwatch sw;
		ASSERT_EQ( UAClient::StopReconnects(), 1u );
		while( UAClient::ReconnectsWaiting()!=waiting )
			ASSERT_NO_THROW( sw.CheckTimeout(500ms, 1ms) ) << "the reconnect's wait was not cancelled - it ran on toward its tick";

		Reconnect();//the suite shares _client, and nothing reconnects it now.
	}

	//subscription-disconnect #4: a rebuild held its listeners in its own frame, where neither unsubscribe could reach them, so a
	//session that closed - or dropped a node - while the rebuild was putting nodes back had them re-created for a listener nothing
	//would ever unsubscribe, pinning the monitored item for the client's life.  Resubscribe() hands the listener to a rebuild whose
	//first step is a subscribe round trip, so an unsubscribe right behind it lands before the rebuild reaches the listener, every
	//time.  The listener is the gateway side's own IDataChange, subscribed on the client directly, so the test can unsubscribe it.
	Ω unsubscribeDuringRebuild( const sp<UAClient>& client, function<void(const sp<IDataChange>&)> unsubscribe )->void{
		let before = client->MonitoredNodes().Count();
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( SubscribeCounting(client, listener, {Intensity}) );//the initial push - the item is live from here.
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
			let dropped = UAClient::UnsubscribePending( OpcServerSlug, listener, {Intensity} );
			EXPECT_EQ( dropped.size(), 1u ) << "a node the rebuild still held was reported as a failed unsubscribe";
		});
	}

	//Historian 5a (#222):  the connection-state callbacks the historian's gap rule needs (spec *Collection*, *Gaps*) - ConnectionLost
	//with the time the subscription went, ConnectionRestored once the rebuild has the nodes back and before the new subscription
	//delivers a value - and the per-node item parameters a listener answers (IDataChange::ItemParameters).
	struct StateListener final : PushCount{
		α SendDataChange( const ServerCnnctnNK&, const NodeId&, const Value&, const ITypes* )ι->void override{
			if( Lost && !Restored )
				++PushesWhileLost;//nothing was in flight at either staged loss, so one of these is the new item delivering before ConnectionRestored.
			if( Restored )
				++PushesAfterRestore;
			++Pushes;
		}
		α ConnectionLost( const ServerCnnctnNK& opcId, const flat_set<NodeId>& nodes, TimePoint when )ι->void override{
			lg _{ Mutex }; LostOpc = opcId; LostNodes = nodes; LostAt = when; ++Lost;
		}
		α ConnectionRestored( const ServerCnnctnNK& opcId, const flat_set<NodeId>& nodes )ι->void override{
			lg _{ Mutex }; RestoredOpc = opcId; RestoredNodes = nodes; ++Restored;
		}
		α ItemParameters( const ServerCnnctnNK&, const NodeId&, Duration )Ι->MonitoringParameters override{ return Parameters; }
		α RunsAt( Duration publishingInterval )ι->void override{ RanAt = publishingInterval; }
		α ItemModified( const ServerCnnctnNK&, const NodeId&, const MonitoredItemModifyResult& result, const optional<AskReverted>& )ι->void override{
			RevisedSamplingInterval = result.RevisedSamplingInterval; RevisedQueueSize = result.RevisedQueueSize; ++Modified;
		}
		α to_string()Ι->string override{ return "SubscribeTests.StateListener"; }
		MonitoringParameters Parameters;
		std::atomic<Duration> RanAt{};
		atomic<uint> PushesWhileLost, PushesAfterRestore, Lost, Restored, Modified;
		atomic<double> RevisedSamplingInterval{}; atomic<uint32> RevisedQueueSize{};
		mutex Mutex; string LostOpc, RestoredOpc; flat_set<NodeId> LostNodes, RestoredNodes; TimePoint LostAt;
	};
	Ω drainListener( const sp<UAClient>& client, const sp<StateListener>& listener, uint remaining )->void{//off the client, and its item gone - the DeleteMonitoring timer runs _itemDeleteDelay behind the unsubscribe.
		UAClient::Unsubscribe( listener );
		Stopwatch sw;
		while( client->MonitoredNodes().Count()!=remaining )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 1ms) ) << "the listener's item never went";
	}
	//The server's half of a session loss, as ResubscribesAfterSubscriptionLoss stages it:  the take tells the listener, with the
	//node and a time no earlier than the loss, and the rebuild tells it again once the node is back - before the first push the
	//new item makes.  Nothing a first subscribe does is a restore.
	TEST_F( SubscribeTests, ConnectionStateReachesTheListener ){
		let before = _client->MonitoredNodes().Count();
		auto listener = ms<StateListener>();
		ASSERT_NO_THROW( SubscribeCounting(_client, listener, {Intensity}) );
		EXPECT_EQ( listener->Lost.load(), 0u );
		EXPECT_EQ( listener->Restored.load(), 0u ) << "a first subscribe is not a restore";

		let subscriptionId = _client->SubscriptionId();
		ASSERT_TRUE( subscriptionId );
		let lostAfter = Clock::now();
		ASSERT_NO_THROW( deleteSubscription(_client, subscriptionId) );
		_client->Resubscribe();//the take and its ConnectionLost run here, before the rebuild is posted.
		ASSERT_EQ( listener->Lost.load(), 1u ) << "the take did not tell the listener";
		{
			lg _{ listener->Mutex };
			EXPECT_EQ( listener->LostOpc, OpcServerSlug );
			EXPECT_EQ( listener->LostNodes, flat_set<NodeId>{Intensity} );
			EXPECT_GE( listener->LostAt, lostAfter ) << "the break time predates the loss";
			EXPECT_LE( listener->LostAt, Clock::now() );
		}
		Stopwatch sw;//the rebuild:  a subscribe and a create round trip, then the new item's initial push.
		while( !listener->PushesAfterRestore )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 1ms) ) << "no push after the rebuild";
		EXPECT_EQ( listener->Restored.load(), 1u );
		EXPECT_EQ( listener->PushesWhileLost.load(), 0u ) << "the new subscription delivered a value before ConnectionRestored";
		{
			lg _{ listener->Mutex };
			EXPECT_EQ( listener->RestoredOpc, OpcServerSlug );
			EXPECT_EQ( listener->RestoredNodes, flat_set<NodeId>{Intensity} );
		}
		EXPECT_EQ( _client->MonitoredNodes().Count(), before+1 );
		drainListener( _client, listener, before );
	}
	//historian-gateway-plumbing #3:  ConnectionLost reported when the loss was found - for a session loss, the new session's
	//activation, after the outage - so the historian's late-report rule (spec *Compression*) kept the heartbeats written through
	//it.  It reports the break StateCallback recorded instead:  the first state short of an activated session, here a round trip
	//before the rebuild finds the session gone.  The take clears it, so the next loss is a break of its own.
	TEST_F( SubscribeTests, ConnectionLostCarriesTheBreak ){
		let before = _client->MonitoredNodes().Count();
		auto listener = ms<StateListener>();
		ASSERT_NO_THROW( SubscribeCounting(_client, listener, {Intensity}) );
		auto subscriptionId = _client->SubscriptionId();
		ASSERT_TRUE( subscriptionId );

		let brokeAfter = Clock::now();
		_client->RecordBreak();//as StateCallback records the channel or session going...
		let brokeBefore = Clock::now();
		_client->RecordBreak();//...and a later state of the same outage keeps the first.
		ASSERT_NO_THROW( deleteSubscription(_client, subscriptionId) );//a round trip, so the take runs well after the break.
		_client->Resubscribe();
		ASSERT_EQ( listener->Lost.load(), 1u );
		{
			lg _{ listener->Mutex };
			EXPECT_GE( listener->LostAt, brokeAfter );
			EXPECT_LE( listener->LostAt, brokeBefore ) << "the time the loss was found, not the break";
		}
		Stopwatch sw;
		while( listener->Restored.load()<1 )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 1ms) ) << "the first rebuild never restored the node";

		subscriptionId = _client->SubscriptionId();
		ASSERT_TRUE( subscriptionId );
		let secondAfter = Clock::now();
		ASSERT_NO_THROW( deleteSubscription(_client, subscriptionId) );
		_client->Resubscribe();
		ASSERT_EQ( listener->Lost.load(), 2u );
		{
			lg _{ listener->Mutex };
			EXPECT_GE( listener->LostAt, secondAfter ) << "the first loss's break was reported again";
		}
		sw.Reset();
		while( listener->Restored.load()<2 )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 1ms) ) << "the second rebuild never restored the node";
		drainListener( _client, listener, before );
	}
	//historian-gateway-plumbing #10:  a server that drops a subscription while the session lives - its lifetime ran out in an
	//outage the session outlived - says so with a StatusChangeNotification, which only traced:  the items stayed on the client,
	//dead, and nothing rebuilt them.  OpcServer keeps sessions shorter than subscriptions, so the notification is delivered by
	//hand, on the strand as open62541 delivers it.  One about another subscription, or with another status, changes nothing.
	TEST_F( SubscribeTests, ASubscriptionTheServerDropsIsRebuilt ){
		let before = _client->MonitoredNodes().Count();
		auto listener = ms<StateListener>();
		ASSERT_NO_THROW( SubscribeCounting(_client, listener, {Intensity}) );
		let subscriptionId = _client->SubscriptionId();
		ASSERT_TRUE( subscriptionId );
		auto statusChange = [client=_client]( SubscriptionId id, StatusCode sc ){
			BlockAwait( UAStrandAwait<bool>{client, [client, id, sc]{ SubscriptionStatusChanged( client, id, sc ); return true; }} );
		};
		statusChange( subscriptionId+1, UA_STATUSCODE_BADTIMEOUT );
		statusChange( subscriptionId, UA_STATUSCODE_GOOD );
		EXPECT_EQ( listener->Lost.load(), 0u ) << "another subscription's, or another status, was taken for a loss";
		EXPECT_EQ( _client->SubscriptionId(), subscriptionId );

		statusChange( subscriptionId, UA_STATUSCODE_BADTIMEOUT );
		ASSERT_EQ( listener->Lost.load(), 1u ) << "the listener was not told its subscription went";
		Stopwatch sw;
		while( !listener->PushesAfterRestore )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 1ms) ) << "no push after the rebuild";
		EXPECT_EQ( listener->Restored.load(), 1u );
		EXPECT_NE( _client->SubscriptionId(), subscriptionId ) << "the items are still on the dropped subscription";
		EXPECT_EQ( _client->MonitoredNodes().Count(), before+1 );
		drainListener( _client, listener, before );
	}
	//The other way a subscription goes - the connection itself (ConnectionLost, as ResubscribesAfterClientLoss stages it):  the
	//dying client's park tells the listener before ConnectionLost returns, and the reconnect's rebuild restores on a new client.
	TEST_F( SubscribeTests, ConnectionLossReachesTheListener ){
		auto listener = ms<StateListener>();
		ASSERT_NO_THROW( SubscribeCounting(_client, listener, {Intensity}) );
		let lost = _client->Handle();
		let lostAfter = Clock::now();
		UAClient::ConnectionLost( sp<UAClient>{_client} );
		ASSERT_EQ( listener->Lost.load(), 1u ) << "the park did not tell the listener";
		{
			lg _{ listener->Mutex };
			EXPECT_EQ( listener->LostNodes, flat_set<NodeId>{Intensity} );
			EXPECT_GE( listener->LostAt, lostAfter );
		}
		Stopwatch sw;//the reconnect's first wait, then a connect, subscribe and create.
		while( !listener->PushesAfterRestore ){
			ASSERT_NO_THROW( sw.CheckTimeout(30s, 10ms) ) << "no push after the reconnect";
		}
		let revived = WaitRevived( lost, OpcServerSlug, 0s );
		ASSERT_TRUE( revived ) << "no replacement client holds the restored node";
		_client = revived;//the suite shares it: later tests must not reach for the deregistered one.
		EXPECT_EQ( listener->Restored.load(), 1u );
		EXPECT_EQ( listener->PushesWhileLost.load(), 0u ) << "the new client's subscription delivered a value before ConnectionRestored";
		drainListener( _client, listener, 0 );
	}
	//The listener's parameters reach the server at the create, the ack carries what it granted, and ModifyMonitoredItems changes
	//them in place - the item count does not move, and a node with no item is refused without a round trip.
	TEST_F( SubscribeTests, ItemParametersAreTheListenersAndModifiable ){
		let before = _client->MonitoredNodes().Count();
		auto listener = ms<StateListener>();
		listener->Parameters = { .SamplingInterval=1000.0, .QueueSize=5 };
		FromServer::SubscriptionAck ack;
		ASSERT_NO_THROW( ack = SubscribeCounting(_client, listener, {Intensity}) );
		ASSERT_EQ( ack.results_size(), 1 );
		EXPECT_EQ( ack.results(0).status_code(), UA_STATUSCODE_GOOD );
		EXPECT_EQ( ack.results(0).revised_sampling_interval(), 1000.0 ) << "the server did not take the listener's sampling interval";
		EXPECT_EQ( ack.results(0).revised_queue_size(), 5u ) << "the server did not take the listener's queue size";

		const NodeId unmonitored{ 4, 6018 };
		auto results = BlockAwait( ModifyMonitoredItemsAwait{{{Intensity, {.SamplingInterval=250.0, .QueueSize=3}}, {unmonitored, {}}}, _client} );
		ASSERT_EQ( results.size(), 2u ) << "every node asked about has a result";
		EXPECT_EQ( results.at(Intensity).Status, UA_STATUSCODE_GOOD );
		EXPECT_EQ( results.at(Intensity).RevisedSamplingInterval, 250.0 );
		EXPECT_EQ( results.at(Intensity).RevisedQueueSize, 3u );
		EXPECT_EQ( results.at(unmonitored).Status, UA_STATUSCODE_BADMONITOREDITEMIDINVALID ) << "a node this client does not monitor";
		EXPECT_EQ( _client->MonitoredNodes().Count(), before+1 ) << "a modify is in place - no item was deleted or created";
		//A second listener joining the item sees the modified parameters in its ack (GetResult reads what Modified stored).
		auto joiner = ms<StateListener>();
		let joined = BlockAwait( DataChangeAwait{{Intensity}, joiner, _client} );
		ASSERT_EQ( joined.results_size(), 1 );
		EXPECT_EQ( joined.results(0).revised_sampling_interval(), 250.0 );
		EXPECT_EQ( joined.results(0).revised_queue_size(), 3u );
		UAClient::Unsubscribe( joiner );
		drainListener( _client, listener, before );
	}

	//A listener whose nodes all join existing items hears the interval too, though it asks no ItemParameters:  a historian host
	//restored within DeleteMonitoring's grace joins every item, and kept the request's interval for its heartbeat hold and its edits.
	TEST_F( SubscribeTests, AJoiningListenerHearsTheRevisedInterval ){
		let before = _client->MonitoredNodes().Count();
		auto first = ms<StateListener>();
		ASSERT_NO_THROW( SubscribeCounting(_client, first, {Intensity}) );
		auto joiner = ms<StateListener>();
		absl::Cleanup drain = [&]{ UAClient::Unsubscribe( joiner ); drainListener( _client, first, before ); };
		EXPECT_EQ( first->RanAt.load(), _publishingInterval );
		RevisedInterval revised{ _client, 2000 };
		ASSERT_NO_THROW( BlockAwait(DataChangeAwait{{Intensity}, joiner, _client}) );
		EXPECT_EQ( _client->MonitoredNodes().Count(), before+1 ) << "the joiner made no item";
		EXPECT_EQ( joiner->RanAt.load(), 2s );
	}

	//#418 (spec *Watch list*):  a Subscribe's per-node parameters reach the item through the session's ItemParameters.  An absolute
	//deadband on pumpManual's rpm, a Double:  the server suppresses a change under it and reports one over it, each measured from
	//the last value it reported.  Every write is sampled - an interval of 0 samples on the write - and queued, so a change the
	//filter let through would arrive, in order, before the one after it (418-watch-items #11:  a fixed sleep either side, which a
	//slow publish failed or passed without proving anything).
	TEST_F( SubscribeTests, AnAbsoluteDeadbandFiltersTheStream ){
		let before = _client->MonitoredNodes().Count();
		const NodeId rpm{ PumpsNs(), RpmManual };
		ASSERT_NO_THROW( WriteDouble(rpm, 1000) );
		auto listener = ms<WatchListener>();
		ASSERT_NO_THROW( Watch(rpm, {.SamplingInterval=0, .QueueSize=10, .Filter=DataChangeFilter{.DeadbandType=UA_DEADBANDTYPE_ABSOLUTE, .DeadbandValue=10}}, listener) );
		ASSERT_NO_THROW( WaitValue(listener, 1000) );
		ASSERT_NO_THROW( WriteDouble(rpm, 1005) );
		ASSERT_NO_THROW( WriteDouble(rpm, 1020) );
		ASSERT_NO_THROW( WaitValue(listener, 1020) ) << "a change over the deadband wasn't pushed";
		EXPECT_FALSE( listener->Saw(1005) ) << "a change under the deadband was pushed";
		ASSERT_NO_THROW( UnsubscribeAndDrain(rpm, before) );
	}
	//A percent deadband is the server's to apply against the node's EURange, which open62541 does under UA_ENABLE_DA:  pump3's is
	//0-3000 (pumps.NodeSet2.xml), so 1 % is 30 rpm.  Sampled and queued as above.
	TEST_F( SubscribeTests, APercentDeadbandFiltersAgainstTheRange ){
		let before = _client->MonitoredNodes().Count();
		const NodeId rpm{ PumpsNs(), Rpm3 };
		ASSERT_NO_THROW( WriteDouble(rpm, 1000) );
		auto listener = ms<WatchListener>();
		ASSERT_NO_THROW( Watch(rpm, {.SamplingInterval=0, .QueueSize=10, .Filter=DataChangeFilter{.DeadbandType=UA_DEADBANDTYPE_PERCENT, .DeadbandValue=1}}, listener) );
		ASSERT_NO_THROW( WaitValue(listener, 1000) );
		ASSERT_NO_THROW( WriteDouble(rpm, 1020) );
		ASSERT_NO_THROW( WriteDouble(rpm, 1040) );
		ASSERT_NO_THROW( WaitValue(listener, 1040) ) << "a change over 1 % of the range wasn't pushed";
		EXPECT_FALSE( listener->Saw(1020) ) << "a change under 1 % of the range was pushed";
		ASSERT_NO_THROW( UnsubscribeAndDrain(rpm, before) );
	}
	//Two listeners on one node share its item, which takes the strictest of what they ask:  the first, through the socket, asks
	//1 s and a queue of 2, which its ack carries revised;  a second asking 250 ms modifies the item, and every listener hears what
	//the server revised it to - the first by the MonitoredItemModified push, the joiner by ItemModified, since its ack answered
	//first - and the second's unsubscribe restores the first's rate.
	TEST_F( SubscribeTests, AJoinModifiesTheSharedItemAndALeaveRestoresIt ){
		let before = _client->MonitoredNodes().Count();
		auto watcher = ms<WatchListener>();
		FromServer::SubscriptionAck ack;
		ASSERT_NO_THROW( ack = Watch(Intensity, {.SamplingInterval=1000.0, .QueueSize=2}, watcher) );
		EXPECT_EQ( ack.results(0).revised_sampling_interval(), 1000.0 ) << "the ack's revised sampling interval";
		EXPECT_EQ( ack.results(0).revised_queue_size(), 2u ) << "the ack's revised queue size";
		auto joiner = ms<StateListener>();
		joiner->Parameters = { .SamplingInterval=250.0, .QueueSize=1 };
		let joined = BlockAwait( DataChangeAwait{{Intensity}, joiner, _client} );
		ASSERT_EQ( joined.results_size(), 1 );
		EXPECT_EQ( joined.results(0).status_code(), UA_STATUSCODE_GOOD );
		EXPECT_EQ( _client->MonitoredNodes().Count(), before+1 ) << "the joiner made no item";
		Stopwatch sw;
		while( watcher->Modified.load()<1 )
			ASSERT_NO_THROW( sw.CheckTimeout(5s, 1ms) ) << "the first listener never heard the modify";
		{
			lg _{ watcher->Mutex };
			EXPECT_EQ( watcher->RevisedSamplingInterval, 250.0 ) << "the fastest sampling";
			EXPECT_EQ( watcher->RevisedQueueSize, 2u ) << "the largest queue";
		}
		sw.Reset();
		while( joiner->Modified.load()<1 )
			ASSERT_NO_THROW( sw.CheckTimeout(5s, 1ms) ) << "the joiner never heard the modify its join made";
		EXPECT_EQ( joiner->RevisedSamplingInterval.load(), 250.0 );
		EXPECT_EQ( joiner->RevisedQueueSize.load(), 2u );
		UAClient::Unsubscribe( joiner );
		sw.Reset();
		while( watcher->Modified.load()<2 )
			ASSERT_NO_THROW( sw.CheckTimeout(5s, 1ms) ) << "the leave never restored the first's rate";
		{
			lg _{ watcher->Mutex };
			EXPECT_EQ( watcher->RevisedSamplingInterval, 1000.0 );
			EXPECT_EQ( watcher->RevisedQueueSize, 2u );
		}
		EXPECT_EQ( _client->MonitoredNodes().Count(), before+1 ) << "the leave took the item the first still listens on";
		ASSERT_NO_THROW( UnsubscribeAndDrain(Intensity, before) );
	}
	//A listener that asks a deadband on a node another watches unfiltered gets the unfiltered stream - more than it asked, never
	//less - and its join modifies nothing:  the unfiltered listener asks the server's default, which lifts the deadband.
	TEST_F( SubscribeTests, AFilteredListenerJoiningAnUnfilteredItemGetsTheUnfilteredStream ){
		let before = _client->MonitoredNodes().Count();
		const NodeId rpm{ PumpsNs(), RpmManual };
		ASSERT_NO_THROW( WriteDouble(rpm, 1000) );
		auto first = ms<StateListener>();
		ASSERT_NO_THROW( SubscribeCounting(_client, first, {rpm}) );
		auto watcher = ms<WatchListener>();
		ASSERT_NO_THROW( Watch(rpm, {.Filter=DataChangeFilter{.DeadbandType=UA_DEADBANDTYPE_ABSOLUTE, .DeadbandValue=10}}, watcher) );
		EXPECT_EQ( _client->MonitoredNodes().Count(), before+1 ) << "the join made an item";
		ASSERT_NO_THROW( WaitValue(watcher, 1000) );
		ASSERT_NO_THROW( WriteDouble(rpm, 1005) );
		ASSERT_NO_THROW( WaitValue(watcher, 1005) ) << "the unfiltered stream didn't reach the filtered listener";
		EXPECT_EQ( watcher->Modified.load(), 0u ) << "the join modified the item";
		EXPECT_EQ( first->Modified.load(), 0u );
		ASSERT_NO_THROW( UnsubscribeAndDrain(rpm, before+1) );
		drainListener( _client, first, before );
	}
	//418-watch-items #1:  the unfiltered listener's default lifts a deadband but not a trigger.  The first, through the socket,
	//asks StatusValueTimestamp;  an unfiltered joiner leaves the item as it is, so a write of the same value, which the server
	//stamps with a new source time, still reaches the first.  The merge used to drop the whole filter.
	TEST_F( SubscribeTests, AnUnfilteredJoinKeepsATimestampTrigger ){
		let before = _client->MonitoredNodes().Count();
		const NodeId rpm{ PumpsNs(), RpmManual };
		ASSERT_NO_THROW( WriteDouble(rpm, 1000) );
		auto watcher = ms<WatchListener>();
		ASSERT_NO_THROW( Watch(rpm, {.Filter=DataChangeFilter{.Trigger=UA_DATACHANGETRIGGER_STATUSVALUETIMESTAMP}}, watcher) );
		Stopwatch sw;
		while( watcher->Pushes.load()<2 )//the read's push and the item's first notification.
			ASSERT_NO_THROW( sw.CheckTimeout(5s, 1ms) ) << "the watch's first values never came";
		auto joiner = ms<StateListener>();
		let joined = BlockAwait( DataChangeAwait{{rpm}, joiner, _client} );
		ASSERT_EQ( joined.results_size(), 1 );
		EXPECT_EQ( joined.results(0).status_code(), UA_STATUSCODE_GOOD );
		EXPECT_EQ( _client->MonitoredNodes().Count(), before+1 ) << "the joiner made an item";
		let pushes = watcher->Pushes.load();
		ASSERT_NO_THROW( WriteDouble(rpm, 1000) );//behind any modify the join posted:  one client, so the server takes them in order.
		sw.Reset();
		while( watcher->Pushes.load()==pushes )
			ASSERT_NO_THROW( sw.CheckTimeout(5s, 1ms) ) << "the unfiltered join dropped the timestamp trigger";
		EXPECT_EQ( watcher->Modified.load(), 0u ) << "the join modified the item";
		UAClient::Unsubscribe( joiner );
		ASSERT_NO_THROW( UnsubscribeAndDrain(rpm, before) );
	}
	//418-watch-items #2:  a filter sent for its deadband alone - as a protobufjs or ts-proto client sends it - leaves the trigger
	//unset, which is StatusValue, the server's default.  It was proto3's zero, Status, and the item reported no value change.
	TEST_F( SubscribeTests, AnUnsetTriggerIsStatusValue ){
		FromClient::MonitoringParameters proto;
		auto& f = *proto.mutable_filter();
		f.set_deadband_type( UA_DEADBANDTYPE_ABSOLUTE );
		f.set_deadband_value( 10 );
		let parameters = FromClientUtils::ToParameters( proto, Intensity );
		ASSERT_TRUE( parameters.Filter );
		EXPECT_EQ( parameters.Filter->Trigger, UA_DATACHANGETRIGGER_STATUSVALUE );
		EXPECT_EQ( parameters.Filter->DeadbandType, UA_DEADBANDTYPE_ABSOLUTE );
		EXPECT_EQ( parameters.Filter->DeadbandValue, 10 );
		f.set_trigger( UA_DATACHANGETRIGGER_STATUS );
		EXPECT_EQ( FromClientUtils::ToParameters(proto, Intensity).Filter->Trigger, UA_DATACHANGETRIGGER_STATUS ) << "a trigger sent as 0 is Status";
	}
	//418-watch-items #5:  a parameter the server would misread or the merge couldn't order refuses the whole Subscribe, naming the
	//node and the field, before anything is stored or asked - here the second node's, so the first, good, isn't subscribed
	//either.  The gateway passed them on:  a trigger of 3 aborted a debug OpcServer at its first sample (UA_assert in
	//ua_subscription_datachange.c), and a NaN interval made every merge look changed.
	TEST_F( SubscribeTests, ABadParameterRefusesTheSubscribe ){
		let before = _client->MonitoredNodes().Count();
		const NodeId rpm{ PumpsNs(), RpmManual };
		let nan = std::numeric_limits<double>::quiet_NaN();
		const vector<std::pair<MonitoringParameters,string>> bad{
			{ {.SamplingInterval=nan}, "sampling_interval" },
			{ {.Filter=DataChangeFilter{.Trigger=(UA_DataChangeTrigger)3}}, "trigger" },
			{ {.Filter=DataChangeFilter{.DeadbandType=(UA_DeadbandType)3}}, "deadband_type" },
			{ {.Filter=DataChangeFilter{.DeadbandType=UA_DEADBANDTYPE_ABSOLUTE, .DeadbandValue=-5}}, "deadband_value" },
			{ {.Filter=DataChangeFilter{.DeadbandType=UA_DEADBANDTYPE_ABSOLUTE, .DeadbandValue=nan}}, "deadband_value" },
			{ {.Filter=DataChangeFilter{.DeadbandType=UA_DEADBANDTYPE_PERCENT, .DeadbandValue=150}}, "percent" }
		};
		for( let& [parameters, field] : bad ){
			auto watcher = ms<WatchListener>();
			try{
				BlockAwait<ClientSocketAwait<FromServer::SubscriptionAck>,FromServer::SubscriptionAck>( Socket().Subscribe(OpcServerSlug, {Intensity, rpm}, {{}, parameters}, watcher) );
				ADD_FAILURE() << "a Subscribe asking a bad " << field << " was taken";
			}
			catch( const std::exception& e ){
				EXPECT_NE( string{e.what()}.find(field), string::npos ) << e.what();
				EXPECT_NE( string{e.what()}.find(rpm.ToString()), string::npos ) << "the refusal doesn't name the node:  " << e.what();
			}
		}
		EXPECT_EQ( _client->MonitoredNodes().Count(), before ) << "a refused Subscribe made an item";
	}
	//418-watch-items #4:  a negative sampling interval asks the publishing interval, so it is ordered as that interval - the suite's
	//100 ms - not as the fastest.  A joiner asking 60 ms is faster, so its join modifies the item to 60, and its leave puts the
	//watcher's 100 back.  The merge took -1 for the fastest:  the join changed nothing, and the server sampled at 100.
	TEST_F( SubscribeTests, ANegativeSamplingIntervalIsThePublishingInterval ){
		let before = _client->MonitoredNodes().Count();
		let publishing = std::chrono::duration<double,std::milli>{ _client->PublishingInterval() }.count();
		auto watcher = ms<WatchListener>();
		FromServer::SubscriptionAck ack;
		ASSERT_NO_THROW( ack = Watch(Intensity, {.SamplingInterval=-1}, watcher) );
		EXPECT_EQ( ack.results(0).revised_sampling_interval(), publishing );
		auto joiner = ms<StateListener>();
		joiner->Parameters = { .SamplingInterval=60.0 };
		let joined = BlockAwait( DataChangeAwait{{Intensity}, joiner, _client} );
		ASSERT_EQ( joined.results_size(), 1 );
		Stopwatch sw;
		while( watcher->Modified.load()<1 )
			ASSERT_NO_THROW( sw.CheckTimeout(5s, 1ms) ) << "the faster joiner modified nothing";
		{
			lg _{ watcher->Mutex };
			EXPECT_EQ( watcher->RevisedSamplingInterval, 60.0 ) << "the joiner's, the faster";
		}
		UAClient::Unsubscribe( joiner );
		sw.Reset();
		while( watcher->Modified.load()<2 )
			ASSERT_NO_THROW( sw.CheckTimeout(5s, 1ms) ) << "the leave never restored the publishing interval";
		{
			lg _{ watcher->Mutex };
			EXPECT_EQ( watcher->RevisedSamplingInterval, publishing );
		}
		ASSERT_NO_THROW( UnsubscribeAndDrain(Intensity, before) );
	}
	//418-watch-items #6:  a Subscribe to "", the default connection, keeps its parameters under the slug the connect resolves "" to,
	//which the create asks ItemParameters by.  They were kept under "", so every node was created with the defaults:  here the
	//ack's revised sampling was 500, not the 1 s asked.  And the connect takes the session's credential for the slug, so the item
	//is on _client, the one a Subscribe by slug reaches (418-watch-items N2:  it took the credential for "", a token of its own).
	TEST_F( SubscribeTests, TheDefaultConnectionKeepsItsParameters ){
		let before = _client->MonitoredNodes().Count();
		let id = GetConnection( OpcServerSlug ).Id;
		QL().QuerySync<jvalue>( Ƒ("mutation updateServerConnection( id:{}, isDefault:true )", id), {}, {UserPK::System} );
		absl::Cleanup notDefault = [id]{ QL().QuerySync<jvalue>( Ƒ("mutation updateServerConnection( id:{}, isDefault:false )", id), {}, {UserPK::System} ); };
		auto watcher = ms<WatchListener>();
		FromServer::SubscriptionAck ack;
		ASSERT_NO_THROW( (ack = BlockAwait<ClientSocketAwait<FromServer::SubscriptionAck>,FromServer::SubscriptionAck>(Socket().Subscribe("", {Intensity}, {{.SamplingInterval=1000.0}}, watcher))) );
		ASSERT_EQ( ack.results_size(), 1 );
		EXPECT_EQ( ack.results(0).status_code(), UA_STATUSCODE_GOOD );
		EXPECT_EQ( ack.results(0).revised_sampling_interval(), 1000.0 ) << "the default connection's Subscribe was created with the defaults";
		EXPECT_EQ( _client->MonitoredNodes().Count(), before+1 ) << "\"\" connected with another credential, on another client";
		ASSERT_NO_THROW( UnsubscribeAndDrain(Intensity, before) );
		Stopwatch sw;//and any other client's item goes - one N2 made:  a later WaitRevived would take a client of the slug monitoring a node for the revival.
		for( let& client : UAClient::LiveClients() ){
			while( client->Slug()==OpcServerSlug && client!=_client && client->MonitoredNodes().Count() )
				ASSERT_NO_THROW( sw.CheckTimeout(10s, 10ms) ) << "the other client's item never went";
		}
	}
	//418-watch-items #9:  a re-subscribe the server refuses as a modify puts the session's ask back to the one the server granted,
	//so a rebuild re-creates the item as it stood.  The refused ask - an absolute deadband on the server's ProductName, a String
	//- was kept, and the rebuild's create was refused with it:  the node was lost.
	TEST_F( SubscribeTests, ARefusedAskIsntRebuilt ){
		let before = _client->MonitoredNodes().Count();
		const NodeId productName{ 0, 2261 };//Server_ServerStatus_BuildInfo_ProductName
		auto watcher = ms<WatchListener>();
		ASSERT_NO_THROW( Watch(productName, {.SamplingInterval=1000.0}, watcher) );
		ASSERT_NO_THROW( Watch(productName, {.SamplingInterval=1000.0, .Filter=DataChangeFilter{.DeadbandType=UA_DEADBANDTYPE_ABSOLUTE, .DeadbandValue=1}}, watcher) );
		Stopwatch sw;
		while( watcher->Modified.load()<1 )
			ASSERT_NO_THROW( sw.CheckTimeout(5s, 1ms) ) << "the refusal never came";
		{
			lg _{ watcher->Mutex };
			ASSERT_EQ( watcher->Status, UA_STATUSCODE_BADFILTERNOTALLOWED );
		}
		let lost = _client->Handle();
		UAClient::ConnectionLost( sp<UAClient>{_client} );
		let revived = WaitRevived( lost, OpcServerSlug );
		if( !revived )
			Reconnect();//the suite shares _client.
		ASSERT_TRUE( revived ) << "no replacement client monitors anything:  the rebuild lost the node, re-creating the refused ask";
		_client = revived;
		sw.Reset();
		while( _client->MonitoredNodes().Handles({productName}).empty() )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 10ms) ) << "the rebuild lost the node:  it re-created the refused ask";
		ASSERT_NO_THROW( UnsubscribeAndDrain(productName, before) );
	}
	//418-watch-items #3:  a modify the server refuses reaches every listener of the node, with the status and what the item still
	//runs at, and the next ask tries again.  An absolute deadband on the server's ProductName, a String, which open62541 refuses
	//(checkAdjustMonitoredItemParams), asked twice is refused twice.  The merge was kept as asked before the answer, so the
	//refusal reached no one and the second Subscribe posted nothing.
	TEST_F( SubscribeTests, ARefusedModifyIsToldAndAskedAgain ){
		let before = _client->MonitoredNodes().Count();
		const NodeId productName{ 0, 2261 };//Server_ServerStatus_BuildInfo_ProductName
		auto watcher = ms<WatchListener>();
		FromServer::SubscriptionAck ack;
		ASSERT_NO_THROW( ack = Watch(productName, {}, watcher) );
		let sampling = ack.results(0).revised_sampling_interval();
		const MonitoringParameters refused{ .Filter=DataChangeFilter{.DeadbandType=UA_DEADBANDTYPE_ABSOLUTE, .DeadbandValue=1} };
		for( uint i=1; i<=2; ++i ){
			ASSERT_NO_THROW( Watch(productName, refused, watcher) ) << "the ack, which answers before the modify, said the item failed";
			Stopwatch sw;
			while( watcher->Modified.load()<i )
				ASSERT_NO_THROW( sw.CheckTimeout(5s, 1ms) ) << "refusal " << i << " never reached the listener";
			lg _{ watcher->Mutex };
			EXPECT_EQ( watcher->Status, UA_STATUSCODE_BADFILTERNOTALLOWED ) << "refusal " << i;
			EXPECT_EQ( watcher->RevisedSamplingInterval, sampling ) << "the item still runs at what it had";
		}
		EXPECT_EQ( _client->MonitoredNodes().Count(), before+1 ) << "a refused modify made or took an item";
		ASSERT_NO_THROW( UnsubscribeAndDrain(productName, before) );
	}

	//subscription-disconnect #5: a client dying with a create in flight resumed that subscribe with an ack of zero results for its
	//nodes - stashPending's TakeForResubscribe had wiped the request GetResult answers from.  The web client reads an empty list
	//as success, the test client registers nothing and says so only in a log line, and the soak throws.  The request now survives
	//the take, so the ack has a result per node.  And the result has to be true:  Good exactly when the node was restored-able -
	//its create completed before the take, which parked it - and a failure otherwise.  GetResult used to ack Good for any node it
	//had no error for, a failed create included.  ConnectionLost is posted to the client's strand right behind the create's
	//submission, so it normally runs before the server's answer is read; either order has to satisfy that rule.
	TEST_F( SubscribeTests, InFlightCreateFailsPerNodeOnClientLoss ){
		ASSERT_NO_THROW( BlockAwait(SubscribeAwait{_client}) );//a subscription to create on - without one the create fails before it is submitted.
		struct Result{ std::atomic<bool> Done; FromServer::SubscriptionAck Ack; };
		auto result = ms<Result>();
		auto listener = ms<PushCount>();
		[]( sp<Result> r, sp<IDataChange> listener, sp<UAClient> client, NodeId node )->TAwait<FromServer::SubscriptionAck>::Task{
			r->Ack = co_await DataChangeAwait{ {node}, move(listener), move(client) };
			r->Done = true;
		}( result, listener, _client, Intensity );//eager:  its submission is queued on the strand before the next line runs.
		_client->PostUA( [client=_client]{ UAClient::ConnectionLost( sp<UAClient>{client} ); } );
		Stopwatch sw;
		while( !result->Done )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 1ms) ) << "the in-flight create never resumed";
		ASSERT_EQ( result->Ack.results_size(), 1 ) << "the subscribe's ack has no result for its node";
		let status = result->Ack.results(0).status_code();
		let parked = UAClient::UnsubscribePending( OpcServerSlug, listener, {Intensity} );//also takes it back out, so nothing is restored for later tests.
		EXPECT_EQ( status==0, parked.size()==1 ) << "status " << std::hex << status << (status ? ": failed, yet parked for the reconnect" : ": acked Good, yet not parked - the node is not monitored anywhere");
		UAClient::Unsubscribe( listener );

		Reconnect();//the suite shares _client.
	}

	//subscription-disconnect #6, ordering A: a subscribe decides to reuse the cached subscription (SubscribeAwait finds one) and only
	//later, on the strand, registers its create.  A delete pass landing between the two - another listener's last item going, a
	//second after it was unsubscribed - deleted that subscription and cleared the cache, and the create failed every node with
	//BadInternalError.  The create now builds a subscription of its own and carries on.  Deleting the subscription right after
	//SubscribeAwait, the way the pass does, is that window pinned open.
	TEST_F( SubscribeTests, CreateSurvivesSubscriptionDeletedAfterReuse ){
		let before = _client->MonitoredNodes().Count();
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( BlockAwait(SubscribeAwait{_client}) );
		let subscriptionId = _client->SubscriptionId();
		ASSERT_TRUE( subscriptionId );
		ASSERT_NO_THROW( deleteSubscription(_client, subscriptionId) );//deleteSubscriptionCallback clears the cache, as DeleteMonitoring's pass does.
		ASSERT_FALSE( _client->SubscriptionId() );

		FromServer::SubscriptionAck ack;
		ASSERT_NO_THROW( ack = BlockAwait(DataChangeAwait{{Intensity}, listener, _client}) );
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
		const NodeId& good = Intensity; const NodeId unknown{ 4, 999'999 };
		let before = _client->MonitoredNodes().Count();
		auto listener = ms<PushCount>();
		FromServer::SubscriptionAck ack;
		ASSERT_NO_THROW( ack = SubscribeOn(_client, listener, {unknown, good}) );
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
	//Timing, not a pinned window:  each pass waits exactly a second, so pass 2 is armed 0.85s after pass 1 - before pass 1 removes
	//the item it needs, and late enough that the new subscription, a round trip of milliseconds since the loop stopped polling
	//(#526), is in place first.  A run that misses either condition skips rather than passing on a scenario it did not stage.
	TEST_F( SubscribeTests, EmptyDeletePassKeepsNewSubscription ){
		const DurationOverride delay{ _itemDeleteDelay, 1s };//the timings below are the shipped second's.
		auto listener = ms<PushCount>();
		auto& nodes = _client->MonitoredNodes();
		ASSERT_NO_THROW( SubscribeCounting(_client, listener, {Intensity}) );
		Stopwatch sw;
		let first = _client->SubscriptionId();

		(void)nodes.Unsubscribe( {Intensity}, listener );//pass 1, due in a second.
		std::this_thread::sleep_for( 850ms );
		if( _client->SubscriptionId()!=first )
			GTEST_SKIP() << "pass 1 fired before pass 2 could be armed";
		ASSERT_NO_THROW( BlockAwait(DataChangeAwait{{Intensity}, listener, _client}) );//back onto the same item - no create.
		let dropped = get<0>( nodes.Unsubscribe({Intensity}, listener) );//pass 2.
		let armed = steady_clock::now();
		if( dropped.size()!=1 )
			GTEST_SKIP() << "the item was gone before pass 2 could be armed";
		sw.Reset();
		while( _client->SubscriptionId() )//pass 1:  the item goes, and with it the subscription.
			ASSERT_NO_THROW( sw.CheckTimeout(2s, 1ms) );
		ASSERT_NO_THROW( BlockAwait(SubscribeAwait{_client}) );//a new one, before pass 2.
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
		std::this_thread::sleep_for( _reconnectDelay+2s );//a revival is the reconnect's first wait, then a connect, a subscribe, a create and an initial push - about 0.7 s.
		EXPECT_EQ( listener->Pushes.load(), pushes ) << "the removed client's listener was sent data again";
		for( let& client : UAClient::LiveClients() ){
			let nodes = client->TryMonitoredNodes();
			EXPECT_FALSE( client->Slug()==OpcServerSlug && nodes && nodes->Count() ) << "a client came back monitoring what the removed client monitored";
		}
	}
	TEST_F( SubscribeTests, RemoveClientForgetsSubscriptions ){
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( SubscribeCounting(_client, listener, {Intensity}) );
		let waiting = UAClient::ReconnectsWaiting();

		UAClient::RemoveClient( sp<UAClient>{_client} );
		EXPECT_EQ( UAClient::ReconnectsWaiting(), waiting ) << "removing the client started a reconnect";
		EXPECT_TRUE( UAClient::UnsubscribePending(OpcServerSlug, listener, {Intensity}).empty() ) << "removing the client parked what it monitored";
		expectNothingRevived( listener, listener->Pushes.load() );

		UAClient::Unsubscribe( listener );//nothing of it may linger for later tests, whatever the outcome above.
		Reconnect();//the suite shares _client.
	}
	TEST_F( SubscribeTests, RemoveClientDuringRebuildParksNothing ){
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( SubscribeCounting(_client, listener, {Intensity}) );
		let pushes = listener->Pushes.load();

		_client->Resubscribe();//the listener goes into a rebuild whose first step is a subscribe round trip...
		UAClient::RemoveClient( sp<UAClient>{_client} );//...so the removal lands inside it, and the rebuild finds its client gone.
		expectNothingRevived( listener, pushes );

		UAClient::Unsubscribe( listener );
		Reconnect();
	}

	//reviews/m2-closing.md #2, the status half.  A refused submission reaches RemoveIfDisconnected (UACε), which asked one
	//question - BadServerNotConnected? - and open62541 answers that only for a channel already down when the call starts.  One
	//that goes under the call comes back as BadConnectionClosed or BadSecureChannelClosed, and the client stayed registered and
	//`Connected` with its items on it.  Each now parks them and deregisters the client;  a refusal that says nothing about the
	//connection - the server's, or a local one - still leaves it alone.  On the strand, where UACε calls it.
	TEST_F( SubscribeTests, ARefusalThatMeansTheConnectionWentParks ){
		for( let refusal : {UA_STATUSCODE_BADCONNECTIONCLOSED, UA_STATUSCODE_BADSECURECHANNELCLOSED} ){
			auto listener = ms<PushCount>();
			ASSERT_NO_THROW( SubscribeCounting(_client, listener, {Intensity}) );//the initial push - the item is live from here.

			auto refuse = [client=_client]( StatusCode sc ){
				return BlockAwait( UAStrandAwait<bool>{client, [client, sc]{ UAClient::RemoveIfDisconnected( sc, client ); return client->IsConnected(); }} );
			};
			EXPECT_TRUE( refuse(UA_STATUSCODE_BADTOOMANYSUBSCRIPTIONS) ) << "the server's refusal took the client down";
			EXPECT_TRUE( refuse(UA_STATUSCODE_BADOUTOFMEMORY) ) << "a local refusal took the client down";
			EXPECT_FALSE( refuse(refusal) ) << std::hex << refusal << ": the client is still taken for connected";
			let parked = UAClient::UnsubscribePending( OpcServerSlug, listener, {Intensity} );//also takes it back out, so nothing is restored for later tests.
			EXPECT_EQ( parked.size(), 1u ) << std::hex << refusal << ": what the client monitored was not parked for the reconnect";
			UAClient::Unsubscribe( listener );

			Reconnect();//the suite shares _client.
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
		const flat_set<NodeId> nodes{ Intensity };
		constexpr uint rounds{ 25 };
		uint missed{};
		{
			auto bystander = ms<PushCount>();//monitors nothing:  its lookups only hold the lock.
			std::jthread contention{ [&]( std::stop_token stop ){ while( !stop.stop_requested() ) UAClient::UnsubscribePending( OpcServerSlug, bystander, nodes ); } };
			for( uint i=0; i<rounds; ++i ){
				auto listener = ms<PushCount>();
				ASSERT_NO_THROW( SubscribeOn(_client, listener, {Intensity}) );
				ASSERT_EQ( _client->MonitoredNodes().Count(), 1u ) << "round " << i << ": the node is not on the client, so there is nothing to park";

				std::atomic<bool> spinning{};
				uint parked{};
				std::jthread unsubscriber{ [&, client=_client]{
					spinning = true;
					while( client->IsConnected() ){}//step one:  not among the live clients...
					parked = UAClient::UnsubscribePending( OpcServerSlug, listener, nodes ).size();//...so, step two, it is parked.
				}};
				while( !spinning )
					std::this_thread::yield();
				UAClient::ConnectionLost( sp<UAClient>{_client} );
				unsubscriber.join();
				missed += parked ? 0 : 1;
				UAClient::Unsubscribe( listener );//a miss was parked behind the lookup - nothing of it may come back for later tests.
				Reconnect();//the suite shares _client.
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
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( BlockAwait(SubscribeAwait{_client}) );
		auto removed = _client;
		UAClient::RemoveClient( sp<UAClient>{_client} );
		std::this_thread::sleep_for( 1s );//past the disconnect, so the create below is refused rather than answered.
		ASSERT_TRUE( removed->SubscriptionId() ) << "no cached subscription - the create would not register, and there would be nothing to leak";

		FromServer::SubscriptionAck ack;
		ASSERT_NO_THROW( ack = BlockAwait(DataChangeAwait{{Intensity}, listener, removed}) );
		ASSERT_EQ( ack.results_size(), 1 );
		EXPECT_NE( ack.results(0).status_code(), 0u ) << "a create on a removed client succeeded";
		EXPECT_EQ( listener.use_count(), 1 ) << "the failed create's bookkeeping still holds its listener";

		removed = nullptr;
		Reconnect();//the suite shares _client.
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
		auto finished = []( const Result& r ){ return WaitUntil( [&]{ return r.Done.load(); }, 5s, 1ms ); };
		let first = subscribe( removed );
		ASSERT_TRUE( finished(*first) ) << "the refused subscribe never resumed";
		EXPECT_FALSE( first->Error.empty() ) << "a subscribe on a removed client succeeded";
		let second = subscribe( removed );
		EXPECT_TRUE( finished(*second) ) << "queued behind the first subscribe's refused create, which nothing will ever answer";
		if( second->Done )
			EXPECT_FALSE( second->Error.empty() ) << "a subscribe on a removed client succeeded";

		wp<UAClient> weak{ removed };
		removed = nullptr; _client = nullptr;//the fixture's is the other reference this test can see.
		EXPECT_TRUE( WaitUntil([&]{ return weak.expired(); }) ) << "the refused subscribe's queue entry still holds the client - " << weak.use_count() << " reference(s)";

		Reconnect();//the suite shares _client.
	}

	//subscription-disconnect #12: a delete pass that finds a create in flight keeps the subscription for it.  If that create then
	//failed for every node, the subscription was left with no items, so nothing would ever schedule the pass that retires it, and
	//SubscriptionRequestId stayed queued:  the processing loop never stopped and the client never idled out.  Now the pass stops
	//processing once nothing is monitored.  The create is for a node the server does not have, and the strand is held across the
	//pass, so the create is still in flight when the pass decides.
	TEST_F( SubscribeTests, KeptEmptySubscriptionStopsProcessing ){
		const DurationOverride delay{ _itemDeleteDelay, 1s };//the timings below are the shipped second's.
		const NodeId missing{ 4, 999'999 };//not on the server:  its create fails for every node.
		if( _client->MonitoredNodes().Count() )
			GTEST_SKIP() << "an earlier test left items monitored - the subscription would not end up empty";
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( SubscribeCounting(_client, listener, {Intensity}) );
		Stopwatch sw;
		let subscriptionId = _client->SubscriptionId();

		(void)_client->MonitoredNodes().Unsubscribe( {Intensity}, listener );//the pass, due in a second.
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
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( SubscribeCounting(_client, listener, {Intensity}) );
		let waiting = UAClient::ReconnectsWaiting();

		absl::Cleanup restoreTrust = []{ ServerTrust::OverrideTrustedCertDirs( nullopt ); };//whatever the assertions below do - the rest of the suite needs the server trusted.
		ServerTrust::OverrideTrustedCertDirs( vector<fs::path>{ fs::temp_directory_path()/"jde-reconnect-no-anchors" } );
		UAClient::ConnectionLost( sp<UAClient>{_client} );//parks the node; every reconnect from here is refused.

		//Given up means no chain waiting for a while:  the chain waits _reconnectDelay, then twice and four times it, between attempts
		//refused at once, so retrying forever it is waiting nearly all the time.
		let quiet = std::max<Duration>( 1s, 3*_reconnectDelay );
		auto quietSince = steady_clock::now();
		ASSERT_TRUE( WaitUntil([&]{
			if( UAClient::ReconnectsWaiting()!=waiting )
				quietSince = steady_clock::now();
			return steady_clock::now()-quietSince>=quiet;
		}, 30s) ) << "the reconnect chain is still retrying a certificate the gateway refuses every time";
		EXPECT_TRUE( UAClient::UnsubscribePending(OpcServerSlug, listener, {Intensity}).empty() ) << "the chain stopped but left the node parked";
		let errors = UAClient::ConnectErrors();
		let error = errors.find( OpcServerSlug );
		EXPECT_TRUE( error!=errors.end() && error->second.find("server certificate")!=string::npos ) << "the attempts did not fail on the certificate - the chain ended for some other reason";

		UAClient::Unsubscribe( listener );
		ServerTrust::OverrideTrustedCertDirs( nullopt );
		Reconnect();//the suite shares _client.
	}

	//subscription-disconnect #15: MonitoredItemsRequest only finds nodes already monitored, so two creates for one node in flight at
	//once - a rebuild restoring it while its session subscribes again - made two monitored items:  every change pushed twice, and an
	//unsubscribe by node removed only one.  The second create to answer now joins the first's item and deletes its own.  Two creates
	//queued back to back on the strand are both submitted before either answer can be read.
	TEST_F( SubscribeTests, ConcurrentCreatesShareOneItem ){
		if( _client->MonitoredNodes().Count() )
			GTEST_SKIP() << "an earlier test left items monitored";
		ASSERT_NO_THROW( BlockAwait(SubscribeAwait{_client}) );
		auto listener = ms<PushCount>();
		struct Result{ std::atomic<bool> Done; FromServer::SubscriptionAck Ack; };
		auto create = []( sp<Result> r, sp<IDataChange> l, sp<UAClient> c, NodeId n )->TAwait<FromServer::SubscriptionAck>::Task{
			r->Ack = co_await DataChangeAwait{ {n}, move(l), move(c) };
			r->Done = true;
		};
		auto first = ms<Result>(), second = ms<Result>();
		create( first, listener, _client, Intensity );//eager:  each queues its submission on the strand before the next line runs.
		create( second, listener, _client, Intensity );
		Stopwatch sw;
		while( !first->Done || !second->Done )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 1ms) );
		for( let& r : {first, second} ){
			ASSERT_EQ( r->Ack.results_size(), 1 );
			EXPECT_EQ( r->Ack.results(0).status_code(), 0u );
		}
		EXPECT_EQ( _client->MonitoredNodes().Count(), 1u ) << "two monitored items for one node";

		(void)_client->MonitoredNodes().Unsubscribe( {Intensity}, listener );//one unsubscribe by node has to leave nothing.
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
		if( _client->MonitoredNodes().Count() )
			GTEST_SKIP() << "an earlier test left items monitored";
		ASSERT_NO_THROW( BlockAwait(SubscribeAwait{_client}) );
		auto listener = ms<PushCount>();
		HeldCreate create{ _client, Intensity, listener };
		if( _client->MonitoredNodes().Count() )
			GTEST_SKIP() << "the create answered before the strand was held - nothing was in flight to unsubscribe";

		auto [dropped, failed] = _client->MonitoredNodes().Unsubscribe( {Intensity}, listener );
		EXPECT_EQ( dropped.size(), 1u ) << "the node's create was out, and the unsubscribe found it nowhere";
		EXPECT_TRUE( failed.empty() );
		auto [again, repeated] = _client->MonitoredNodes().Unsubscribe( {Intensity}, listener );//nothing left to drop - as for an item.
		EXPECT_TRUE( again.empty() );

		FromServer::SubscriptionAck ack;
		ASSERT_NO_THROW( ack = create.Ack() );
		ASSERT_EQ( ack.results_size(), 1 );
		EXPECT_EQ( ack.results(0).status_code(), UA_STATUSCODE_BADREQUESTCANCELLEDBYCLIENT ) << "the subscribe's ack still says the node is subscribed";
		std::this_thread::sleep_for( 1500ms );//an item the answer attached would have pushed its initial value by now.
		EXPECT_EQ( listener->Pushes.load(), 0u ) << "the unsubscribed listener is being sent data changes";
		EXPECT_EQ( _client->MonitoredNodes().Count(), 0u ) << "the answer attached the node its listener had unsubscribed";
		EXPECT_EQ( listener.use_count(), 1 ) << "the client still holds the listener";
		(void)_client->MonitoredNodes().Unsubscribe( {Intensity}, listener );//nothing of it may linger for later tests, whatever the outcome above.
	}

	//The close path's unsubscribe, a listener whole, inside the same round trip:  it erased the create's call, so the answer found
	//none, the subscribe was acked with no results, and the item the server made was never deleted (historian-groups-templates #3).
	TEST_F( SubscribeTests, CloseReachesACreateStillOut ){
		if( _client->MonitoredNodes().Count() )
			GTEST_SKIP() << "an earlier test left items monitored";
		ASSERT_NO_THROW( BlockAwait(SubscribeAwait{_client}) );
		auto listener = ms<PushCount>();
		HeldCreate create{ _client, Intensity, listener };
		if( _client->MonitoredNodes().Count() )
			GTEST_SKIP() << "the create answered before the strand was held - nothing was in flight to unsubscribe";

		_client->MonitoredNodes().Unsubscribe( listener );

		FromServer::SubscriptionAck ack;
		ASSERT_NO_THROW( ack = create.Ack() );
		ASSERT_EQ( ack.results_size(), 1 ) << "the answer found no call:  the item it made is left on the server";
		EXPECT_EQ( ack.results(0).status_code(), UA_STATUSCODE_BADREQUESTCANCELLEDBYCLIENT );
		std::this_thread::sleep_for( 1500ms );//an item the answer attached would have pushed its initial value by now.
		EXPECT_EQ( listener->Pushes.load(), 0u ) << "the closed listener is being sent data changes";
		EXPECT_EQ( _client->MonitoredNodes().Count(), 0u ) << "the answer attached the node its listener had closed";
		EXPECT_EQ( listener.use_count(), 1 ) << "the client still holds the listener";
	}
	//418-watch-items #7:  a ParametersChanged while the listener's create is out reaches the item the answer makes.  The create asks
	//1 s and is held unread;  the listener changes to 250 ms and says so;  the answer's item is modified to 250, which the listener
	//hears.  ParametersChanged skipped a node with no item yet, so the item kept the 1 s the create sent.
	TEST_F( SubscribeTests, AnAskChangedWhileTheCreateIsOutReachesTheItem ){
		if( _client->MonitoredNodes().Count() )
			GTEST_SKIP() << "an earlier test left items monitored";
		ASSERT_NO_THROW( BlockAwait(SubscribeAwait{_client}) );
		auto listener = ms<StateListener>();
		listener->Parameters = { .SamplingInterval=1000.0 };
		HeldCreate create{ _client, Intensity, listener };
		if( _client->MonitoredNodes().Count() )
			GTEST_SKIP() << "the create answered before the strand was held - nothing was in flight to change";
		listener->Parameters = { .SamplingInterval=250.0 };
		_client->MonitoredNodes().ParametersChanged( listener, {Intensity} );
		FromServer::SubscriptionAck ack;
		ASSERT_NO_THROW( ack = create.Ack() );
		ASSERT_EQ( ack.results_size(), 1 );
		EXPECT_EQ( ack.results(0).revised_sampling_interval(), 1000.0 ) << "the ack is the create's, sent before the change";
		Stopwatch sw;
		while( listener->Modified.load()<1 )
			ASSERT_NO_THROW( sw.CheckTimeout(5s, 1ms) ) << "the change never reached the item";
		EXPECT_EQ( listener->RevisedSamplingInterval.load(), 250.0 );
		drainListener( _client, listener, 0 );
	}

	//And a rebuild's create, where both of the by-node unsubscribe's steps now find the node:  the create drops it, and the rebuild
	//has to forget it as well (GatewaySocketSession::Unsubscribe asks _pending/_rebuilds for every node, not just what is left) -
	//or it counts the node as refused, and parks it again should the client die.  The rebuild goes straight to its create, as in
	//ResubscribesWhenTheRebuildsCreateIsRefused, and the strand is held behind it.
	TEST_F( SubscribeTests, UnsubscribeReachesARebuildsCreateStillOut ){
		if( _client->MonitoredNodes().Count() )
			GTEST_SKIP() << "an earlier test left items monitored";
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( SubscribeCounting(_client, listener, {Intensity}) );//the initial push - the item is live from here.
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
			Reconnect();//the suite shares _client.
		};
		if( _client->MonitoredNodes().Count() ){
			restoreClient();
			GTEST_SKIP() << "the rebuild's create answered before the strand was held - nothing was in flight to unsubscribe";
		}

		auto [dropped, failed] = _client->MonitoredNodes().Unsubscribe( {Intensity}, listener );//GatewaySocketSession::Unsubscribe's first step...
		let forgotten = UAClient::UnsubscribePending( OpcServerSlug, listener, {Intensity} );//...and its second.
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
		let before = _client->MonitoredNodes().Count();
		auto session = GatewayClientSocket::Open( "localhost", GatewayPort(), AppClient()->SessionId() );
		BlockAwait<ClientSocketAwait<FromServer::SubscriptionAck>,FromServer::SubscriptionAck>( session->Subscribe(OpcServerSlug, {Intensity}, _listener) );
		ASSERT_EQ( _client->MonitoredNodes().Count(), before+1 );

		BlockAwait( session->Close(false, SRCE_CUR) );
		Stopwatch sw;//the item goes away _itemDeleteDelay after the close, so poll rather than fixed-sleep.
		while( _client->MonitoredNodes().Count()!=before )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 1ms) );
	}

	//reviews/m2-closing.md #15 - the harness's own leak, and the soak is scored by this harness.  The test client registers a
	//subscribe's record before it writes the request, and only the ack ever took it out:  a request that failed - answered
	//with an error, or on a socket that had gone - left its record, listener and all, for the life of the process, and
	//subscription-disconnect #7's per-cycle re-subscribe makes that one a second for the length of an outage.  Both ways a
	//request fails, and the unsubscribe's record with the subscribe's.
	TEST_F( SubscribeTests, AFailedRequestLeavesNoRecord ){
		let before = Socket().PendingSubscriptionRecords();
		//lambdas:  the awaits' template commas do not survive a gtest macro.
		auto subscribe = [&]( GatewayClientSocket& socket, ServerCnnctnNK slug ){ BlockAwait<ClientSocketAwait<FromServer::SubscriptionAck>,FromServer::SubscriptionAck>( socket.Subscribe(move(slug), {Intensity}, _listener) ); };
		auto unsubscribe = [&]( GatewayClientSocket& socket ){ BlockAwait<ClientSocketAwait<FromServer::UnsubscribeAck>,FromServer::UnsubscribeAck>( socket.Unsubscribe(OpcServerSlug, {Intensity}) ); };

		//answered:  the gateway has no such connection, and says so over a working socket.
		EXPECT_THROW( subscribe(Socket(), "m2Closing15NoSuchConnection"), GatewayErrorResponse );
		EXPECT_EQ( Socket().PendingSubscriptionRecords(), before ) << "a subscribe the gateway answered with an error left its record";

		//a dead socket:  the write finds the stream gone and every pending task is failed - CloseTasks, which is also where
		//a timeout and a close mid-request end up.
		auto session = GatewayClientSocket::Open( "localhost", GatewayPort(), AppClient()->SessionId() );
		BlockAwait( session->Close(false, SRCE_CUR) );
		EXPECT_ANY_THROW( subscribe(*session, string{OpcServerSlug}) );
		EXPECT_EQ( session->PendingSubscriptionRecords(), 0u ) << "a subscribe on a dead socket left its record";
		EXPECT_ANY_THROW( unsubscribe(*session) );
		EXPECT_EQ( session->PendingSubscriptionRecords(), 0u ) << "an unsubscribe on a dead socket left its record";
	}

	//Sessions racing to connect to the same slug coalesce in ConnectAwait::_requests, and a Create() failure fans the
	//one exception out to all of them.  The fan-out "cloned" with e.Move(), which moves the payload *out of* e, so every
	//waiter after the first - the last, which takes e itself, included - got the format string with its arguments gone
	//("Could not find connection:  '{}'") instead of the real message (review3 #5).
	//NB this only bites when Create() actually suspends before it throws, i.e. when ServerCnnctnAwait's SelectAsync is
	//async.  Under the ctest sqlite config it completes inline, Create runs to completion inside the first Suspend(), and
	//the fan-out only ever sees one handle - so a green run here is not by itself evidence of coverage.  Verified against
	//the bug by forcing a suspension into Create (a 200ms DurationTimer), which makes the fan-out see all three.
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
			TRACE( "waiter {}: '{}'", i, results->What[i] );
		for( uint i=0; i<count; ++i ){
			ASSERT_FALSE( results->What[i].empty() ) << "waiter " << i << " got a message-less exception.";
			ASSERT_EQ( results->What[i], results->What[0] ) << "waiter " << i << " got a different exception than waiter 0.";
		}
	}

	//Events (clients spec *Events*, #424):  a socket listener collecting each EventValues, and a listener on the client itself answering a
	//select list of its own, as the session does from a SubscribeEvents - each event its fields as Variants, null for one the item
	//did not deliver - and counting the item's modifies, which a join of another select posts.
	struct EventListener final : IListener{
		α OnData( string, NodeId, const Proto::Value& )ι->void override{}
		α OnEvent( const FromServer::EventValues& event )ι->void override{ lg _{ Mutex }; Events.push_back( event ); ++Count; }
		α OnEventsRestored()ι->void override{ ++Restored; }
		α Last()ι->FromServer::EventValues{ lg _{ Mutex }; return Events.back(); }
		atomic<uint> Count{};
		atomic<uint> Restored{};
		mutex Mutex; vector<FromServer::EventValues> Events;
	};
	struct EventSelectListener final : IDataChange{
		EventSelectListener( vector<string> select, uint32 queueSize=100 )ι:Select{ move(select) }, QueueSize{ queueSize }{}
		α SendDataChange( const ServerCnnctnNK&, const NodeId&, const Value&, const ITypes* )ι->void override{}
		α SendEvent( const ServerCnnctnNK&, const NodeId&, const vector<const UA_Variant*>& fields, const ITypes* )ι->void override{
			vector<optional<Variant>> copy;
			for( let field : fields )
				copy.push_back( field ? optional<Variant>{Variant{*field}} : nullopt );
			lg _{ Mutex };
			Events.push_back( move(copy) );
			++Count;
		}
		α ItemParameters( const ServerCnnctnNK&, const NodeId&, Duration )Ι->MonitoringParameters override{ return MonitoringParameters::ForEvents( EventFilter::Of(Select), QueueSize ); }
		α ItemModified( const ServerCnnctnNK&, const NodeId&, const MonitoredItemModifyResult& result, const optional<AskReverted>& )ι->void override{ ModifyStatus = result.Status; ++Modified; }
		α to_string()Ι->string override{ return "Tests.EventSelectListener"; }
		α Last()ι->vector<optional<Variant>>{ lg _{ Mutex }; return Events.back(); }
		vector<string> Select;
		uint32 QueueSize;
		atomic<uint> Count{}, Modified{};
		atomic<StatusCode> ModifyStatus{};
		mutex Mutex; vector<vector<optional<Variant>>> Events;
	};
	//pumpManual.raiseEvent through `call` (Pumps.h RaiseEvent):  the EventId the server answered, as its json spells a ByteString, hex.
	Ω raiseEvent( sv message, uint16 severity )ε->string{
		const NodeId object{ PumpsNs(), PumpManual }, method{ PumpsNs(), RaiseEvent };
		const jobject vars{ {"opc", OpcServerSlug}, {"object", object.ToJson()}, {"method", method.ToJson()}, {"args", jarray{message, severity}} };
		let y = Socket().QuerySync( "call( opc: $opc, object: $object, method: $method, args: $args ){ statusCode outputArguments }", vars ).as_object();
		THROW_IF( y.at("statusCode").to_number<StatusCode>(), "raiseEvent answered {}", serialize(y) );
		return string{ y.at("outputArguments").as_array().at(0).as_string() };
	}
	Ω subscribeEvents( const NodeId& node, const vector<string>& select, sp<IListener> listener )ε->FromServer::SubscriptionAck{
		return BlockAwait<ClientSocketAwait<FromServer::SubscriptionAck>,FromServer::SubscriptionAck>( Socket().SubscribeEvents(OpcServerSlug, node, select, move(listener)) );
	}
	Ω unsubscribeEvents( const NodeId& node )ε->FromServer::UnsubscribeAck{
		return BlockAwait<ClientSocketAwait<FromServer::UnsubscribeAck>,FromServer::UnsubscribeAck>( Socket().UnsubscribeEvents(OpcServerSlug, node) );
	}
	Ω text( const UA_Variant& v )ι->string{ return UA_Variant_hasScalarType( &v, &UA_TYPES[UA_TYPES_LOCALIZEDTEXT] ) ? ToString( static_cast<const UA_LocalizedText*>(v.data)->text ) : string{}; }

	//An event the OpcServer's method raises reaches the socket's subscription with the eight base fields and the one selected, in
	//that order:  the EventId the call answered, BaseEventType, pumpManual as the source with its DisplayName, both times, the message
	//and severity given, and the pump's rpm.
	TEST_F( SubscribeTests, AnEventReachesItsSubscriptionWithItsFields ){
		if( !Server::FindUAServer() )
			GTEST_SKIP() << "Needs the embedded OpcServer.";
		const NodeId pump{ PumpsNs(), PumpManual };
		auto listener = ms<EventListener>();
		FromServer::SubscriptionAck ack;
		ASSERT_NO_THROW( ack = subscribeEvents(pump, {"Rpm"}, listener) );
		ASSERT_EQ( ack.results_size(), 1 );
		ASSERT_EQ( ack.results(0).status_code(), UA_STATUSCODE_GOOD ) << UAException::Message( ack.results(0).status_code() );
		let rpm = ReadValue( _client, NodeId{PumpsNs(), RpmManual} ).AsNumber<double>();
		string eventId;
		ASSERT_NO_THROW( eventId = raiseEvent("hello", 500) );
		Stopwatch sw;
		while( !listener->Count )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 1ms) ) << "the event never reached the subscription";
		let event = listener->Last();
		EXPECT_EQ( event.opc_id(), OpcServerSlug );
		EXPECT_EQ( ProtoUtils::ToNodeId(event.node()), pump );
		ASSERT_EQ( event.fields_size(), 9 ) << "the eight base fields and Rpm";
		EXPECT_EQ( Str::ToHex(event.fields(0).byte_string()), eventId );
		EXPECT_EQ( ProtoUtils::ToNodeId(event.fields(1).node()), NodeId{UA_NS0ID_BASEEVENTTYPE} );
		EXPECT_EQ( ProtoUtils::ToNodeId(event.fields(2).node()), pump );
		EXPECT_EQ( event.fields(3).string_value(), "Pump (Manual)" );//SourceName is a String, the source's DisplayName text
		EXPECT_TRUE( event.fields(4).has_date() ) << "Time";
		EXPECT_TRUE( event.fields(5).has_date() ) << "ReceiveTime";
		EXPECT_EQ( event.fields(6).localized_text().text(), "hello" );
		EXPECT_EQ( event.fields(7).uint16(), 500u );
		EXPECT_EQ( event.fields(8).double_value(), rpm );
		FromServer::UnsubscribeAck end;
		ASSERT_NO_THROW( end = unsubscribeEvents(pump) );
		EXPECT_EQ( end.successes_size(), 1 ) << "the node was not this session's";
	}
	//Two listeners on one node's item with different lists:  the item's clauses are their union, the second's join modifying it in
	//place, and each is pushed its own list - eight fields for one, nine with the rpm for the other.  One item for the two, and gone
	//with the last.
	TEST_F( SubscribeTests, TwoEventListenersEachGetTheirOwnSelect ){
		if( !Server::FindUAServer() )
			GTEST_SKIP() << "Needs the embedded OpcServer.";
		const NodeId pump{ PumpsNs(), PumpManual };
		auto base = ms<EventSelectListener>( vector<string>{} ), rpmToo = ms<EventSelectListener>( vector<string>{"Rpm"} );
		FromServer::SubscriptionAck ack;
		ASSERT_NO_THROW( ack = SubscribeOn(_client, base, {pump}) );
		ASSERT_EQ( ack.results(0).status_code(), UA_STATUSCODE_GOOD ) << UAException::Message( ack.results(0).status_code() );
		ASSERT_NO_THROW( ack = SubscribeOn(_client, rpmToo, {pump}) );
		ASSERT_EQ( ack.results(0).status_code(), UA_STATUSCODE_GOOD ) << UAException::Message( ack.results(0).status_code() );
		EXPECT_EQ( _client->MonitoredNodes().Handles({pump}).size(), 1u ) << "one item per node";//Count would catch the test before's item, gone a second after its unsubscribe
		Stopwatch sw;
		while( !rpmToo->Modified )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 1ms) ) << "the join's modify never answered";
		EXPECT_EQ( rpmToo->ModifyStatus.load(), UA_STATUSCODE_GOOD ) << "the server refused the union's filter:  " << UAException::Message( rpmToo->ModifyStatus.load() );
		EXPECT_GE( base->Modified.load(), 1u ) << "every listener of the item hears its modify";//at least the join's:  the test before's item, a second from its delete, may have taken the first listener's join as a modify too
		let rpm = ReadValue( _client, NodeId{PumpsNs(), RpmManual} ).AsNumber<double>();
		ASSERT_NO_THROW( raiseEvent("union", 200) );
		sw.Reset();
		while( !base->Count || !rpmToo->Count )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 1ms) ) << "the event never reached both listeners";
		let eight = base->Last();
		ASSERT_EQ( eight.size(), 8u );
		ASSERT_TRUE( eight[6] );
		EXPECT_EQ( text(*eight[6]), "union" );
		let nine = rpmToo->Last();
		ASSERT_EQ( nine.size(), 9u );
		ASSERT_TRUE( nine[6] );
		EXPECT_EQ( text(*nine[6]), "union" );
		ASSERT_TRUE( nine[8] ) << "the selected field was not delivered";
		ASSERT_TRUE( UA_Variant_hasScalarType(&*nine[8], &UA_TYPES[UA_TYPES_DOUBLE]) );
		EXPECT_EQ( *static_cast<const double*>(nine[8]->data), rpm );
		UAClient::Unsubscribe( rpmToo );
		UAClient::Unsubscribe( base );
		sw.Reset();
		while( _client->MonitoredNodes().Handles({pump}).size() )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 1ms) ) << "the event item never went";
	}
	//An item's queue overflowing puts an EventQueueOverflowEventType event in the stream, with the listener's whole field list:  how the
	//Alarms view learns its list may be incomplete (426-alarms-view #6).  Enough events for some publishing interval to hold more than two.
	TEST_F( SubscribeTests, AnOverflowedEventQueueSaysSo ){
		if( !Server::FindUAServer() )
			GTEST_SKIP() << "Needs the embedded OpcServer.";
		const NodeId pump{ PumpsNs(), PumpManual };
		Stopwatch sw;
		while( _client->MonitoredNodes().Handles({pump}).size() )//the merge would keep the test before's larger queue
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 1ms) ) << "the test before's item never went";
		auto listener = ms<EventSelectListener>( vector<string>{"i=2782#NodeId"}, 2 );
		FromServer::SubscriptionAck ack;
		ASSERT_NO_THROW( ack = SubscribeOn(_client, listener, {pump}) );
		ASSERT_EQ( ack.results(0).status_code(), UA_STATUSCODE_GOOD ) << UAException::Message( ack.results(0).status_code() );
		let overflowed = [&]{
			auto type = UA_NODEID_NUMERIC( 0, UA_NS0ID_EVENTQUEUEOVERFLOWEVENTTYPE );
			lg _{ listener->Mutex };
			return std::ranges::any_of( listener->Events, [&]( let& fields ){
				return fields.size()==9 && fields[1] && UA_Variant_hasScalarType( &*fields[1], &UA_TYPES[UA_TYPES_NODEID] ) && UA_NodeId_equal( static_cast<const UA_NodeId*>(fields[1]->data), &type );
			} );
		};
		for( uint i=0; i<40 && !overflowed(); ++i )
			ASSERT_NO_THROW( raiseEvent("burst", 200) );
		sw.Reset();
		while( !overflowed() )
			ASSERT_NO_THROW( sw.CheckTimeout(5s, 1ms) ) << "no EventQueueOverflowEventType event in " << listener->Count.load() << " events";
		UAClient::Unsubscribe( listener );
		sw.Reset();
		while( _client->MonitoredNodes().Handles({pump}).size() )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 1ms) ) << "the event item never went";
	}
	//An event item lost with its client is parked and put back on the reconnect's client as an event item - the rebuild asks the
	//listener's kind - and delivers again.
	TEST_F( SubscribeTests, AnEventItemSurvivesTheReconnect ){
		if( !Server::FindUAServer() )
			GTEST_SKIP() << "Needs the embedded OpcServer.";
		const NodeId pump{ PumpsNs(), PumpManual };
		auto listener = ms<EventListener>();
		FromServer::SubscriptionAck ack;
		ASSERT_NO_THROW( ack = subscribeEvents(pump, {"Rpm"}, listener) );
		ASSERT_EQ( ack.results(0).status_code(), UA_STATUSCODE_GOOD ) << UAException::Message( ack.results(0).status_code() );
		let lost = _client->Handle();
		UAClient::ConnectionLost( sp<UAClient>{_client} );
		let revived = WaitRevived( lost, OpcServerSlug );//the reconnect's first wait, then a connect, subscribe and create.
		ASSERT_TRUE( revived ) << "no replacement client came up with the event item restored";
		_client = revived;//the suite shares it: later tests must not reach for the deregistered one.
		ASSERT_NO_THROW( raiseEvent("again", 300) );
		Stopwatch sw;
		while( !listener->Count )
			ASSERT_NO_THROW( sw.CheckTimeout(10s, 1ms) ) << "no event after the client was replaced";
		let event = listener->Last();
		ASSERT_EQ( event.fields_size(), 9 ) << "the rebuilt item lost the listener's list";
		EXPECT_EQ( event.fields(6).localized_text().text(), "again" );
		EXPECT_EQ( event.fields(7).uint16(), 300u );
		FromServer::UnsubscribeAck end;
		ASSERT_NO_THROW( end = unsubscribeEvents(pump) );
		EXPECT_EQ( end.successes_size(), 1 );
	}
	//historyEvents against the OpcServer, which keeps no event history:  the node's status says so, Bad_HistoryOperationUnsupported, with
	//no events and no continuation - an answer, not a refusal of the read.
	TEST_F( SubscribeTests, HistoryEventsAgainstTheOpcServerIsUnsupported ){
		if( !Server::FindUAServer() )
			GTEST_SKIP() << "Needs the embedded OpcServer.";
		let now = Clock::now();
		const jobject vars{ {"opc", OpcServerSlug}, {"node", NodeId{UA_NS0ID_SERVER}.ToJson()}, {"start", UADateTime{now-1h}.ToJson()}, {"end", UADateTime{now}.ToJson()}, {"select", jarray{"Rpm"}} };
		let y = Socket().QuerySync( "historyEvents( opc: $opc, node: $node, start: $start, end: $end, select: $select ){ status continuation events{ fields } }", vars ).as_object();
		EXPECT_EQ( y.at("status").to_number<StatusCode>(), UA_STATUSCODE_BADHISTORYOPERATIONUNSUPPORTED ) << serialize( y );
		EXPECT_TRUE( y.at("events").as_array().empty() );
		EXPECT_TRUE( y.at("continuation").is_null() );
	}
	//A server with no history service, as an OpcServer against a hub is, refuses the whole read:  the status says so, as above (424-clients-events W1).
	TEST_F( SubscribeTests, HistoryEventsAgainstAServerWithNoHistoryServiceIsUnsupported ){
		if( !Server::FindUAServer() )
			GTEST_SKIP() << "Needs the embedded OpcServer.";
		auto& database = UA_Server_getConfig( Server::FindUAServer()->Ptr() )->historyDatabase;
		let context = database.context;
		database.context = nullptr;//what the service tests for
		let now = Clock::now();
		const jobject vars{ {"opc", OpcServerSlug}, {"node", NodeId{UA_NS0ID_SERVER}.ToJson()}, {"start", UADateTime{now-1h}.ToJson()}, {"end", UADateTime{now}.ToJson()}, {"select", jarray{}} };
		jobject y;
		EXPECT_NO_THROW( y = Socket().QuerySync("historyEvents( opc: $opc, node: $node, start: $start, end: $end, select: $select ){ status continuation events{ fields } }", vars).as_object() );
		database.context = context;
		ASSERT_TRUE( y.contains("status") ) << serialize( y );
		EXPECT_EQ( y.at("status").to_number<StatusCode>(), UA_STATUSCODE_BADNOTSUPPORTED ) << serialize( y );
		EXPECT_TRUE( y.at("events").as_array().empty() );
		EXPECT_TRUE( y.at("continuation").is_null() );
	}
	//A select path the gateway cannot read refuses the SubscribeEvents before any server round trip, and a data-change ask for a
	//node whose item is an event item is refused in the ack, Bad_FilterNotAllowed, the item standing.
	TEST_F( SubscribeTests, AnEventSubscriptionRefusesABadFieldAndTheOtherKind ){
		if( !Server::FindUAServer() )
			GTEST_SKIP() << "Needs the embedded OpcServer.";
		const NodeId server{ UA_NS0ID_SERVER };
		auto listener = ms<EventListener>();
		EXPECT_THROW( subscribeEvents(server, {"70000:x"}, listener), GatewayErrorResponse ) << "a namespace index no UInt16 holds";
		EXPECT_THROW( subscribeEvents(server, {"Message/"}, listener), GatewayErrorResponse ) << "an empty segment";
		FromServer::SubscriptionAck ack;
		ASSERT_NO_THROW( ack = subscribeEvents(server, {}, listener) );
		ASSERT_EQ( ack.results(0).status_code(), UA_STATUSCODE_GOOD ) << UAException::Message( ack.results(0).status_code() );
		auto dataListener = ms<EventListener>();//its OnData takes a push with no value, which the suite's own listener asserts on.
		try{//the socket fails the await with the one node's status (GatewayClientSocket::onSubscriptionAck)
			BlockAwait<ClientSocketAwait<FromServer::SubscriptionAck>,FromServer::SubscriptionAck>( Socket().Subscribe(OpcServerSlug, {server}, dataListener) );
			FAIL() << "the data-change ask for the event item's node was not refused";
		}
		catch( const UAException& e ){
			EXPECT_EQ( (StatusCode)e.Code(), UA_STATUSCODE_BADFILTERNOTALLOWED ) << e.what();
		}
		FromServer::UnsubscribeAck end;
		ASSERT_NO_THROW( end = unsubscribeEvents(server) );
		EXPECT_EQ( end.successes_size(), 1 ) << "the event item did not survive the refused join";
	}

	//A rebuild tells a session which event items came back, so a view keeping state from their events can start over (426-alarms-view #1);
	//a data item has nothing to start over.
	TEST_F( SubscribeTests, ARebuildSaysItsEventItemsAreBack ){
		if( !Server::FindUAServer() )
			GTEST_SKIP() << "Needs the embedded OpcServer.";
		const NodeId server{ UA_NS0ID_SERVER };
		auto events = ms<EventListener>(), data = ms<EventListener>();
		FromServer::SubscriptionAck ack;
		ASSERT_NO_THROW( ack = subscribeEvents(server, {}, events) );
		ASSERT_EQ( ack.results(0).status_code(), UA_STATUSCODE_GOOD ) << UAException::Message( ack.results(0).status_code() );
		ASSERT_NO_THROW( (BlockAwait<ClientSocketAwait<FromServer::SubscriptionAck>,FromServer::SubscriptionAck>( Socket().Subscribe(OpcServerSlug, {Intensity}, data) )) );

		let subscriptionId = _client->SubscriptionId();
		ASSERT_TRUE( subscriptionId );
		ASSERT_NO_THROW( deleteSubscription(_client, subscriptionId) );
		_client->Resubscribe();
		ASSERT_TRUE( WaitUntil([&]{ return events->Restored.load()==1; }, 10s) ) << "no EventsRestored for the event item";
		EXPECT_EQ( data->Restored.load(), 0u ) << "a data item is not said to be restored";

		FromServer::UnsubscribeAck end;
		ASSERT_NO_THROW( end = unsubscribeEvents(server) );
		EXPECT_EQ( end.successes_size(), 1 );
		ASSERT_NO_THROW( (BlockAwait<ClientSocketAwait<FromServer::UnsubscribeAck>,FromServer::UnsubscribeAck>( Socket().Unsubscribe(OpcServerSlug, {Intensity}) )) );
	}
	//A browse name may hold a `#` or an `=`, and only a node id with a path or attribute after it, or a leading `/` with an attribute,
	//is the operand's spelling (426-alarms-view #5).
	TEST( EventClauseTests, ANameHoldingOperandCharactersStaysAName ){
		for( sv name : {"2:Flow#1"sv, "Flow#1"sv, "Mode=Auto"sv, "s=Foo"sv, "#Value"sv} ){
			let clause = EventClause::Parse( name );
			ASSERT_EQ( clause.Path.size(), 1u ) << name;
			EXPECT_EQ( clause.TypeDefinition, NodeId{UA_NS0ID_BASEEVENTTYPE} ) << name;
			EXPECT_EQ( clause.Attribute, UA_ATTRIBUTEID_VALUE ) << name;
		}
		EXPECT_EQ( ToSV(EventClause::Parse("2:Flow#1").Path[0].name), "Flow#1" );
		EXPECT_EQ( EventClause::Parse("2:Flow#1").Path[0].namespaceIndex, 2 );

		let condition = EventClause::Parse( "i=2782#NodeId" );
		EXPECT_TRUE( condition.Path.empty() );
		EXPECT_EQ( condition.TypeDefinition, NodeId{UA_NS0ID_CONDITIONTYPE} );
		EXPECT_EQ( condition.Attribute, UA_ATTRIBUTEID_NODEID );
		let path = EventClause::Parse( "ns=2;s=Truck/3:Wheel" );
		ASSERT_EQ( path.Path.size(), 1u );
		EXPECT_EQ( ToSV(path.Path[0].name), "Wheel" );
		EXPECT_EQ( *path.TypeDefinition.String(), "Truck" );
		let attribute = EventClause::Parse( "/2:Foo#DisplayName" );
		ASSERT_EQ( attribute.Path.size(), 1u );
		EXPECT_EQ( attribute.Attribute, UA_ATTRIBUTEID_DISPLAYNAME );
		EXPECT_THROW( EventClause::Parse("/2:Foo"), Exception ) << "an empty first segment, as before";
	}

	//A run of events on one tick spans pages (424-clients-events #7):  each page's continuation counts what was returned there.
	TEST( EventsCursorTests, ARunOnOneTickReadsOnAcrossPages ){
		const UA_DateTime tick{ 1000 }, later{ 2000 };
		EventsCursor first;
		EXPECT_FALSE( first.Passes(tick) );
		EXPECT_FALSE( first.Passes(tick) );
		ASSERT_EQ( first.Next(), "1000/2" );

		auto second = EventsCursor::Parse( *first.Next() );
		ASSERT_TRUE( second );
		EXPECT_EQ( second->Start(), tick );
		EXPECT_TRUE( second->Skipping() );
		EXPECT_TRUE( second->Passes(tick) );
		EXPECT_TRUE( second->Passes(tick) );
		EXPECT_FALSE( second->Skipping() );
		EXPECT_FALSE( second->Passes(tick) );//the third, which neither page before returned
		EXPECT_EQ( second->Next(), "1000/3" );

		EXPECT_FALSE( second->Passes(later) );
		EXPECT_EQ( second->Next(), "2000/1" );
	}
	TEST( EventsCursorTests, ACursorWithNoTimedEventHasNoNext ){
		EXPECT_FALSE( EventsCursor{}.Next() );
	}
	TEST( EventsCursorTests, TextThatIsNoCursorIsRefused ){
		for( sv text : {"", "1000", "x/1", "1000/x", "1000/-1"} )
			EXPECT_FALSE( EventsCursor::Parse(text) ) << text;
	}

	//#195 review #4:  an unconvertible reading's failure rode in Value.status_code, which a client reads as a StatusCode reading.
	TEST( FromServerTests, AnUnconvertibleReadingSendsItsCodeAsTheQuality ){
		UA_DataValue dv{};
		dv.hasValue = true;
		const UA_DataValue inner{};
		UA_Variant_setScalarCopy( &dv.value, &inner, &UA_TYPES[UA_TYPES_DATAVALUE] );
		let m = FromServer::ToProto( "opc", NodeId{}, Opc::Value{move(dv)}, 0, nullptr );
		ASSERT_TRUE( m.has_node_values() );
		EXPECT_FALSE( m.node_values().has_value() );
		EXPECT_EQ( m.node_values().sc(), UA_STATUSCODE_BADNOTSUPPORTED );

		UA_DataValue bad{};
		bad.hasValue = true; bad.hasStatus = true; bad.status = UA_STATUSCODE_BADSENSORFAILURE;
		const UA_Int32 i{ 7 };
		UA_Variant_setScalarCopy( &bad.value, &i, &UA_TYPES[UA_TYPES_INT32] );
		let ok = FromServer::ToProto( "opc", NodeId{}, Opc::Value{move(bad)}, 0, nullptr );
		EXPECT_EQ( ok.node_values().value().int32(), 7 );
		EXPECT_EQ( ok.node_values().sc(), UA_STATUSCODE_BADSENSORFAILURE );
	}

	//historian 4A #218:  the web joins a push to a history read by source time, so a push carries the timestamps the reading
	//has, and none for one the server left out.
	TEST( FromServerTests, APushCarriesTheReadingsTimestamps ){
		UA_DataValue dv{};
		dv.hasValue = true;
		const UA_Int32 i{ 7 };
		UA_Variant_setScalarCopy( &dv.value, &i, &UA_TYPES[UA_TYPES_INT32] );
		let bare = FromServer::ToProto( "opc", NodeId{}, Opc::Value{dv}, 0, nullptr );
		EXPECT_FALSE( bare.node_values().has_source() );
		EXPECT_FALSE( bare.node_values().has_server() );

		let source = UA_DateTime_now()-UA_DATETIME_SEC, server = source+UA_DATETIME_MSEC;
		dv.hasSourceTimestamp = true; dv.sourceTimestamp = source;
		dv.hasServerTimestamp = true; dv.serverTimestamp = server;
		let stamped = FromServer::ToProto( "opc", NodeId{}, Opc::Value{move(dv)}, 0, nullptr );
		ASSERT_TRUE( stamped.node_values().has_source() );
		ASSERT_TRUE( stamped.node_values().has_server() );
		EXPECT_EQ( UADateTime{stamped.node_values().source()}.UA(), source );
		EXPECT_EQ( UADateTime{stamped.node_values().server()}.UA(), server );
	}
}