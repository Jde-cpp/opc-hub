#include <absl/cleanup/cleanup.h>
#include "../src/GatewayAppClient.h"
#include "../src/UAClient.h"
#include "../src/ql/GatewayQL.h"
#include "../src/ql/NodeQLAwait.h"
#include "../src/async/TranslateBrowsePathsAwait.h"
#include "utils/helpers.h"
#include "utils/ITest.h"
#include "../src/types/UAClientException.h"
#include <jde/fwk/co/BlockAwait.h>
#define let const auto

namespace Jde::Opc::Gateway::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };
	struct BrowseTests : ITest{
	protected:
		//`query` through NodeQLAwait on the suite's client, as the node page asks it.
		Ω NodeQL( string query, jobject vars )ε->jvalue{
			auto ql = QL::Parse( move(query), move(vars), Schemas(), true );
			return BlockAwait<NodeQLAwait,jvalue>( NodeQLAwait{move(ql.Queries().front()), _client} );
		}
	};

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
		EnforceResource( "gateway", "serverConnections", true );
		absl::Cleanup _ = []{ try{ EnforceResource("gateway", "serverConnections", false); }catch( const std::exception& ){} };

		constexpr Jde::UserPK ungranted{ (Jde::UserPK::Type)0x39'0000 };//no acl, no role, no group names it
		let sessionId = Web::Server::Sessions::Add( ungranted, "localhost", false )->SessionId;
		auto cred = SessionCredential( ungranted );
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

	//reviews/m3-closing.md #3, ruled 09-21:  an unset Default Namespace means 1.  A connection saved without one - this helper's
	//shape, the installer's seeded row, every SPA create before the fix - is a NULL column, which the SPA reads as 1 and so spells
	//a namespace-1 child as a bare segment.  The gateway read the NULL as 0, so a reloaded, bookmarked or Recently-visited url of
	//a namespace-1 node translated to 0:name and answered "Not found."  All three readers have to agree with the SPA.
	TEST_F( BrowseTests, AnUnsetDefaultNamespaceIsOne ){
		let stored = QL().QuerySync<jobject>( Ƒ(R"(serverConnection( slug:"{}" ){{ id defaultBrowseNs }})", OpcServerSlug), {}, {UserPK::System} );
		let column = stored.if_contains( "defaultBrowseNs" );
		ASSERT_TRUE( !column || column->is_null() ) << "the precondition, a NULL column:  " << serialize( stored );
		let rows = BlockAwait( ServerCnnctnAwait{DB::Key{string{OpcServerSlug}}} );
		ASSERT_EQ( rows.size(), 1u );
		EXPECT_EQ( rows.front().DefaultBrowseNs, 1 ) << "the row ctor - what ConnectAwait builds a client from";
		EXPECT_EQ( ServerCnnctn{jobject{stored}}.DefaultBrowseNs, 1 ) << "the json ctor";
		EXPECT_EQ( _client->DefaultBrowseNs(), 1 ) << "the live client, which translates a bare path segment";
	}

	//reviews/m3-closing.md #9:  the node page's path query asks the node's class too, so a search hit or a url that names a
	//Variable - which has no page of its own - goes to its parent instead of being browsed as an object.  The class has to come
	//back as the number the SPA's ENodeClass compares.
	TEST_F( BrowseTests, APathQueryAnswersTheNodesClass ){
		auto run = [&]( string query, jobject vars ){
			let value = NodeQL( move(query), move(vars) );
			TRACE( "{}", serialize(value) );
			return value.as_object();
		};
		auto nodeClass = [&]( sv path ){//NodeResolver's query, parents and all
			return Json::FindNumber<uint>( run("node( opc: $opc, path:$path ){ id name nodeClass parents{id name path} }", {{"opc", OpcServerSlug}, {"path", path}}), "nodeClass" );
		};
		EXPECT_EQ( nodeClass("4~Examples/4~Stacklights/4~ExampleStacklight/4~Lamp1"), UA_NODECLASS_OBJECT );
		let path = string{ Json::AsSV(run("node( opc: $opc, id:$id ){ path }", {{"opc", OpcServerSlug}, {"id", Intensity.ToJson()}}), "path") };//the Variable SubscribeTests monitors - its page path, as the node-scoped resources' links find it
		EXPECT_EQ( nodeClass(path), UA_NODECLASS_VARIABLE ) << path;
	}

	TEST_F( BrowseTests, NodeId ){
		let value = NodeQL( "node( opc: $opc, path:$path ){ id name parents{id name path} }", {{"opc", OpcServerSlug}, {"path", "4~Examples/4~Stacklights/4~ExampleStacklight/4~Lamp1"}} );
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
		let lamp = ExNodeId{ NodeQL("node( opc: $opc, path:$path ){ id parents{ id } }", {{"opc", OpcServerSlug}, {"path", path}}) };//parents{}: the lamp hangs off a HasComponent, which the plain translate (Organizes only) cannot follow - the parents walk can, as NodeId above relies on.
		let value = NodeQL( "node( opc: $opc, id:$id ){ id name path }", {{"opc", OpcServerSlug}, {"id", NodeId{lamp.nodeId}.ToJson()}} );
		TRACE( "value: {}", serialize(value) );
		EXPECT_EQ( Json::AsSV(value.as_object(), "path"), path );
		EXPECT_EQ( Json::AsSV(value.as_object(), "name"), "Lamp1" ) << "the read still answers alongside";
		EXPECT_TRUE( ExNodeId{value}.Numeric().has_value() );

		let objectsValue = NodeQL( "node( opc: $opc, id:$id ){ path }", {{"opc", OpcServerSlug}, {"id", NodeId::ObjectsFolder().ToJson()}} );
		EXPECT_EQ( Json::AsSV(objectsValue.as_object(), "path"), "" );

		let typeValue = NodeQL( "node( opc: $opc, id:$id ){ path }", {{"opc", OpcServerSlug}, {"id", NodeId{0, UA_NS0ID_BASEOBJECTTYPE}.ToJson()}} );
		EXPECT_TRUE( typeValue.as_object().at("path").is_null() ) << "the Types tree has no page";
	}

	//Historian 5a (#222):  the translate follows HierarchicalReferences with subtypes, so a path through HasComponent resolves
	//without the parents walk NodeId/PathFromId lean on, and it starts where the caller says - a template's root (spec *Templates*),
	//the Objects folder by default.
	TEST_F( BrowseTests, TranslateFollowsHierarchicalReferencesFromAStart ){
		constexpr sv stacklight{ "4~Examples/4~Stacklights/4~ExampleStacklight" };
		const string lampPath{ string{stacklight}+"/4~Lamp1" };
		auto translate = [&]( string path, bool parents, NodeId start=NodeId::ObjectsFolder() ){
			auto prefixes = PathPrefixes( path, parents, _client->DefaultBrowseNs(), start );
			return ByPath( prefixes.Paths, BlockAwait(TranslateBrowsePathsAwait{move(prefixes.Requests), _client}) );
		};
		let direct = translate( lampPath, false );
		ASSERT_EQ( direct.size(), 1u );
		ASSERT_TRUE( direct.at(lampPath).has_value() ) << "the lamp hangs off a HasComponent - Organizes alone could not follow it:  " << UAException::Message( direct.at(lampPath).error() );
		let lamp = NodeId{ direct.at(lampPath)->nodeId }.ToString();

		let withParents = translate( lampPath, true );
		ASSERT_EQ( withParents.size(), 4u ) << "every prefix of the path";
		for( let& [path, node] : withParents )
			EXPECT_TRUE( node.has_value() ) << path;
		ASSERT_TRUE( withParents.at(string{stacklight}).has_value() );
		const NodeId root{ withParents.at(string{stacklight})->nodeId };

		let fromRoot = translate( "4~Lamp1", false, root );
		ASSERT_TRUE( fromRoot.at("4~Lamp1").has_value() ) << "a path relative to a start node";
		EXPECT_EQ( NodeId{fromRoot.at("4~Lamp1")->nodeId}.ToString(), lamp ) << "the same node the absolute path names";
		let elsewhere = translate( "4~Examples", false, root );
		EXPECT_FALSE( elsewhere.at("4~Examples").has_value() ) << "a name the start has no child by is no match, not the Objects folder's";
	}

	//A path with no segments - "" or "/" - names nothing to translate, with parents{} or without:  the query fails with the
	//server's BadNothingToDo.
	TEST_F( BrowseTests, EmptyPath ){
		for( let& [query, path] : vector<tuple<string,string>>{ {"node( opc: $opc, path:$path ){ id parents{ path } }", "/"}, {"node( opc: $opc, path:$path ){ id parents{ path } }", ""}, {"node( opc: $opc, path:$path ){ id }", "/"} } ){
			try{
				NodeQL( query, {{"opc", OpcServerSlug}, {"path", path}} );
				ADD_FAILURE() << query << " '" << path << "' answered";
			}
			catch( const Exception& e ){
				EXPECT_EQ( e.Code(), UA_STATUSCODE_BADNOTHINGTODO ) << query << " '" << path << "':  " << e.what();
			}
		}
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
		ASSERT_EQ( described.ClientDetail(), described.UserMessage() ) << "what a 500 body appends - the status alone lost the reason";
		ASSERT_EQ( string{described.what()}, "(80340000)BadNodeIdUnknown - applicationUri mismatch" );
	}

	//A batched Hierarchical request (PerNode) hands back a bad result instead of failing, and keeps the good ones;  a request
	//without PerNode still fails on a bad results[0] - the single-node contract every other caller relies on (opc-server-search.md #12).
	TEST_F( BrowseTests, PerNodeDeliversABadResult ){
		const NodeId missing{ UA_NODEID_STRING_ALLOC(1, "noSuchNode") };
		constexpr UA_BrowseResultMask mask{ UA_BROWSERESULTMASK_BROWSENAME };
		{
			vector<NodeId> ids; ids.push_back( NodeId::ObjectsFolder() ); ids.push_back( NodeId{missing} );
			auto request = Browse::Request::Hierarchical( move(ids), mask );
			ASSERT_TRUE( request.PerNode );
			let response = BlockAwait<Browse::FoldersAwait,Browse::Response>( Browse::FoldersAwait{move(request), _client} );
			ASSERT_EQ( response.resultsSize, 2u );
			EXPECT_EQ( response.results[0].statusCode, UA_STATUSCODE_GOOD );
			EXPECT_GT( response.results[0].referencesSize, 0u ) << "the good node's references must survive the bad one";
			EXPECT_EQ( response.results[1].statusCode, UA_STATUSCODE_BADNODEIDUNKNOWN );
		}
		{
			vector<NodeId> ids; ids.push_back( NodeId{missing} ); ids.push_back( NodeId::ObjectsFolder() );
			Browse::Request request{ move(ids), mask };//the plain constructor:  no PerNode.
			ASSERT_FALSE( request.PerNode );
			try{
				BlockAwait<Browse::FoldersAwait,Browse::Response>( Browse::FoldersAwait{move(request), _client} );
				ADD_FAILURE() << "a bad results[0] without PerNode did not fail the request";
			}
			catch( const UAException& e ){
				EXPECT_EQ( e.Code(), UA_STATUSCODE_BADNODEIDUNKNOWN ) << e.what();
			}
		}
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
		let cred = SessionCredential( Jwt().UserPK );//the only credential this server accepts - anonymous is BadIdentityTokenRejected.
		auto a = BlockAwait( ConnectAwait{string{OpcServerSlug}, cred} );
		ASSERT_TRUE( a );
		ASSERT_TRUE( UAClient::RemoveClient(sp<UAClient>{a}) );

		auto b = BlockAwait( ConnectAwait{string{OpcServerSlug}, cred} );
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