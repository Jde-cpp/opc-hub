//Clients phase 8a (#421, docs/clients/plan.md):  the type registry - a connection's structure types, fetched from the server's Structure
//subtree after the activation and chained onto every client of it (TypeRegistry), the serializers decoding a structure to named fields by
//it and a write walking them back (clients spec *Structures*):  pumpManual._Test.sample, a PumpSampleType of the pumps' own, read as fields
//through `node`, written back from them, read from the historian's records decoded the same, the dataType query answered from the registry,
//and a type added through the embedded server at runtime, which the activation's fetch could not know, opaque until the fetch its first
//read posts fills it.
#include <absl/cleanup/cleanup.h>
#include <jde/tests/WaitUntil.h>
#include "utils/HistGroupFixture.h"
#define let const auto

namespace Jde::Opc::Gateway::Tests{
	struct StructureTests : HistGroupFixture{
		Ω SampleDataType()ε->const UA_DataType&{
			let id = Node( SampleType );
			let type = UA_Server_findDataType( Server::GetUAServer().Ptr(), &id );
			THROW_IF( !type, "PumpSampleType ({}) is not among the server's types.", Node(SampleType).ToString() );
			return *type;
		}
		//The sample as Value::ToJson writes it and Value::Set reads it back:  `rpm` and `label` the test's, the rest fixed.
		Ω SampleJson( double rpm, sv label, bool running=true )ι->jobject{
			return { {"Rpm", rpm}, {"Running", running}, {"Label", label}, {"Limits", jobject{{"Low", 0.0}, {"High", 3000.0}}}, {"History", jarray{1470.0, 1475.0, rpm}} };
		}
		//The sample written on the server itself, as the source would, with the time the source gave it.
		Ω WriteSample( const jobject& j, TimePoint source )ε->void{
			const Value v{ j, &SampleDataType() };
			Write( TestSample, v.value.data, SampleDataType(), source );
		}
		//Every registry a client of the slug decodes by:  a row made again under the slug (ServerCnnctnDBTests' purge) leaves the clients
		//built before it on the old row's, where FindTypes answers the new row's - the socket's client may be either.
		Ω Registries()ι->vector<sp<TypeRegistry>>{
			vector<sp<TypeRegistry>> y;
			if( auto found = UAClient::FindTypes(OpcServerSlug); found )
				y.push_back( move(found) );
			for( let& client : UAClient::LiveClients() ){
				if( client->Slug()==OpcServerSlug && std::ranges::find(y, client->Types())==y.end() )
					y.push_back( client->Types() );
			}
			return y;
		}
		Ω Fetches( const vector<sp<TypeRegistry>>& registries )ι->uint{ uint y{}; for( let& r : registries ) y += r->Fetches(); return y; }
		Ω Holds( const vector<sp<TypeRegistry>>& registries, const NodeId& type )ι->bool{ return std::ranges::any_of( registries, [&]( let& r ){ return r->Find(type)!=nullptr; } ); }
		Ω ReadQL( const NodeId& node )ε->jvalue{
			return Socket().QuerySync( "node( opc: $opc, id: $id ){ value }", {{"opc", OpcServerSlug}, {"id", node.ToJson()}} ).as_object().at( "value" );
		}
		Ω Registry()ε->sp<TypeRegistry>{
			auto y = UAClient::FindTypes( OpcServerSlug );
			THROW_IF( !y, "No type registry for '{}' - no client of it was built.", OpcServerSlug );
			return y;
		}
		Ω Decoded( const jvalue& v, sv member="Rpm" )ι->bool{ return v.is_object() && v.as_object().contains(member); }
		Ω Opaque( const jvalue& v )ι->bool{ return v.is_object() && v.as_object().contains("typeId") && v.as_object().contains("body"); }
		//The registry holds the sample's type:  the activation's fetch, which the socket's first query starts, has landed.
		Ω WaitSampleType()ε->void{
			ReadQL( Node(TestSample) );//the connect, and with it the fetch.
			ASSERT_TRUE( WaitUntil([&]{ return Registry()->Find( Node(SampleType) )!=nullptr; }, 15s) ) << "the registry never held PumpSampleType:  " << Registry()->Count() << " type(s), " << Registry()->Fetches() << " fetch(es)";
		}
	};

	//`node( opc, id ){ value }` of the sample answers an object of its members by name, each in the gateway's own shape - the Range nested,
	//the array an array - once the activation's fetch has registered the type.
	TEST_F( StructureTests, ReadsAsNamedFields ){
		WriteSample( SampleJson(1480.5, "sample"), Since() );
		WaitSampleType();
		jvalue value;
		ASSERT_TRUE( WaitUntil([&]{ value = ReadQL( Node(TestSample) ); return Decoded( value ); }, 15s) ) << serialize( value );
		let& o = value.as_object();
		EXPECT_DOUBLE_EQ( Json::AsNumber<double>(o, "Rpm"), 1480.5 );
		EXPECT_TRUE( Json::AsBool(o, "Running") );
		EXPECT_EQ( Json::AsSV(o, "Label"), "sample" );
		EXPECT_DOUBLE_EQ( Json::AsNumber<double>(Json::AsObject(o, "Limits"), "High"), 3000 ) << "a namespace-0 structure member, nested";
		let& history = Json::AsArray( o, "History" );
		ASSERT_EQ( history.size(), 3u ) << serialize( history );
		EXPECT_DOUBLE_EQ( history[2].to_number<double>(), 1480.5 );
		EXPECT_EQ( o.size(), 5u ) << "every member, and nothing else:  " << serialize( o );
	}

	//updateVariable takes the same object back, each member through Set by the registered type's member list:  the server holds what was
	//written, and the answer reads it back as fields.
	TEST_F( StructureTests, WritesBackFromFields ){
		WriteSample( SampleJson(1480.5, "sample"), Since() );
		WaitSampleType();
		let written = SampleJson( 1510.25, "written", false );
		let answer = Socket().QuerySync( "updateVariable( opc: $opc, id: $id, value: $value ){ value }", {{"opc", OpcServerSlug}, {"id", Node(TestSample).ToJson()}, {"value", written}} ).as_object().at( "value" );
		ASSERT_TRUE( Decoded(answer) ) << serialize( answer );
		EXPECT_EQ( answer, jvalue{written} ) << serialize( answer );
		let held = Opc::ReadValue( Server::GetUAServer(), Node(TestSample) ).ToJson();//the server's own type:  no registry needed to walk it.
		ASSERT_TRUE( Decoded(held) ) << serialize( held );
		EXPECT_EQ( held, jvalue{written} ) << "the server holds the write:  " << serialize( held );
		//A member missing, or of the wrong kind, refuses the write and names the member.
		jobject missing = written; missing.erase( "Label" );
		EXPECT_THROW( Socket().QuerySync("updateVariable( opc: $opc, id: $id, value: $value ){ value }", {{"opc", OpcServerSlug}, {"id", Node(TestSample).ToJson()}, {"value", missing}}), std::exception );
		jobject wrong = written; wrong["Rpm"] = "fast";
		EXPECT_THROW( Socket().QuerySync("updateVariable( opc: $opc, id: $id, value: $value ){ value }", {{"opc", OpcServerSlug}, {"id", Node(TestSample).ToJson()}, {"value", wrong}}), std::exception );
		EXPECT_EQ( Opc::ReadValue(Server::GetUAServer(), Node(TestSample)).ToJson(), jvalue{written} ) << "a refused write changes nothing";
	}

	//A group over the sample stores the structure as the ExtensionObject it travelled as (libs/historian/docs/spec.md), and history with
	//`group` decodes the stored body against the registry as it is:  the record reads as the same fields a live read gives.
	TEST_F( StructureTests, HistorianRecordsDecodeTheSame ){
		WriteSample( SampleJson(1480.5, "sample"), Since() );
		WaitSampleType();
		const OneNodeGroup group{ "structures", TestSample };
		let t0 = Since();
		let stored = SampleJson( 1490.75, "stored" );
		WriteSample( stored, t0 );
		ASSERT_NO_THROW( WaitStored(group.Host, group.Index, t0, 1) );
		let rows = ReadAll( Group(group.Id), {Ex(TestSample)}, {.Start=t0, .End=t0+1s, .Limit=100} );
		ASSERT_EQ( rows.size(), 1u );
		EXPECT_EQ( rows[0].Source, t0 );
		EXPECT_EQ( rows[0].Status, UA_STATUSCODE_GOOD );
		ASSERT_TRUE( Decoded(rows[0].Value) ) << serialize( rows[0].Value );
		EXPECT_EQ( rows[0].Value, jvalue{stored} ) << serialize( rows[0].Value );
		let live = ReadQL( Node(TestSample) );
		EXPECT_EQ( live, rows[0].Value ) << "the record and the live read agree:  " << serialize( live );
	}

	//`dataType( opc, id )` answers from the registry:  the pumps' own type by name with its members, a namespace-0 structure beside it, and
	//nothing for a type that is no structure.
	TEST_F( StructureTests, DataTypeQueryAnswersFromTheRegistry ){
		WaitSampleType();
		let value = Socket().QuerySync( "dataTypes( opc: $opc, id: $ids ){ name typeId members{ name isArray } }", {{"opc", OpcServerSlug}, {"ids", jarray{Node(SampleType).ToJson(), NodeId{0, UA_NS0ID_RANGE}.ToJson(), NodeId{0, UA_NS0ID_DOUBLE}.ToJson()}}} );
		let& types = value.as_array();
		ASSERT_EQ( types.size(), 2u ) << serialize( value );
		let& sample = types[0].as_object();
		EXPECT_EQ( Json::AsSV(sample, "name"), Ƒ("{}:PumpSampleType", PumpsNs()) ) << "as open62541 names a type built from its definition:  the browse name, qualified";
		EXPECT_EQ( NodeId{sample.at("typeId")}, Node(SampleType) );
		let& members = Json::AsArray( sample, "members" );
		ASSERT_EQ( members.size(), 5u ) << serialize( members );
		const array<sv,5> names{ "Rpm", "Running", "Label", "Limits", "History" };
		for( uint i=0; i<names.size(); ++i ){
			EXPECT_EQ( Json::AsSV(members[i].as_object(), "name"), names[i] );
			EXPECT_EQ( Json::AsBool(members[i].as_object(), "isArray"), i==4 ) << names[i];
		}
		EXPECT_EQ( Json::AsSV(types[1].as_object(), "name"), "Range" );
	}

	//A DataType added through the embedded server after the activation's fetch, and a variable of it:  its value reads as the opaque
	//{typeId, body}, the read noting the encoding the registry lacks, and as its fields once the fetch that note posts has landed.
	TEST_F( StructureTests, ARuntimeTypeDecodesAfterThePostedFetch ){
		WaitSampleType();
		constexpr UA_UInt32 typeId{ 9071 }, encodingId{ 9072 }, variable{ 9073 };//past every other suite's 90xx (GatewayHistoryTests takes 9061-9065).
		auto& ua = Server::GetUAServer();
		let ns = PumpsNs();
		ReadQL( Node(TestSample) );//the socket's client, connected, among Registries'.
		let registries = Registries();
		if( Holds(registries, Node(typeId)) )//the clients and their registries live as long as the process:  a repeat finds the type fetched.
			GTEST_SKIP() << "RuntimeSampleType is in a registry from an earlier run in this process.";
		let fetchesBefore = Fetches( registries );
		let fetching = [&]{ return std::ranges::any_of( registries, []( let& r ){ return r->Fetching(); } ); };
		if( UA_NodeClass c; UA_Server_readNodeClass(ua.Ptr(), Node(typeId), &c) ){//the embedded server's, which a repeat finds added.
			auto typeAttributes = UA_DataTypeAttributes_default;
			typeAttributes.displayName = UA_LOCALIZEDTEXT( (char*)"", (char*)"RuntimeSampleType" );
			UAε( UA_Server_addDataTypeNode(ua.Ptr(), Node(typeId), UA_NODEID_NUMERIC(0, UA_NS0ID_STRUCTURE), UA_NODEID_NUMERIC(0, UA_NS0ID_HASSUBTYPE), UA_QUALIFIEDNAME(ns, (char*)"RuntimeSampleType"), typeAttributes, nullptr, nullptr) );
			UA_StructureField fields[2]; UA_StructureField_init( &fields[0] ); UA_StructureField_init( &fields[1] );
			fields[0].name = UA_STRING( (char*)"X" ); fields[0].dataType = UA_NODEID_NUMERIC( 0, UA_NS0ID_DOUBLE ); fields[0].valueRank = UA_VALUERANK_SCALAR;
			fields[1].name = UA_STRING( (char*)"Name" ); fields[1].dataType = UA_NODEID_NUMERIC( 0, UA_NS0ID_STRING ); fields[1].valueRank = UA_VALUERANK_SCALAR;
			UA_StructureDescription description; UA_StructureDescription_init( &description );
			description.dataTypeId = Node( typeId );
			description.name = UA_QUALIFIEDNAME( ns, (char*)"RuntimeSampleType" );
			description.structureDefinition.defaultEncodingId = Node( encodingId );
			description.structureDefinition.baseDataType = UA_NODEID_NUMERIC( 0, UA_NS0ID_STRUCTURE );
			description.structureDefinition.structureType = UA_STRUCTURETYPE_STRUCTURE;
			description.structureDefinition.fields = fields;
			description.structureDefinition.fieldsSize = 2;
			UA_ExtensionObject eo; UA_ExtensionObject_setValueNoDelete( &eo, &description, &UA_TYPES[UA_TYPES_STRUCTUREDESCRIPTION] );
			UAε( UA_Server_addDataTypeFromDescription(ua.Ptr(), &eo) );
			let id = Node( typeId );
			let type = UA_Server_findDataType( ua.Ptr(), &id );
			ASSERT_TRUE( type );
			const Value initial{ parse(R"({"X":1.5,"Name":"runtime"})"), type };
			auto attributes = UA_VariableAttributes_default;
			attributes.value = initial.value;//borrowed:  addVariableNode copies.
			attributes.dataType = Node( typeId );
			attributes.displayName = UA_LOCALIZEDTEXT( (char*)"", (char*)"runtimeSample" );
			attributes.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
			UAε( UA_Server_addVariableNode(ua.Ptr(), Node(variable), UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES), UA_QUALIFIEDNAME(ns, (char*)"runtimeSample"), UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attributes, nullptr, nullptr) );
		}
		let first = ReadQL( Node(variable) );
		if( Fetches(registries)==fetchesBefore && !fetching() ){//no fetch since the type was added:  the read came before the one it posts.
			EXPECT_TRUE( Opaque(first) ) << serialize( first );
			if( Opaque(first) )
				EXPECT_EQ( NodeId{first.as_object().at("typeId")}, Node(encodingId) ) << "the encoding id, as it came";
		}
		jvalue decoded;
		ASSERT_TRUE( WaitUntil([&]{ decoded = ReadQL( Node(variable) ); return Decoded( decoded, "X" ); }, 15s) ) << serialize( decoded ) << " after " << Fetches(registries)-fetchesBefore << " fetch(es)";
		EXPECT_GT( Fetches(registries), fetchesBefore ) << "the read's note posted a fetch";
		EXPECT_DOUBLE_EQ( Json::AsNumber<double>(decoded.as_object(), "X"), 1.5 );
		EXPECT_EQ( Json::AsSV(decoded.as_object(), "Name"), "runtime" );
		EXPECT_TRUE( Holds(registries, Node(typeId)) ) << "registered";
	}
}