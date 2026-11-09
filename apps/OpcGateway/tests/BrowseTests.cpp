#include <absl/cleanup/cleanup.h>
#include "../src/GatewayAppClient.h"
#include "../src/UAClient.h"
#include "../src/ql/GatewayQL.h"
#include "../src/ql/NodeQLAwait.h"
#include "../src/async/ReleaseBrowseAwait.h"
#include "../src/async/TranslateBrowsePathsAwait.h"
#include "utils/helpers.h"
#include "utils/ITest.h"
#include "utils/Pumps.h"
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
	//Variable, which had no page of its own then, went to its parent instead of being browsed as an object.  The class has to come
	//back as the number the SPA's ENodeClass compares.  The page path a node id answers runs from Root (#416), so the path form
	//takes it under `root: Root`.
	TEST_F( BrowseTests, APathQueryAnswersTheNodesClass ){
		auto run = [&]( string query, jobject vars ){
			let value = NodeQL( move(query), move(vars) );
			TRACE( "{}", serialize(value) );
			return value.as_object();
		};
		auto nodeClass = [&]( sv path, sv root ){//NodeResolver's query, parents and all
			return Json::FindNumber<uint>( run(Ƒ("node( opc: $opc, path:$path, root:\"{}\" ){{ id name nodeClass parents{{id name path}} }}", root), {{"opc", OpcServerSlug}, {"path", path}}), "nodeClass" );
		};
		EXPECT_EQ( nodeClass("4~Examples/4~Stacklights/4~ExampleStacklight/4~Lamp1", "Objects"), UA_NODECLASS_OBJECT );
		let path = string{ Json::AsSV(run("node( opc: $opc, id:$id ){ path }", {{"opc", OpcServerSlug}, {"id", Intensity.ToJson()}}), "path") };//the Variable SubscribeTests monitors - its page path, as the node-scoped resources' links find it
		EXPECT_EQ( nodeClass(path, "Root"), UA_NODECLASS_VARIABLE ) << path;
	}

	TEST_F( BrowseTests, NodeId ){
		let value = NodeQL( "node( opc: $opc, path:$path ){ id name parents{id name path} }", {{"opc", OpcServerSlug}, {"path", "4~Examples/4~Stacklights/4~ExampleStacklight/4~Lamp1"}} );
		TRACE( "value: {}", serialize(value) );
		auto result = ExNodeId{ value };
		ASSERT_TRUE( *result.Numeric()>0 );
		ASSERT_EQ( value.at("name"), "Lamp1" );
	}

	//The reverse of NodeId above:  a node known only by id answers `path` - the inverse-hierarchical walk up to Root
	//(ParentPathsAwait), spelled as the search index spells a path, so the answer routes straight back into the path form under
	//`root: Root` and into the SPA's node url.  What the Effective rights tab links a node-scoped resource ("ns=4;i=…") with.
	//Root itself has an empty path, the connection's page;  the Objects folder's is its bare namespace-0 name, as a type's is
	//(RootPathsResolveTypesAndWalkBack).
	TEST_F( BrowseTests, PathFromId ){
		constexpr sv path{ "4~Examples/4~Stacklights/4~ExampleStacklight/4~Lamp1" };
		let lamp = ExNodeId{ NodeQL("node( opc: $opc, path:$path ){ id parents{ id } }", {{"opc", OpcServerSlug}, {"path", path}}) };//parents{}: the lamp hangs off a HasComponent, which the plain translate (Organizes only) cannot follow - the parents walk can, as NodeId above relies on.
		let value = NodeQL( "node( opc: $opc, id:$id ){ id name path }", {{"opc", OpcServerSlug}, {"id", NodeId{lamp.nodeId}.ToJson()}} );
		TRACE( "value: {}", serialize(value) );
		EXPECT_EQ( Json::AsSV(value.as_object(), "path"), Ƒ("Objects/{}", path) );
		EXPECT_EQ( Json::AsSV(value.as_object(), "name"), "Lamp1" ) << "the read still answers alongside";
		EXPECT_TRUE( ExNodeId{value}.Numeric().has_value() );

		let objectsValue = NodeQL( "node( opc: $opc, id:$id ){ path }", {{"opc", OpcServerSlug}, {"id", NodeId::ObjectsFolder().ToJson()}} );
		EXPECT_EQ( Json::AsSV(objectsValue.as_object(), "path"), "Objects" );

		let rootValue = NodeQL( "node( opc: $opc, id:$id ){ path }", {{"opc", OpcServerSlug}, {"id", NodeId::RootFolder().ToJson()}} );
		EXPECT_EQ( Json::AsSV(rootValue.as_object(), "path"), "" );
	}

	//reviews-issue/416-address-space.md #3, #13:  the node page's path query asks a Variable's columns plainly - the path form reads no
	//`... on Variable{}` fragment, as it learns the class with the columns - for the node and for its parents.  A Variable answers them;
	//an object answers each with its status, not an error.
	TEST_F( BrowseTests, PathReadsVariableColumnsOfEveryClass ){
		let ns = PumpsNs(); let defaultNs = _client->DefaultBrowseNs();
		auto segment = [&]( sv name ){ return ns==defaultNs ? string{name} : Ƒ("{}~{}", ns, name); };
		let path = Ƒ( "Objects/{}/{}", segment("pump1"), segment("motorRpm") );
		let value = NodeQL( "node( opc: $opc, path:$path, root:\"Root\" ){ id nodeClass value historizing parents{ id nodeClass path value historizing } }", {{"opc", OpcServerSlug}, {"path", path}} ).as_object();
		TRACE( "{}: {}", path, serialize(value) );
		EXPECT_EQ( Json::FindNumber<uint>(value, "nodeClass"), UA_NODECLASS_VARIABLE ) << serialize( value );
		EXPECT_EQ( value.at("historizing"), jvalue{true} ) << serialize( value );
		EXPECT_TRUE( value.contains("value") ) << serialize( value );
		let& parents = value.at( "parents" ).as_array();
		auto pump = find_if( parents, [&]( let& p ){ return NodeId{p}==NodeId(ns, Pump1); } );
		ASSERT_NE( pump, parents.end() ) << serialize( value );
		let& o = pump->as_object();
		EXPECT_EQ( Json::FindNumber<uint>(o, "nodeClass"), UA_NODECLASS_OBJECT ) << serialize( o );
		EXPECT_EQ( Json::FindNumber<StatusCode>(o, "historizing/sc"), UA_STATUSCODE_BADATTRIBUTEIDINVALID ) << serialize( o );
		EXPECT_EQ( Json::FindNumber<StatusCode>(o, "value/sc"), UA_STATUSCODE_BADATTRIBUTEIDINVALID ) << serialize( o );
	}

	//reviews-issue/416-address-space.md #11:  the path form's level-by-level browse followed every forward reference, so a segment could
	//resolve through a non-hierarchical one - a pump's PumpType by its HasTypeDefinition - to a page for a node the pump's Children
	//table doesn't list.  It walks what `children` lists, as the translate does:  forward HierarchicalReferences.
	TEST_F( BrowseTests, PathFollowsHierarchicalReferencesOnly ){
		let ns = PumpsNs(); let defaultNs = _client->DefaultBrowseNs();
		auto segment = [&]( sv name ){ return ns==defaultNs ? string{name} : Ƒ("{}~{}", ns, name); };
		let typeDef = Ƒ( "Objects/{}/{}", segment("pump1"), segment("PumpType") );
		for( let parents : {false, true} ){
			let value = NodeQL( Ƒ("node( opc: $opc, path:$path, root:\"Root\" ){{ id{} }}", parents ? " parents{ id }" : ""), {{"opc", OpcServerSlug}, {"path", typeDef}} ).as_object();
			EXPECT_EQ( Json::FindNumber<StatusCode>(value, "sc"), UA_STATUSCODE_BADNOMATCH ) << "parents:" << parents << " " << serialize( value );
		}
	}

	//reviews-issue/416-address-space.md #7:  a namespace-0 name and the default namespace's, siblings, were both spelled bare - one url
	//for two nodes, which the path form answers with the default's (childNamed).  The default's keeps the bare name and its namespace-0
	//twin is `0~name`, in the id form's path and in the search index's, so each leads back to its own node.  A folder and the two
	//twins are added to the embedded server for the test, under Objects.
	TEST_F( BrowseTests, NamespaceZeroTwinIsSpelledApart ){
		let defaultNs = _client->DefaultBrowseNs();
		if( defaultNs==0 )
			GTEST_SKIP() << "the default namespace is namespace 0:  no twin to tell apart";
		auto& ua = Server::GetUAServer();
		const UA_NodeId folderId = UA_NODEID_STRING( PumpsNs(), (char*)"BrowseTests.twins" ), ns0Id = UA_NODEID_STRING( PumpsNs(), (char*)"BrowseTests.ns0Twin" ), ownId = UA_NODEID_STRING( PumpsNs(), (char*)"BrowseTests.ownTwin" );
		const NodeId folder{ folderId }, ns0{ ns0Id }, own{ ownId };//copied:  from the prvalue it would take the literal, and free it.
		vector<NodeId> added;
		absl::Cleanup remove = [&]{
			for( auto p = added.rbegin(); p!=added.rend(); ++p )
				UA_Server_deleteNode( ua.Ptr(), *p, true );
		};
		ASSERT_EQ( UA_Server_addObjectNode(ua.Ptr(), folder, UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES), UA_QUALIFIEDNAME(defaultNs, (char*)"twins"), UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE), UA_ObjectAttributes_default, nullptr, nullptr), UA_STATUSCODE_GOOD );
		added.push_back( folder );
		for( let& [id, ns] : {std::pair<const NodeId*,NsIndex>{&ns0, 0}, std::pair<const NodeId*,NsIndex>{&own, defaultNs}} ){
			ASSERT_EQ( UA_Server_addVariableNode(ua.Ptr(), *id, folder, UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT), UA_QUALIFIEDNAME(ns, (char*)"Twin"), UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), UA_VariableAttributes_default, nullptr, nullptr), UA_STATUSCODE_GOOD );
			added.push_back( *id );
		}
		const string ns0Path{ "Objects/twins/0~Twin" }, ownPath{ "Objects/twins/Twin" };
		let rows = NodeQL( "nodes( opc: $opc, id:$id ){ id path }", {{"opc", OpcServerSlug}, {"id", jarray{ns0.ToJson(), own.ToJson()}}} );
		TRACE( "twins: {}", serialize(rows) );
		ASSERT_EQ( rows.as_array().size(), 2u ) << serialize( rows );
		EXPECT_EQ( Json::AsSV(rows.as_array()[0].as_object(), "path"), ns0Path );
		EXPECT_EQ( Json::AsSV(rows.as_array()[1].as_object(), "path"), ownPath );
		for( let& [path, id] : {std::pair<sv,const NodeId*>{ns0Path, &ns0}, std::pair<sv,const NodeId*>{ownPath, &own}} ){
			for( let query : {"node( opc: $opc, path:$path, root:\"Root\" ){ id }"sv, "node( opc: $opc, path:$path, root:\"Root\" ){ id parents{ id } }"sv} ){
				let node = NodeQL( string{query}, {{"opc", OpcServerSlug}, {"path", path}} );
				EXPECT_EQ( NodeId{ExNodeId{node}.nodeId}, *id ) << path << " " << query << ": " << serialize( node );
			}
		}
		BlockAwait( _client->Index().Ready(_client, true) );
		flat_map<NodeId,string> indexed;
		for( let& e : _client->Index().Search("twin", 0) )
			indexed.emplace( e.Id, e.Path );
		EXPECT_EQ( indexed[ns0], ns0Path );
		EXPECT_EQ( indexed[own], ownPath );
	}

	//Phase 4 (#416, spec *Address space*):  the path form's `root` - Objects by default, so today's urls and the historian's template
	//paths stand, or Root, the web's pages - and the walk back up from an id, which stops at Root, so a type node has a path.  A
	//namespace-0 name is spelled bare, as the default namespace's are:  the translate takes the first segment from Root in namespace 0
	//and the level-by-level browse the rest (Browse.h), with parents{} and without.
	TEST_F( BrowseTests, RootPathsResolveTypesAndWalkBack ){
		let ns = PumpsNs(); let defaultNs = _client->DefaultBrowseNs();
		auto segment = [&]( sv name ){ return ns==defaultNs ? string{name} : Ƒ("{}~{}", ns, name); };
		constexpr sv base{ "Types/ObjectTypes/BaseObjectType" };
		const string pumpType = Ƒ( "{}/{}", base, segment("PumpType") ), pump1 = Ƒ( "Objects/{}", segment("pump1") );
		auto byPath = [&]( string path, sv root, bool parents ){
			let value = NodeQL( Ƒ("node( opc: $opc, path:$path, root:\"{}\" ){{ id name nodeClass{} }}", root, parents ? " parents{ id path }" : ""), {{"opc", OpcServerSlug}, {"path", path}} );
			TRACE( "{}: {}", path, serialize(value) );
			return value.as_object();
		};
		let baseType = byPath( string{base}, "Root", true );
		EXPECT_EQ( NodeId{jvalue{baseType}}, NodeId(0, UA_NS0ID_BASEOBJECTTYPE) );
		EXPECT_EQ( Json::AsSV(baseType, "name"), "BaseObjectType" );
		EXPECT_EQ( Json::FindNumber<uint>(baseType, "nodeClass"), UA_NODECLASS_OBJECTTYPE );
		vector<string> parentPaths;
		for( let& parent : baseType.at("parents").as_array() )
			parentPaths.push_back( Json::AsString(parent.as_object(), "path") );
		EXPECT_EQ( parentPaths, (vector<string>{"Types", "Types/ObjectTypes"}) );
		EXPECT_EQ( NodeId{jvalue{byPath(string{base}, "Root", false)}}, NodeId(0, UA_NS0ID_BASEOBJECTTYPE) ) << "without parents{}:  every segment but the first browsed level by level";
		let pumpTypeNode = byPath( pumpType, "Root", true );
		EXPECT_EQ( NodeId{jvalue{pumpTypeNode}}, NodeId(ns, 1001) ) << serialize( pumpTypeNode );
		EXPECT_EQ( Json::AsSV(pumpTypeNode, "name"), "PumpType" );
		EXPECT_EQ( NodeId{jvalue{byPath(pump1, "Root", false)}}, NodeId(ns, Pump1) ) << "an object under Objects, from Root";
		EXPECT_EQ( NodeId{jvalue{byPath(segment("pump1"), "Objects", false)}}, NodeId(ns, Pump1) ) << "today's url, from the Objects folder";
		let lampPath = "4~Examples/4~Stacklights/4~ExampleStacklight/4~Lamp1"s;
		EXPECT_EQ( NodeId{jvalue{byPath(lampPath, "Objects", true)}}, NodeId{jvalue{byPath("Objects/"+lampPath, "Root", true)}} ) << "the same node either way";
		try{
			byPath( string{base}, "Views", false );
			ADD_FAILURE() << "a root the form does not take answered";
		}
		catch( const Exception& e ){
			EXPECT_TRUE( string{e.what()}.contains("root is 'Objects' or 'Root'") ) << e.what();
		}

		auto pathOf = [&]( const NodeId& id ){ return string{ Json::AsSV(NodeQL("node( opc: $opc, id:$id ){ path }", {{"opc", OpcServerSlug}, {"id", id.ToJson()}}).as_object(), "path") }; };
		EXPECT_EQ( pathOf(NodeId(0, UA_NS0ID_BASEOBJECTTYPE)), base );
		EXPECT_EQ( pathOf(NodeId(ns, 1001)), pumpType );
		EXPECT_EQ( pathOf(NodeId(ns, Pump1)), pump1 );
		EXPECT_EQ( pathOf(NodeId(0, UA_NS0ID_TYPESFOLDER)), "Types" );
	}

	//Every class the browse answers is kept (spec *Address space*), and `children` is what hangs under a node, its hierarchical
	//references forward:  Root's are the standard's three folders, ObjectTypes' are object types, and a pump's are its variables -
	//its type, reached by HasTypeDefinition, is no child.  A Variable page lists its `children` too (reviews-issue/416-address-space.md
	//#4):  pump3's motorRpm has its two ranges and, by a HasHistoricalConfiguration, its HA Configuration;  `properties` is the
	//HasProperty ones alone.
	TEST_F( BrowseTests, ChildrenAreHierarchicalOfEveryClass ){
		let ns = PumpsNs();
		auto rows = [&]( const NodeId& id, sv table ){
			let value = NodeQL( Ƒ("node( opc: $opc, id:$id ){{ {}{{ id name browse nodeClass }} }}", table), {{"opc", OpcServerSlug}, {"id", id.ToJson()}} );
			TRACE( "{} {}: {}", id.ToString(), table, serialize(value) );
			return value.as_object().at( table ).as_array();
		};
		auto names = []( const jarray& a ){ vector<string> y; for( let& r : a ) y.push_back( Json::AsString(r.as_object(), "name") ); std::ranges::sort( y ); return y; };
		auto classOf = []( const jarray& a, sv name )->optional<uint>{ for( let& r : a ) if( Json::AsSV(r.as_object(), "name")==name ) return Json::FindNumber<uint>( r.as_object(), "nodeClass" ); return {}; };
		let root = rows( NodeId::RootFolder(), "children" );
		EXPECT_EQ( names(root), (vector<string>{"Objects", "Types", "Views"}) );
		let objectTypes = rows( NodeId(0, UA_NS0ID_OBJECTTYPESFOLDER), "children" );
		EXPECT_EQ( classOf(objectTypes, "BaseObjectType"), UA_NODECLASS_OBJECTTYPE );
		let pump = rows( NodeId(ns, Pump1), "children" );
		EXPECT_EQ( names(pump), (vector<string>{"motorRpm", "reasonCode", "rejectCount", "status", "totalCount"}) ) << "PumpType is the pump's type definition, not a child";
		EXPECT_EQ( classOf(pump, "status"), UA_NODECLASS_VARIABLE );
		let rpm = rows( NodeId(ns, Rpm3), "children" );
		EXPECT_EQ( names(rpm), (vector<string>{"EURange", "HA Configuration", "InstrumentRange"}) );
		let properties = rows( NodeId(ns, Rpm3), "properties" );
		EXPECT_EQ( names(properties), (vector<string>{"EURange", "InstrumentRange"}) );
	}

	//reviews-issue/416-address-space.md #9:  `references` was every reference in both directions, uncapped - a type node's inverse
	//HasTypeDefinition is every instance it has - and a server's own cap cut the list short unsaid, its continuation point left
	//holding one of the session's slots.  The read stops at /gateway/browse/maxReferences or the server's cap, says `truncated`,
	//and releases the point:  a second release of it is refused.  The embedded server caps at 2 for the test.
	TEST_F( BrowseTests, ReferencesPastTheCapAreTruncatedAndReleased ){
		auto& config = *UA_Server_getConfig( Server::GetUAServer().Ptr() );
		let limit = config.maxReferencesPerNode;
		absl::Cleanup restore = [&config, limit]{ config.maxReferencesPerNode = limit; };
		const NodeId pump1{ PumpsNs(), Pump1 };
		auto references = [&]{ return NodeQL( "node( opc: $opc, id:$id ){ references{ direction } truncated }", {{"opc", OpcServerSlug}, {"id", pump1.ToJson()}} ).as_object(); };
		config.maxReferencesPerNode = 2;
		let capped = references();
		EXPECT_EQ( capped.at("references").as_array().size(), 2u ) << serialize( capped );
		EXPECT_EQ( capped.at("truncated"), jvalue{true} ) << serialize( capped );
		config.maxReferencesPerNode = limit;
		let all = references();
		EXPECT_GT( all.at("references").as_array().size(), 2u ) << serialize( all );
		EXPECT_EQ( all.at("truncated"), jvalue{false} ) << serialize( all );

		auto browse = BlockAwait<Browse::FoldersAwait,Browse::Response>( Browse::FoldersAwait{Browse::Request::References(NodeId{pump1}, 1), _client} );
		ASSERT_TRUE( browse.resultsSize && browse.results[0].continuationPoint.length ) << "one reference of several leaves a continuation point";
		auto release = [&]{ return BlockAwait<ReleaseBrowseAwait,StatusCode>( ReleaseBrowseAwait{browse, _client} ); };
		EXPECT_EQ( release(), UA_STATUSCODE_GOOD );
		EXPECT_EQ( release(), UA_STATUSCODE_BADCONTINUATIONPOINTINVALID ) << "released already";
	}

	//reviews-issue/416-address-space.md #8:  the DisplayNames a `references` row names were one Read whatever their count, and any
	//refusal failed the whole query.  A server whose MaxNodesPerRead is below the batch is asked in halves until it answers;  the
	//embedded server's limit is 1 for the test.
	TEST_F( BrowseTests, DisplayNamesHalveWhenTooMany ){
		auto& config = *UA_Server_getConfig( Server::GetUAServer().Ptr() );
		absl::Cleanup restore = [&config, limit=config.maxNodesPerRead]{ config.maxNodesPerRead = limit; };
		config.maxNodesPerRead = 1;
		DisplayNameCache cache;
		const NodeId component{ 0, UA_NS0ID_HASCOMPONENT }, organizes{ 0, UA_NS0ID_ORGANIZES }, typeDef{ 0, UA_NS0ID_HASTYPEDEFINITION };
		let names = BlockAwait<DisplayNameCache::GetAwait,DisplayNames>( cache.Get(_client, {component, organizes, typeDef}) );
		EXPECT_EQ( names.size(), 3u );
		EXPECT_EQ( names.contains(component) ? names.at(component) : "", "HasComponent" );
		EXPECT_EQ( names.contains(typeDef) ? names.at(typeDef) : "", "HasTypeDefinition" );
		EXPECT_EQ( cache.Size(), 3u );
	}

	//Phase 4 (#416):  `references` answers every reference of a node in both directions, the reference type and the target's type
	//definition named from the per-client DisplayName cache, read in one batch for the ids not yet seen.  pump1 (pumps.NodeSet2.xml):
	//HasComponent forward to its status and motorRpm, HasTypeDefinition to PumpType, and Organizes inverse to the Objects folder.
	TEST_F( BrowseTests, ReferencesAnswerBothDirectionsNamed ){
		let ns = PumpsNs();
		let value = NodeQL( "node( opc: $opc, id:$id ){ references{ direction refType target class typeDef } }", {{"opc", OpcServerSlug}, {"id", NodeId(ns, Pump1).ToJson()}} );
		TRACE( "{}", serialize(value) );
		let& rows = value.as_object().at( "references" ).as_array();
		auto find = [&]( sv direction, sv refType, const NodeId& target )->const jobject*{
			auto p = find_if( rows, [&](let& r){ let& o = r.as_object(); return Json::AsSV(o, "direction")==direction && Json::AsSV(o, "refType/name")==refType && NodeId{o.at("target")}==target; } );
			return p==rows.end() ? nullptr : &p->as_object();
		};
		let status = find( "Forward", "HasComponent", NodeId(ns, Status1) );
		ASSERT_TRUE( status ) << serialize( rows );
		EXPECT_EQ( Json::AsSV(*status, "target/name"), "status" );
		EXPECT_EQ( Json::AsSV(*status, "target/browse/name"), "status" );
		EXPECT_EQ( Json::FindNumber<uint>(*status, "class"), UA_NODECLASS_VARIABLE );
		EXPECT_EQ( NodeId{status->at("typeDef")}, NodeId(0, UA_NS0ID_BASEDATAVARIABLETYPE) );
		EXPECT_EQ( Json::AsSV(*status, "typeDef/name"), "BaseDataVariableType" );
		EXPECT_EQ( NodeId{status->at("refType")}, NodeId(0, UA_NS0ID_HASCOMPONENT) );
		EXPECT_TRUE( find("Forward", "HasComponent", NodeId(ns, Rpm1)) ) << serialize( rows );
		let type = find( "Forward", "HasTypeDefinition", NodeId(ns, 1001) );
		ASSERT_TRUE( type ) << serialize( rows );
		EXPECT_EQ( Json::AsSV(*type, "target/name"), "PumpType" );
		EXPECT_EQ( Json::FindNumber<uint>(*type, "class"), UA_NODECLASS_OBJECTTYPE );
		EXPECT_TRUE( type->at("typeDef").is_null() ) << "a type has no type definition";
		let objects = find( "Inverse", "Organizes", NodeId::ObjectsFolder() );
		ASSERT_TRUE( objects ) << serialize( rows );
		EXPECT_EQ( Json::AsSV(*objects, "target/name"), "Objects" );
		EXPECT_EQ( Json::AsSV(*objects, "typeDef/name"), "FolderType" );
		EXPECT_GE( _client->DisplayNames().Size(), 5u ) << "HasComponent, HasTypeDefinition, Organizes, BaseDataVariableType and FolderType, kept for the next query";
		let again = NodeQL( "node( opc: $opc, id:$id ){ references{ refType } }", {{"opc", OpcServerSlug}, {"id", NodeId(ns, Pump2).ToJson()}} ).as_object().at( "references" ).as_array();
		EXPECT_TRUE( find_if(again, [](let& r){ return Json::AsSV(r.as_object(), "refType/name")=="Organizes"; })!=again.end() ) << "named from the cache:  " << serialize( again );
		EXPECT_FALSE( again.front().as_object().contains("direction") ) << "projection:  only the requested column";
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