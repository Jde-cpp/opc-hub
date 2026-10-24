//Servers phase 3b (#384, docs/servers/plan.md):  the endpoint's live values - each served variable a callback source over the
//mirror's cache, fed by the group host's tap.  A subscription sees the value the source wrote, with the source's timestamps, and the
//served value agrees with the group's last record, the spec's differential;  a structure-typed and a NodeId-typed member read live
//in the gateway's namespaces;  a connection's loss sets Bad_NoCommunication, the value kept, until the first value after;  a
//variable with no value answers its status alone;  a variable re-added serves its member's value as the cache holds it;  a value
//is remapped by the NamespaceArray it came under;  and a restarted endpoint maps by the arrays its collectors last read.
#include <absl/cleanup/cleanup.h>
#include <jde/opc/uatypes/DateTime.h>
#include "../src/server/ServedValues.h"
#include "utils/EndpointFixture.h"
#define let const auto

namespace Jde::Opc::Gateway::Tests{
	struct GatewayValuesTests : EndpointFixture{
		Ω SourceTime( const Value& v )ι->optional<TimePoint>{ return v.hasSourceTimestamp ? optional<TimePoint>{ UADateTime{v.sourceTimestamp}.Time() } : nullopt; }
		Ω NodeIdNamespace( const Value& v )ι->optional<UA_UInt16>{ return v.hasValue && UA_Variant_hasScalarType(&v.value, &UA_TYPES[UA_TYPES_NODEID]) ? optional<UA_UInt16>{ static_cast<const UA_NodeId*>(v.value.data)->namespaceIndex } : nullopt; }
		//A NodeId in the source's namespace `ns` as the member's value, as the tap stores one.
		Ω StoreNodeId( ServedValues& values, const OneNodeGroup& group, UA_UInt16 ns )ι->void{
			UA_DataValue value; UA_DataValue_init( &value );
			const UA_NodeId id = UA_NODEID_NUMERIC( ns, 1234 );
			UA_Variant_setScalarCopy( &value.value, &id, &UA_TYPES[UA_TYPES_NODEID] );
			value.hasValue = true;
			values.Store( group.Id, group.Index, value );
			UA_DataValue_clear( &value );
		}
		//The pumps' index in the source's array, which a test needs to differ from the endpoint's for it to tell them apart.
		Ω PumpsIndex( const vector<string>& uris )ι->optional<UA_UInt16>{
			let p = std::ranges::find( uris, PumpsUri );
			return p==uris.end() ? nullopt : optional<UA_UInt16>{ (UA_UInt16)std::distance(uris.begin(), p) };
		}
	};

	//A subscription on a served node sees the value the source wrote, with the source's timestamp and status (spec *Live values*), a read
	//answers the same, and both agree with the group's last record of it:  the spec's differential.
	TEST_F( GatewayValuesTests, ASubscriptionSeesTheSourcesValue ){
		const OneNodeGroup group{ "gatewayValuesLive", RpmManual };
		WaitPath( group.Host, group.Index );
		vector<Value> notified;//before the client, which fills it until it disconnects.
		auto client = Client();
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = Served( *client, "pumpManual", "motorRpm" ); return node.has_value(); }) );
		let subscription = client->Subscribe( 100 );
		client->Monitor( subscription, *node, 100, notified );
		let value = BaseRpm();
		let source = Since();
		WriteRpm( value, source );
		let seen = [&]{ return std::ranges::find_if( notified, [&]( let& v ){ return Double(v)==value; } ); };
		ASSERT_TRUE( WaitUntil([&]{ client->Iterate( 50 ); return seen()!=notified.end(); }, 10s, 0ms) ) << notified.size() << " notification(s), none with the value";
		EXPECT_EQ( SourceTime(*seen()), source ) << "the source's timestamp";
		EXPECT_TRUE( seen()->hasServerTimestamp );
		EXPECT_EQ( seen()->status, UA_STATUSCODE_GOOD );
		let read = client->ReadValue( *node );
		EXPECT_EQ( Double(read), value );
		EXPECT_EQ( SourceTime(read), source );
		EXPECT_EQ( read.status, UA_STATUSCODE_GOOD );
		let stored = WaitStored( group.Host, group.Index, source, 1 );
		let record = Opc::Hist::ToUA( stored.back().Value );
		EXPECT_EQ( Double(record), Double(read) ) << "the served value is the group's last record";
		EXPECT_EQ( SourceTime(record), SourceTime(read) );
		EXPECT_EQ( record.status, read.status );
	}

	//A structure-typed member and a NodeId-typed one read live in the gateway's namespaces (spec *Namespaces*):  a Range, namespace
	//0's structure, decodes as the structure;  a NodeId in the pumps' namespace comes in the endpoint's index for it;  an Argument's
	//DataType, a structure's NodeId member, moves the same way;  and an ExtensionObject of a type this build can't decode keeps its
	//body, its encoding id moved.  The four are added to the embedded OpcServer here, under Objects.
	TEST_F( GatewayValuesTests, MembersDecodeInTheGatewaysNamespaces ){
		constexpr UA_UInt32 range{ 9041 }, nodeId{ 9042 }, opaque{ 9043 }, encoding{ 9044 }, argument{ 9045 };//past HistGroupTests' 9024-9034, which it adds later in the run.
		auto& ua = Server::GetUAServer();
		UA_Range r{ 0, 1500 };
		UA_NodeId id = Node( RpmManual );
		const array<UA_Byte,4> body{ 1, 2, 3, 4 };
		UA_ExtensionObject o; UA_ExtensionObject_init( &o );
		o.encoding = UA_EXTENSIONOBJECT_ENCODED_BYTESTRING;
		o.content.encoded.typeId = Node( encoding );
		o.content.encoded.body = UA_ByteString{ body.size(), (UA_Byte*)body.data() };
		UA_Argument a; UA_Argument_init( &a );
		a.name = UA_STRING( (char*)"rpm" );
		a.dataType = Node( encoding );
		a.valueRank = UA_VALUERANK_SCALAR;
		let add = [&]( UA_UInt32 node, const char* name, UA_UInt32 dataType, void* value, const UA_DataType& type ){
			if( UA_NodeClass c; !UA_Server_readNodeClass(ua.Ptr(), Node(node), &c) )//a repeat of this test.
				return;
			auto attributes = UA_VariableAttributes_default;
			UA_Variant_setScalar( &attributes.value, value, &type );
			attributes.dataType = UA_NODEID_NUMERIC( 0, dataType );
			attributes.displayName = UA_LOCALIZEDTEXT( (char*)"", (char*)name );
			attributes.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
			UAε( UA_Server_addVariableNode(ua.Ptr(), Node(node), UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES), UA_QUALIFIEDNAME(_ns, (char*)name), UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attributes, nullptr, nullptr) );
		};
		add( range, "servedRange", UA_NS0ID_RANGE, &r, UA_TYPES[UA_TYPES_RANGE] );
		add( nodeId, "servedNodeId", UA_NS0ID_NODEID, &id, UA_TYPES[UA_TYPES_NODEID] );
		add( opaque, "servedOpaque", UA_NS0ID_BASEDATATYPE, &o, UA_TYPES[UA_TYPES_EXTENSIONOBJECT] );
		add( argument, "servedArgument", UA_NS0ID_ARGUMENT, &a, UA_TYPES[UA_TYPES_ARGUMENT] );

		let itemsBefore = Items();
		let groupId = CreateGroup( "gatewayValuesTypes" );
		absl::Cleanup purge = [&]{ Purge( groupId, itemsBefore ); };
		const array indexes{ AddNode(groupId, range), AddNode(groupId, nodeId), AddNode(groupId, opaque), AddNode(groupId, argument) };
		auto host = Host( groupId );
		WaitSubscribed( host, indexes.size() );
		for( let index : indexes )
			WaitPath( host, index );
		auto client = Client();
		optional<NodeId> rangeNode, nodeIdNode, opaqueNode, argumentNode;
		ASSERT_TRUE( WaitUntil([&]{ rangeNode = ServedRoot( *client, "servedRange" ); nodeIdNode = ServedRoot( *client, "servedNodeId" ); opaqueNode = ServedRoot( *client, "servedOpaque" ); argumentNode = ServedRoot( *client, "servedArgument" ); return rangeNode && nodeIdNode && opaqueNode && argumentNode; }) );
		let ns = *Mapped( PumpsUri );
		Value rangeValue{ UA_STATUSCODE_BADWAITINGFORINITIALDATA }, nodeIdValue{ UA_STATUSCODE_BADWAITINGFORINITIALDATA }, opaqueValue{ UA_STATUSCODE_BADWAITINGFORINITIALDATA }, argumentValue{ UA_STATUSCODE_BADWAITINGFORINITIALDATA };
		ASSERT_TRUE( WaitUntil([&]{
			rangeValue = client->ReadValue( *rangeNode ); nodeIdValue = client->ReadValue( *nodeIdNode ); opaqueValue = client->ReadValue( *opaqueNode ); argumentValue = client->ReadValue( *argumentNode );
			return rangeValue.hasValue && nodeIdValue.hasValue && opaqueValue.hasValue && argumentValue.hasValue;
		}) ) << "statuses " << rangeValue.status << " " << nodeIdValue.status << " " << opaqueValue.status << " " << argumentValue.status;
		ASSERT_TRUE( UA_Variant_hasScalarType(&rangeValue.value, &UA_TYPES[UA_TYPES_RANGE]) ) << "decoded as the structure";
		EXPECT_EQ( static_cast<const UA_Range*>(rangeValue.value.data)->high, 1500 );
		ASSERT_TRUE( UA_Variant_hasScalarType(&nodeIdValue.value, &UA_TYPES[UA_TYPES_NODEID]) );
		EXPECT_EQ( NodeId{*static_cast<const UA_NodeId*>(nodeIdValue.value.data)}, NodeId(ns, RpmManual) ) << "the pumps' namespace, mapped";
		ASSERT_TRUE( UA_Variant_hasScalarType(&opaqueValue.value, &UA_TYPES[UA_TYPES_EXTENSIONOBJECT]) );
		let& eo = *static_cast<const UA_ExtensionObject*>( opaqueValue.value.data );
		ASSERT_EQ( eo.encoding, UA_EXTENSIONOBJECT_ENCODED_BYTESTRING ) << "a type the gateway can't decode stays encoded";
		EXPECT_EQ( NodeId{eo.content.encoded.typeId}, NodeId(ns, encoding) ) << "the encoding id, mapped";
		ASSERT_EQ( eo.content.encoded.body.length, body.size() );
		EXPECT_TRUE( std::equal(body.begin(), body.end(), eo.content.encoded.body.data) ) << "the body as it came";
		ASSERT_TRUE( UA_Variant_hasScalarType(&argumentValue.value, &UA_TYPES[UA_TYPES_ARGUMENT]) ) << "decoded as the structure";
		let& arg = *static_cast<const UA_Argument*>( argumentValue.value.data );
		EXPECT_EQ( NodeId{arg.dataType}, NodeId(ns, encoding) ) << "a structure's NodeId member, mapped";
		EXPECT_EQ( ToSV(arg.name), "rpm" );
	}

	//The collector's connection drops, as HistCollectorTests provokes it:  the served value's status is Bad_NoCommunication, its value
	//and timestamps kept, until the first value after the rebuild, which replaces it.
	TEST_F( GatewayValuesTests, ALossSetsNoCommunication ){
		const OneNodeGroup group{ "gatewayValuesLoss", RpmManual };
		WaitPath( group.Host, group.Index );
		auto client = Client();
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = Served( *client, "pumpManual", "motorRpm" ); return node.has_value(); }) );
		Value before{ UA_STATUSCODE_BADWAITINGFORINITIALDATA };
		ASSERT_TRUE( WaitUntil([&]{ before = client->ReadValue( *node ); return before.hasValue && before.status==UA_STATUSCODE_GOOD; }) ) << "status " << before.status;
		auto collector = Collector();
		ASSERT_TRUE( collector );
		UAClient::ConnectionLost( sp<UAClient>{collector} );
		Value lost{ UA_STATUSCODE_GOOD };
		ASSERT_TRUE( WaitUntil([&]{ lost = client->ReadValue( *node ); return lost.status==UA_STATUSCODE_BADNOCOMMUNICATION; }, 10s, 1ms) ) << "status " << lost.status;
		EXPECT_EQ( Double(lost), Double(before) ) << "the value kept";
		EXPECT_EQ( SourceTime(lost), SourceTime(before) );
		let value = BaseRpm();
		let source = Since();
		WriteRpm( value, source );
		Value after{ UA_STATUSCODE_GOOD };
		EXPECT_TRUE( WaitUntil([&]{ after = client->ReadValue( *node ); return after.status==UA_STATUSCODE_GOOD && Double(after)==value; }, 15s) ) << "the first value after the return replaces it:  status " << after.status;
		EXPECT_EQ( SourceTime(after), source );
	}

	//A variable with no value answers its status alone, no value of null:  Bad_WaitingForInitialData before the first, and a loss
	//before it Bad_NoCommunication, which open62541 answers as Bad_WaitingForInitialData for a Double the read calls Good.  The
	//member is made again in the cache without a value here, the source's unchanged so no value follows.
	TEST_F( GatewayValuesTests, NoValueAnswersItsStatus ){
		const OneNodeGroup group{ "gatewayValuesNone", RpmManual };
		WaitPath( group.Host, group.Index );
		auto client = Client();
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = Served( *client, "pumpManual", "motorRpm" ); return node.has_value(); }) );
		ASSERT_TRUE( WaitUntil([&]{ return client->ReadValue( *node ).status==UA_STATUSCODE_GOOD; }) ) << "the item's first value";
		auto& values = *GatewayServer::Instance()->Values();
		values.Leave( group.Id, group.Index );
		values.Join( group.Id, group.Index );
		let none = client->ReadValue( *node );
		EXPECT_EQ( none.status, UA_STATUSCODE_BADWAITINGFORINITIALDATA );
		EXPECT_FALSE( none.hasValue );
		const HistGroupNodePK index = group.Index;
		values.Lose( group.Id, {&index, 1} );
		let lost = client->ReadValue( *node );
		EXPECT_EQ( lost.status, UA_STATUSCODE_BADNOCOMMUNICATION ) << "the loss, not Bad_WaitingForInitialData";
		EXPECT_FALSE( lost.hasValue );
		let value = BaseRpm();
		WriteRpm( value, Since() );
		Value after{ UA_STATUSCODE_GOOD };
		EXPECT_TRUE( WaitUntil([&]{ after = client->ReadValue( *node ); return after.status==UA_STATUSCODE_GOOD && Double(after)==value; }) ) << "the first value replaces it:  status " << after.status;
	}

	//A variable re-added, as a describe's move re-adds it, serves its member's value as the cache holds it, kept while the node had no
	//variable:  the last value with its timestamp, and after a loss Bad_NoCommunication with the value kept, not a stale Good.
	TEST_F( GatewayValuesTests, AReAddedVariableKeepsItsMembersValue ){
		const OneNodeGroup group{ "gatewayValuesReAdd", RpmManual };
		WaitPath( group.Host, group.Index );
		auto client = Client();
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = Served( *client, "pumpManual", "motorRpm" ); return node.has_value(); }) );
		Value before{ UA_STATUSCODE_BADWAITINGFORINITIALDATA };
		ASSERT_TRUE( WaitUntil([&]{ before = client->ReadValue( *node ); return before.status==UA_STATUSCODE_GOOD; }) ) << "the item's first value";
		auto& values = *GatewayServer::Instance()->Values();
		let readd = [&]{ values.Remove( *node ); return values.Add( *node, group.Id, group.Index, OpcServerSlug, group.Host->Group() ); };
		ASSERT_TRUE( readd() );
		let again = client->ReadValue( *node );
		EXPECT_EQ( again.status, UA_STATUSCODE_GOOD );
		EXPECT_EQ( Double(again), Double(before) );
		EXPECT_EQ( SourceTime(again), SourceTime(before) );
		const HistGroupNodePK index = group.Index;
		values.Lose( group.Id, {&index, 1} );
		ASSERT_TRUE( readd() );
		let lost = client->ReadValue( *node );
		EXPECT_EQ( lost.status, UA_STATUSCODE_BADNOCOMMUNICATION ) << "the loss, as the cache has it";
		EXPECT_EQ( Double(lost), Double(before) ) << "the value kept";
	}

	//A value is remapped by the NamespaceArray it came under (spec *Namespaces*):  a NodeId stored under the collector's array reads in
	//the endpoint's namespace for its URI after the array is read again reordered, as a restarted source can, and one stored after
	//reads by the new array.
	TEST_F( GatewayValuesTests, AValueKeepsTheNamespacesItCameUnder ){
		const OneNodeGroup group{ "gatewayValuesUris", RpmManual };
		WaitPath( group.Host, group.Index );
		auto client = Client();
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = Served( *client, "pumpManual", "motorRpm" ); return node.has_value(); }) );
		auto uris = group.Host->NamespaceUris();
		let source = PumpsIndex( uris );
		ASSERT_TRUE( source && uris.size()>2 );
		ASSERT_NE( *source, *Mapped(PumpsUri) ) << "the test can't tell the endpoint's index from the source's";
		auto& values = *GatewayServer::Instance()->Values();
		StoreNodeId( values, group, *source );
		EXPECT_EQ( NodeIdNamespace(client->ReadValue(*node)), Mapped(PumpsUri) );
		std::swap( uris[*source], uris[*source==1 ? 2 : 1] );
		values.SetUris( group.Id, uris );
		EXPECT_EQ( NodeIdNamespace(client->ReadValue(*node)), Mapped(PumpsUri) ) << "read by the array it came under";
		StoreNodeId( values, group, *source );
		EXPECT_EQ( NodeIdNamespace(client->ReadValue(*node)), Mapped(uris[*source]) ) << "a value after, by the new one";
	}

	//A restarted endpoint maps a value's indices from its start:  a stable connection reads its NamespaceArray again only on a return,
	//so each group's array is the one its collector last read.  Its members' values come from their next change.
	TEST_F( GatewayValuesTests, ARestartMapsTheNamespacesRead ){
		const OneNodeGroup group{ "gatewayValuesRestart", RpmManual };
		WaitPath( group.Host, group.Index );
		let source = PumpsIndex( group.Host->NamespaceUris() );
		ASSERT_TRUE( source );
		GatewayServer::Stop();
		ASSERT_NO_THROW( GatewayServer::Listen(Ssl()) );
		ASSERT_NO_THROW( GatewayServer::Start() );
		auto client = Client();
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = Served( *client, "pumpManual", "motorRpm" ); return node.has_value(); }) );
		ASSERT_NE( *source, *Mapped(PumpsUri) ) << "the test can't tell the endpoint's index from the source's";
		StoreNodeId( *GatewayServer::Instance()->Values(), group, *source );
		EXPECT_EQ( NodeIdNamespace(client->ReadValue(*node)), Mapped(PumpsUri) ) << "unmapped:  the read before the restart is lost";
		let value = BaseRpm();
		WriteRpm( value, Since() );
		Value after{ UA_STATUSCODE_GOOD };
		EXPECT_TRUE( WaitUntil([&]{ after = client->ReadValue( *node ); return after.status==UA_STATUSCODE_GOOD && Double(after)==value; }) ) << "the member's next value:  status " << after.status;
	}
}