#include <jde/fwk/co/AnyAwait.h>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/fwk/process/execution.h>
#include <absl/cleanup/cleanup.h>
#include "utils/GatewayClientSocket.h"
#include "utils/helpers.h"
#include <jde/fwk/str.h>
#include <jde/web/client/proto/Web.FromServer.pb.h>
#include <jde/tests/HttpTest.h>
#include "../src/GatewayAppClient.h"
#include "../src/auth/OpcServerSession.h"
#include <jde/web/server/Sessions.h>
#include "../src/ql/GatewayQL.h"//after Sessions.h:  IQLAwaitExe.h names Web::Server::SessionInfo.
#include "../src/ql/DiscoverQLAwait.h"
#include "../src/ql/OpcSessionsQLAwait.h"
#include "../src/ql/SearchQLAwait.h"
#include "../src/async/BlockingPool.h"
#include "../src/UAClient.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	using Web::Client::ClientHttpResException;
	constexpr ELogTags _tags{ ELogTags::Test };

	//A real web session, not a fabricated id:  SessionCounts sweeps out entries whose session Web::Server no longer knows, so a
	//seeded credential only survives to be counted if there is a live session behind it.
	Ω addSession( string token )ι->SessionPK{
		auto session = Web::Server::Sessions::Add( AppClient()->UserPK(), string{"127.0.0.1"}, false );
		Credential cred{ move(token) }; cred.SetUserPK( AppClient()->UserPK() );
		AddSession( session->SessionId, OpcServerSlug, move(cred) );
		return session->SessionId;
	}
	Ω removeSession( SessionPK sessionId )ι->void{
		Logout( sessionId );
		Web::Server::Sessions::Remove( sessionId );
	}

	//What a test may set on a client that production sets only from its own callbacks - UAClient befriends it.
	struct UAClientState final{
		Ω SetConnected( UAClient& client, bool connected )ι->void{ client._connected = connected; }
	};
	struct QLTests : HttpTest{
	protected:
		Ω SetUpTestSuite()ε->void{ GetConnection( OpcServerSlug ); }//ε: GetConnection throws - under ι gtest never sees it and the whole binary terminates.
		//`typeName`'s introspected fields, each with its name and type.
		Ω Fields( sv typeName )ε->jarray{
			let type = Socket().QuerySync( Ƒ("__type( name: \"{}\" ){{ fields{{ name type{{ name kind ofType{{ name kind }} }} }} }}", typeName) );
			TRACE( "__type({}): {}.", typeName, serialize(type) );
			return Json::AsArray( type.as_object(), "fields" );
		}
		//The row of `rows` whose `key` is `value`, or rows.end().
		Ω FindRow( const jarray& rows, sv key, sv value )ι->jarray::const_iterator{
			return std::ranges::find_if( rows, [&]( let& row ){ return Json::AsSV(row.as_object(), key)==value; } );
		}
	};

	TEST_F( QLTests, ServerDescriptionTest ){
		let q = "serverDescription( opc: $opc ){ applicationUri productUri applicationName applicationType gatewayServerUri discoveryProfileUri discoveryUrls }";
		const jobject vars{ {"opc", OpcServerSlug} };
		let value = Socket().QuerySync( q, vars );
		//{"applicationUri":"urn:open62541.server.application","productUri":"http://open62541.org","applicationName":"Jde-Cpp OpcServer","applicationType":"Server","gatewayServerUri":"","discoveryProfileUri":"","discoveryUrls":["opc.tcp://workstation25:4840","opc.tcp://127.0.0.1:4840"]}.
		TRACE( "ServerDescription: {}.", serialize(value) );
		let obj = value.as_object();
		ASSERT_TRUE( obj.contains("applicationUri") );
		ASSERT_TRUE( obj.contains("productUri") );
		ASSERT_TRUE( obj.contains("applicationName") );
		ASSERT_TRUE( obj.contains("applicationType") );
		ASSERT_TRUE( obj.contains("gatewayServerUri") );
		ASSERT_TRUE( obj.contains("discoveryProfileUri") );
		ASSERT_TRUE( obj.contains("discoveryUrls") );
	}

	//dataTypes:  the server's structure definitions, built over the asynchronous read (DataTypesAwait) - each node's definition
	//and name read together, results[i*2] and [i*2+1];  a node that is no structure is left out.
	TEST_F( QLTests, DataTypesBuildsTheStructures ){
		const jobject vars{ {"opc", OpcServerSlug}, {"ids", jarray{NodeId{0, UA_NS0ID_RANGE}.ToJson(), NodeId{0, UA_NS0ID_DOUBLE}.ToJson(), NodeId{0, UA_NS0ID_EUINFORMATION}.ToJson()}} };
		let value = Socket().QuerySync( "dataTypes( opc: $opc, id: $ids ){ typeId members{ isArray } }", vars );
		let& types = value.as_array();
		ASSERT_EQ( types.size(), 2u ) << serialize( value );
		EXPECT_EQ( NodeId{types[0].at("typeId")}, (NodeId{0, UA_NS0ID_RANGE}) ) << serialize( types[0] );
		EXPECT_EQ( types[0].at("members").as_array().size(), 2u ) << "low, high";
		EXPECT_EQ( NodeId{types[1].at("typeId")}, (NodeId{0, UA_NS0ID_EUINFORMATION}) ) << serialize( types[1] );
		EXPECT_EQ( types[1].at("members").as_array().size(), 4u ) << "namespaceUri, unitId, displayName, description";
	}

	//#255:  a stand-alone gateway's web sessions, for its page's Web Sessions tab - asked by the AppServer as InstanceSessionsAwait
	//forwards it:  raw, over the AppServer socket.  The socket this test queries on holds one of them.
	TEST_F( QLTests, sessions ){
		auto q = QL::Parse( "sessions{ endpoint lastUpdate expiration }", {}, {}, true );
		let value = BlockAwait( move(*AppClient()->ClientQuery(move(q), AppClient()->UserPK())) );
		ASSERT_TRUE( value.is_array() ) << serialize( value );
		EXPECT_FALSE( value.as_array().empty() ) << "the querying socket's own session is one of them";
	}
	//#364:  and only so - the gateway's acl holds no app schema, so it has no `sessions` to refuse a caller of its own ql with.
	TEST_F( QLTests, sessionsAreNotThisGatewaysQuery ){
		EXPECT_THROW( Socket().QuerySync( "sessions{ endpoint }" ), std::exception );
		EXPECT_THROW( Socket().QuerySync( "mutation purgeSession( id:\"1\" )" ), std::exception );
	}

	TEST_F( QLTests, namespaces ){
		let q = "namespaces( opc: $opc ){ index uri }";
		const jobject vars{ {"opc", OpcServerSlug} };
		let value = Socket().QuerySync( q, vars );
		//[{"index":0,"uri":"http://opcfoundation.org/UA/"},{"index":1,"uri":"urn:open62541.server.application"},...].
		TRACE( "namespaces: {}.", serialize(value) );
		let& rows = value.as_array();
		ASSERT_GE( rows.size(), 2u ) << serialize( value );//ns0 is the standard uri, ns1 the server's application uri.
		EXPECT_EQ( Json::AsNumber<uint16>(rows[0].as_object(), "index"), 0 );
		EXPECT_EQ( Json::AsSV(rows[0].as_object(), "uri"), "http://opcfoundation.org/UA/" );
		EXPECT_TRUE( Json::AsSV(rows[1].as_object(), "uri").size() ) << "ns1 is the server's application uri.";
	}

	TEST_F( QLTests, securityPolicyUri ){
		let q = "securityPolicyUri( opc: $opc )";
		const jobject vars{ {"opc", OpcServerSlug} };
		let value = Socket().QuerySync( q, vars );
		TRACE( "securityPolicyUri: {}.", serialize(value) );
		ASSERT_TRUE( serialize(value).size() );
	}

	TEST_F( QLTests, securityMode ){
		let q = "securityMode( opc: $opc )";
		const jobject vars{ {"opc", OpcServerSlug} };
		let value = Socket().QuerySync( q, vars );
		TRACE( "securityMode: {}.", serialize(value) );
		ASSERT_TRUE( serialize(value).size() );
	}
	TEST_F( QLTests, opcSessions ){
		let sessionId = addSession( "opcSessionsTestToken" );
		let q = "opcSessions{ connection{slug} type user{id slug name} count }";
		let value = Socket().QuerySync( q );
		TRACE( "opcSessions: {}.", serialize(value) );
		let projected = Socket().QuerySync( "opcSessions{ count }" );
		removeSession( sessionId );

		let& rows = value.as_array();
		let userPK = AppClient()->UserPK().Value;
		auto row = find_if( rows, [&](let& r){
			let& o = r.as_object();
			return Json::AsSV(o, "connection/slug")==OpcServerSlug && Json::AsSV(o, "type")=="IssuedToken" && Json::FindNumber<Jde::UserPK::Type>(o, "user/id")==userPK; //user may be null for anonymous rows.
		} );
		ASSERT_NE( row, rows.end() ) << serialize( value );
		let& user = Json::AsObject( row->as_object(), "user" );
		EXPECT_TRUE( user.contains("name") && user.contains("slug") ) << serialize( user ); //fetched from AppServer's users table.
		EXPECT_GE( Json::AsNumber<uint32>(row->as_object(), "count"), 1u );

		ASSERT_FALSE( projected.as_array().empty() );
		for( let& r : projected.as_array() ){
			let& o = r.as_object();
			EXPECT_TRUE( o.contains("count") && !o.contains("user") && !o.contains("connection") && !o.contains("type") ) << serialize( o );
		}
	}

	//reviews/m2-closing.md #8:  the gateway gates on three resources and only one had a row.  `opcSessions` and `search` are
	//QL system tables, so no table declared gateway/sessions or gateway/search, ResourceSyncAwait created neither, and
	//Authorize::Test passes whatever it finds no active row for - both gates were unconditional, with no product path to
	//make them anything else.  With gateway/serverConnections enforced an ungranted user was refused `serverConnections{}`
	//and still read who holds which credential on which connection.  The meta declares both now (`resources`), so the rows
	//are there - shipped unenforced, as every resource is - and enforcing one closes its gate.  The probe is a user no grant
	//names, as in BrowseTests.EnforcedConnectionResourceRefusesAnUngrantedSession;  each resource goes back as it was found.
	TEST_F( QLTests, SessionsAndSearchAreResourcesAnAdminCanEnforce ){
		let listed = AppClient()->QuerySync<jvalue>( "resources( schemaName:[\"gateway\"] ){ slug deleted allowed }", {} );
		let& rows = listed.is_array() ? listed.get_array() : Json::AsArray( listed.as_object(), "resources" );
		for( let slug : {"serverConnections"sv, "sessions"sv, "search"sv, "endpoint"sv} ){
			auto row = find_if( rows, [&]( let& r ){ return Json::AsSV(r.as_object(), "slug")==slug; } );
			ASSERT_NE( row, rows.end() ) << "gateway/" << slug << " has no resource row - its gate cannot be enforced: " << serialize( listed );
			let deleted = row->as_object().if_contains( "deleted" );
			EXPECT_TRUE( deleted && !deleted->is_null() ) << "gateway/" << slug << " ships enforced: " << serialize( *row );
			let allowed = row->as_object().if_contains( "allowed" );
			EXPECT_TRUE( allowed && allowed->is_array() && !allowed->get_array().empty() ) << "gateway/" << slug << " offers the permission table nothing to tick: " << serialize( *row );
		}

		constexpr Jde::UserPK ungranted{ (Jde::UserPK::Type)0x08'0000 };//no acl, no role, no group names it
		auto refused = [&]( sv slug, function<void()> query )->bool{
			EnforceResource( "gateway", slug, true );
			absl::Cleanup _ = [slug]{ try{ EnforceResource("gateway", slug, false); }catch( const std::exception& ){} };
			return WaitUntil( [&]{
				try{
					query();
				}
				catch( const Exception& e ){
					return e.HttpStatus()==EHttpStatus::Unauthorized || e.HttpStatus()==EHttpStatus::Forbidden;
				}
				return false;
			}, 5s, 50ms );
		};
		EXPECT_TRUE( refused("sessions", [&]{
			auto ql = QL::Parse( "opcSessions{ connection{slug} type user{id name} count }", {}, Schemas(), true );
			BlockAwait<OpcSessionsQLAwait,jvalue>( OpcSessionsQLAwait{move(ql.Queries().front()), QL::Creds{ungranted}} );
		}) ) << "gateway/sessions is enforced and an ungranted user still reads every session on every connection";
		EXPECT_TRUE( refused("search", [&]{
			auto ql = QL::Parse( "search( text:\"lamp\" ){ name }", {}, Schemas(), true );
			BlockAwait<SearchQLAwait,jvalue>( SearchQLAwait{move(ql.Queries().front()), QL::Creds{ungranted}} );
		}) ) << "gateway/search is enforced and an ungranted user is still let through its gate";
		EXPECT_TRUE( refused("endpoint", [&]{//servers-7-web review #2:  the description had no gate of its own.
			auto ql = QL::Parse( "endpoint{ url policies{ uri mode } trustedCertDirs rejectedCertDir }", {}, Schemas(), true );
			BlockAwait<EndpointQLAwait,jvalue>( EndpointQLAwait{move(ql.Queries().front()), QL::Creds{ungranted}} );
		}) ) << "gateway/endpoint is enforced and an ungranted user still reads the endpoint's url, policies and certificate directories";

		let value = Socket().QuerySync( "opcSessions{ count }" );//and unenforced again, the harness's own session still reads it.
		EXPECT_TRUE( value.is_array() ) << serialize( value );
	}

	//reviews-issue/415-clients-trust.md #3:  the two certificate grafts are the connection's, not live-usage telemetry, so they sit under
	//serverConnections Read alone - gated on gateway/sessions too, an enforced sessions resource hid the page's Trust panel from a user
	//who could read the connection.  The telemetry grafts keep the sessions gate.  serverConnections ships unenforced, so the ungranted
	//probe reads its rows;  the refused telemetry first shows the enforcement has landed.
	TEST_F( QLTests, CertificatesNeedNoSessionsRead ){
		constexpr Jde::UserPK ungranted{ (Jde::UserPK::Type)0x08'0000 };//no acl, no role, no group names it
		auto query = [&]( str text )->jvalue{
			auto ql = QL::Parse( text, {}, Schemas(), true );
			return BlockAwait<ServerCnnctnSessionsQLAwait,jvalue>( ServerCnnctnSessionsQLAwait{move(ql.Queries().front()), QL::Creds{ungranted}} );
		};
		EnforceResource( "gateway", "sessions", true );
		absl::Cleanup open = []{ try{ EnforceResource("gateway", "sessions", false); }catch( const std::exception& ){} };
		let refused = WaitUntil( [&]{
			try{
				query( "serverConnections( deleted:null ){ slug opcConnections{ count } }" );
			}
			catch( const Exception& e ){
				return e.HttpStatus()==EHttpStatus::Unauthorized || e.HttpStatus()==EHttpStatus::Forbidden;
			}
			return false;
		}, 5s, 50ms );
		ASSERT_TRUE( refused ) << "gateway/sessions is enforced and an ungranted user still reads opcConnections";

		jvalue rows;
		try{
			rows = query( "serverConnections( deleted:null ){ slug rejectedCertificate{ thumbprint } clientCertificate{ path } }" );
		}
		catch( const Exception& e ){
			FAIL() << "the certificates were refused for want of gateway/sessions Read:  " << e.what();
		}
		ASSERT_TRUE( rows.is_array() && !rows.get_array().empty() ) << serialize( rows );
		let& o = rows.get_array().front().as_object();
		EXPECT_TRUE( o.contains("rejectedCertificate") && o.contains("clientCertificate") ) << serialize( o );
	}

	TEST_F( QLTests, webSessionCounted ){ //no manual AddSession: a jwt-backed web session's connect must register itself (ConnectAwait::await_resume).
		const jobject vars{ {"opc", OpcServerSlug} };
		Socket().QuerySync( "serverDescription( opc: $opc ){ applicationUri }", vars );//forces a ConnectAwait for this socket's session.
		let value = Socket().QuerySync( "opcSessions{ connection{slug} type user{id} count }" );
		TRACE( "webSessionCounted: {}.", serialize(value) );
		let userPK = AppClient()->UserPK().Value;
		let& rows = value.as_array();
		let row = find_if( rows, [&](let& r){
			let& o = r.as_object();
			return Json::AsSV(o, "connection/slug")==OpcServerSlug && Json::AsSV(o, "type")=="IssuedToken" && Json::FindNumber<Jde::UserPK::Type>(o, "user/id")==userPK;
		} );
		ASSERT_NE( row, rows.end() ) << serialize( value );
		EXPECT_GE( Json::AsNumber<uint32>(row->as_object(), "count"), 1u );
	}

	//A session that ends without a /logout - a timeout, a purge, a browser that never came back - must stop being counted.
	//Nothing but that logout ever removed an entry, so opcSessions reported every session that had touched the slug since
	//startup, an ever-climbing number that opcConnections (drained by the idle ttl) never matched.
	TEST_F( QLTests, deadSessionsAreNotCounted ){
		let count = []ι->uint32 {
			uint32 y{};
			for( let& c : SessionCounts() ){
				if( c.Connection==OpcServerSlug )
					y += c.Count;
			}
			return y;
		};
		let before = count();
		let sessionId = addSession( "deadSessionTestToken" );
		EXPECT_EQ( count(), before+1 );
		Web::Server::Sessions::Remove( sessionId ); //the session ends;  nothing tells the gateway.
		EXPECT_EQ( count(), before );
		EXPECT_FALSE( GetCredential(sessionId, OpcServerSlug) ) << "the swept entry takes its cached credential with it.";
	}

	TEST_F( QLTests, serverConnectionSessions ){
		let sessionId = addSession( "serverConnectionSessionsTestToken" );
		Socket().QuerySync( "serverDescription( opc: $opcSlug ){ applicationUri }", {{"opcSlug", OpcServerSlug}} );//ensure a live UAClient so opcConnections has something to count.
		//the exact shape View.query() emits for the Connections list - args must survive the graft's DB pass.
		let listQL = "serverConnections(limit:25,orderBy:[{name:\"asc\"}],deleted:$deleted){ id connectionStatus{name} name certificateUri url opcSessions{count} opcConnections{count} description slug }";
		let rows = Socket().QuerySync( listQL, {{"deleted",nullptr}} );
		TRACE( "serverConnections: {}.", serialize(rows) );
		const jobject vars{ {"opc", OpcServerSlug} };
		let single = Socket().QuerySync( "serverConnection( slug: $opc ){ name opcSessions{count} }", vars );
		TRACE( "serverConnection: {}.", serialize(single) );
		removeSession( sessionId );

		let connection = SelectServerCnnctn( OpcServerSlug ); ASSERT_TRUE( connection );
		bool found{};
		for( let& r : rows.as_array() ){
			let& o = r.as_object();
			EXPECT_TRUE( o.contains("slug") ) << serialize( o ); //requested explicitly here, so it must not be erased.
			let count = Json::FindNumber<uint32>( o, "opcSessions/count" );
			ASSERT_TRUE( count ) << serialize( o );
			let connectionCount = Json::FindNumber<uint32>( o, "opcConnections/count" );
			ASSERT_TRUE( connectionCount ) << serialize( o );
			let status = Json::FindSV( o, "connectionStatus/name" );
			ASSERT_TRUE( status ) << serialize( o ); //grafted for every row, connected or not.
			if( Json::AsNumber<ServerCnnctnPK>(o, "id")==connection->Id ){
				found = true;
				EXPECT_GE( *count, 1u );
				EXPECT_GE( *connectionCount, 1u ) << "a UAClient for the slug is live - the serverDescription above connected it.";
				EXPECT_EQ( *status, "Connected" ) << serialize( o ); //the same live client the count above sees.
			}
			else
				EXPECT_TRUE( *status=="Idle" || *status=="Error" ) << serialize( o );
		}
		EXPECT_TRUE( found ) << serialize( rows );
		let& o = single.as_object();
		EXPECT_FALSE( o.contains("slug") ) << serialize( o );
		EXPECT_GE( Json::FindNumber<uint32>(o, "opcSessions/count").value_or(0), 1u ) << serialize( o );
	}

	TEST_F( QLTests, search ){
		const jobject vars{ {"opc", OpcServerSlug} };
		Socket().QuerySync( "serverDescription( opc: $opc ){ applicationUri }", vars );//search never connects - give this socket's session a live client first.
		constexpr sv lampPath{ "Objects/4~Examples/4~Stacklights/4~ExampleStacklight/4~Lamp1" };//the page path, from Root (#416);  BrowseTests.NodeId resolves it from the Objects folder.
		auto rows = [&]( string q ){
			auto value = Socket().QuerySync( string{q}, vars );
			TRACE( "{}: {}.", q, serialize(value) );
			return value.as_array();
		};
		auto find = [&]( const jarray& rows, sv path ){ return find_if( rows, [&](let& r){ return Json::AsSV(r.as_object(), "path")==path; } ); };

		let first = rows( "search( opc: $opc, text: \"lamp1\" ){ connection{ slug name } id path name browse{ ns name } nodeClass depth }" );//the first search crawls.
		auto lamp = find( first, lampPath );
		ASSERT_NE( lamp, first.end() ) << serialize( first );
		let& o = lamp->as_object();
		EXPECT_EQ( Json::AsSV(o, "name"), "Lamp1" );
		EXPECT_EQ( Json::AsSV(o, "connection/slug"), OpcServerSlug );
		EXPECT_EQ( Json::AsSV(o, "browse/name"), "Lamp1" );
		EXPECT_EQ( Json::AsNumber<uint16>(o.at("browse").as_object(), "ns"), 4 );
		EXPECT_EQ( Json::AsNumber<uint8>(o, "depth"), 4 );
		EXPECT_TRUE( o.contains("ns") && o.contains("i") ) << "id is spelled the way node{id} spells it: " << serialize( o );
		EXPECT_TRUE( o.contains("nodeClass") ) << serialize( o );
		for( let& row : first )//substring, case-insensitive, on the display or browse name.
			EXPECT_TRUE( Str::ToLower(Json::AsString(row.as_object(), "name")).contains("lamp1") || Str::ToLower(Json::AsSV(row.as_object(), "browse/name")).contains("lamp1") ) << serialize( row );

		let again = rows( "search( opc: $opc, text: \"LAMP1\" ){ path }" );//served from the index, case-folded.
		EXPECT_EQ( again.size(), first.size() );
		ASSERT_NE( find(again, lampPath), again.end() ) << serialize( again );
		EXPECT_EQ( again.front().as_object().size(), 1u ) << "projection: only the requested column";

		let fanout = rows( "search( text: \"lamp1\" ){ connection{ slug } path }" );//no opc: every client this session already holds.
		ASSERT_NE( find(fanout, lampPath), fanout.end() ) << serialize( fanout );

		let unknown = rows( "search( opc: \"noSuchConnection\", text: \"lamp1\" ){ path }" );//no live client ⇒ empty, and no ConnectAwait (which would throw 'not found').
		EXPECT_TRUE( unknown.empty() ) << serialize( unknown );
		{//a client that is held but not connected - a lapsed session, a dropped socket - rejects rather than answering empty.
			vector<sp<UAClient>> held;
			for( auto& client : UAClient::LiveClients() ){
				if( client->Slug()==OpcServerSlug )
					held.push_back( client );
			}
			ASSERT_FALSE( held.empty() );
			absl::Cleanup _ = [&held]{ for( auto& c : held ) UAClientState::SetConnected( *c, true ); };
			for( auto& c : held )
				UAClientState::SetConnected( *c, false );
			try{
				Socket().QuerySync( "search( opc: $opc, text: \"lamp1\" ){ path }", vars );
				ADD_FAILURE() << "a search on a disconnected client answered";
			}
			catch( const GatewayErrorResponse& e ){
				EXPECT_NE( string{e.what()}.find("BadServerNotConnected"), string::npos ) << e.what();
			}
		}

		let refreshed = rows( "search( opc: $opc, text: \"lamp1\", refresh: true ){ path }" );
		ASSERT_NE( find(refreshed, lampPath), refreshed.end() ) << serialize( refreshed );

		let limited = rows( "search( opc: $opc, text: \"a\", limit: 3 ){ path }" );
		EXPECT_EQ( limited.size(), 3u ) << serialize( limited );

		let blank = rows( "search( opc: $opc, text: \"  \" ){ path }" );
		EXPECT_TRUE( blank.empty() ) << serialize( blank );
	}

	TEST_F( QLTests, searchIntrospection ){
		for( sv typeName : {"Search"sv, "search"sv} ){ //both spellings are declared in config/introspection/search.jsonnet.
			let fields = Fields( typeName );
			for( sv name : {"connection"sv, "id"sv, "path"sv, "name"sv, "browse"sv, "nodeClass"sv, "depth"sv} )
				EXPECT_NE( FindRow(fields, "name", name), fields.end() ) << name << ": " << serialize( fields );
			auto connection = FindRow( fields, "name", "connection" );
			ASSERT_NE( connection, fields.end() );
			EXPECT_EQ( Json::AsSV(connection->as_object(), "type/name"), "SearchConnection" );
		}
	}

	TEST_F( QLTests, historyIntrospection ){
		for( sv typeName : {"History"sv, "history"sv} ){ //both spellings are declared in config/introspection/hist.jsonnet.
			let fields = Fields( typeName );
			for( sv name : {"continuation"sv, "values"sv, "nodes"sv} )
				EXPECT_NE( FindRow(fields, "name", name), fields.end() ) << name << ": " << serialize( fields );
			auto values = FindRow( fields, "name", "values" );
			ASSERT_NE( values, fields.end() );
			EXPECT_EQ( Json::AsSV(values->as_object(), "type/kind"), "LIST" );
			EXPECT_EQ( Json::AsSV(values->as_object(), "type/ofType/name"), "HistoryValue" );
		}
		for( sv edit : {"createHistory"sv, "updateHistory"sv, "upsertHistory"sv, "purgeHistory"sv} ){
			let fields = Fields( edit );
			for( sv name : {"values"sv, "nodes"sv} )
				EXPECT_NE( FindRow(fields, "name", name), fields.end() ) << edit << "." << name << ": " << serialize( fields );
		}
		let fields = Fields( "HistoryValue" );
		for( sv name : {"node"sv, "source"sv, "server"sv, "status"sv, "value"sv, "bound"sv, "heartbeat"sv, "modification"sv} )
			EXPECT_NE( FindRow(fields, "name", name), fields.end() ) << name << ": " << serialize( fields );
		//Historian 5b (#223):  resolveHistory, and the group tables the schema introspects itself.
		let resolveFields = Fields( "resolveHistory" );
		auto nodes = FindRow( resolveFields, "name", "nodes" );
		ASSERT_NE( nodes, resolveFields.end() ) << serialize( resolveFields );
		EXPECT_EQ( Json::AsSV(nodes->as_object(), "type/ofType/name"), "HistoryResolvedNode" );
		let resolvedFields = Fields( "HistoryResolvedNode" );
		for( sv name : {"templateNode"sv, "path"sv, "node"sv, "status"sv} )
			EXPECT_NE( FindRow(resolvedFields, "name", name), resolvedFields.end() ) << name << ": " << serialize( resolvedFields );
		for( let& [typeName,column] : vector<std::pair<sv,sv>>{ {"histGroup", "guid"}, {"histGroupNode", "node"}, {"histTemplate", "slug"}, {"histTemplateNode", "path"} } ){
			let tableFields = Fields( typeName );
			EXPECT_NE( FindRow(tableFields, "name", column), tableFields.end() ) << typeName << "." << column << ": " << serialize( tableFields );
		}
	}

	TEST_F( QLTests, serverConnectionIntrospection ){
		for( sv typeName : {"ServerConnection"sv, "serverConnections"sv} ){ //both spellings are declared in config/introspection/serverConnection.jsonnet.
			let fields = Fields( typeName );
			ASSERT_NE( FindRow(fields, "name", "url"), fields.end() ) << serialize( fields ); //extend:true keeps the DB columns.
			auto opcSessions = FindRow( fields, "name", "opcSessions" );
			ASSERT_NE( opcSessions, fields.end() ) << serialize( fields );
			EXPECT_EQ( Json::AsSV(opcSessions->as_object(), "type/kind"), "OBJECT" );
			EXPECT_EQ( Json::AsSV(opcSessions->as_object(), "type/name"), "OpcSessions" );
			auto opcConnections = FindRow( fields, "name", "opcConnections" );
			ASSERT_NE( opcConnections, fields.end() ) << serialize( fields );
			EXPECT_EQ( Json::AsSV(opcConnections->as_object(), "type/name"), "OpcConnections" );
			auto connectionStatus = FindRow( fields, "name", "connectionStatus" );
			ASSERT_NE( connectionStatus, fields.end() ) << serialize( fields );
			EXPECT_EQ( Json::AsSV(connectionStatus->as_object(), "type/kind"), "OBJECT" );
			EXPECT_EQ( Json::AsSV(connectionStatus->as_object(), "type/name"), "ConnectionStatus" );
			auto rejectedCertificate = FindRow( fields, "name", "rejectedCertificate" );//#415
			ASSERT_NE( rejectedCertificate, fields.end() ) << serialize( fields );
			EXPECT_EQ( Json::AsSV(rejectedCertificate->as_object(), "type/name"), "RejectedCertificate" );
			auto clientCertificate = FindRow( fields, "name", "clientCertificate" );
			ASSERT_NE( clientCertificate, fields.end() ) << serialize( fields );
			EXPECT_EQ( Json::AsSV(clientCertificate->as_object(), "type/name"), "ClientCertificate" );
			auto clientDiagnostics = FindRow( fields, "name", "clientDiagnostics" );//#427
			ASSERT_NE( clientDiagnostics, fields.end() ) << serialize( fields );
			EXPECT_EQ( Json::AsSV(clientDiagnostics->as_object(), "type/name"), "ClientDiagnostics" );
		}
		{ //the status type carries a name and the reason the last connect failed - the SPA's list selects only the name.
			let fields = Fields( "ConnectionStatus" );
			ASSERT_EQ( fields.size(), 2u ) << serialize( fields );
			EXPECT_EQ( Json::AsSV(fields[0].as_object(), "name"), "name" );
			EXPECT_EQ( Json::AsSV(fields[0].as_object(), "type/ofType/name"), "String" );
			EXPECT_EQ( Json::AsSV(fields[1].as_object(), "name"), "error" );
			EXPECT_EQ( Json::AsSV(fields[1].as_object(), "type/name"), "String" );
		}
		for( sv typeName : {"OpcSessions"sv, "OpcConnections"sv} ){ //config-only types - no view behind them.
			let fields = Fields( typeName );
			ASSERT_EQ( fields.size(), 1u ) << serialize( fields );
			let& count = fields[0].as_object();
			EXPECT_EQ( Json::AsSV(count, "name"), "count" );
			EXPECT_EQ( Json::AsSV(count, "type/kind"), "NON_NULL" );
			EXPECT_EQ( Json::AsSV(count, "type/ofType/name"), "UInt" );
		}
		{ //#415:  the two certificate types the trust panel reads - the rejected server certificate and the gateway's own.
			let rejected = Fields( "RejectedCertificate" );
			EXPECT_EQ( rejected.size(), 6u ) << serialize( rejected );//subject issuer notBefore notAfter uri thumbprint
			EXPECT_NE( FindRow(rejected, "name", "thumbprint"), rejected.end() ) << serialize( rejected );
			let client = Fields( "ClientCertificate" );
			EXPECT_EQ( client.size(), 6u ) << serialize( client );//path subject notBefore notAfter uri pem
			EXPECT_NE( FindRow(client, "name", "pem"), client.end() ) << serialize( client );
		}
		{ //#427:  the caller's own client as it stands - the Server tab's gateway panel.
			let diagnostics = Fields( "ClientDiagnostics" );
			EXPECT_EQ( diagnostics.size(), 8u ) << serialize( diagnostics );//connectedSince reconnects lastBreak sessionAge monitoredItems parkedItems reconnectWaiting clients
			auto lastBreak = FindRow( diagnostics, "name", "lastBreak" );
			ASSERT_NE( lastBreak, diagnostics.end() ) << serialize( diagnostics );
			EXPECT_EQ( Json::AsSV(lastBreak->as_object(), "type/name"), "String" );//nullable:  a client that never broke
			auto sessionAge = FindRow( diagnostics, "name", "sessionAge" );
			ASSERT_NE( sessionAge, diagnostics.end() ) << serialize( diagnostics );
			EXPECT_EQ( Json::AsSV(sessionAge->as_object(), "type/name"), "UInt" );//nullable:  the lost client a reconnect chain holds
			auto waiting = FindRow( diagnostics, "name", "reconnectWaiting" );
			ASSERT_NE( waiting, diagnostics.end() ) << serialize( diagnostics );
			EXPECT_EQ( Json::AsSV(waiting->as_object(), "type/ofType/name"), "Boolean" );
		}
	}

	TEST_F( QLTests, enumTypes ){//an enumeration DataType's definition comes from the server (src/EnumTypeCache), not a config table.
		const jobject vars{ {"opc", OpcServerSlug} };
		auto enumType = [&]( uint16 ns, uint32 i ){
			auto value = Socket().QuerySync( Ƒ("__type( opc: $opc, ns:{}, i:{} ){{ name enumValues{{ id name description }} }}", ns, i), vars );
			TRACE( "__type(ns={};i={}): {}.", ns, i, serialize(value) );
			return value;
		};
		auto expectValues = [&]( const jvalue& v, sv typeName, const vector<sv>& names ){
			let& o = v.as_object();
			EXPECT_EQ( Json::AsSV(o, "name"), typeName ) << serialize( v );
			let& rows = Json::AsArray( o, "enumValues" );
			ASSERT_EQ( rows.size(), names.size() ) << serialize( v );
			for( uint i=0; i<names.size(); ++i ){
				let& r = rows[i].as_object();
				EXPECT_EQ( Json::AsNumber<_int>(r, "id"), (_int)i ) << serialize( r );
				EXPECT_EQ( Json::AsSV(r, "name"), names[i] ) << serialize( r );
				EXPECT_TRUE( r.contains("description") ) << "requested, so present even when null: " << serialize( r );
			}
		};
		const vector<sv> di{ "NORMAL", "FAILURE", "CHECK_FUNCTION", "OFF_SPEC", "MAINTENANCE_REQUIRED" };//DI DeviceHealthEnumeration - ns2 on the test server, which loads DI first.
		const vector<sv> ia{ "Off", "Red", "Green", "Blue", "Yellow", "Purple", "Cyan", "White" };//IA SignalColor - ns3.
		expectValues( enumType(2, 6244), "DeviceHealthEnumeration", di );
		expectValues( enumType(3, 3004), "SignalColor", ia );
		expectValues( enumType(2, 6244), "DeviceHealthEnumeration", di );//again - served from the client's cache, no second read.

		EXPECT_ANY_THROW( Socket().QuerySync("__type( opc: $opc, ns:0, i:884 ){ enumValues{ id name } }", vars) );//i=884 is Range - a structure, not an enumeration.
		EXPECT_ANY_THROW( Socket().QuerySync("__type( opc: $opc, ns:2, i:6244 ){ enumValues{ id name } }", jobject{{"opc", "no-such-connection"}}) );//the definition is per connection.
		let logTags = Socket().QuerySync( "__type( name: \"logTags\" ){ enumValues{ id name } }", {} );//no opc:  still the generic QueryType path.
		EXPECT_FALSE( Json::AsArray(logTags.as_object(), "enumValues").empty() ) << serialize( logTags );
	}

	//testConnection( opc, url, certificateUri ):  the connection page's Test Connection - settings that are not saved, connected as the
	//saved row would be and described in one object, with nothing left registered for the slug;  a wrong url is the mutation's error,
	//not the slug's connectionStatus;  and the slug names the certificate file, so it is held to the insert's rule.
	TEST_F( QLTests, testConnection ){
		const string slug{ "qlTestsTestConnection" };//no such row:  the point is a connection that is not saved.
		ASSERT_FALSE( SelectServerCnnctn({slug}) );
		let url = OpcUrl();
		let urn = OpcUrn();
		const string q{ "testConnection( opc: $opc, url: $url, certificateUri: $certificateUri ){ applicationUri productUri applicationName applicationType gatewayServerUri discoveryProfileUri discoveryUrls securityPolicyUri securityMode namespaces{ index uri } }" };
		let value = Socket().QuerySync( string{q}, {{"opc", slug}, {"url", url}, {"certificateUri", urn}} );
		TRACE( "testConnection: {}.", serialize(value) );
		let& o = value.as_object();
		EXPECT_EQ( Json::AsSV(o, "applicationUri"), urn ) << serialize( value );
		for( sv field : {"productUri", "applicationName", "applicationType", "gatewayServerUri", "discoveryProfileUri"} )
			EXPECT_TRUE( o.contains(field) ) << field << ": " << serialize( value );
		EXPECT_FALSE( Json::AsArray(o, "discoveryUrls").empty() ) << serialize( value );
		EXPECT_EQ( Json::AsSV(o, "securityMode"), "SignAndEncrypt" ) << "a certificateUri means Sign & Encrypt: " << serialize( value );
		EXPECT_TRUE( Json::AsSV(o, "securityPolicyUri").starts_with("http://opcfoundation.org/UA/SecurityPolicy#") ) << serialize( value );
		let& namespaces = Json::AsArray( o, "namespaces" );
		ASSERT_GE( namespaces.size(), 2u ) << serialize( value );
		EXPECT_EQ( Json::AsNumber<uint16>(namespaces[0].as_object(), "index"), 0 );
		EXPECT_EQ( Json::AsSV(namespaces[0].as_object(), "uri"), "http://opcfoundation.org/UA/" );
		EXPECT_FALSE( UAClient::ConnectionCounts().contains(slug) ) << "a test connect leaves no client registered.";
		EXPECT_FALSE( UAClient::ConnectErrors().contains(slug) );
		{//#250 review #10:  the namespace array is read only when selected.
			let narrow = Socket().QuerySync( "testConnection( opc: $opc, url: $url, certificateUri: $certificateUri ){ applicationUri }", {{"opc", slug}, {"url", url}, {"certificateUri", urn}} );
			EXPECT_EQ( Json::AsSV(narrow.as_object(), "applicationUri"), urn ) << serialize( narrow );
			EXPECT_FALSE( narrow.as_object().contains("namespaces") ) << serialize( narrow );
		}

		try{
			Socket().QuerySync( string{q}, {{"opc", slug}, {"url", "opc.tcp://127.0.0.1:1"}, {"certificateUri", urn}} );
			ADD_FAILURE() << "a connect to a closed port succeeded";
		}
		catch( const GatewayErrorResponse& e ){
			TRACE( "testConnection refused: {}.", e.what() );
			EXPECT_TRUE( sv{e.what()}.size() ) << "the failure carries its reason";
		}
		{//never accepted, so the kernel completes the handshake and nothing reads the HEL - open62541 gives that no deadline of its own.
			boost::asio::io_context ctx;
			boost::asio::ip::tcp::acceptor silent{ ctx, {boost::asio::ip::make_address("127.0.0.1"), 0} };
			let start = steady_clock::now();
			//over http, as the page sends it:  BadTimeout answers 500, whose body carried the status alone and not the reason.  The row's
			//requestTimeout is the deadline, short here - the gateway's 10 s fallback was nearly all of QLTests' time.
			const string timed{ "testConnection( opc: $opc, url: $url, certificateUri: $certificateUri, requestTimeout: $requestTimeout ){ applicationUri }" };
			jobject body{ {"query", timed}, {"variables", jobject{{"opc", slug}, {"url", Ƒ("opc.tcp://127.0.0.1:{}", silent.local_endpoint().port())}, {"certificateUri", urn}, {"requestTimeout", 2000}}} };
			try{
				Post( GatewayPort(), "/graphql", serialize(body), Ƒ("{:x}", AppClient()->SessionId()) );
				ADD_FAILURE() << "a connect to a silent listener succeeded";
			}
			catch( const ClientHttpResException& e ){
				TRACE( "testConnection unanswered: {}.", e.Res().Body() );
				EXPECT_EQ( e.Status(), boost::beast::http::status::internal_server_error ) << e.Res().Body();
				EXPECT_NE( e.Res().Body().find("(800a0000)BadTimeout - "), string::npos ) << e.Res().Body();
				EXPECT_NE( e.Res().Body().find("did not complete the connection"), string::npos ) << e.Res().Body();
			}
			let elapsed = steady_clock::now()-start;
			EXPECT_GE( elapsed, 2s ) << "the requestTimeout is the deadline";
			EXPECT_LT( elapsed, 8s ) << "the row's requestTimeout, 2 s, is the deadline - not the gateway's 10 s.";
		}
		EXPECT_FALSE( UAClient::ConnectErrors().contains(slug) ) << "a failed test is not the slug's connectionStatus.";
		EXPECT_FALSE( UAClient::ConnectionCounts().contains(slug) );
		EXPECT_THROW( Socket().QuerySync(string{q}, {{"opc", "../" + slug}, {"url", url}, {"certificateUri", urn}}), GatewayErrorResponse ) << "the slug names the certificate file.";
		try{//a mutation:  a GET - a cross-site <img> - must not connect the gateway anywhere or issue a certificate.
			Get( GatewayPort(), "/graphql?query=testConnection(opc:%22qlTestsTestConnection%22,url:%22opc.tcp://127.0.0.1:1%22)%7BapplicationUri%7D" );
			ADD_FAILURE() << "testConnection ran over GET";
		}
		catch( const ClientHttpResException& e ){
			EXPECT_EQ( e.Status(), boost::beast::http::status::bad_request ) << e.Res().Body();
			EXPECT_TRUE( e.Res().Body().contains("Mutations must use post.") ) << e.Res().Body();
		}
	}

	//The detached form of a socket query, for one that is meant to take a while:  what it answered, into `state`.
	Ω querying( string query, jobject vars, sp<Awaited<jvalue>> state )ι->VoidTask{
		try{
			state->Result = co_await Any( Socket().Query(move(query), move(vars), true) );
		}
		catch( Exception& e ){
			state->Error = e.Move();
		}
		state->Done.test_and_set();
	}
	//discover( url, servers, requestTimeout ) - the connection page's Discover (docs/clients/spec.md *Discovery*):  FindServers at a
	//discovery url lists the servers it knows, GetEndpoints at a server url its endpoints with their token policies and its certificate
	//decoded, both on a throwaway None-policy client on the blocking pool, so a url that isn't reached answers its error while the
	//executor answers other requests;  under serverConnections' Update, so an ungranted user is refused;  a mutation, so not over GET.
	TEST_F( QLTests, discover ){
		let url = OpcUrl();
		let urn = OpcUrn();
		{//servers:  the embedded OpcServer answers FindServers with itself.
			let value = Socket().QuerySync( "discover( url: $url, servers: true ){ applicationUri productUri applicationName applicationType gatewayServerUri discoveryProfileUri discoveryUrls }", {{"url", url}} );
			TRACE( "discover servers: {}.", serialize(value) );
			ASSERT_TRUE( value.is_array() ) << serialize( value );
			let& servers = value.get_array();
			let server = FindRow( servers, "applicationUri", urn );
			ASSERT_NE( server, servers.end() ) << serialize( value );
			let& o = server->as_object();
			EXPECT_EQ( Json::AsSV(o, "applicationType"), "Server" ) << serialize( value );
			EXPECT_FALSE( Json::AsSV(o, "applicationName").empty() ) << serialize( value );
			EXPECT_FALSE( Json::AsArray(o, "discoveryUrls").empty() ) << serialize( value );
			for( sv field : {"productUri", "gatewayServerUri", "discoveryProfileUri"} )
				EXPECT_TRUE( o.contains(field) ) << field << ": " << serialize( value );
		}
		{//endpoints:  each with its token policies - the username policy among them, which the suite's config gives the server (Opc.Tests.jsonnet opc.users) - and the certificate it presents, decoded.
			let value = Socket().QuerySync( "discover( url: $url ){ url applicationUri policy mode securityLevel tokenPolicies{ type policy policyId } certificate{ subject issuer notBefore notAfter uri thumbprint fingerprint } }", {{"url", url}} );
			TRACE( "discover endpoints: {}.", serialize(value) );
			ASSERT_TRUE( value.is_array() ) << serialize( value );
			let& endpoints = value.get_array();
			ASSERT_FALSE( endpoints.empty() ) << serialize( value );
			ETokenType offered{};
			const jobject* secured{};
			optional<uint32> noneLevel;
			for( let& row : endpoints ){
				let& ep = row.as_object();
				EXPECT_EQ( Json::AsSV(ep, "applicationUri"), urn ) << serialize( row );
				EXPECT_TRUE( Json::AsSV(ep, "url").starts_with("opc.tcp://") ) << serialize( row );
				EXPECT_TRUE( Json::AsSV(ep, "policy").starts_with("http://opcfoundation.org/UA/SecurityPolicy#") ) << serialize( row );
				let mode = Json::AsSV( ep, "mode" );
				EXPECT_TRUE( mode==ServerCnnctn::ModeNone || mode==ServerCnnctn::ModeSign || mode==ServerCnnctn::ModeSignAndEncrypt ) << serialize( row );
				let level = Json::AsNumber<uint32>( ep, "securityLevel" );
				if( mode==ServerCnnctn::ModeNone )
					noneLevel = level;
				else if( mode==ServerCnnctn::ModeSignAndEncrypt && !secured )
					secured = &ep;
				let& policies = Json::AsArray( ep, "tokenPolicies" );
				EXPECT_FALSE( policies.empty() ) << serialize( row );
				for( let& p : policies ){
					let& policy = p.as_object();
					let type = Json::AsSV( policy, "type" );
					let i = std::ranges::find( TokenTypeNames, type );
					ASSERT_TRUE( i!=TokenTypeNames.end() && i!=TokenTypeNames.begin() ) << serialize( p );
					offered |= (ETokenType)( 1 << (std::distance(TokenTypeNames.begin(), i)-1) );//bit-indexed, as TokenTypeNames is.
					EXPECT_TRUE( Json::AsSV(policy, "policy").starts_with("http://opcfoundation.org/UA/SecurityPolicy#") ) << "the token's own policy, or the channel's: " << serialize( p );
					EXPECT_TRUE( policy.contains("policyId") ) << serialize( p );
				}
			}
			let available = AvailableUserTokens( url );//what the suite's own GetEndpoints reads.
			EXPECT_EQ( (uint)offered, (uint)available ) << serialize( value );
			EXPECT_TRUE( (uint)offered & (uint)ETokenType::Username ) << "the config's users give the server a username policy: " << serialize( value );
			ASSERT_TRUE( secured ) << "a Sign & Encrypt endpoint, which carries the server's certificate: " << serialize( value );
			if( noneLevel )
				EXPECT_GT( Json::AsNumber<uint32>(*secured, "securityLevel"), *noneLevel ) << "the server ranks its secured endpoint over None";
			let& cert = Json::AsObject( *secured, "certificate" );
			EXPECT_EQ( Json::AsSV(cert, "uri"), urn ) << "the SAN URI, the applicationUri a connection's certificateUri names: " << serialize( cert );
			EXPECT_FALSE( Json::AsSV(cert, "subject").empty() ) << serialize( cert );
			EXPECT_FALSE( Json::AsSV(cert, "issuer").empty() ) << serialize( cert );
			let thumbprint = Json::AsSV( cert, "thumbprint" );
			EXPECT_EQ( thumbprint.size(), 40u ) << "sha-1, upper hex, no separators: " << thumbprint;
			EXPECT_TRUE( std::ranges::all_of(thumbprint, []( char c ){ return std::isxdigit((unsigned char)c) && !std::islower((unsigned char)c); }) ) << thumbprint;
			EXPECT_EQ( std::ranges::count(Json::AsSV(cert, "fingerprint"), ':'), 31 ) << "sha-256 with colons: " << Json::AsSV( cert, "fingerprint" );
			let notBefore = Json::AsSV( cert, "notBefore" ), notAfter = Json::AsSV( cert, "notAfter" );
			EXPECT_LT( notBefore, notAfter ) << notBefore << " " << notAfter;
			EXPECT_TRUE( notBefore.ends_with('Z') && notAfter.ends_with('Z') ) << "ISO-8601, UTC:  " << notBefore << " " << notAfter;
		}
		{//A url that isn't reached - a listener that never answers the HEL, open62541 giving the handshake no deadline of its own - answers its
			//error after the requestTimeout, from the discovery pool:  a query on a connected slug answers the executor meanwhile, and with
			//both discovery threads held (/workers/discovery/threads' default 2) a connect's job still runs on the blocking pool at once
			//(414-clients-discovery review #1:  on the blocking pool, two discovers held every connect).
			boost::asio::io_context ctx;
			boost::asio::ip::tcp::acceptor silent{ ctx, {boost::asio::ip::make_address("127.0.0.1"), 0} };
			let start = steady_clock::now();
			array<sp<Awaited<jvalue>>,2> discovering{ ms<Awaited<jvalue>>(), ms<Awaited<jvalue>>() };
			for( auto& d : discovering )
				querying( "discover( url: $url, requestTimeout: $requestTimeout ){ url }", {{"url", Ƒ("opc.tcp://127.0.0.1:{}", silent.local_endpoint().port())}, {"requestTimeout", 2000}}, d );
			let node = Socket().QuerySync( "node( opc: $opc, id: $id ){ id name }", {{"opc", OpcServerSlug}, {"id", Intensity.ToJson()}} );
			let nodeAnswered = steady_clock::now()-start;
			auto ran = ms<std::atomic_flag>();
			let posted = steady_clock::now();
			BlockingPool::Post( [ran]{ ran->test_and_set(); } );
			ASSERT_TRUE( WaitUntil([&]{ return ran->test(); }, 5s, 10ms) ) << "the blocking pool ran the job";
			let jobRan = steady_clock::now()-posted;
			EXPECT_FALSE( discovering[0]->Done.test() || discovering[1]->Done.test() ) << "the discovers are still waiting on the listener";
			EXPECT_FALSE( Json::AsSV(node.as_object(), "name").empty() ) << serialize( node );
			EXPECT_LT( nodeAnswered, 2s ) << "the node query did not wait on the discovers";
			EXPECT_LT( jobRan, 1s ) << "a connect's job did not queue behind the discovers";
			for( let& d : discovering ){
				ASSERT_TRUE( d->Wait(10s) ) << "the discover answered";
				ASSERT_TRUE( d->Error ) << "a listener that never answers is the mutation's error: " << serialize( d->Result );
				TRACE( "discover unanswered: {}.", d->Error->what() );
				EXPECT_TRUE( sv{d->Error->what()}.contains("did not answer GetEndpoints") ) << d->Error->what();
			}
			let elapsed = steady_clock::now()-start;
			EXPECT_GE( elapsed, 2s ) << "the requestTimeout is the deadline";
			EXPECT_LT( elapsed, 8s ) << "the request's 2 s, not the gateway's 10 s, and the two at once";
		}
		{//A name that doesn't exist (.invalid, RFC 6761) is said so;  a lookup past its deadline - a nameserver that doesn't answer - names
			//the deadline, which is the request's where that is shorter than 3 s (review #5).
			try{
				Socket().QuerySync( "discover( url: $url, requestTimeout: $requestTimeout ){ url }", {{"url", "opc.tcp://jde-discover-test.invalid:4840"}, {"requestTimeout", 1000}} );
				ADD_FAILURE() << "a discover at a name that doesn't exist succeeded";
			}
			catch( const GatewayErrorResponse& e ){
				const sv what{ e.what() };
				TRACE( "discover unresolved: {}.", what );
				EXPECT_TRUE( what.contains("'jde-discover-test.invalid' did not resolve") ) << what;
				if( what.contains("within") )
					EXPECT_TRUE( what.contains("within 1000 ms") ) << "the request's deadline, not 3 s: " << what;
			}
		}
		try{
			Socket().QuerySync( "discover( url: $url ){ url }", {{"url", "opc.tcp://127.0.0.1:1"}} );
			ADD_FAILURE() << "a discover at a closed port succeeded";
		}
		catch( const GatewayErrorResponse& e ){
			TRACE( "discover refused: {}.", e.what() );
			EXPECT_TRUE( sv{e.what()}.contains("did not answer GetEndpoints") ) << e.what();
		}
		try{//over http, as the dialog sends it:  the body carried "Query failed." and the status alone, the reason dropped with what().
			jobject body{ {"query", "mutation discover( url: $url, servers: true ){ applicationUri }"}, {"variables", jobject{{"url", "opc.tcp://127.0.0.1:1"}}} };
			Post( GatewayPort(), "/graphql", serialize(body), Ƒ("{:x}", AppClient()->SessionId()) );
			ADD_FAILURE() << "a discover at a closed port succeeded over http";
		}
		catch( const ClientHttpResException& e ){
			TRACE( "discover refused over http: {}.", e.Res().Body() );
			let& body = e.Res().Body();
			let reason = body.find( " - 'opc.tcp://127.0.0.1:1' did not answer FindServers." );
			EXPECT_NE( reason, string::npos ) << "the reason: " << body;
			EXPECT_LT( body.find(")Bad"), reason ) << "the status before it: " << body;
		}
		EXPECT_THROW( Socket().QuerySync("discover( url: $url ){ url }", {{"url", " "}}), GatewayErrorResponse ) << "a url is required";
		EXPECT_THROW( Socket().QuerySync("discover( url: $url, requestTimeout: 0 ){ url }", {{"url", url}}), GatewayErrorResponse ) << "a positive timeout";
		try{//no longer than MaxTimeout:  a caller's deadline holds a discovery thread, and DiscoveryPool::Stop's join waits on it.
			Socket().QuerySync( "discover( url: $url, requestTimeout: $requestTimeout ){ url }", {{"url", url}, {"requestTimeout", DiscoverQLAwait::MaxTimeout.count()+1}} );
			ADD_FAILURE() << "a discover took a requestTimeout past MaxTimeout";
		}
		catch( const GatewayErrorResponse& e ){
			EXPECT_TRUE( sv{e.what()}.contains(Ƒ("from 1 to {}", DiscoverQLAwait::MaxTimeout.count())) ) << e.what();
		}
		{//Under serverConnections' Update:  enforced, a user no grant names is refused, as an ungranted viewer is Test Connection.
			constexpr Jde::UserPK ungranted{ (Jde::UserPK::Type)0x08'0000 };
			EnforceResource( "gateway", "serverConnections", true );
			absl::Cleanup _ = []{ try{ EnforceResource("gateway", "serverConnections", false); }catch( const std::exception& ){} };
			let refused = WaitUntil( [&]{
				try{
					auto ql = QL::Parse( Ƒ("discover( url: \"{}\" ){{ url }}", url), {}, Schemas(), true );
					BlockAwait<DiscoverQLAwait,jvalue>( DiscoverQLAwait{move(ql.Mutations().front()), QL::Creds{ungranted}} );
				}
				catch( const Exception& e ){
					return e.HttpStatus()==EHttpStatus::Unauthorized || e.HttpStatus()==EHttpStatus::Forbidden;
				}
				return false;
			}, 5s, 50ms );
			EXPECT_TRUE( refused ) << "gateway/serverConnections is enforced and an ungranted user still runs discover";
		}
		try{//a mutation:  a GET - a cross-site <img> - must not connect the gateway anywhere.
			Get( GatewayPort(), "/graphql?query=discover(url:%22opc.tcp://127.0.0.1:1%22)%7Burl%7D" );
			ADD_FAILURE() << "discover ran over GET";
		}
		catch( const ClientHttpResException& e ){
			EXPECT_EQ( e.Status(), boost::beast::http::status::bad_request ) << e.Res().Body();
			EXPECT_TRUE( e.Res().Body().contains("Mutations must use post.") ) << e.Res().Body();
		}
	}

	TEST_F( QLTests, multipleQueries ){
		let q =
			"connection: serverConnection( slug: $opc ){ id name slug url certificateUri defaultBrowseNs }"
			"server: serverDescription( opc: $opc ){ applicationUri productUri applicationName applicationType gatewayServerUri discoveryProfileUri discoveryUrls }"
			"policy: securityPolicyUri( opc: $opc )"
			"mode: securityMode( opc: $opc )"
			"namespaces( opc: $opc ){ index uri }";//unaliased, as the ui sends it - the result is keyed by the query name.
		const jobject vars{ {"opc", OpcServerSlug} };
		let value = Socket().QuerySync( q, vars, false );
		TRACE( "multipleQueries: {}.", serialize(value) );
		ASSERT_TRUE( serialize(value).size() );
		let& namespaces = Json::AsArray( value.as_object(), "namespaces" );//keyed by the query name, the alias the others carry.
		EXPECT_GE( namespaces.size(), 2u ) << serialize( value );
	}

	//GatewayErrorResponse means the gateway *answered* with an error, over a socket that works; a dead socket has to fail as a
	//plain Exception.  The soak's reconnect keys on the difference:  when CloseTasks stamped its transport failures as answers,
	//every dropped socket reset the soak's failure count and it never reconnected (subscription-disconnect #1).
	TEST_F( QLTests, errorsTypedByOrigin ){
		auto session = GatewayClientSocket::Open( "localhost", GatewayPort(), AppClient()->SessionId() );
		let q = "__type( opc: $opc, ns:2, i:6244 ){ enumValues{ id name } }";//answered with an error - enumTypes' no-such-connection case.
		const jobject vars{ {"opc", "no-such-connection"} };
		try{
			session->QuerySync( q, vars );
			ADD_FAILURE() << "a query on an unknown connection succeeded";
		}
		catch( const GatewayErrorResponse& ){}
		catch( const std::exception& e ){
			ADD_FAILURE() << "the gateway's answer did not arrive as a GatewayErrorResponse: " << e.what();
		}

		BlockAwait( session->Close(false, SRCE_CUR) );
		try{
			session->QuerySync( q, vars );//no stream: Write fails the request through CloseTasks( not_connected ).
			ADD_FAILURE() << "a query on a closed socket succeeded";
		}
		catch( const GatewayErrorResponse& e ){
			ADD_FAILURE() << "a closed socket's failure read as the gateway's answer: " << e.what();
		}
		catch( const std::exception& ){}
	}
}