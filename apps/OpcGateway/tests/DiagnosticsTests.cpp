//Clients phase 13, diagnostics (#427, docs/clients/spec.md *Diagnostics*):  the Server tab's four reads against the embedded OpcServer -
//ServerStatus, the ServerDiagnosticsSummary, the SessionDiagnosticsArray and the SubscriptionDiagnosticsArray, namespace-0 structures
//the gateway decodes to json already - with the suite's own session and its subscription listed;  and clientDiagnostics, the caller's
//own client of the slug:  one reconnect and its break after the loss the suite provokes, and null for a slug with no live client.
#include <absl/cleanup/cleanup.h>
#include "utils/GatewayClientSocket.h"
#include "utils/helpers.h"
#include "utils/ITest.h"
#include "utils/Subscribing.h"
#include "../src/async/ConnectAwait.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };
	struct DiagnosticsTests : ITest{
	protected:
		inline static const string IdleSlug{ "opcTestsDiagnosticsIdle" };//a row no test connects:  its clientDiagnostics is null.
		Ω SetUpTestSuite()ε->void{
			ITest::SetUpTestSuite();
			GetConnection( IdleSlug, OpcUrl(), OpcUrn() );
		}
		Ω TearDownTestSuite()ι->void{
			PurgeServerCnnctn( IdleSlug );
			ITest::TearDownTestSuite();
		}
		static constexpr array<UA_UInt32,4> Reads{ UA_NS0ID_SERVER_SERVERSTATUS, UA_NS0ID_SERVER_SERVERDIAGNOSTICS_SERVERDIAGNOSTICSSUMMARY, UA_NS0ID_SERVER_SERVERDIAGNOSTICS_SESSIONSDIAGNOSTICSSUMMARY_SESSIONDIAGNOSTICSARRAY, UA_NS0ID_SERVER_SERVERDIAGNOSTICS_SUBSCRIPTIONDIAGNOSTICSARRAY };
		//The Server tab's reads, as it sends them:  the four values by id, in Reads' order - the gateway answers them by id order, so the
		//tab matches on the id too.
		Ω ServerReads()ε->jarray{
			jarray ids;
			for( let i : Reads )
				ids.push_back( NodeId{0, i}.ToJson() );
			const jobject vars{ {"opc", OpcServerSlug}, {"id", move(ids)} };
			auto rows = Socket().QuerySync( "nodes( opc: $opc, id: $id ){ id value }", vars );
			TRACE( "server reads: {}", serialize(rows) );
			THROW_IF( !rows.is_array() || rows.as_array().size()!=4, "four rows expected:  {}", serialize(rows) );
			jarray y;
			for( let i : Reads ){
				let row = std::ranges::find_if( rows.as_array(), [i]( let& r ){ return NodeId{ r.as_object().at("id") }==NodeId{0, i}; } );
				THROW_IF( row==rows.as_array().end(), "no row for i={}:  {}", i, serialize(rows) );
				y.push_back( *row );
			}
			return y;
		}
		Ω ReconnectWaiting()ι->bool{
			let parked = UAClient::ParkedCounts();
			let p = parked.find( OpcServerSlug );
			return p!=parked.end() && p->second.Waiting;
		}
		//clientDiagnostics for `slug` from this socket's session - the credential ITest::SetUpTestSuite stored for it, _client's key.
		Ω ClientDiagnostics( str slug )ε->jvalue{
			auto row = Socket().QuerySync( "serverConnection( slug: $opc ){ slug clientDiagnostics{ connectedSince reconnects lastBreak sessionAge monitoredItems parkedItems reconnectWaiting clients } }", jobject{ {"opc", slug} } );
			TRACE( "clientDiagnostics: {}", serialize(row) );
			return row.as_object().at( "clientDiagnostics" );
		}
		//A member of a decoded structure by its OPC UA json name, "ClientDescription/ApplicationUri" - the gateway hands the structures
		//over as open62541's json encoder writes them, so the names are the type's.
		Ω Member( const jvalue& structure, sv path )ι->const jvalue*{
			const jvalue* y = &structure;
			for( let& name : Str::Split(path, '/') ){
				let o = y->if_object();
				y = o ? o->if_contains( name ) : nullptr;
				if( !y )
					break;
			}
			return y;
		}
		//The listener's item off the client:  the delete runs a DeleteMonitoring timer (1 s) after the last listener goes, and a subscribe
		//of the same node inside that second joins the item on its way out - so the next test waits for it to go.
		Ω Unsubscribe( const sp<PushCount>& listener )ι->void{
			UAClient::Unsubscribe( listener );
			if( !WaitUntil([]{ let nodes = _client ? _client->TryMonitoredNodes() : nullptr; return !nodes || !nodes->Count(); }, 6s) )
				ADD_FAILURE() << "the item was not deleted after the unsubscribe";
		}
		//A state callback as open62541 makes it, on the client's strand:  the channel open, the session as given.
		Ω StateCallback( const sp<UAClient>& client, UA_SessionState sessionState )ι->bool{
			auto done = ms<std::atomic<bool>>( false );
			client->PostUA( [ua=client->UAPointer(), sessionState, done]{
				UA_Client_getConfig( ua )->stateCallback( ua, UA_SECURECHANNELSTATE_OPEN, sessionState, UA_STATUSCODE_GOOD );
				*done = true;
			} );
			return WaitUntil( [done]{ return done->load(); }, 5s );
		}
		Ω Text( const jvalue* v )ι->string{
			if( !v )
				return {};
			if( let s = v->if_string(); s )
				return string{ *s };
			if( let o = v->if_object(); o && o->contains("Text") )//a LocalizedText, reversibly encoded
				return Text( &o->at("Text") );
			return serialize( *v );
		}
	};

	//The four reads:  ServerStatus with its state and times, the summary's counts, and the two arrays - this session, by the application
	//uri the suite's client presents, with its subscription listed under the session's id.  The arrays are open62541's, filled whatever
	//the EnabledFlag;  a server that does not fill them answers an empty array, which the tab explains.
	TEST_F( DiagnosticsTests, TheServerTabsFourReadsListThisSession ){
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( SubscribeCounting(_client, listener, {Intensity}) );//a subscription of this session for the subscription array to list.
		absl::Cleanup unsubscribe = [&]{ Unsubscribe( listener ); };
		let rows = ServerReads();
		for( uint i=0; i<4; ++i ){
			let& row = rows[i].as_object();
			ASSERT_TRUE( row.contains("value") && !row.at("value").is_null() ) << i << ": " << serialize( row );
			ASSERT_FALSE( row.at("value").is_object() && row.at("value").as_object().contains("sc") && !row.at("value").as_object().contains("v") ) << i << " read Bad:  " << serialize( row );//a status alone is a failed read.
		}
		let& status = rows[0].at( "value" );//ServerStatusDataType:  StartTime CurrentTime State BuildInfo SecondsTillShutdown ShutdownReason
		ASSERT_TRUE( status.is_object() ) << serialize( status );
		let state = Member( status, "State" );
		ASSERT_TRUE( state ) << serialize( status );
		EXPECT_TRUE( (state->is_number() && state->to_number<int>()==0) || (state->is_string() && state->as_string().starts_with("Running")) ) << "Running expected:  " << serialize( *state );//an enumeration's value, as the member walk writes it
		let time = []( const jvalue* v ){ return v && v->is_object() && v->as_object().contains("seconds"); };//a DateTime member in the gateway's own shape, {seconds, nanos}, as the member walk writes it (clients spec *Structures*, #421).
		EXPECT_TRUE( time(Member(status, "StartTime")) ) << serialize( status );
		EXPECT_TRUE( time(Member(status, "CurrentTime")) ) << serialize( status );
		EXPECT_TRUE( Member(status, "BuildInfo/ProductName") ) << serialize( status );

		let& summary = rows[1].at( "value" );//ServerDiagnosticsSummaryDataType
		ASSERT_TRUE( summary.is_object() ) << serialize( summary );
		let currentSessions = Member( summary, "CurrentSessionCount" );
		ASSERT_TRUE( currentSessions && currentSessions->is_number() ) << serialize( summary );
		EXPECT_GE( currentSessions->to_number<uint>(), 1u ) << "this session, at least";
		EXPECT_TRUE( Member(summary, "CumulatedSessionCount") ) << serialize( summary );
		EXPECT_TRUE( Member(summary, "CurrentSubscriptionCount") ) << serialize( summary );

		let& sessions = rows[2].at( "value" );//SessionDiagnosticsDataType[]
		ASSERT_TRUE( sessions.is_array() ) << serialize( sessions );
		let uri = _client->AdvertisedUri();
		let ours = std::ranges::find_if( sessions.as_array(), [&]( let& session ){ return Text( Member(session, "ClientDescription/ApplicationUri") )==uri; } );
		ASSERT_NE( ours, sessions.as_array().end() ) << "no session presents '" << uri << "':  " << serialize( sessions );
		EXPECT_TRUE( Member(*ours, "SessionId") ) << serialize( *ours );
		EXPECT_TRUE( Member(*ours, "ClientConnectionTime") ) << serialize( *ours );
		if( let count = Member(*ours, "CurrentSubscriptionsCount"); count && count->is_number() )
			EXPECT_GE( count->to_number<uint>(), 1u ) << serialize( *ours );

		let& subscriptions = rows[3].at( "value" );//SubscriptionDiagnosticsDataType[]
		ASSERT_TRUE( subscriptions.is_array() ) << serialize( subscriptions );
		let sessionId = Member( *ours, "SessionId" );
		ASSERT_TRUE( sessionId ) << serialize( *ours );
		let subscribed = std::ranges::find_if( subscriptions.as_array(), [&]( let& subscription ){ let id = Member( subscription, "SessionId" ); return id && *id==*sessionId; } );
		EXPECT_NE( subscribed, subscriptions.as_array().end() ) << "no subscription under session " << serialize( *sessionId ) << ":  " << serialize( subscriptions );
		if( subscribed!=subscriptions.as_array().end() )
			EXPECT_TRUE( Member(*subscribed, "SubscriptionId") && Member(*subscribed, "PublishingInterval") ) << serialize( *subscribed );
	}

	//The break ResubscribesAfterClientLoss provokes, read back:  before it the client has no break and one monitored item;  in the
	//chain's backoff, with no client registered, the lost client as the chain holds it - its start and break, no session, the item
	//parked and the reconnect waiting;  after the reconnect brings its replacement up, that client reads one reconnect, a break at the
	//loss, the start the lost client had, and the item restored - and the slug's client count stays one.  lastBreak and connectedSince
	//are to the millisecond.
	TEST_F( DiagnosticsTests, ClientDiagnosticsReadAReconnectAndItsBreak ){
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( SubscribeCounting(_client, listener, {Intensity}) );
		absl::Cleanup unsubscribe = [&]{ Unsubscribe( listener ); };//off whichever client holds it by then - the revived one, once the reconnect has restored it.
		let before = ClientDiagnostics( OpcServerSlug );
		ASSERT_TRUE( before.is_object() ) << "this session's client is live:  " << serialize( before );
		let& b = before.as_object();
		EXPECT_EQ( b.at("reconnects").to_number<uint>(), 0u ) << serialize( b );
		EXPECT_TRUE( b.at("lastBreak").is_null() ) << serialize( b );
		EXPECT_EQ( b.at("monitoredItems").to_number<uint>(), 1u ) << serialize( b );
		EXPECT_EQ( b.at("parkedItems").to_number<uint>(), 0u ) << serialize( b );
		EXPECT_FALSE( b.at("reconnectWaiting").as_bool() ) << serialize( b );
		EXPECT_GE( b.at("clients").to_number<uint>(), 1u ) << serialize( b );
		let connectedSince = string{ b.at("connectedSince").as_string() };
		EXPECT_FALSE( connectedSince.empty() ) << serialize( b );

		let lost = _client->Handle();
		let broke = Clock::now();
		UAClient::ConnectionLost( sp<UAClient>{_client} );
		EXPECT_TRUE( WaitUntil([]{ return ReconnectWaiting(); }, 1s) ) << "the chain never waited";//it waits a second before its first attempt:  read in that second.
		let during = ClientDiagnostics( OpcServerSlug );
		ASSERT_TRUE( during.is_object() ) << "the lost client, as the chain holds it:  " << serialize( during );
		let& w = during.as_object();
		EXPECT_EQ( w.at("reconnects").to_number<uint>(), 0u ) << "not back yet:  " << serialize( w );
		EXPECT_EQ( string{w.at("connectedSince").as_string()}, connectedSince ) << serialize( w );
		ASSERT_TRUE( w.at("lastBreak").is_string() ) << serialize( w );
		EXPECT_GE( string{w.at("lastBreak").as_string()}, ToIsoString<std::chrono::milliseconds>(broke-1s) ) << "the break is the loss's time:  " << serialize( w );
		EXPECT_TRUE( w.at("sessionAge").is_null() ) << "no session while it reconnects:  " << serialize( w );
		EXPECT_EQ( w.at("monitoredItems").to_number<uint>(), 0u ) << serialize( w );
		EXPECT_EQ( w.at("parkedItems").to_number<uint>(), 1u ) << "the item, parked for the reconnect:  " << serialize( w );
		EXPECT_TRUE( w.at("reconnectWaiting").as_bool() ) << serialize( w );
		let listed = Socket().QuerySync( "serverConnections( deleted:null ){ slug clientDiagnostics{ parkedItems reconnectWaiting } }", {} );//every row from one snapshot
		let& rows = listed.is_array() ? listed.get_array() : Json::AsArray( listed.as_object(), "serverConnections" );
		let row = [&]( str slug )->const jobject*{
			let r = std::ranges::find_if( rows, [&]( let& r ){ return Json::AsSV(r.as_object(), "slug")==slug; } );
			return r==rows.end() ? nullptr : &r->as_object();
		};
		let chained = row( OpcServerSlug );
		ASSERT_TRUE( chained ) << serialize( listed );
		let& listedDiagnostics = Json::AsObject( *chained, "clientDiagnostics" );
		EXPECT_EQ( listedDiagnostics.at("parkedItems").to_number<uint>(), 1u ) << serialize( *chained );
		EXPECT_TRUE( listedDiagnostics.at("reconnectWaiting").as_bool() ) << serialize( *chained );
		let idle = row( IdleSlug );
		ASSERT_TRUE( idle ) << serialize( listed );
		EXPECT_TRUE( idle->at("clientDiagnostics").is_null() ) << serialize( *idle );
		let parked = UAClient::ParkedCounts().at( OpcServerSlug );
		EXPECT_EQ( parked.Items, 1u );
		EXPECT_GE( std::get<2>(UAClient::StatusCounts()), parked.Items ) << "the status query's parked sums the slugs'";
		let revived = WaitRevived( lost, OpcServerSlug );//the reconnect waits a second before its first attempt, then connects, subscribes and re-creates the item.
		ASSERT_TRUE( revived ) << "no replacement client came up with the monitored node restored";
		_client = revived;//the suite shares it:  a later test must not reach for the deregistered one.
		let after = ClientDiagnostics( OpcServerSlug );
		ASSERT_TRUE( after.is_object() ) << "the chain's client is this session's:  " << serialize( after );
		let& a = after.as_object();
		EXPECT_EQ( a.at("reconnects").to_number<uint>(), 1u ) << serialize( a );
		EXPECT_EQ( string{a.at("connectedSince").as_string()}, connectedSince ) << "the lost client's start carries over:  " << serialize( a );
		ASSERT_TRUE( a.at("lastBreak").is_string() ) << serialize( a );
		let lastBreak = string{ a.at("lastBreak").as_string() };//the same rendering as the query's, so the strings order as the times do.
		EXPECT_GE( lastBreak, ToIsoString<std::chrono::milliseconds>(broke-1s) ) << "the break is the loss's time:  " << serialize( a );
		EXPECT_LE( lastBreak, ToIsoString<std::chrono::milliseconds>(Clock::now()) ) << serialize( a );
		EXPECT_LE( a.at("sessionAge").to_number<uint>(), 30u ) << "the session now up is the reconnect's:  " << serialize( a );
		EXPECT_EQ( a.at("monitoredItems").to_number<uint>(), 1u ) << "the item restored on the chain's client:  " << serialize( a );
		EXPECT_FALSE( a.at("reconnectWaiting").as_bool() ) << serialize( a );
		EXPECT_EQ( a.at("clients").to_number<uint>(), b.at("clients").to_number<uint>() ) << serialize( a );
	}

	//An edit is no break:  the client an edit's reconnect builds on the new settings starts afresh - no reconnect, no break, its own
	//start - though the edit parks what it monitored as a loss does.  ConnectionLost( edited ) is ConnectionEdited's path for one client;
	//the edit itself would reach every client of the shared slug.
	TEST_F( DiagnosticsTests, AnEditStartsAfresh ){
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( SubscribeCounting(_client, listener, {Intensity}) );//monitored:  the edit's park starts a chain, which carried the figures over.
		absl::Cleanup unsubscribe = [&]{ Unsubscribe( listener ); };
		let before = ClientDiagnostics( OpcServerSlug );
		ASSERT_TRUE( before.is_object() ) << serialize( before );
		let lost = _client->Handle();
		UAClient::ConnectionLost( sp<UAClient>{_client}, true );
		EXPECT_TRUE( ClientDiagnostics(OpcServerSlug).is_null() ) << "no client stands in for an edited one";
		let revived = WaitRevived( lost, OpcServerSlug );
		ASSERT_TRUE( revived ) << "no replacement client came up with the monitored node restored";
		_client = revived;
		let after = ClientDiagnostics( OpcServerSlug );
		ASSERT_TRUE( after.is_object() ) << serialize( after );
		let& a = after.as_object();
		EXPECT_EQ( a.at("reconnects").to_number<uint>(), 0u ) << serialize( a );
		EXPECT_TRUE( a.at("lastBreak").is_null() ) << serialize( a );
		EXPECT_GT( string{a.at("connectedSince").as_string()}, string{before.as_object().at("connectedSince").as_string()} ) << "its own start, not the edited client's:  " << serialize( a );
		EXPECT_EQ( a.at("monitoredItems").to_number<uint>(), 1u ) << "the item restored on the new client:  " << serialize( a );
	}

	//open62541 calls back on a change of any of the channel, session and connect states, so a callback can find the session still
	//ACTIVATED:  no re-activation, and sessionAge keeps its activation.  One after the session went is, and restarts it with a reconnect
	//counted - open62541's in-place reconnect, made here as its two callbacks, on the client's strand as open62541 makes them.
	TEST_F( DiagnosticsTests, OnlyAReactivationRestartsTheSessionAge ){
		let before = _client->ClientDiagnostics();
		ASSERT_NE( before.ActivatedAt, TimePoint{} );
		ASSERT_TRUE( StateCallback(_client, UA_SESSIONSTATE_ACTIVATED) );
		let still = _client->ClientDiagnostics();
		EXPECT_EQ( still.ActivatedAt, before.ActivatedAt ) << "a callback that finds the session up restarted its age";
		EXPECT_EQ( still.Reconnects, before.Reconnects );

		ASSERT_TRUE( StateCallback(_client, UA_SESSIONSTATE_CREATED) );//the session went
		ASSERT_TRUE( StateCallback(_client, UA_SESSIONSTATE_ACTIVATED) );//and open62541 activated it again in place
		let back = _client->ClientDiagnostics();
		EXPECT_GT( back.ActivatedAt, before.ActivatedAt );
		EXPECT_EQ( back.Reconnects, before.Reconnects+1 );
		EXPECT_NE( back.LastBreak, TimePoint{} );
	}

	//While open62541 reconnects a session in place the client stays registered, with no session up:  no age, until it is back.
	TEST_F( DiagnosticsTests, ADownSessionHasNoAge ){
		ASSERT_TRUE( StateCallback(_client, UA_SESSIONSTATE_CREATED) );//the session went
		absl::Cleanup up = []{ StateCallback( _client, UA_SESSIONSTATE_ACTIVATED ); };
		let down = ClientDiagnostics( OpcServerSlug );
		ASSERT_TRUE( down.is_object() ) << "still registered:  " << serialize( down );
		EXPECT_TRUE( down.as_object().at("sessionAge").is_null() ) << serialize( down );
		ASSERT_TRUE( StateCallback(_client, UA_SESSIONSTATE_ACTIVATED) );
		let back = ClientDiagnostics( OpcServerSlug );
		ASSERT_TRUE( back.is_object() ) << serialize( back );
		EXPECT_TRUE( back.as_object().at("sessionAge").is_number() ) << serialize( back );
	}

	//A second client of the key, built while the chain waits out its backoff and lost in turn, joins the chain:  the client the
	//reconnect brings up counts both breaks, reports the later, and keeps the first client's start.
	TEST_F( DiagnosticsTests, ALossInTheBackoffJoinsTheChain ){
		auto listener = ms<PushCount>(), second = ms<PushCount>();
		ASSERT_NO_THROW( SubscribeCounting(_client, listener, {Intensity}) );
		absl::Cleanup unsubscribe = [&]{ UAClient::Unsubscribe( second ); Unsubscribe( listener ); };
		let before = _client->ClientDiagnostics();
		let credential = _client->Credential();
		UAClient::ConnectionLost( sp<UAClient>{_client} );//the chain waits a second before its first attempt
		let built = BlockAwait( ConnectAwait{string{OpcServerSlug}, credential} );//in the backoff:  nothing is registered under the key
		ASSERT_NO_THROW( SubscribeCounting(built, second, {Intensity}) );
		ASSERT_TRUE( ReconnectWaiting() ) << "the chain attempted first - the second loss has to land inside its backoff";
		let secondBreak = Clock::now();
		UAClient::ConnectionLost( sp<UAClient>{built} );
		let revived = WaitRevived( built->Handle(), OpcServerSlug );
		ASSERT_TRUE( revived ) << "no replacement client came up with the monitored nodes restored";
		_client = revived;
		let after = revived->ClientDiagnostics();
		EXPECT_EQ( after.Reconnects, before.Reconnects+2 ) << "the first client's break and the second's";
		EXPECT_GE( after.LastBreak, secondBreak ) << "the second, the later";
		EXPECT_EQ( after.ConnectedSince, before.ConnectedSince );
	}

	//The chain's reconnect hands the listeners to whatever client is registered under the key - here one built in the backoff, with
	//an in-place reconnect of its own.  That client continues from the lost one:  its own break kept, one more reconnect for the loss.
	TEST_F( DiagnosticsTests, AReconnectOntoALiveClientAddsToItsOwn ){
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( SubscribeCounting(_client, listener, {Intensity}) );
		absl::Cleanup unsubscribe = [&]{ Unsubscribe( listener ); };
		let before = _client->ClientDiagnostics();
		let lost = _client->Handle();
		let credential = _client->Credential();
		UAClient::ConnectionLost( sp<UAClient>{_client} );
		let built = BlockAwait( ConnectAwait{string{OpcServerSlug}, credential} );
		ASSERT_TRUE( StateCallback(built, UA_SESSIONSTATE_CREATED) );
		ASSERT_TRUE( StateCallback(built, UA_SESSIONSTATE_ACTIVATED) );
		let own = built->ClientDiagnostics();
		ASSERT_EQ( own.Reconnects, 1u );
		ASSERT_TRUE( ReconnectWaiting() ) << "the chain attempted first - the client has to be up before it does";
		let revived = WaitRevived( lost, OpcServerSlug );
		ASSERT_EQ( revived, built ) << "the chain restores onto the registered client";
		_client = revived;
		let after = built->ClientDiagnostics();
		EXPECT_EQ( after.Reconnects, before.Reconnects+1+own.Reconnects );
		EXPECT_EQ( after.LastBreak, own.LastBreak ) << "its own break, the later";
		EXPECT_EQ( after.ConnectedSince, before.ConnectedSince );
	}

	//A slug with no live client reads null - the sub-query never connects - beside the row it is on.
	TEST_F( DiagnosticsTests, NoLiveClientIsNull ){
		auto row = Socket().QuerySync( "serverConnection( slug: $opc ){ slug opcConnections{ count } clientDiagnostics{ clients } }", jobject{ {"opc", IdleSlug} } );
		let& o = row.as_object();
		EXPECT_EQ( Json::AsSV(o, "slug"), IdleSlug ) << serialize( o );
		EXPECT_EQ( Json::FindNumber<uint>(o, "opcConnections/count").value_or(99), 0u ) << serialize( o );
		ASSERT_TRUE( o.contains("clientDiagnostics") ) << serialize( o );
		EXPECT_TRUE( o.at("clientDiagnostics").is_null() ) << serialize( o );
	}
}