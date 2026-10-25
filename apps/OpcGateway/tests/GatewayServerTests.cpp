//Servers phase 2 (#382, docs/servers/plan.md):  the gateway's own OPC UA endpoint on 4941, beside the embedded OpcServer's 4940 -
//two UA servers in one process.  A UA client logs in by issued token, by certificate and by user name, browses
//Objects/<slug>/_Connection, and sees Connected, Url and LastError follow the slug's clients and the folders follow the rows.
#include <jde/fwk/crypto/issue.h>
#include <jde/fwk/io/file.h>
#include <jde/fwk/io/json.h>
#include <jde/tests/WaitUntil.h>
#include <jde/opc/tests/UATestClient.h>
#include <jde/opc/server/UAServer.h>
#include <jde/opc/server/UATrust.h>
#include <jde/ql/LocalQL.h>
#include "../src/GatewayAppClient.h"
#include "../src/ql/GatewayQL.h"
#include "../src/server/GatewayServer.h"
#include "utils/ClientFixture.h"
#include "utils/GatewayClientSocket.h"
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
		//A fresh pair in `dir`, under no trusted dir:  a client the endpoint refuses.
		Ω UntrustedPair( const fs::path& dir )ε->Crypto::CryptoSettings{
			const jobject settings{
				{"certificate", jobject{ {"path", (dir/"untrusted.pem").string()}, {"commonName", "gatewayServerTests.untrusted"}, {"subjectAltName", "URI:urn:jde:gatewayServerTests:untrusted,DNS:localhost"} }},
				{"privateKey", jobject{ {"path", (dir/"untrusted.key").string()} }},
				{"publicKey", jobject{ {"path", (dir/"untrusted.pub").string()} }}
			};
			Crypto::CryptoSettings y{ settings };
			y.CreateDirectories();
			Crypto::EnsureKeyCertificate( y );
			return y;
		}
		//Where the endpoint writes `pair`'s certificate once it refuses it:  rejectedCertDir/<thumbprint>.der.
		Ω RejectedFile( const Crypto::CryptoSettings& pair )ε->fs::path{
			let der = Crypto::ReadCertificate( pair.Certificate.Path );
			string thumbprint{ Crypto::Certificate{std::span<const byte>{der}}.Fingerprint };
			std::erase( thumbprint, ':' );
			return GatewayServer::Instance()->Settings().RejectedCertDir/Ƒ( "{}.der", thumbprint );
		}
		//Connects with `pair` until `written` exists.  An issued token, which the endpoint offers, or the client never opens a secured
		//channel to present its certificate on;  and again past a refusal within 50 ms of another, which writes nothing (UATrust's
		//rescan throttle).
		Ω RefuseUntilWritten( const Crypto::CryptoSettings& pair, const fs::path& written )ε->bool{
			UATestClient client{ Url(), pair, Issued(), UA_MESSAGESECURITYMODE_SIGNANDENCRYPT };
			return WaitUntil( [&]{ EXPECT_NE( client.Connect(), UA_STATUSCODE_GOOD ); return fs::exists(written); }, 10s, 60ms );
		}
		//A row of its own on the embedded OpcServer:  no other test's client shares the slug, so its sessions are the test's alone.
		Ω OwnConnection( const ServerCnnctnNK& slug )ε->ServerCnnctn{
			PurgeServerCnnctn( slug );
			return GetConnection( slug, OpcUrl(), "" );
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
		EXPECT_EQ( settings->WriteTimeout, optional<Duration>{10s} ) << "writeTimeout's default:  how long a Write holds for the source";
		EXPECT_EQ( settings->Limit.MaxSecureChannels, optional<uint16>{120} ) << "the block's limits";
		EXPECT_FALSE( settings->Limit.MaxSessions );
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
	//that comes back after a loss counts a reconnect.  A row of its own:  another test's client of the suite's slug, still up, would
	//make the loss no outage.
	TEST_F( GatewayServerTests, ConnectedFollowsTheClients ){
		const string slug{ "gatewayServerTestsClients" };
		let connection = OwnConnection( slug );
		absl::Cleanup purge = [&]{ PurgeServerCnnctn( connection.Id ); };
		auto client = Client( Issued() );
		ASSERT_EQ( Attempt(slug, SessionCredential()), "" );
		EXPECT_TRUE( WaitUntil([&]{ return Connected(*client, slug)==optional<bool>{true}; }) ) << "a live client, and Connected stays false";
		let before = Reconnects( *client, slug ).value_or( 0 );
		UAClient::ConnectionLost( sp<UAClient>{_client} );//as a dropped connection takes it:  Lost, then the next activation is a reconnect.
		_client = nullptr;
		ASSERT_EQ( Attempt(slug, SessionCredential()), "" );
		EXPECT_TRUE( WaitUntil([&]{ return Reconnects(*client, slug)==optional<uint32>{before+1}; }) );
		EXPECT_EQ( Connected(*client, slug), optional<bool>{true} );
	}

	//open62541 re-activating a registered client in place - its session went, and the client reconnects without a ConnectionLost - is
	//a loss and a reconnect too (reviews-issue/servers-2-endpoint.md #6).  Re-activating the current session takes the same path
	//through StateCallback:  out of Activated, then back.
	TEST_F( GatewayServerTests, AnInPlaceReconnectCounts ){
		const string slug{ "gatewayServerTestsInPlace" };
		let connection = OwnConnection( slug );
		absl::Cleanup purge = [&]{ PurgeServerCnnctn( connection.Id ); };
		auto client = Client( Issued() );
		ASSERT_EQ( Attempt(slug, SessionCredential()), "" );
		ASSERT_TRUE( WaitUntil([&]{ return Connected(*client, slug)==optional<bool>{true}; }) );
		let before = Reconnects( *client, slug ).value_or( 0 );
		let registered = _client;
		struct Activation final{ std::atomic<StatusCode> Status{ UA_STATUSCODE_BADINTERNALERROR }; std::atomic_flag Done; };
		auto activation = ms<Activation>();//the post's own:  it may outlive a wait that timed out.
		registered->PostUA( [activation, registered]{//UA_Client_* on the client's strand alone.
			activation->Status = UA_Client_activateCurrentSession( *registered );
			activation->Done.test_and_set();
		} );
		ASSERT_TRUE( WaitUntil([&]{ return activation->Done.test(); }) );
		ASSERT_EQ( activation->Status.load(), UA_STATUSCODE_GOOD ) << UAException::Message( activation->Status.load() );
		EXPECT_TRUE( WaitUntil([&]{ return Reconnects(*client, slug)==optional<uint32>{before+1}; }) );
		EXPECT_EQ( Connected(*client, slug), optional<bool>{true} );
		EXPECT_EQ( UAClient::Find(slug, SessionCredential()), registered ) << "a reconnect in place keeps the client";
	}

	//Reconnects counts the slug's outages, not a client's:  one credential's client lost while another's session stays up is none, so
	//its next connect is no reconnect (reviews-issue/servers-2-endpoint.md #6).
	TEST_F( GatewayServerTests, OneCredentialsLossIsNoOutage ){
		const string slug{ "gatewayServerTestsCredentials" };
		let connection = OwnConnection( slug );
		absl::Cleanup purge = [&]{ PurgeServerCnnctn( connection.Id ); };
		auto client = Client( Issued() );
		let other = TryConnect( slug, Credential{User{"user1", "0123456789ABCD"}} );//the OpcServer's user-name login:  a second credential.
		ASSERT_TRUE( other ) << other.error()->what();
		ASSERT_EQ( Attempt(slug, SessionCredential()), "" );
		ASSERT_TRUE( WaitUntil([&]{ return Connected(*client, slug)==optional<bool>{true}; }) );
		let before = Reconnects( *client, slug ).value_or( 0 );
		UAClient::ConnectionLost( sp<UAClient>{_client} );
		_client = nullptr;
		ASSERT_EQ( Attempt(slug, SessionCredential()), "" );
		std::this_thread::sleep_for( 100ms );//the Lost and the Activated both applied.
		EXPECT_EQ( Reconnects(*client, slug), optional<uint32>{before} ) << "a loss while another credential's session stayed up counted as an outage";
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

	//LastError is the last failure as a live Failed words it, "<status> - <detail>", and a recovery keeps it:  an endpoint started
	//after both reads the same.  It was seeded from connectionStatus's error, the detail alone, which a success clears
	//(reviews-issue/servers-2-endpoint.md #11).
	TEST_F( GatewayServerTests, LastErrorOutlivesARecoveryAndARestart ){
		const string slug{ "gatewayServerTestsLastError" };
		let connection = OwnConnection( slug );
		absl::Cleanup purge = [&]{ PurgeServerCnnctn( connection.Id ); };
		let lastError = [&]( UATestClient& reader )->optional<string>{ auto node = Variable( reader, slug, "LastError" ); return node ? String( reader.Read(*node) ) : nullopt; };
		optional<string> live;
		{
			auto client = Client( Issued() );
			EXPECT_NE( Attempt(slug, Credential{User{"user1", "not the password"}}), "" );
			ASSERT_TRUE( WaitUntil([&]{ live = lastError( *client ); return live && live->size(); }) );
			EXPECT_TRUE( live->contains(" - ") ) << *live;
			ASSERT_EQ( Attempt(slug, SessionCredential()), "" ) << "the recovery";
			EXPECT_FALSE( UAClient::ConnectErrors().contains(slug) ) << "connectionStatus's error clears on a success";
		}
		GatewayServer::Stop();
		ASSERT_NO_THROW( GatewayServer::Listen(Ssl()) );
		ASSERT_NO_THROW( GatewayServer::Start() );//its first values from the registry and the failures, after both.
		auto client = Client( Issued() );
		EXPECT_EQ( lastError(*client), live ) << "the restarted endpoint lost the failure, or words it differently";
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
		//the folder's delete takes its children (reviews-issue/servers-2-endpoint.md #15).
		EXPECT_EQ( client->Read(GatewayServer::ConnectionId(slug)).status, UA_STATUSCODE_BADNODEIDUNKNOWN );
		for( let name : {"Connected", "Since", "Url", "LastError", "Reconnects"} )
			EXPECT_EQ( client->Read(GatewayServer::VariableId(slug, name)).status, UA_STATUSCODE_BADNODEIDUNKNOWN ) << name;
	}

	//A purge and the row made again under its slug, back to back:  applied in that order, the new row keeps a folder, with its own
	//url.  On the executor's many threads the create could run first, find the old slug and do nothing, and the purge then took
	//the folder (reviews-issue/servers-2-endpoint.md #5).
	TEST_F( GatewayServerTests, APurgedSlugMadeAgainKeepsItsFolder ){
		const string slug{ "gatewayServerTestsAgain" };
		PurgeServerCnnctn( slug );
		auto client = Client( Issued() );
		let url = [&]()->optional<string> { auto node = Variable( *client, slug, "Url" ); return node ? String( client->Read(*node) ) : nullopt; };
		let first = GetConnection( slug, "opc.tcp://127.0.0.1:3", "" );
		ASSERT_TRUE( WaitUntil([&]{ return url()==optional<string>{"opc.tcp://127.0.0.1:3"}; }) );
		PurgeServerCnnctn( first.Id );
		let second = GetConnection( slug, "opc.tcp://127.0.0.1:4", "" );
		absl::Cleanup purge = [&]{ PurgeServerCnnctn( second.Id ); };
		EXPECT_TRUE( WaitUntil([&]{ return url()==optional<string>{"opc.tcp://127.0.0.1:4"}; }) );
		std::this_thread::sleep_for( 100ms );//the posts all applied:  nothing left to take the folder.
		EXPECT_EQ( url(), optional<string>{"opc.tcp://127.0.0.1:4"} );
	}

	//The row's slug, url and id bound through variables, as an API client binds them:  the folder is the slug's and follows the url
	//and the purge, where the mutation's Args hold each value's `\b$name` marker (reviews-issue/servers-2-endpoint.md #7).
	TEST_F( GatewayServerTests, VariablesReachTheFolder ){
		const string slug{ "gatewayServerTestsVariables" };
		PurgeServerCnnctn( slug );
		auto client = Client( Issued() );
		let url = [&]()->optional<string> { auto node = Variable( *client, slug, "Url" ); return node ? String( client->Read(*node) ) : nullopt; };
		QL().QuerySync<jvalue>( "mutation createServerConnection( slug:$slug, name:$slug, url:$url, certificateUri:\"\", isDefault:false ){id}", {{"slug", slug}, {"url", "opc.tcp://127.0.0.1:5"}}, {UserPK::System} );
		let row = SelectServerCnnctn( {slug} );
		ASSERT_TRUE( row ) << "a slug bound through a variable was refused";
		absl::Cleanup purge = [&]{ PurgeServerCnnctn( slug ); };
		EXPECT_TRUE( WaitUntil([&]{ return url()==optional<string>{"opc.tcp://127.0.0.1:5"}; }) );
		QL().QuerySync<jvalue>( "mutation updateServerConnection( id:$id, url:$url )", {{"id", row->Id}, {"url", "opc.tcp://127.0.0.1:6"}}, {UserPK::System} );
		EXPECT_TRUE( WaitUntil([&]{ return url()==optional<string>{"opc.tcp://127.0.0.1:6"}; }) );
		QL().QuerySync<jvalue>( "mutation purgeServerConnection( id:$id )", {{"id", row->Id}}, {UserPK::System} );
		EXPECT_TRUE( WaitUntil([&]{ return !client->Translate(NodeId::ObjectsFolder(), {{_own, slug}}).has_value(); }) );
	}

	//A client the endpoint does not trust is refused, and its certificate lands in rejectedCertDir as <thumbprint>.der:  DER, the
	//leaf alone, as UaExpert and `openssl x509 -inform der` read it (reviews-issue/servers-2-endpoint.md #12).
	TEST_F( GatewayServerTests, ARefusedCertificateIsWrittenAsDer ){
		let dir = fs::temp_directory_path()/Ƒ( "gatewayServerTests-{}", Process::ProcessId() );
		absl::Cleanup removeDir = [&]{ std::error_code ec; fs::remove_all( dir, ec ); };
		let untrusted = UntrustedPair( dir );
		let der = Crypto::ReadCertificate( untrusted.Certificate.Path );
		let written = RejectedFile( untrusted );
		absl::Cleanup removeWritten = [&]{ std::error_code ec; fs::remove( written, ec ); };
		ASSERT_TRUE( RefuseUntilWritten(untrusted, written) );
		let bytes = IO::LoadBinary( written );
		EXPECT_TRUE( std::ranges::equal(bytes, der, []( char a, byte b ){ return (byte)a==b; }) ) << "the certificate's DER, byte for byte";
	}

	//The rejected directory keeps the newest UATrust::MaxRejected certificates:  a flood of fresh ones, an unauthenticated OPN apiece,
	//fills no disk, and a client refused after it is still written (reviews-issue/servers-2-endpoint.md #4).
	TEST_F( GatewayServerTests, TheRejectedDirKeepsTheNewest ){
		let rejectedDir = GatewayServer::Instance()->Settings().RejectedCertDir;//the suite's own.
		fs::remove_all( rejectedDir );
		fs::create_directories( rejectedDir );
		absl::Cleanup clear = [&]{ std::error_code ec; fs::remove_all( rejectedDir, ec ); };
		let old = fs::file_time_type::clock::now()-1h;
		for( uint i=0; i<Opc::Server::UATrust::MaxRejected; ++i ){//as many as it keeps, flood-000 the oldest.
			let path = rejectedDir/Ƒ( "flood-{:03}.der", i );
			std::ofstream{ path } << "a flood's";
			fs::last_write_time( path, old+std::chrono::seconds(i) );
		}
		let dir = fs::temp_directory_path()/Ƒ( "gatewayServerTests-keep-{}", Process::ProcessId() );
		absl::Cleanup removeDir = [&]{ std::error_code ec; fs::remove_all( dir, ec ); };
		let untrusted = UntrustedPair( dir );
		let written = RejectedFile( untrusted );
		ASSERT_TRUE( RefuseUntilWritten(untrusted, written) ) << "the newest refused was not written";
		EXPECT_TRUE( WaitUntil([&]{ return !fs::exists(rejectedDir/"flood-000.der"); }) ) << "the oldest was kept";
		uint count{};
		for( let& entry : fs::directory_iterator(rejectedDir) )
			count += entry.path().extension()==".der";
		EXPECT_EQ( count, Opc::Server::UATrust::MaxRejected );
		EXPECT_TRUE( fs::exists(rejectedDir/"flood-001.der") ) << "more than the oldest went";
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
	//Phase 7 (#388):  endpoint{…}, what the web's Endpoint tab reads - the url, the policies offered, the trust directories and the
	//sessions held now, this client's among them while it is connected and gone once it isn't.
	TEST_F( GatewayServerTests, EndpointQueryDescribesTheEndpointAndItsSessions ){
		constexpr sv q{ "endpoint{ url policies{ uri mode } trustedCertDirs rejectedCertDir sessions{ id type policy mode since user{ id name } } }" };
		let own = [&]( const jvalue& answer )->optional<jobject>{
			for( let& row : Json::AsArray(answer.as_object(), "sessions") ){
				let& o = row.as_object();
				if( Json::AsSV(o, "type")=="IssuedToken" && Json::FindNumberPath<Jde::UserPK::Type>(o, "user/id")==AppClient()->UserPK().Value )
					return o;
			}
			return nullopt;
		};
		string id;
		{
			auto client = Client( Issued() );
			let answer = Socket().QuerySync( string{q} );
			let& endpoint = answer.as_object();
			EXPECT_TRUE( Json::AsSV(endpoint, "url").ends_with(Ƒ(":{}", Settings::FindNumber<PortType>("/gateway/server/port").value_or(4841))) ) << serialize( answer );
			let& policies = Json::AsArray( endpoint, "policies" );
			EXPECT_TRUE( std::ranges::any_of(policies, []( let& p ){ return Json::AsSV(p.as_object(), "uri").ends_with("#Basic256Sha256") && Json::AsSV(p.as_object(), "mode")=="SignAndEncrypt"; }) ) << serialize( policies );
			EXPECT_EQ( Json::AsArray(endpoint, "trustedCertDirs").size(), GatewayServer::Instance()->Settings().TrustedCertDirs.size() );
			let session = own( answer );
			ASSERT_TRUE( session ) << serialize( answer );
			EXPECT_EQ( Json::AsSV(*session, "policy"), ToSV(UA_SECURITY_POLICY_NONE_URI) );//UATestClient's channel is None unless asked otherwise.
			EXPECT_EQ( Json::AsSV(*session, "mode"), "None" );
			EXPECT_FALSE( Json::AsSV(*session, "since").empty() );
			EXPECT_TRUE( Json::AsObject(*session, "user").contains("name") ) << serialize( *session );//from the AppServer's users table, as opcSessions.
			id = Json::AsString( *session, "id" );
		}
		EXPECT_TRUE( WaitUntil([&]{ return !own( Socket().QuerySync(string{q}) ); }) ) << "session '" << id << "' is listed after its client disconnected";
	}
}