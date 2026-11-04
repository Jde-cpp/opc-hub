//Clients phase 13, diagnostics (#427, docs/clients/spec.md *Diagnostics*):  the Server tab's four reads against the embedded OpcServer -
//ServerStatus, the ServerDiagnosticsSummary, the SessionDiagnosticsArray and the SubscriptionDiagnosticsArray, namespace-0 structures
//the gateway decodes to json already - with the suite's own session and its subscription listed;  and clientDiagnostics, the caller's
//own client of the slug:  one reconnect and its break after the loss the suite provokes, and null for a slug with no live client.
#include <absl/cleanup/cleanup.h>
#include "utils/GatewayClientSocket.h"
#include "utils/helpers.h"
#include "utils/ITest.h"
#include "utils/Subscribing.h"
#include "../src/UAClient.h"

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
		EXPECT_TRUE( (state->is_number() && state->to_number<int>()==0) || (state->is_string() && state->as_string().starts_with("Running")) ) << "Running expected:  " << serialize( *state );//reversibly a number, else Name_Value
		EXPECT_TRUE( Member(status, "StartTime") && Member(status, "StartTime")->is_string() ) << serialize( status );
		EXPECT_TRUE( Member(status, "CurrentTime") && Member(status, "CurrentTime")->is_string() ) << serialize( status );
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

	//The break ResubscribesAfterClientLoss provokes, read back:  before it the client has no break and one monitored item;  after the
	//reconnect brings its replacement up, that client reads one reconnect, a break at the loss, the start the lost client had, and
	//the item restored - and the slug's client count stays one.  lastBreak and connectedSince are to the millisecond.
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

	//A slug with no live client reads null - the sub-query never connects - beside the row it is on.
	TEST_F( DiagnosticsTests, NoLiveClientIsNull ){
		auto row = Socket().QuerySync( "serverConnection( slug: $opc ){ slug opcConnections{ count } clientDiagnostics{ clients } }", jobject{ {"opc", IdleSlug} } );
		let& o = row.as_object();
		EXPECT_EQ( Json::AsSV(o, "slug"), IdleSlug ) << serialize( o );
		EXPECT_EQ( Json::FindNumberPath<uint>(o, "opcConnections/count").value_or(99), 0u ) << serialize( o );
		ASSERT_TRUE( o.contains("clientDiagnostics") ) << serialize( o );
		EXPECT_TRUE( o.at("clientDiagnostics").is_null() ) << serialize( o );
	}
}