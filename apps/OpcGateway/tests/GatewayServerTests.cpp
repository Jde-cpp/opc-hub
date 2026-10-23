//Servers phase 2 (#382, docs/servers/plan.md):  the gateway's own OPC UA endpoint on 4941, beside the embedded OpcServer's 4940 -
//two UA servers in one process.  A UA client logs in by issued token, by certificate and by user name, browses
//Objects/<slug>/_Connection, and sees Connected, Url and LastError follow the slug's clients and the folders follow the rows.
#include <jde/fwk/io/json.h>
#include <jde/tests/WaitUntil.h>
#include <jde/opc/tests/UATestClient.h>
#include <jde/opc/server/UAServer.h>
#include <jde/ql/LocalQL.h>
#include "../src/GatewayAppClient.h"
#include "../src/ql/GatewayQL.h"
#include "../src/server/GatewayServer.h"
#include "utils/ClientFixture.h"
#define let const auto

namespace Jde::Opc::Gateway::Tests{
	using Opc::Tests::UATestClient;
	constexpr UA_UInt16 _own{ 1 };//the endpoint's own namespace, where its folders are (spec *Namespaces*).

	struct GatewayServerTests : ClientFixture{
		Ω Ssl()ι->Crypto::CryptoSettings{ return Crypto::CryptoSettings{ Json::FindDefaultObject(Settings::AsObject("/http/gateway"), "ssl"), {} }; }//the process pair:  the endpoint's, and the client's own.
		Ω Url()ι->string{ return Ƒ( "opc.tcp://127.0.0.1:{}", Settings::FindNumber<PortType>("/gateway/server/port").value_or(4841) ); }
		Ω Issued()ι->UA_ExtensionObject{ return UATestClient::Issued( Ƒ("{:x}", AppClient()->SessionId()) ); }//this program's hub session, answered over the socket - the split shape.
		Ω Client( UA_ExtensionObject identity )ε->up<UATestClient>{
			auto y = mu<UATestClient>( Url(), Ssl(), identity );
			y->ConnectOrThrow();
			return y;
		}
		Ω Variable( UATestClient& client, const ServerCnnctnNK& slug, sv name )ι->optional<NodeId>{ return client.Translate( NodeId::ObjectsFolder(), {{_own, slug}, {_own, "_Connection"}, {_own, string{name}}} ); }
		Ω Boolean( const Value& v )ι->optional<bool>{ return v.hasValue && UA_Variant_hasScalarType(&v.value, &UA_TYPES[UA_TYPES_BOOLEAN]) ? optional<bool>{ *(UA_Boolean*)v.value.data } : nullopt; }
		Ω String( const Value& v )ι->optional<string>{ return v.hasValue && UA_Variant_hasScalarType(&v.value, &UA_TYPES[UA_TYPES_STRING]) ? optional<string>{ ToString(*(UA_String*)v.value.data) } : nullopt; }
		Ω Connected( UATestClient& client, const ServerCnnctnNK& slug )ι->optional<bool>{ auto node = Variable( client, slug, "Connected" ); return node ? Boolean( client.Read(*node) ) : nullopt; }
		Ω Reconnects( UATestClient& client, const ServerCnnctnNK& slug )ι->optional<uint32>{
			auto node = Variable( client, slug, "Reconnects" );
			if( !node )
				return nullopt;
			let v = client.Read( *node );
			return v.hasValue && UA_Variant_hasScalarType(&v.value, &UA_TYPES[UA_TYPES_UINT32]) ? optional<uint32>{ *(UA_UInt32*)v.value.data } : nullopt;
		}
	};

	TEST_F( GatewayServerTests, TheBlockIsReadAndTheEndpointRuns ){
		auto server = GatewayServer::Instance();
		ASSERT_TRUE( server ) << "the tests config has a /gateway/server block";
		EXPECT_EQ( server->Settings().Port, Settings::FindNumber<PortType>("/gateway/server/port").value_or(0) );
		EXPECT_FALSE( server->Settings().Users.empty() ) << "the block's users";
		EXPECT_TRUE( server->Server() );
		let settings = GatewayServer::ReadSettings( Ssl() );
		ASSERT_TRUE( settings );
		EXPECT_TRUE( settings->None );
		EXPECT_EQ( settings->Ssl.Certificate.Path, Ssl().Certificate.Path ) << "no ssl in the block:  the process pair";
	}

	//The suite's connection's folder, with its _Connection and the five variables typed as the spec has them.
	TEST_F( GatewayServerTests, BrowsesTheConnectionFolder ){
		let connection = GetConnection( OpcServerSlug );
		auto client = Client( Issued() );
		auto folder = client->Translate( NodeId::ObjectsFolder(), {{_own, connection.Slug}} );
		ASSERT_TRUE( folder );
		EXPECT_EQ( *folder, GatewayServer::FolderId(connection.Slug) );
		auto objects = client->Browse( NodeId::ObjectsFolder() );
		EXPECT_TRUE( std::ranges::any_of(objects, [&](let& c){ return c.BrowseName==connection.Slug; }) );
		auto children = client->Browse( *folder );
		ASSERT_EQ( children.size(), 1u );
		EXPECT_EQ( children[0].BrowseName, "_Connection" );
		auto variables = client->Browse( children[0].Id );
		flat_set<string> names;
		for( let& v : variables )
			names.emplace( v.BrowseName );
		EXPECT_EQ( names, (flat_set<string>{"Connected", "Since", "Url", "LastError", "Reconnects"}) );
		let url = Variable( *client, connection.Slug, "Url" );
		ASSERT_TRUE( url );
		EXPECT_EQ( String(client->Read(*url)), optional<string>{connection.Url} );
		let since = Variable( *client, connection.Slug, "Since" );
		ASSERT_TRUE( since );
		let sinceValue = client->Read( *since );
		EXPECT_TRUE( UA_Variant_hasScalarType(&sinceValue.value, &UA_TYPES[UA_TYPES_DATETIME]) );
	}

	//Connected follows the slug's live clients:  true while one has an activated session, false once it is removed;  a client
	//that comes back after a loss counts a reconnect.
	TEST_F( GatewayServerTests, ConnectedFollowsTheClients ){
		auto client = Client( Issued() );
		let slug = GetConnection( OpcServerSlug ).Slug;
		ASSERT_EQ( Attempt(slug, SessionCredential()), "" );
		EXPECT_TRUE( WaitUntil([&]{ return Connected(*client, slug)==optional<bool>{true}; }) ) << "a live client, and Connected stays false";
		let before = Reconnects( *client, slug ).value_or( 0 );
		UAClient::ConnectionLost( sp<UAClient>{_client} );//as a dropped connection takes it:  Lost, then the next activation is a reconnect.
		_client = nullptr;
		ASSERT_EQ( Attempt(slug, SessionCredential()), "" );
		EXPECT_TRUE( WaitUntil([&]{ return Reconnects(*client, slug)==optional<uint32>{before+1}; }) );
		EXPECT_EQ( Connected(*client, slug), optional<bool>{true} );
	}

	//A row nothing can reach:  its folder serves from the row, Connected false, and a failed connect lands in LastError.
	TEST_F( GatewayServerTests, LastErrorNamesTheFailure ){
		const string slug{ "gatewayServerTestsDead" };
		PurgeServerCnnctn( slug );
		let connection = GetConnection( slug, "opc.tcp://127.0.0.1:1", "" );
		absl::Cleanup purge = [&]{ PurgeServerCnnctn( connection.Id ); };
		auto client = Client( Issued() );
		ASSERT_TRUE( WaitUntil([&]{ return Variable(*client, slug, "Connected").has_value(); }) ) << "the hook adds the folder once the row is written";
		EXPECT_EQ( Connected(*client, slug), optional<bool>{false} );
		let error = Variable( *client, slug, "LastError" );
		ASSERT_TRUE( error );
		EXPECT_EQ( String(client->Read(*error)), optional<string>{""} ) << "no attempt yet";
		EXPECT_NE( Attempt(slug, SessionCredential()), "" ) << "nothing listens on port 1";
		EXPECT_TRUE( WaitUntil([&]{ auto s = String( client->Read(*error) ); return s && s->size(); }) );
		EXPECT_EQ( Connected(*client, slug), optional<bool>{false} );
	}

	//A folder per row:  a row made while the endpoint runs gets its folder, and a purge takes it;  a delete leaves it.
	TEST_F( GatewayServerTests, FoldersFollowTheRows ){
		const string slug{ "gatewayServerTestsRow" };
		PurgeServerCnnctn( slug );
		auto client = Client( Issued() );
		EXPECT_FALSE( client->Translate(NodeId::ObjectsFolder(), {{_own, slug}}) );
		let connection = GetConnection( slug, "opc.tcp://127.0.0.1:2", "" );
		EXPECT_TRUE( WaitUntil([&]{ return client->Translate(NodeId::ObjectsFolder(), {{_own, slug}}).has_value(); }) );
		QL().QuerySync<jvalue>( Ƒ("mutation deleteServerConnection( id:{} )", connection.Id), {}, {UserPK::System} );//the gateway's QL, the table's.
		EXPECT_TRUE( client->Translate(NodeId::ObjectsFolder(), {{_own, slug}}) ) << "a deleted row keeps its folder";
		PurgeServerCnnctn( connection.Id );
		EXPECT_TRUE( WaitUntil([&]{ return !client->Translate(NodeId::ObjectsFolder(), {{_own, slug}}).has_value(); }) );
	}

	//The three token types, each a hub user:  the pair itself as an x509 token, whose key is this program's enrolled user;  the
	//block's user name;  and a token that is no session is refused.
	TEST_F( GatewayServerTests, LogsInByCertificateAndUserName ){
		auto certificate = Client( UATestClient::Certificate(Ssl()) );
		EXPECT_TRUE( certificate->Translate(NodeId::ObjectsFolder(), {{_own, GetConnection(OpcServerSlug).Slug}}) );
		let users = GatewayServer::Instance()->Settings().Users;
		ASSERT_FALSE( users.empty() );
		auto username = Client( UATestClient::Username(users[0].first, users[0].second) );
		EXPECT_FALSE( username->Browse(NodeId::ObjectsFolder()).empty() );
		UATestClient wrongPassword{ Url(), Ssl(), UATestClient::Username(users[0].first, users[0].second+"x") };
		EXPECT_EQ( wrongPassword.Connect(), UA_STATUSCODE_BADUSERACCESSDENIED );
		UATestClient noSession{ Url(), Ssl(), UATestClient::Issued("abc") };
		EXPECT_NE( noSession.Connect(), UA_STATUSCODE_GOOD );
	}
}