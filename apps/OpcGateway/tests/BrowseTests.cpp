#include <jde/web/Jwt.h>
#include <jde/web/client/socket/ClientSocketAwait.h>
#include "../src/GatewayAppClient.h"
#include "../src/UAClient.h"
#include "../src/ql/GatewayQL.h"
#include "../src/ql/NodeQLAwait.h"
#include "utils/helpers.h"
#include "../src/types/UAClientException.h"
#define let const auto

namespace Jde::Opc::Gateway::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };
	struct BrowseTests : ::testing::Test{
	protected:
		Ω SetUpTestCase()ε->void{
			try{
				_jwt = BlockAwait<Web::Client::ClientSocketAwait<Jde::Web::Jwt>,Web::Jwt>( AppClient()->Jwt() );
				auto sessionId = *Str::TryTo<SessionPK>(_jwt->SessionId, nullptr, 16);
				TRACE( "UserPK: {:x}, SessionId: {:x}", _jwt->UserPK.Value, sessionId );
				auto con = GetConnection( OpcServerSlug );
				Credential cred{ _jwt->Payload() }; cred.SetUserPK( _jwt->UserPK );
				_client = BlockAwait<TAwait<sp<UAClient>>,sp<UAClient>>( ConnectAwait{move(con.Slug), cred} );
				AddSession( sessionId, OpcServerSlug, move(cred) );
			}
			catch( runtime_error& e ){
				INFOT( ELogTags::Test, "Failed to connect to gateway: {}", e.what() );
				THROW( "Failed to connect to gateway: {}", e.what() );
			}
		};
		Ω TearDownTestCase()ι->void{
			if( _client )
				UAClient::RemoveClient( move(_client) );
		}
		α SetUp()ι->void{}

		static optional<Web::Jwt> _jwt;
		static sp<UAClient> _client;
	};
	optional<Web::Jwt> BrowseTests::_jwt;
	sp<UAClient> BrowseTests::_client;

	//install-issues #24: a signed-out page is an anonymous web session - a real session id, no user, no stored credential - which
	//resolves to an anonymous credential.  Anonymous access is allowed while gateway/serverConnections is unenforced (the default),
	//so the hub does NOT refuse it: the connect is attempted, and here fails at the OpcServer, which offers no anonymous endpoint -
	//a 403, not the hub's own 401.  Once an admin enforces the resource, Authorize::Test throws Unauthorized for the unknown user
	//(AuthorizeTests) and the connect is refused before a client is made.  #39: that is now true of *every* web session, not only
	//an anonymous one - being signed in is no longer a way past the resource - so this test says what unenforced means, and
	//EnforcedConnectionResourceRefusesAnUngrantedSession below says what enforced does.
	TEST_F( BrowseTests, AnonymousWebSessionNotRefusedWhenUnenforced ){
		let sessionId = Web::Server::Sessions::Add( Jde::UserPK{}, "localhost", false )->SessionId;
		try{
			BlockAwait<TAwait<sp<UAClient>>,sp<UAClient>>( ConnectAwait{ string{OpcServerSlug}, sessionId, Jde::UserPK{} } );
		}
		catch( Exception& e ){
			EXPECT_NE( e.HttpStatus(), EHttpStatus::Unauthorized ) << "the hub refused an anonymous session while the resource was unenforced: " << e.what();
		}
	}

	//install-issues #39.  The gate used to run only for the *anonymous* credential, so a signed-in session walked past an enforced
	//gateway/serverConnections and opened a client on any connection it could name - measured on a live hub, where a user with no
	//grant anywhere was refused `serverConnections{}` and still read, browsed and wrote that connection's nodes.
	//The probe has to be **authenticated**, or it only re-tests the anonymous path this was always closed against:  the session
	//carries the harness's own jwt - a valid IssuedToken, so the connect would otherwise succeed - under a user id no grant names.
	//That is the whole of the finding:  a credential good enough to open a UA session, on a resource the admin has enforced and
	//this user holds nothing on.  The resource is restored before the test leaves - every other test in the process reads it.
	TEST_F( BrowseTests, EnforcedConnectionResourceRefusesAnUngrantedSession ){
		//`deleted` is the enforcement switch, so restore = enforce - the Resources page's toggle by another name.  The schemaName
		//is named as well as the slug:  one slug can belong to more than one schema, and only the gateway's is this test's.
		let enforce = []( bool on ){ AppClient()->QuerySync<jvalue>( Ƒ("mutation {}Resource( schemaName:\"gateway\", slug:\"serverConnections\", criteria:null )", on ? "restore" : "delete"), {} ); };
		enforce( true );
		struct Restore final{ decltype(enforce) F; ~Restore(){ try{ F(false); }catch( const std::exception& ){} } } _{ enforce };

		constexpr Jde::UserPK ungranted{ (Jde::UserPK::Type)0x39'0000 };//no acl, no role, no group names it
		let sessionId = Web::Server::Sessions::Add( ungranted, "localhost", false )->SessionId;
		Credential cred{ _jwt->Payload() }; cred.SetUserPK( ungranted );
		ASSERT_NE( cred.Type(), ETokenType::Anonymous ) << "the probe must be authenticated or it tests the wrong branch";
		AddSession( sessionId, OpcServerSlug, move(cred) );
		try{
			BlockAwait<TAwait<sp<UAClient>>,sp<UAClient>>( ConnectAwait{ string{OpcServerSlug}, sessionId, ungranted } );
			ADD_FAILURE() << "a signed-in session with no grant opened a client on an enforced connection resource";
		}
		catch( Exception& e ){
			EXPECT_EQ( e.HttpStatus(), EHttpStatus::Unauthorized ) << e.what();
		}
	}

	TEST_F( BrowseTests, NodeId ){
		auto query = "node( opc: $opc, path:$path ){ id name parents{id name path} }";
		jobject variables{ {"opc", OpcServerSlug}, {"path", "4~Examples/4~Stacklights/4~ExampleStacklight/4~Lamp1"} };
		auto ql = QL::Parse( move(query), move(variables), Schemas(), true );
		auto value = BlockAwait<NodeQLAwait, jvalue>( NodeQLAwait{move(ql.Queries().front()), _client} );
		TRACE( "value: {}", serialize(value) );
		auto result = ExNodeId{ value };
		ASSERT_TRUE( *result.Numeric()>0 );
		ASSERT_EQ( value.at("name"), "Lamp1" );
	}

	//The reverse of NodeId above:  a node known only by id answers `path` - the inverse-hierarchical walk up to the Objects
	//folder (NodeQLAwait::Path), spelled as BrowsePathsToNodeIds and the search index spell a path, so the answer routes
	//straight back into the path form and into the SPA's node url.  What the Effective rights tab links a node-scoped
	//resource ("ns=4;i=…") with.  The Objects folder itself has an empty path; a node outside its tree (a type) has none.
	TEST_F( BrowseTests, PathFromId ){
		constexpr sv path{ "4~Examples/4~Stacklights/4~ExampleStacklight/4~Lamp1" };
		auto byPath = QL::Parse( "node( opc: $opc, path:$path ){ id parents{ id } }", jobject{{"opc", OpcServerSlug}, {"path", path}}, Schemas(), true );//parents{}: the lamp hangs off a HasComponent, which the plain translate (Organizes only) cannot follow - the parents walk can, as NodeId above relies on.
		let lamp = ExNodeId{ BlockAwait<NodeQLAwait, jvalue>( NodeQLAwait{move(byPath.Queries().front()), _client} ) };
		auto byId = QL::Parse( "node( opc: $opc, id:$id ){ id name path }", jobject{{"opc", OpcServerSlug}, {"id", NodeId{lamp.nodeId}.ToJson()}}, Schemas(), true );
		let value = BlockAwait<NodeQLAwait, jvalue>( NodeQLAwait{move(byId.Queries().front()), _client} );
		TRACE( "value: {}", serialize(value) );
		EXPECT_EQ( Json::AsSV(value.as_object(), "path"), path );
		EXPECT_EQ( Json::AsSV(value.as_object(), "name"), "Lamp1" ) << "the read still answers alongside";
		EXPECT_TRUE( ExNodeId{value}.Numeric().has_value() );

		auto objects = QL::Parse( "node( opc: $opc, id:$id ){ path }", jobject{{"opc", OpcServerSlug}, {"id", NodeId::ObjectsFolder().ToJson()}}, Schemas(), true );
		let objectsValue = BlockAwait<NodeQLAwait, jvalue>( NodeQLAwait{move(objects.Queries().front()), _client} );//locals:  the template comma is one argument too many for the gtest macros
		EXPECT_EQ( Json::AsSV(objectsValue.as_object(), "path"), "" );

		auto type = QL::Parse( "node( opc: $opc, id:$id ){ path }", jobject{{"opc", OpcServerSlug}, {"id", NodeId{0, UA_NS0ID_BASEOBJECTTYPE}.ToJson()}}, Schemas(), true );
		let typeValue = BlockAwait<NodeQLAwait, jvalue>( NodeQLAwait{move(type.Queries().front()), _client} );
		EXPECT_TRUE( typeValue.as_object().at("path").is_null() ) << "the Types tree has no page";
	}

	//UserMessage() is the client-facing text.  Its combine branch tested _userMessage inside `if( _userMessage.empty() )`,
	//so it was dead and a 3-arg exception returned the bare description with the UA status stripped (review3 #14).  what()
	//is checked alongside to pin down why reviving that branch was not the fix:  ExternalException has already folded the
	//description into it, so combining with the base message would have repeated the description instead.
	TEST( UAClientExceptionTests, UserMessageCarriesStatusAndDescription ){
		const UAClientException status{ (StatusCode)UA_STATUSCODE_BADNODEIDUNKNOWN, Jde::Handle{0xABC}, RequestId{0xDEF} };
		ASSERT_EQ( status.ClientDetail(), "(80340000)BadNodeIdUnknown" );
		ASSERT_EQ( status.UserMessage(), "(80340000)BadNodeIdUnknown" ) << "no description, so the status alone - and never what()'s [handle.requestId] prefix";
		ASSERT_EQ( string{status.what()}, "[abc.def](80340000)BadNodeIdUnknown" );

		const UAClientException described{ (StatusCode)UA_STATUSCODE_BADNODEIDUNKNOWN, Jde::Handle{0xABC}, string{"applicationUri mismatch"} };
		ASSERT_EQ( described.UserMessage(), "(80340000)BadNodeIdUnknown - applicationUri mismatch" ) << "the status must survive alongside the description";
		ASSERT_EQ( string{described.what()}, "(80340000)BadNodeIdUnknown - applicationUri mismatch" );
	}

	//A server may answer a browse with a GOOD serviceResult and resultsSize==0 (results==nullptr);  FoldersAwait::OnComplete
	//treats that as success, and the QL path then visits it.  VisitWhile's only bounds check was an ASSERT_DESC, which
	//merely logs and carries on into results[0] (review3 #13).
	TEST( BrowseResponseTests, VisitWhileToleratesAnEmptyResult ){
		Browse::Response response{ UA_BrowseResponse{} };
		ASSERT_EQ( response.resultsSize, 0u );
		uint visits{};
		ASSERT_TRUE( response.VisitWhile(0, [&](const UA_ReferenceDescription&){ ++visits; return true; }) );
		ASSERT_EQ( visits, 0u );
	}

	//RemoveClient erased whatever occupied the (Slug,Credential) key without checking it was the client being removed.
	//A stale second remove of A - the UAClientException ctor's BadServerNotConnected path feeds them - then evicted the
	//replacement B that had reconnected under the same key, leaving B connected but unreachable through Find, holding its
	//monitored items, NodeIndex and EnumTypeCache (review3 #12).
	TEST( RemoveClientTests, StaleRemoveKeepsTheReplacement ){
		let jwt = BlockAwait<Web::Client::ClientSocketAwait<Jde::Web::Jwt>,Web::Jwt>( AppClient()->Jwt() );
		Credential cred{ jwt.Payload() }; cred.SetUserPK( jwt.UserPK );//the only credential this server accepts - anonymous is BadIdentityTokenRejected.
		auto a = BlockTAwait<sp<UAClient>>( ConnectAwait{string{OpcServerSlug}, cred} );
		ASSERT_TRUE( a );
		ASSERT_TRUE( UAClient::RemoveClient(sp<UAClient>{a}) );

		auto b = BlockTAwait<sp<UAClient>>( ConnectAwait{string{OpcServerSlug}, cred} );
		ASSERT_TRUE( b );
		ASSERT_NE( a, b ) << "expected a fresh client at the same key";
		ASSERT_EQ( UAClient::Find(OpcServerSlug, cred), b );

		UAClient::RemoveClient( sp<UAClient>{a} );//the stale double-remove of the predecessor
		ASSERT_EQ( UAClient::Find(OpcServerSlug, cred), b ) << "a stale remove of the predecessor evicted the replacement";
		UAClient::RemoveClient( move(b) );
	}

	//ReadRequest owns its node ids.  The test address space is all numeric, so a shallow UA_NodeId slice - which shares
	//identifier.string.data with a source the request outlives - shows up in no other test here; ASan reports the read
	//below as a heap-use-after-free without the deep copy (gateway-review3 #2).
	TEST( ReadRequestTests, OwnsNodeIdIdentifier ){
		let identifier = "a.string.identifier.long.enough.to.need.the.heap"s;
		//The temporary NodeId dies at the ';' - as the temporary vector<NodeId> does at NodeQLAwait.cpp:109, and as the
		//caller's Browse::Response does while the read is still in flight.
		ReadRequest request{ NodeId{UA_NODEID_STRING_ALLOC(2, "a.string.identifier.long.enough.to.need.the.heap")}, {UA_ATTRIBUTEID_VALUE} };
		ASSERT_EQ( request.nodesToReadSize, 1u );
		ASSERT_EQ( request.nodesToRead[0].nodeId.identifierType, UA_NODEIDTYPE_STRING );
		ASSERT_EQ( request.nodesToRead[0].nodeId.namespaceIndex, 2 );
		ASSERT_EQ( Opc::ToString(request.nodesToRead[0].nodeId.identifier.string), identifier );

		auto moved = move( request );//ReadAwait moves it in, then ReadResponse moves it again - neither may double-free.
		ASSERT_EQ( moved.nodesToReadSize, 1u );
		ASSERT_EQ( Opc::ToString(moved.nodesToRead[0].nodeId.identifier.string), identifier );
	}
}