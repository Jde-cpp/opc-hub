#include <jde/fwk/process/execution.h>
#include "utils/GatewayClientSocket.h"
#include "utils/helpers.h"
#include <jde/fwk/str.h>
#include <jde/web/client/proto/Web.FromServer.pb.h>
#include "../src/GatewayAppClient.h"
#include "../src/auth/OpcServerSession.h"
#include <jde/web/server/Sessions.h>

#define let const auto

namespace Jde::Opc::Gateway::Tests{
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

	struct QLTests : ::testing::Test{
	protected:
		Ω SetUpTestCase()ε->void{ //ε: CreateServerCnnctn throws - under ι gtest never sees it and the whole binary terminates.
			if( !SelectServerCnnctn( OpcServerSlug ) )
				CreateServerCnnctn();
		};
	};

	TEST_F( QLTests, ServerDescriptionTest ){
		let q = "serverDescription( opc: $opc ){ applicationUri productUri applicationName applicationType gatewayServerUri discoveryProfileUri discoveryUrls }";
		const jobject vars{ {"opc", OpcServerSlug} };
		let value = BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>(	Socket().Query(q, vars, true) );
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

	TEST_F( QLTests, namespaces ){
		let q = "namespaces( opc: $opc ){ index uri }";
		const jobject vars{ {"opc", OpcServerSlug} };
		let value = BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>( Socket().Query(q, vars, true) );
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
		let value = BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>(	Socket().Query(q, vars, true) );
		TRACE( "securityPolicyUri: {}.", serialize(value) );
		ASSERT_TRUE( serialize(value).size() );
	}

	TEST_F( QLTests, securityMode ){
		let q = "securityMode( opc: $opc )";
		const jobject vars{ {"opc", OpcServerSlug} };
		let value = BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>(	Socket().Query(q, vars, true) );
		TRACE( "securityMode: {}.", serialize(value) );
		ASSERT_TRUE( serialize(value).size() );
	}
	TEST_F( QLTests, opcSessions ){
		let sessionId = addSession( "opcSessionsTestToken" );
		let q = "opcSessions{ connection{slug} type user{id slug name} count }";
		let value = BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>( Socket().Query(q, {}, true) );
		TRACE( "opcSessions: {}.", serialize(value) );
		let projected = BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>( Socket().Query("opcSessions{ count }", {}, true) );
		removeSession( sessionId );

		let& rows = value.as_array();
		let userPK = AppClient()->UserPK().Value;
		auto row = find_if( rows, [&](let& r){
			let& o = r.as_object();
			return Json::AsSVPath(o, "connection/slug")==OpcServerSlug && Json::AsSV(o, "type")=="IssuedToken" && Json::FindNumberPath<Jde::UserPK::Type>(o, "user/id")==userPK; //user may be null for anonymous rows.
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

	TEST_F( QLTests, webSessionCounted ){ //no manual AddSession: a jwt-backed web session's connect must register itself (ConnectAwait::await_resume).
		const jobject vars{ {"opc", OpcServerSlug} };
		BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>( Socket().Query("serverDescription( opc: $opc ){ applicationUri }", vars, true) );//forces a ConnectAwait for this socket's session.
		let value = BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>( Socket().Query("opcSessions{ connection{slug} type user{id} count }", {}, true) );
		TRACE( "webSessionCounted: {}.", serialize(value) );
		let userPK = AppClient()->UserPK().Value;
		let& rows = value.as_array();
		let row = find_if( rows, [&](let& r){
			let& o = r.as_object();
			return Json::AsSVPath(o, "connection/slug")==OpcServerSlug && Json::AsSV(o, "type")=="IssuedToken" && Json::FindNumberPath<Jde::UserPK::Type>(o, "user/id")==userPK;
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
		BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>( Socket().Query("serverDescription( opc: $opcSlug ){ applicationUri }", {{"opcSlug", OpcServerSlug}}, true) );//ensure a live UAClient so opcConnections has something to count.
		//the exact shape View.query() emits for the Connections list - args must survive the graft's DB pass.
		let listQL = "serverConnections(limit:25,orderBy:[{name:\"asc\"}],deleted:$deleted){ id connectionStatus{name} name certificateUri url opcSessions{count} opcConnections{count} description slug }";
		let rows = BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>( Socket().Query(listQL, {{"deleted",nullptr}}, true) );
		TRACE( "serverConnections: {}.", serialize(rows) );
		const jobject vars{ {"opc", OpcServerSlug} };
		let single = BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>( Socket().Query("serverConnection( slug: $opc ){ name opcSessions{count} }", vars, true) );
		TRACE( "serverConnection: {}.", serialize(single) );
		removeSession( sessionId );

		let connection = SelectServerCnnctn( OpcServerSlug ); ASSERT_TRUE( connection );
		bool found{};
		for( let& r : rows.as_array() ){
			let& o = r.as_object();
			EXPECT_TRUE( o.contains("slug") ) << serialize( o ); //requested explicitly here, so it must not be erased.
			let count = Json::FindNumberPath<uint32>( o, "opcSessions/count" );
			ASSERT_TRUE( count ) << serialize( o );
			let connectionCount = Json::FindNumberPath<uint32>( o, "opcConnections/count" );
			ASSERT_TRUE( connectionCount ) << serialize( o );
			let status = Json::FindSVPath( o, "connectionStatus/name" );
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
		EXPECT_GE( Json::FindNumberPath<uint32>(o, "opcSessions/count").value_or(0), 1u ) << serialize( o );
	}

	TEST_F( QLTests, search ){
		const jobject vars{ {"opc", OpcServerSlug} };
		Socket().QuerySync( "serverDescription( opc: $opc ){ applicationUri }", vars );//search never connects - give this socket's session a live client first.
		constexpr sv lampPath{ "4~Examples/4~Stacklights/4~ExampleStacklight/4~Lamp1" };//BrowseTests.NodeId resolves the same path.
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
		EXPECT_EQ( Json::AsSVPath(o, "connection/slug"), OpcServerSlug );
		EXPECT_EQ( Json::AsSVPath(o, "browse/name"), "Lamp1" );
		EXPECT_EQ( Json::AsNumber<uint16>(o.at("browse").as_object(), "ns"), 4 );
		EXPECT_EQ( Json::AsNumber<uint8>(o, "depth"), 4 );
		EXPECT_TRUE( o.contains("ns") && o.contains("i") ) << "id is spelled the way node{id} spells it: " << serialize( o );
		EXPECT_TRUE( o.contains("nodeClass") ) << serialize( o );
		for( let& row : first )//substring, case-insensitive, on the display or browse name.
			EXPECT_TRUE( Str::ToLower(Json::AsString(row.as_object(), "name")).contains("lamp1") || Str::ToLower(Json::AsSVPath(row.as_object(), "browse/name")).contains("lamp1") ) << serialize( row );

		let again = rows( "search( opc: $opc, text: \"LAMP1\" ){ path }" );//served from the index, case-folded.
		EXPECT_EQ( again.size(), first.size() );
		ASSERT_NE( find(again, lampPath), again.end() ) << serialize( again );
		EXPECT_EQ( again.front().as_object().size(), 1u ) << "projection: only the requested column";

		let fanout = rows( "search( text: \"lamp1\" ){ connection{ slug } path }" );//no opc: every client this session already holds.
		ASSERT_NE( find(fanout, lampPath), fanout.end() ) << serialize( fanout );

		let unknown = rows( "search( opc: \"noSuchConnection\", text: \"lamp1\" ){ path }" );//no live client ⇒ empty, and no ConnectAwait (which would throw 'not found').
		EXPECT_TRUE( unknown.empty() ) << serialize( unknown );

		let refreshed = rows( "search( opc: $opc, text: \"lamp1\", refresh: true ){ path }" );
		ASSERT_NE( find(refreshed, lampPath), refreshed.end() ) << serialize( refreshed );

		let limited = rows( "search( opc: $opc, text: \"a\", limit: 3 ){ path }" );
		EXPECT_EQ( limited.size(), 3u ) << serialize( limited );

		let blank = rows( "search( opc: $opc, text: \"  \" ){ path }" );
		EXPECT_TRUE( blank.empty() ) << serialize( blank );
	}

	TEST_F( QLTests, searchIntrospection ){
		constexpr sv fieldsQL{ "{ fields{ name type{ name kind ofType{ name kind } } } }" };
		for( sv typeName : {"Search"sv, "search"sv} ){ //both spellings are declared in config/introspection/search.jsonnet.
			let value = Socket().QuerySync( Ƒ("__type( name: \"{}\" ){}", typeName, fieldsQL), {} );
			let& fields = Json::AsArray( value.as_object(), "fields" );
			auto find = [&]( sv name ){ return find_if( fields, [&](let& f){ return Json::AsSV(f.as_object(), "name")==name; } ); };
			for( sv name : {"connection"sv, "id"sv, "path"sv, "name"sv, "browse"sv, "nodeClass"sv, "depth"sv} )
				EXPECT_NE( find(name), fields.end() ) << name << ": " << serialize( value );
			auto connection = find( "connection" );
			ASSERT_NE( connection, fields.end() );
			EXPECT_EQ( Json::AsSVPath(connection->as_object(), "type/name"), "SearchConnection" );
		}
	}

	TEST_F( QLTests, serverConnectionIntrospection ){
		constexpr sv fieldsQL{ "{ fields{ name type{ name kind ofType{ name kind } } } }" };
		for( sv typeName : {"ServerConnection"sv, "serverConnections"sv} ){ //both spellings are declared in config/introspection/serverConnection.jsonnet.
			let value = Socket().QuerySync( Ƒ("__type( name: \"{}\" ){}", typeName, fieldsQL), {} );
			TRACE( "__type({}): {}.", typeName, serialize(value) );
			let& fields = Json::AsArray( value.as_object(), "fields" );
			auto find = [&]( sv name ){ return find_if( fields, [&](let& f){ return Json::AsSV(f.as_object(), "name")==name; } ); };
			ASSERT_NE( find("url"), fields.end() ) << serialize( value ); //extend:true keeps the DB columns.
			auto opcSessions = find( "opcSessions" );
			ASSERT_NE( opcSessions, fields.end() ) << serialize( value );
			EXPECT_EQ( Json::AsSVPath(opcSessions->as_object(), "type/kind"), "OBJECT" );
			EXPECT_EQ( Json::AsSVPath(opcSessions->as_object(), "type/name"), "OpcSessions" );
			auto opcConnections = find( "opcConnections" );
			ASSERT_NE( opcConnections, fields.end() ) << serialize( value );
			EXPECT_EQ( Json::AsSVPath(opcConnections->as_object(), "type/name"), "OpcConnections" );
			auto connectionStatus = find( "connectionStatus" );
			ASSERT_NE( connectionStatus, fields.end() ) << serialize( value );
			EXPECT_EQ( Json::AsSVPath(connectionStatus->as_object(), "type/kind"), "OBJECT" );
			EXPECT_EQ( Json::AsSVPath(connectionStatus->as_object(), "type/name"), "ConnectionStatus" );
		}
		{ //the status type carries a name and the reason the last connect failed - the SPA's list selects only the name.
			let value = Socket().QuerySync( Ƒ("__type( name: \"ConnectionStatus\" ){}", fieldsQL), {} );
			let& fields = Json::AsArray( value.as_object(), "fields" );
			ASSERT_EQ( fields.size(), 2u ) << serialize( value );
			EXPECT_EQ( Json::AsSV(fields[0].as_object(), "name"), "name" );
			EXPECT_EQ( Json::AsSVPath(fields[0].as_object(), "type/ofType/name"), "String" );
			EXPECT_EQ( Json::AsSV(fields[1].as_object(), "name"), "error" );
			EXPECT_EQ( Json::AsSVPath(fields[1].as_object(), "type/name"), "String" );
		}
		for( sv typeName : {"OpcSessions"sv, "OpcConnections"sv} ){ //config-only types - no view behind them.
			let value = Socket().QuerySync( Ƒ("__type( name: \"{}\" ){}", typeName, fieldsQL), {} );
			TRACE( "__type({}): {}.", typeName, serialize(value) );
			let& fields = Json::AsArray( value.as_object(), "fields" );
			ASSERT_EQ( fields.size(), 1u ) << serialize( value );
			let& count = fields[0].as_object();
			EXPECT_EQ( Json::AsSV(count, "name"), "count" );
			EXPECT_EQ( Json::AsSVPath(count, "type/kind"), "NON_NULL" );
			EXPECT_EQ( Json::AsSVPath(count, "type/ofType/name"), "UInt" );
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

	TEST_F( QLTests, multipleQueries ){
		let q =
			"connection: serverConnection( slug: $opc ){ id name slug url certificateUri defaultBrowseNs }"
			"server: serverDescription( opc: $opc ){ applicationUri productUri applicationName applicationType gatewayServerUri discoveryProfileUri discoveryUrls }"
			"policy: securityPolicyUri( opc: $opc )"
			"mode: securityMode( opc: $opc )"
			"namespaces( opc: $opc ){ index uri }";//unaliased, as the ui sends it - the result is keyed by the query name.
		const jobject vars{ {"opc", OpcServerSlug} };
		let value = BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>(	Socket().Query(q, vars, false) );
		TRACE( "multipleQueries: {}.", serialize(value) );
		ASSERT_TRUE( serialize(value).size() );
		let& namespaces = Json::AsArray( value.as_object(), "namespaces" );//keyed by the query name, the alias the others carry.
		EXPECT_GE( namespaces.size(), 2u ) << serialize( value );
	}

	//GatewayErrorResponse means the gateway *answered* with an error, over a socket that works; a dead socket has to fail as a
	//plain Exception.  The soak's reconnect keys on the difference:  when CloseTasks stamped its transport failures as answers,
	//every dropped socket reset the soak's failure count and it never reconnected (subscription-disconnect #1).
	TEST_F( QLTests, errorsTypedByOrigin ){
		optional<ssl::context> ctx;
		auto session = ms<GatewayClientSocket>( Executor(), ctx );
		BlockVoidAwait( session->RunSession("localhost", GatewayPort()) );
		BlockAwait<Web::Client::ClientSocketAwait<uint32>,uint>( session->Connect(AppClient()->SessionId()) );
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

		BlockVoidAwait( session->Close(false, SRCE_CUR) );
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