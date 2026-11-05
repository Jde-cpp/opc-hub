#include <absl/cleanup/cleanup.h>
#include <jde/db/meta/AppSchema.h>
#include "utils/GatewayClientSocket.h"
#include "utils/helpers.h"
#include "utils/Pumps.h"
#include "../src/GatewayAppClient.h"
#include "../../OpcServer/src/access/OpcAuthorize.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	using Access::ERights;
	using Statuses = vector<Opc::StatusCode>;
	//`call( opc, object, method, args ){ statusCode inputArgumentResults outputArguments }` against the embedded OpcServer's
	//pumpManual.toHertz (clients spec *Methods*, #318), and `method( opc, id )`, the arguments the web's dialog is built from.
	struct CallTests : ::testing::Test{
		Ω SetUpTestSuite()->void{ GetConnection( OpcServerSlug ); }
	protected:
		α SetUp()->void override{
			if( !Server::FindUAServer() )
				GTEST_SKIP() << "Needs the embedded OpcServer.";
			_object = NodeId{ PumpsNs(), PumpManual };
			_method = NodeId{ PumpsNs(), ToHertz };
		}
		//the call's answer, whatever its status:  only a failure before the call throws.
		Ω Call( const NodeId& object, const NodeId& method, jarray args )ε->jobject{
			const jobject vars{ {"opc", OpcServerSlug}, {"object", object.ToJson()}, {"method", method.ToJson()}, {"args", move(args)} };
			return Socket().QuerySync( "call( opc: $opc, object: $object, method: $method, args: $args ){ statusCode inputArgumentResults outputArguments }", vars ).as_object();
		}
		Ω StatusCode( const jobject& result )ε->Opc::StatusCode{ return result.at( "statusCode" ).to_number<Opc::StatusCode>(); }
		Ω InputResults( const jobject& result )ε->Statuses{
			Statuses y;
			for( let& sc : result.at("inputArgumentResults").as_array() )
				y.push_back( sc.to_number<Opc::StatusCode>() );
			return y;
		}
		NodeId _object, _method;
	};

	//The json arguments typed from the method's InputArguments - 1500 is a Double there, where its json is an Int64 - and the
	//outputs as the server answered them, each in the value's own json.  No argument results:  open62541 answers them for a
	//refused argument alone.
	TEST_F( CallTests, TypedArgumentsAndOutputs ){
		let y = Call( _object, _method, {1500} );
		EXPECT_EQ( StatusCode(y), UA_STATUSCODE_GOOD ) << serialize( y );
		EXPECT_EQ( InputResults(y), Statuses{} );
		const jarray outputs{ 25.0, "25 Hz" };
		EXPECT_EQ( y.at("outputArguments"), jvalue{outputs} ) << serialize( y );
		const jobject vars{ {"opc", OpcServerSlug}, {"object", _object.ToJson()}, {"method", _method.ToJson()}, {"args", jarray{60}} };
		let projected = Socket().QuerySync( "call( opc: $opc, object: $object, method: $method, args: $args ){ statusCode }", vars ).as_object();
		const jobject statusAlone{ {"statusCode", 0} };
		EXPECT_EQ( projected, statusAlone ) << "the result request projects:  " << serialize( projected );
	}

	//An argument that is no Double is refused as the server refuses a mistyped one, BadInvalidArgument with BadTypeMismatch in its
	//slot, before any call;  too few goes to the server, whose answer it is.
	TEST_F( CallTests, ABadArgumentTypeIsRefusedWithItsResults ){
		let mistyped = Call( _object, _method, {"fast"} );
		EXPECT_EQ( StatusCode(mistyped), UA_STATUSCODE_BADINVALIDARGUMENT ) << serialize( mistyped );
		EXPECT_EQ( InputResults(mistyped), Statuses{UA_STATUSCODE_BADTYPEMISMATCH} );
		EXPECT_EQ( mistyped.at("outputArguments"), jvalue{jarray{}} );
		let missing = Call( _object, _method, {} );
		EXPECT_EQ( StatusCode(missing), UA_STATUSCODE_BADARGUMENTSMISSING ) << serialize( missing );
	}

	//An argument of a type the gateway cannot encode - a NodeId here - is refused as unsupported in its slot, not as a mismatch of
	//the user's value (reviews-issue/318-opc-calls.md #5):  no text is a NodeId to it, a valid one included.  The method is added
	//to the embedded server for the test, under pumpManual;  its callback is never reached.
	TEST_F( CallTests, AnArgumentTypeTheGatewayCannotSendIsUnsupported ){
		auto& ua = Server::GetUAServer();
		UA_Argument argument; UA_Argument_init( &argument );
		argument.name = UA_STRING( (char*)"node" );
		argument.dataType = UA_TYPES[UA_TYPES_NODEID].typeId;
		argument.valueRank = UA_VALUERANK_SCALAR;
		UA_MethodAttributes attributes = UA_MethodAttributes_default;
		attributes.executable = attributes.userExecutable = true;
		const UA_NodeId id = UA_NODEID_STRING( PumpsNs(), (char*)"CallTests.nodeIdArgument" );
		const NodeId method{ id };//copied:  from the prvalue it would take the literal, and free it.
		ASSERT_EQ( UA_Server_addMethodNode(ua.Ptr(), method, _object, UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT), UA_QUALIFIEDNAME(PumpsNs(), (char*)"nodeIdArgument"), attributes,
			[]( UA_Server*, const UA_NodeId*, void*, const UA_NodeId*, void*, const UA_NodeId*, void*, size_t, const UA_Variant*, size_t, UA_Variant* )->UA_StatusCode{ return UA_STATUSCODE_GOOD; },
			1, &argument, 0, nullptr, nullptr, nullptr), UA_STATUSCODE_GOOD );
		absl::Cleanup remove = [&]{ UA_Server_deleteNode( ua.Ptr(), method, true ); };
		let y = Call( _object, method, {"ns=2;i=5"} );
		EXPECT_EQ( StatusCode(y), UA_STATUSCODE_BADINVALIDARGUMENT ) << serialize( y );
		EXPECT_EQ( InputResults(y), Statuses{UA_STATUSCODE_BADNOTSUPPORTED} ) << serialize( y );
	}

	//reviews-issue/318-opc-calls.md #3:  an argument of a type of the server's own goes as its first namespace-0 supertype - DI's
	//DeviceHealthEnumeration as an Int32, a subtype of a subtype of Double as a Double - and one deriving from Structure is refused as
	//unsupported (#421), as a namespace-0 structure is.  Sent as the json implied, both went as Int64, which the server refuses.  The
	//types and the methods are added to the embedded server for the test, under pumpManual;  the callbacks answer Good unread.
	TEST_F( CallTests, AnArgumentOfTheServersOwnTypeGoesAsItsBase ){
		auto& ua = Server::GetUAServer();
		size_t di{};
		ASSERT_EQ( UA_Server_getNamespaceByName(ua.Ptr(), UA_STRING((char*)"http://opcfoundation.org/UA/DI/"), &di), UA_STATUSCODE_GOOD ) << "the embedded server loads DI";
		vector<NodeId> added;
		absl::Cleanup remove = [&]{
			for( auto p = added.rbegin(); p!=added.rend(); ++p )
				UA_Server_deleteNode( ua.Ptr(), *p, true );
		};
		let id = [&]( const char* name ){ const UA_NodeId x = UA_NODEID_STRING( PumpsNs(), (char*)name ); return NodeId{ x }; };//copied:  from the prvalue it would take the literal
		let addType = [&]( const char* name, const NodeId& parent ){
			let type = id( name );
			ASSERT_EQ( UA_Server_addDataTypeNode(ua.Ptr(), type, parent, UA_NODEID_NUMERIC(0, UA_NS0ID_HASSUBTYPE), UA_QUALIFIEDNAME(PumpsNs(), (char*)name), UA_DataTypeAttributes_default, nullptr, nullptr), UA_STATUSCODE_GOOD ) << name;
			added.push_back( type );
		};
		let addMethod = [&]( const char* name, vector<std::pair<const char*, NodeId>> inputs ){
			vector<UA_Argument> arguments( inputs.size() );
			for( uint i=0; i<inputs.size(); ++i ){
				UA_Argument_init( &arguments[i] );
				arguments[i].name = UA_STRING( (char*)inputs[i].first );
				arguments[i].dataType = inputs[i].second;
				arguments[i].valueRank = UA_VALUERANK_SCALAR;
			}
			UA_MethodAttributes attributes = UA_MethodAttributes_default;
			attributes.executable = attributes.userExecutable = true;
			let method = id( name );
			ASSERT_EQ( UA_Server_addMethodNode(ua.Ptr(), method, _object, UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT), UA_QUALIFIEDNAME(PumpsNs(), (char*)name), attributes,
				[]( UA_Server*, const UA_NodeId*, void*, const UA_NodeId*, void*, const UA_NodeId*, void*, size_t, const UA_Variant*, size_t, UA_Variant* )->UA_StatusCode{ return UA_STATUSCODE_GOOD; },
				arguments.size(), arguments.data(), 0, nullptr, nullptr, nullptr), UA_STATUSCODE_GOOD ) << name;
			added.push_back( method );
		};
		addType( "CallTests.Speed", NodeId{UA_NS0ID_DOUBLE} );
		addType( "CallTests.SubSpeed", id("CallTests.Speed") );
		addType( "CallTests.Record", NodeId{UA_NS0ID_STRUCTURE} );
		addMethod( "CallTests.typed", {{"health", NodeId{(NsIndex)di, 6244}}, {"speed", id("CallTests.SubSpeed")}} );
		addMethod( "CallTests.structured", {{"record", id("CallTests.Record")}} );

		let bases = [&]( const char* method ){
			let y = Socket().QuerySync( "method( opc: $opc, id: $id ){ inputArguments{ baseDataType } }", {{"opc", OpcServerSlug}, {"id", id(method).ToJson()}} ).as_object();
			jarray bases;
			for( let& a : y.at("inputArguments").as_array() )
				bases.push_back( a.at("baseDataType") );
			return bases;
		};
		const jarray typedBases{ NodeId{UA_NS0ID_ENUMERATION}.ToJson(), NodeId{UA_NS0ID_DOUBLE}.ToJson() };
		EXPECT_EQ( bases("CallTests.typed"), typedBases ) << "Enumeration, and Double two steps up";
		let typed = Call( _object, id("CallTests.typed"), {2, 1500} );
		EXPECT_EQ( StatusCode(typed), UA_STATUSCODE_GOOD ) << serialize( typed );

		const jarray structuredBases{ NodeId{UA_NS0ID_STRUCTURE}.ToJson() };
		EXPECT_EQ( bases("CallTests.structured"), structuredBases );
		let structured = Call( _object, id("CallTests.structured"), {jobject{}} );
		EXPECT_EQ( StatusCode(structured), UA_STATUSCODE_BADINVALIDARGUMENT ) << serialize( structured );
		EXPECT_EQ( InputResults(structured), Statuses{UA_STATUSCODE_BADNOTSUPPORTED} ) << serialize( structured );
	}

	//A method the server does not have fails reading its class, with the server's status - an error, not a result, since there is
	//no InputArguments to type the arguments from.  Retried from scratch next time:  a mistyped id does not poison the connection.
	TEST_F( CallTests, AnUnknownMethodIsRefused ){
		const NodeId unknown{ PumpsNs(), 9999 };
		try{
			Call( _object, unknown, {1500} );
			ADD_FAILURE() << "a call of a method the server does not have answered";
		}
		catch( const Exception& e ){
			EXPECT_EQ( e.Code(), UA_STATUSCODE_BADNODEIDUNKNOWN ) << e.what();
		}
	}

	//`method( opc, id )`:  the InputArguments and OutputArguments as the nodeset declares them, each argument named, typed and described.
	TEST_F( CallTests, Arguments ){
		let y = Socket().QuerySync( "method( opc: $opc, id: $id ){ name inputArguments{ name dataType valueRank description } outputArguments{ name dataType valueRank description } }", {{"opc", OpcServerSlug}, {"id", _method.ToJson()}} ).as_object();
		EXPECT_EQ( y.at("name"), jvalue{"toHertz"} ) << serialize( y );
		let dataType = []( UA_UInt32 i ){ return NodeId{ i }.ToJson(); };
		const jarray inputs{ jobject{{"name", "rpm"}, {"dataType", dataType(UA_NS0ID_DOUBLE)}, {"valueRank", -1}, {"description", "The motor speed, in rpm."}} };
		const jarray outputs{
			jobject{{"name", "hertz"}, {"dataType", dataType(UA_NS0ID_DOUBLE)}, {"valueRank", -1}, {"description", "The same speed, in Hz."}},
			jobject{{"name", "text"}, {"dataType", dataType(UA_NS0ID_STRING)}, {"valueRank", -1}, {"description", "The frequency spelled, \"25 Hz\"."}} };
		EXPECT_EQ( y.at("inputArguments"), jvalue{inputs} ) << serialize( y );
		EXPECT_EQ( y.at("outputArguments"), jvalue{outputs} ) << serialize( y );
		let unknown = [&]{ Socket().QuerySync( "method( opc: $opc, id: $id ){ name }", {{"opc", OpcServerSlug}, {"id", NodeId{PumpsNs(), 9999}.ToJson()}} ); };
		EXPECT_THROW( unknown(), Exception );
		let variable = [&]{ Socket().QuerySync( "method( opc: $opc, id: $id ){ name }", {{"opc", OpcServerSlug}, {"id", NodeId{PumpsNs(), RpmManual}.ToJson()}} ); };
		EXPECT_THROW( variable(), Exception ) << "a variable is no method";
	}

	//The rights are the server's:  with nodeIds enforced, a user holding Read and Update and not Execute is refused, Update implying
	//nothing (spec *Open questions* #9), and one granted Execute calls - until a deny of Execute on the method node itself, which wins
	//over the object's (reviews-issue/318-opc-calls.md #2).  Denied, not merely ungranted:  another suite's role may
	//already hold it for this user when this runs after it (HistEditTests' GatewayHistoryEditor, All on nodeIds), and a deny wins
	//over any grant.  The roles are found or made as the other suites' are, and this user's acl on them purged after, so the
	//next run starts as this one did;  nodeIds is opened again, as it ships.
	TEST_F( CallTests, AUserWithoutExecuteIsRefused ){
		auto app = Server::AppClient();
		let& schema = app->ResourceSchema;//"opc", or "opc.<resource>" with /opcServer/resource, as OpcServer's startup names it.
		auto& authorizer = static_cast<Server::OpcAuthorize&>( *Server::GetSchema().Authorizer );
		let user = AppClient()->UserPK();//the suite's session's user, which the gateway's client logs in as.
		let enforce = [&]( bool on ){
			EnforceResource( *app, authorizer, schema, "nodeIds", on );
			if( on )
				authorizer.AssignRights( Server::GetUAServer() );
		};
		//`user` holds `role` with `grants`:  made with them the first time (GrantRole), and the acl made again on a run after one that purged it.
		let grant = [&]( sv role, const vector<RoleGrant>& grants ){
			let existing = app->QuerySync( "role(slug:$slug, deleted:null){id}", {{"slug", role}} );
			let pk = GrantRole( *app, role, user, grants );
			if( !existing.empty() )
				app->QuerySync<jvalue>( "createAcl( identity:{ id:$userId }, role:{id:$roleId} )", {{"userId", user.Value}, {"roleId", pk.Value}} );
			return pk;
		};
		vector<Access::RolePK> held;
		optional<Access::RolePK> methodDenied;//purged with the toHertz row, which takes its grant:  the next run's GrantRole would find it without one.
		absl::Cleanup open = [&]{
			try{
				enforce( false );
				for( let role : held )
					app->QuerySync<jvalue>( "purgeAcl( identity:{ id:$userId }, role:{ id:$roleId } )", {{"userId", user.Value}, {"roleId", role.Value}} );
				if( methodDenied ){//the row unenforced first:  while it is, the purge's delegated admin check wants Administer on it, which this login lacks.
					app->QuerySync<jvalue>( "purgeRole( id:$id )", {{"id", methodDenied->Value}} );
					EnforceResource( *app, authorizer, schema, "nodeIds", false, _method.ToString() );
					app->QuerySync<jvalue>( "purgeResource( schemaName:$schema, slug:\"nodeIds\", criteria:$criteria )", {{"schema", schema}, {"criteria", _method.ToString()}} );
				}
			}
			catch( const std::exception& e ){ ADD_FAILURE() << "nodeIds wasn't opened again, or the acl wasn't purged:  " << e.what(); }
		};
		enforce( false );//the grants are made while the resource is unenforced, which is when the delegated admin check passes.
		held.push_back( grant("GatewayCallWriter", {{schema, "nodeIds", ERights::Read | ERights::Update, ERights::Execute}}) );
		enforce( true );
		ASSERT_TRUE( WaitUntil([&]{ let r = authorizer.NodeRights( _object, user ); return !empty(r & ERights::Update) && empty(r & ERights::Execute); }) ) << "the writer's grant reaches the server";
		let refused = Call( _object, _method, {1500} );
		EXPECT_EQ( StatusCode(refused), UA_STATUSCODE_BADUSERACCESSDENIED ) << "Update implies nothing:  " << serialize( refused );

		enforce( false );
		app->QuerySync<jvalue>( "purgeAcl( identity:{ id:$userId }, role:{ id:$roleId } )", {{"userId", user.Value}, {"roleId", held.back().Value}} );
		held.pop_back();
		held.push_back( grant("GatewayCaller", {{schema, "nodeIds", ERights::Read | ERights::Execute}}) );
		enforce( true );
		ASSERT_TRUE( WaitUntil([&]{ return !empty(authorizer.NodeRights(_object, user) & ERights::Execute); }) ) << "the Execute grant reaches the server";
		let allowed = Call( _object, _method, {1500} );
		EXPECT_EQ( StatusCode(allowed), UA_STATUSCODE_GOOD ) << serialize( allowed );

		enforce( false );
		methodDenied = GrantRole( *app, "GatewayCallToHertzDenied", user, {{schema, "nodeIds", ERights::None, ERights::Execute, _method.ToString(), "toHertz"}} );
		enforce( true );
		ASSERT_TRUE( WaitUntil([&]{ return empty(authorizer.NodeRights(_method, user) & ERights::Execute) && !empty(authorizer.NodeRights(_object, user) & ERights::Execute); }) ) << "the method's deny reaches the server";
		let deniedOnMethod = Call( _object, _method, {1500} );
		EXPECT_EQ( StatusCode(deniedOnMethod), UA_STATUSCODE_BADUSERACCESSDENIED ) << "the object's Execute does not carry past the method's deny:  " << serialize( deniedOnMethod );
	}
}