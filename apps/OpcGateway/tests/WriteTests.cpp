#include "utils/GatewayClientSocket.h"
#include "utils/helpers.h"
#include "utils/Pumps.h"
#include "../src/async/ReadAwait.h"
#include "../src/async/WriteAwait.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	struct WriteTests : ::testing::Test{
		Ω SetUpTestSuite()->void{ GetConnection( OpcServerSlug ); }
	protected:
		α SetUp()->void override{
			let server = Server::FindUAServer();
			if( !server )
				GTEST_SKIP() << "Needs the embedded OpcServer.";
			_ua = server->Ptr();
			_node = NodeId{ PumpsNs(), Rpm1 };//HistGroupTests groups it; no other test writes it.
		}
		Ω Hold( UA_Server* ua, const NodeId& node, UA_Int32 held )ε->void{
			UA_Variant v; UA_Variant_setScalar( &v, &held, &UA_TYPES[UA_TYPES_INT32] );
			UAε( UA_Server_writeValue(ua, node, v) );
		}
		//updateVariable's answer:  the value it reads back.
		Ω Update( const NodeId& node, jvalue value )ε->jvalue{
			const jobject vars{ {"opc", OpcServerSlug}, {"id", node.ToJson()}, {"value", move(value)} };
			let written = Socket().QuerySync( "updateVariable( opc: $opc, id: $id, value: $value ){ value }", vars );
			return written.as_object().at( "value" );
		}
		//node( opc, id ){ columns }:  the id form's read of one node.
		Ω Read( const NodeId& node, sv columns )ε->jobject{
			const jobject vars{ {"opc", OpcServerSlug}, {"id", node.ToJson()} };
			return Socket().QuerySync( Ƒ("node( opc: $opc, id: $id ){{ {} }}", columns), vars ).as_object();
		}
		//updateNode's answer:  the columns it reads back.
		Ω UpdateNode( const NodeId& node, jobject attributes, sv columns )ε->jobject{
			const jobject vars{ {"opc", OpcServerSlug}, {"id", node.ToJson()}, {"attributes", move(attributes)} };
			return Socket().QuerySync( Ƒ("updateNode( opc: $opc, id: $id, attributes: $attributes ){{ {} }}", columns), vars ).as_object();
		}
		UA_Server* _ua{};
		NodeId _node;
	};

	//updateVariable types a value as the hist edits do (ValueTypesAwait):  an enumeration of namespace 0 goes out as the Int32 it
	//is encoded as, which the server takes for one, though open62541 has a type of its own for ServerState.
	TEST_F( WriteTests, Enum ){
		const RetypedNode retyped{ _ua, _node, UA_NS0ID_SERVERSTATE };
		constexpr UA_Int32 shutdown{ UA_SERVERSTATE_SHUTDOWN };
		EXPECT_EQ( Update(_node, shutdown), jvalue{shutdown} );
	}

	//A DataType the gateway has no built-in type for takes the type of the node's value, so 9 goes out as the Int32 the node
	//holds, not the json's Int64, which would read back in the Long form:  the abstract Number, and BaseDataType, whose
	//built-in type, Variant, says nothing of a value's.
	TEST_F( WriteTests, AnAbstractDataTypeTakesTheValuesType ){
		for( UA_UInt32 dataType : {UA_NS0ID_NUMBER, UA_NS0ID_BASEDATATYPE} ){
			const RetypedNode retyped{ _ua, _node, dataType };
			Hold( _ua, _node, 7 );
			EXPECT_EQ( Update(_node, 9), jvalue{9} ) << dataType;
		}
	}
	//node( opc, id ){ … } reads the attributes of a node's class plainly (clients spec *Attributes*, #417) - the Attributes tab asks by the
	//class it knows - each answered, or carrying the status the server refused it with, never an error:  the permissions, which open62541
	//doesn't serve, come back BadAttributeIdInvalid.  No View:  the embedded server has none.
	TEST_F( WriteTests, AttributesOfEveryClass ){
		let ns = PumpsNs();
		struct Case{ NodeId Node; UA_NodeClass Class; sv Columns; };
		const vector<Case> cases{
			{ NodeId{ns, Pump1}, UA_NODECLASS_OBJECT, "eventNotifier" },
			{ _node, UA_NODECLASS_VARIABLE, "dataValue dataType valueRank arrayDimensions accessLevel userAccessLevel minimumSamplingInterval historizing" },
			{ NodeId{ns, ToHertz}, UA_NODECLASS_METHOD, "executable userExecutable" },
			{ NodeId{UA_NS0ID_BASEOBJECTTYPE}, UA_NODECLASS_OBJECTTYPE, "isAbstract" },
			{ NodeId{UA_NS0ID_BASEDATAVARIABLETYPE}, UA_NODECLASS_VARIABLETYPE, "value dataType valueRank arrayDimensions isAbstract" },
			{ NodeId{UA_NS0ID_HASCOMPONENT}, UA_NODECLASS_REFERENCETYPE, "isAbstract symmetric inverseName" },
			{ NodeId{UA_NS0ID_NUMBER}, UA_NODECLASS_DATATYPE, "isAbstract dataTypeDefinition" } };
		const flat_set<sv> mayRefuse{ "rolePermissions", "userRolePermissions", "accessRestrictions", "dataTypeDefinition" };
		for( let& c : cases ){
			let columns = Ƒ( "id nodeClass name browse description writeMask userWriteMask rolePermissions userRolePermissions accessRestrictions {}", c.Columns );
			let y = Read( c.Node, columns );
			SCOPED_TRACE( Ƒ("{}:  {}", c.Node.ToString(), serialize(y)) );
			EXPECT_EQ( Json::FindNumber<uint>(y, "nodeClass"), c.Class );
			EXPECT_TRUE( y.at("name").is_string() );
			EXPECT_TRUE( y.at("browse").is_object() );
			EXPECT_TRUE( y.at("writeMask").is_number() );
			EXPECT_TRUE( y.at("userWriteMask").is_number() );
			for( let column : Str::Split(columns, ' ') ){
				ASSERT_TRUE( y.contains(column) ) << column;
				let& v = y.at( column );
				let refused = column!="dataValue" && v.is_object() && v.as_object().contains("sc") && !v.as_object().contains("v");
				if( refused )
					EXPECT_TRUE( UA_StatusCode_isBad(v.as_object().at("sc").to_number<StatusCode>()) ) << column;
				else
					EXPECT_FALSE( mayRefuse.contains(column) && column!="dataTypeDefinition" ) << column << " answered";
				EXPECT_TRUE( !refused || mayRefuse.contains(column) ) << column << " refused";
			}
		}
	}

	//`dataValue`:  the Value as the DataValue the read brings - the reading, its status and both timestamps - where `value` is the reading
	//alone (Value::ToJson), which drops them:  the Attributes tab's Value row.  The server stamps a write as it takes it.
	TEST_F( WriteTests, DataValueCarriesBothTimestamps ){
		UA_Variant held; UA_Variant_init( &held );
		UAε( UA_Server_readValue(_ua, _node, &held) );
		let written = UA_Server_writeValue( _ua, _node, held );
		UA_Variant_clear( &held );
		UAε( written );
		let y = Read( _node, "value dataValue" );
		SCOPED_TRACE( serialize(y) );
		let& dv = y.at( "dataValue" ).as_object();
		EXPECT_EQ( dv.at("v"), y.at("value") );
		EXPECT_EQ( dv.at("sc"), jvalue{0} );
		for( sv stamp : {"source", "server"} ){
			ASSERT_TRUE( dv.contains(stamp) ) << stamp;
			let& t = dv.at( stamp ).as_object();
			EXPECT_GT( Json::AsNumber<int64_t>(t, "seconds"), 1'700'000'000 ) << stamp;//a real time, not 1601's zero.
			EXPECT_TRUE( t.contains("nanos") ) << stamp;
		}
	}

	//updateNode( opc, id, attributes:{ description } ):  written where the node's WriteMask has the bit - pumpManual's, which the OpcServer sets
	//after the load (Server::ConfigurePumps), the nodeset declaring none - and refused on a pump without, whatever the user holds:  open62541
	//answers BadUserAccessDenied for a bit the node's mask lacks as for one the user's rights lack.  A bare string keeps the locale the node's
	//text has - the nodeset's `en` - which is how it reads back on a session with no locale of its own;  {locale, text} adds a text in
	//another, which that session never reads (open62541 answers it the first text added).  An attribute updateNode has no type for is
	//refused before any write.  The description is put back after, and the `de` text removed.
	TEST_F( WriteTests, DescriptionWritesWhereTheMaskAllows ){
		const NodeId masked{ PumpsNs(), PumpManual }, unmasked{ PumpsNs(), Pump1 };
		UA_LocalizedText before; UA_LocalizedText_init( &before );
		UAε( UA_Server_readDescription(_ua, masked, &before) );
		absl::Cleanup restore = [&]{//the `de` text goes too:  a server keeps a text per locale, and an empty one removes its locale's
			const UA_LocalizedText german{ UA_STRING_STATIC("de"), UA_STRING_NULL };
			for( let& text : {german, before} ){
				if( let sc = UA_Server_writeDescription(_ua, masked, text); sc )
					ADD_FAILURE() << "pumpManual's description wasn't put back:  " << UA_StatusCode_name( sc );
			}
			UA_LocalizedText_clear( &before );
		};
		const string written{ "Written by WriteTests through updateNode." };
		let y = UpdateNode( masked, {{"description", written}}, "description writeMask userWriteMask" );
		EXPECT_EQ( y.at("description"), jvalue{written} ) << serialize( y );
		EXPECT_NE( Json::AsNumber<UA_UInt32>(y, "writeMask") & UA_WRITEMASK_DESCRIPTION, 0u ) << serialize( y );
		EXPECT_NE( Json::AsNumber<UA_UInt32>(y, "userWriteMask") & UA_WRITEMASK_DESCRIPTION, 0u ) << serialize( y );
		UA_LocalizedText after; UA_LocalizedText_init( &after );
		UAε( UA_Server_readDescription(_ua, masked, &after) );
		EXPECT_EQ( ToSV(after.text), written ) << "the server holds it";
		EXPECT_EQ( ToSV(after.locale), "en" ) << "under the locale it had";
		UA_LocalizedText_clear( &after );
		let german = UpdateNode( masked, {{"description", jobject{{"locale", "de"}, {"text", "Von WriteTests geschrieben."}}}}, "description" );
		EXPECT_EQ( german.at("description"), jvalue{written} ) << "the session has no locale, so it reads the first text added:  " << serialize( german );
		try{
			UpdateNode( unmasked, {{"description", written}}, "description" );
			ADD_FAILURE() << "a pump with no WriteMask took a description";
		}
		catch( const Exception& e ){
			EXPECT_TRUE( e.Code()==UA_STATUSCODE_BADUSERACCESSDENIED || e.Code()==UA_STATUSCODE_BADNOTWRITABLE ) << e.what();
		}
		EXPECT_THROW( UpdateNode(masked, {{"browse", "pumpManual"}}, "browse"), Exception ) << "no type to write a BrowseName as";
		EXPECT_THROW( UpdateNode(masked, {{"value", 1}}, "value"), Exception ) << "updateVariable's";
		try{//the Value read as its DataValue:  refused as `value` is, never sent as the read's sentinel id (#11)
			UpdateNode( masked, {{"dataValue", 1}}, "description" );
			ADD_FAILURE() << "dataValue took a write";
		}
		catch( const Exception& e ){
			EXPECT_TRUE( string{e.what()}.contains("updateVariable writes a value") ) << e.what();
		}
		EXPECT_THROW( UpdateNode(masked, {}, "description"), Exception ) << "nothing to write";
	}
	//An empty text removes its locale's (open62541):  a Description cleared on the Attributes tab is gone, and reads back empty.  A bare empty
	//name is refused before the write, since a node keeps its DisplayName.  The description is put back after.
	TEST_F( WriteTests, AnEmptyDescriptionRemovesIt ){
		const NodeId masked{ PumpsNs(), PumpManual };
		UA_LocalizedText before; UA_LocalizedText_init( &before );
		UAε( UA_Server_readDescription(_ua, masked, &before) );
		absl::Cleanup restore = [&]{
			if( let sc = UA_Server_writeDescription(_ua, masked, before); sc )
				ADD_FAILURE() << "pumpManual's description wasn't put back:  " << UA_StatusCode_name( sc );
			UA_LocalizedText_clear( &before );
		};
		let y = UpdateNode( masked, {{"description", ""}}, "description" );
		EXPECT_EQ( y.at("description"), jvalue{""} ) << serialize( y );
		UA_LocalizedText after; UA_LocalizedText_init( &after );
		UAε( UA_Server_readDescription(_ua, masked, &after) );
		EXPECT_EQ( after.text.length, 0u ) << "the server holds no text, in any locale:  " << ToSV( after.locale );
		UA_LocalizedText_clear( &after );
		let name = Read( masked, "name" ).at( "name" );
		EXPECT_THROW( UpdateNode(masked, {{"name", ""}}, "name"), Exception ) << "a node keeps its DisplayName";
		EXPECT_EQ( Read(masked, "name").at("name"), name );
	}
	//A Write applies each item on its own (Part 4 §5.10.4), so a refusal leaves the rest written:  the error keeps the first refusal's code
	//and names every item refused and every one written.  A result the server never sent is BadUnexpectedError, not written (#6).
	TEST_F( WriteTests, RefusalNamesWhatWasWritten ){
		vector<WriteItem> items;
		for( let attribute : {UA_ATTRIBUTEID_DISPLAYNAME, UA_ATTRIBUTEID_WRITEMASK, UA_ATTRIBUTEID_ACCESSLEVEL} )
			items.push_back( WriteItem{_node, attribute, Value{UA_STATUSCODE_GOOD}} );
		UA_StatusCode results[]{ UA_STATUSCODE_GOOD, UA_STATUSCODE_BADUSERACCESSDENIED, UA_STATUSCODE_BADOUTOFRANGE };
		UA_WriteResponse response; UA_WriteResponse_init( &response );
		response.results = results;
		response.resultsSize = 3;
		EXPECT_EQ( WriteAwait::Refusal(response, items), std::make_pair(UA_STATUSCODE_BADUSERACCESSDENIED, string{"write writeMask, accessLevel BadOutOfRange; written: name"}) );
		response.resultsSize = 1;
		EXPECT_EQ( WriteAwait::Refusal(response, items), std::make_pair(UA_STATUSCODE_BADUNEXPECTEDERROR, string{"write writeMask, accessLevel BadUnexpectedError; written: name"}) ) << "two results never sent";
		response.resultsSize = 0;
		EXPECT_EQ( WriteAwait::Refusal(response, {items[0]}), std::make_pair(UA_STATUSCODE_BADUNEXPECTEDERROR, string{"write name"}) ) << "a lone item with no result";
		results[1] = results[2] = UA_STATUSCODE_GOOD;
		response.resultsSize = 3;
		EXPECT_EQ( WriteAwait::Refusal(response, items).first, UA_STATUSCODE_GOOD );
	}
	//updateNode( attributes:{name, writeMask} ) on pumpManual, whose WriteMask has DisplayName but not WriteMask:  the server writes the
	//name and refuses the mask, and the error says so.  The name is put back after.
	TEST_F( WriteTests, ARefusedAttributeLeavesTheOthersWritten ){
		const NodeId masked{ PumpsNs(), PumpManual };
		UA_LocalizedText before; UA_LocalizedText_init( &before );
		UAε( UA_Server_readDisplayName(_ua, masked, &before) );
		absl::Cleanup restore = [&]{
			if( let sc = UA_Server_writeDisplayName(_ua, masked, before); sc )
				ADD_FAILURE() << "pumpManual's name wasn't put back:  " << UA_StatusCode_name( sc );
			UA_LocalizedText_clear( &before );
		};
		const string renamed{ "Renamed by WriteTests" };
		try{
			UpdateNode( masked, {{"name", renamed}, {"writeMask", 96}}, "name" );
			ADD_FAILURE() << "pumpManual's WriteMask has no WriteMask bit, yet took one";
		}
		catch( const Exception& e ){
			EXPECT_EQ( e.Code(), UA_STATUSCODE_BADUSERACCESSDENIED ) << e.what();
			EXPECT_TRUE( string{e.what()}.contains("write writeMask; written: name") ) << e.what();
		}
		EXPECT_EQ( Read(masked, "name").at("name"), jvalue{renamed} ) << "written, though the mutation failed";
	}
	//A name updateNode writes leaves this client's DisplayName cache (reviews-issue/417-clients-attributes.md #7):  the References tab names a
	//row's type definition from it, so a renamed type reads the new name, where the cache kept the old one for the connection's life.  PumpType,
	//pump1's type definition, is given a DisplayName bit for the test;  its name and mask are put back after.
	TEST_F( WriteTests, ARenamedTypeLeavesTheNameCache ){
		const NodeId type{ PumpsNs(), 1001 }, pump1{ PumpsNs(), Pump1 };
		UA_LocalizedText before; UA_LocalizedText_init( &before );
		UAε( UA_Server_readDisplayName(_ua, type, &before) );
		UA_UInt32 mask{};
		UAε( UA_Server_readWriteMask(_ua, type, &mask) );
		UAε( UA_Server_writeWriteMask(_ua, type, mask | UA_WRITEMASK_DISPLAYNAME) );
		absl::Cleanup restore = [&]{
			if( let sc = UA_Server_writeDisplayName(_ua, type, before) | UA_Server_writeWriteMask(_ua, type, mask); sc )
				ADD_FAILURE() << "PumpType wasn't put back:  " << UA_StatusCode_name( sc );
			UA_LocalizedText_clear( &before );
		};
		auto typeDefName = [&]()->string{
			let y = Socket().QuerySync( "node( opc: $opc, id: $id ){ references{ target typeDef } }", {{"opc", OpcServerSlug}, {"id", NodeId::ObjectsFolder().ToJson()}} );
			for( let& r : y.as_object().at("references").as_array() ){
				if( NodeId{r.as_object().at("target")}==pump1 )
					return string{ Json::AsSV(r.as_object(), "typeDef/name") };
			}
			return {};
		};
		EXPECT_EQ( typeDefName(), ToSV(before.text) ) << "read, and kept";
		const string renamed{ "Renamed PumpType" };
		UpdateNode( type, {{"name", renamed}}, "name" );
		EXPECT_EQ( typeDefName(), renamed );
	}
	//A Read answered with fewer results than nodes asked - a server out of spec - left SetJson reading past the array
	//(reviews-issue/417-clients-attributes.md #2):  each entry never answered is BadUnexpectedError.
	TEST_F( WriteTests, AShortReadAnswersBadUnexpectedError ){
		UA_ReadResponse rr; UA_ReadResponse_init( &rr );
		rr.results = (UA_DataValue*)UA_Array_new( 1, &UA_TYPES[UA_TYPES_DATAVALUE] );
		rr.resultsSize = 1;
		const UA_LocalizedText name{ UA_STRING_STATIC("en"), UA_STRING_STATIC("Pump") };
		UAε( UA_Variant_setScalarCopy(&rr.results[0].value, &name, &UA_TYPES[UA_TYPES_LOCALIZEDTEXT]) );
		rr.results[0].hasValue = true;
		ReadResponse response{ move(rr), ReadRequest{_node, {UA_ATTRIBUTEID_DISPLAYNAME, UA_ATTRIBUTEID_DESCRIPTION}} };
		let nodes = response.GetJson( nullptr );//no registry:  a name, not a structure.
		ASSERT_EQ( nodes.size(), 1u );
		let& j = nodes.begin()->second;
		EXPECT_EQ( j.at("name"), jvalue{"Pump"} ) << serialize( j );
		EXPECT_EQ( j.at("description"), jvalue{UAException::ToJson(UA_STATUSCODE_BADUNEXPECTEDERROR)} ) << serialize( j );
	}
	//createNode, deleteNode… are refused before a client is opened (#9):  an unknown connection still answers the refusal, not a failed connect.
	TEST_F( WriteTests, OnlyUpdateNodeIsTakenBeforeAConnect ){
		try{
			Socket().QuerySync( "deleteNode( opc: $opc, id: $id ){ id }", {{"opc", "noSuchConnection"}, {"id", _node.ToJson()}} );
			ADD_FAILURE() << "deleteNode was taken";
		}
		catch( const Exception& e ){
			EXPECT_TRUE( string{e.what()}.contains("Only updateNode is supported.") ) << e.what();
		}
	}
}