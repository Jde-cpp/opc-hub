//Servers phase 3b (#384, docs/servers/plan.md):  the endpoint's live values - each served variable a callback source over the
//mirror's cache, fed by the group host's tap.  A subscription sees the value the source wrote, with the source's timestamps, and the
//served value agrees with the group's last record, the spec's differential;  a structure-typed and a NodeId-typed member read live
//in the gateway's namespaces;  and a connection's loss sets Bad_NoCommunication, the value kept, until the first value after.
#include <absl/cleanup/cleanup.h>
#include <jde/opc/uatypes/DateTime.h>
#include "utils/EndpointFixture.h"
#define let const auto

namespace Jde::Opc::Gateway::Tests{
	struct GatewayValuesTests : EndpointFixture{
		Ω Double( const Value& v )ι->optional<double>{ return v.hasValue && UA_Variant_hasScalarType(&v.value, &UA_TYPES[UA_TYPES_DOUBLE]) ? optional<double>{ *(UA_Double*)v.value.data } : nullopt; }
		Ω SourceTime( const Value& v )ι->optional<TimePoint>{ return v.hasSourceTimestamp ? optional<TimePoint>{ UADateTime{v.sourceTimestamp}.Time() } : nullopt; }
		//A served node under the connection folder by its name alone:  a member the source holds under Objects.
		Ω ServedRoot( UATestClient& client, sv name )ε->optional<NodeId>{
			let ns = Mapped( PumpsUri );
			return ns ? client.Translate( NodeId::ObjectsFolder(), {{_own, OpcServerSlug}, {*ns, string{name}}} ) : nullopt;
		}
	};

	//A subscription on a served node sees the value the source wrote, with the source's timestamp and status (spec *Live values*), a read
	//answers the same, and both agree with the group's last record of it:  the spec's differential.
	TEST_F( GatewayValuesTests, ASubscriptionSeesTheSourcesValue ){
		const OneNodeGroup group{ "gatewayValuesLive", RpmManual };
		WaitPath( group.Host, group.Index );
		auto client = Client();
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = Served( *client, "pumpManual", "motorRpm" ); return node.has_value(); }) );
		vector<Value> notified;
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
	//0's structure, decodes as the structure;  a NodeId in the pumps' namespace comes in the endpoint's index for it;  and an
	//ExtensionObject of a type this build can't decode keeps its body, its encoding id moved the same way.  The three are added to
	//the embedded OpcServer here, under Objects.
	TEST_F( GatewayValuesTests, MembersDecodeInTheGatewaysNamespaces ){
		constexpr UA_UInt32 range{ 9041 }, nodeId{ 9042 }, opaque{ 9043 }, encoding{ 9044 };//past HistGroupTests' 9024-9034, which it adds later in the run.
		auto& ua = Server::GetUAServer();
		UA_Range r{ 0, 1500 };
		UA_NodeId id = Node( RpmManual );
		const array<UA_Byte,4> body{ 1, 2, 3, 4 };
		UA_ExtensionObject o; UA_ExtensionObject_init( &o );
		o.encoding = UA_EXTENSIONOBJECT_ENCODED_BYTESTRING;
		o.content.encoded.typeId = Node( encoding );
		o.content.encoded.body = UA_ByteString{ body.size(), (UA_Byte*)body.data() };
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

		let itemsBefore = Items();
		let groupId = CreateGroup( "gatewayValuesTypes" );
		absl::Cleanup purge = [&]{ Purge( groupId, itemsBefore ); };
		const array indexes{ AddNode(groupId, range), AddNode(groupId, nodeId), AddNode(groupId, opaque) };
		auto host = Host( groupId );
		WaitSubscribed( host, indexes.size() );
		for( let index : indexes )
			WaitPath( host, index );
		auto client = Client();
		optional<NodeId> rangeNode, nodeIdNode, opaqueNode;
		ASSERT_TRUE( WaitUntil([&]{ rangeNode = ServedRoot( *client, "servedRange" ); nodeIdNode = ServedRoot( *client, "servedNodeId" ); opaqueNode = ServedRoot( *client, "servedOpaque" ); return rangeNode && nodeIdNode && opaqueNode; }) );
		let ns = *Mapped( PumpsUri );
		Value rangeValue{ UA_STATUSCODE_BADWAITINGFORINITIALDATA }, nodeIdValue{ UA_STATUSCODE_BADWAITINGFORINITIALDATA }, opaqueValue{ UA_STATUSCODE_BADWAITINGFORINITIALDATA };
		ASSERT_TRUE( WaitUntil([&]{ rangeValue = client->ReadValue( *rangeNode ); nodeIdValue = client->ReadValue( *nodeIdNode ); opaqueValue = client->ReadValue( *opaqueNode ); return rangeValue.hasValue && nodeIdValue.hasValue && opaqueValue.hasValue; }) )
			<< "statuses " << rangeValue.status << " " << nodeIdValue.status << " " << opaqueValue.status;
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
}