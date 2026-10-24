//Servers phase 5 (#386, docs/servers/plan.md):  the endpoint's write-through - a client's Write of a served variable held for the
//source's answer (spec *Writes*).  A write under an enforced Update grant lands at the OpcServer before the client is answered and the
//served value follows from the source;  a structure and a NodeId go back into the source's namespaces, and one the endpoint can't map
//back is refused;  a client's status and source timestamp go to the source as given;  a Write with an IndexRange is refused;  a write
//on the unenforced group as it ships, and on an enforced one granted Read alone, is refused;  and a write whose connection is down is
//answered with the client's status, not Good.
#include <absl/cleanup/cleanup.h>
#include <jde/opc/uatypes/DateTime.h>
#include "../src/hist/HistRows.h"
#include "utils/EndpointFixture.h"
#define let const auto

namespace Jde::Opc::Gateway::Tests{
	constexpr sv _ns0Uri{ "http://opcfoundation.org/UA/" };

	struct GatewayWritesTests : EndpointFixture{
		Ω Double( const Value& v )ι->optional<double>{ return v.hasValue && UA_Variant_hasScalarType(&v.value, &UA_TYPES[UA_TYPES_DOUBLE]) ? optional<double>{ *(UA_Double*)v.value.data } : nullopt; }
		Ω UserAccessLevel( UATestClient& client, const NodeId& node )ε->UA_Byte{
			UA_Byte y{};
			UAε( UA_Client_readUserAccessLevelAttribute(client, node, &y) );
			return y;
		}
		//A served node under the connection folder by its name alone:  a member the source holds under Objects.
		Ω ServedRoot( UATestClient& client, sv name )ε->optional<NodeId>{
			let ns = Mapped( PumpsUri );
			return ns ? client.Translate( NodeId::ObjectsFolder(), {{_own, OpcServerSlug}, {*ns, string{name}}} ) : nullopt;
		}
		//A variable of the embedded OpcServer under Objects, in the pumps' namespace, with `accessLevel`:  writable, and with the status
		//and timestamp bits where a test writes those.
		Ω AddSourceVariable( UA_UInt32 node, const char* name, UA_UInt32 dataType, void* value, const UA_DataType& type, UA_Byte accessLevel=UA_ACCESSLEVELMASK_READ|UA_ACCESSLEVELMASK_WRITE )ε->void{
			auto ua = Server::GetUAServer().Ptr();
			if( UA_NodeClass c; !UA_Server_readNodeClass(ua, Node(node), &c) )//a repeat of this test.
				return;
			auto attributes = UA_VariableAttributes_default;
			UA_Variant_setScalar( &attributes.value, value, &type );
			attributes.dataType = UA_NODEID_NUMERIC( 0, dataType );
			attributes.displayName = UA_LOCALIZEDTEXT( (char*)"", (char*)name );
			attributes.accessLevel = accessLevel;
			UAε( UA_Server_addVariableNode(ua, Node(node), UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES), UA_QUALIFIEDNAME(_ns, (char*)name), UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attributes, nullptr, nullptr) );
		}
		Ω AtSource( UA_UInt32 node )ι->Value{ return Opc::ReadValue( Server::GetUAServer(), Node(node), UA_TIMESTAMPSTORETURN_BOTH ); }//the source's node, both timestamps.
		//The suite's user may write the node:  the grant has reached the gateway's authorizer and the source lets writes.
		Ω WaitWritable( UATestClient& client, const NodeId& node )ι->bool{ return WaitUntil( [&]{ return UserAccessLevel( client, node ) & UA_ACCESSLEVELMASK_WRITE; } ); }
	};

	//A write through a served node lands at the source before the client is answered (spec *Writes*), and the served value follows
	//from the source's report of it, not from the write.
	TEST_F( GatewayWritesTests, AWriteGoesThroughToTheSource ){
		const OneNodeGroup group{ "gatewayWritesThrough", RpmManual };
		WaitPath( group.Host, group.Index );
		auto client = Client();
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = Served( *client, "pumpManual", "motorRpm" ); return node.has_value(); }) );
		auto open = EnforcedHistGroups();
		ASSERT_TRUE( WaitWritable(*client, *node) ) << "the grant reaches the gateway's authorizer";
		const double value = BaseRpm();
		EXPECT_EQ( client->Write(*node, &value, UA_TYPES[UA_TYPES_DOUBLE]), UA_STATUSCODE_GOOD );
		EXPECT_EQ( Current(RpmManual).AsNumber<double>(), value ) << "the source holds it when the client is answered";
		Value served{ UA_STATUSCODE_BADWAITINGFORINITIALDATA };
		EXPECT_TRUE( WaitUntil([&]{ served = client->ReadValue( *node ); return Double(served)==value; }) ) << "the served value follows from the source:  " << Double(served).value_or(NAN);
	}

	//A structure goes through whole, and a NodeId goes back into the source's namespace (spec *Namespaces*):  one in the endpoint's index
	//for the pumps' namespace lands in the source's;  one in the endpoint's own namespace, which no source index names, is refused with
	//Bad_NodeIdInvalid and never sent.
	TEST_F( GatewayWritesTests, AStructureAndANodeIdGoBackToTheSource ){
		constexpr UA_UInt32 range{ 9061 }, nodeId{ 9062 };//past GatewayHistoryTests' 905x.
		UA_Range r{ 0, 1500 };
		UA_NodeId id = Node( RpmManual );
		AddSourceVariable( range, "writeRange", UA_NS0ID_RANGE, &r, UA_TYPES[UA_TYPES_RANGE] );
		AddSourceVariable( nodeId, "writeNodeId", UA_NS0ID_NODEID, &id, UA_TYPES[UA_TYPES_NODEID] );
		let itemsBefore = Items();
		let groupId = CreateGroup( "gatewayWritesTypes" );
		absl::Cleanup purge = [&]{ Purge( groupId, itemsBefore ); };
		const array indexes{ AddNode(groupId, range), AddNode(groupId, nodeId) };
		auto host = Host( groupId );
		WaitSubscribed( host, indexes.size() );
		for( let index : indexes )
			WaitPath( host, index );
		auto client = Client();
		optional<NodeId> rangeNode, nodeIdNode;
		ASSERT_TRUE( WaitUntil([&]{ rangeNode = ServedRoot( *client, "writeRange" ); nodeIdNode = ServedRoot( *client, "writeNodeId" ); return rangeNode && nodeIdNode; }) );
		auto open = EnforcedHistGroups();
		ASSERT_TRUE( WaitWritable(*client, *rangeNode) && WaitWritable(*client, *nodeIdNode) ) << "the grant reaches the gateway's authorizer";
		UA_Range written{ 1, 2000 };
		EXPECT_EQ( client->Write(*rangeNode, &written, UA_TYPES[UA_TYPES_RANGE]), UA_STATUSCODE_GOOD );
		let atSource = AtSource( range );
		ASSERT_TRUE( UA_Variant_hasScalarType(&atSource.value, &UA_TYPES[UA_TYPES_RANGE]) ) << "the structure, whole";
		EXPECT_EQ( static_cast<const UA_Range*>(atSource.value.data)->high, 2000 );
		const UA_NodeId mapped = UA_NODEID_NUMERIC( *Mapped(PumpsUri), 4321 );
		EXPECT_EQ( client->Write(*nodeIdNode, &mapped, UA_TYPES[UA_TYPES_NODEID]), UA_STATUSCODE_GOOD );
		let nodeIdAtSource = AtSource( nodeId );
		ASSERT_TRUE( UA_Variant_hasScalarType(&nodeIdAtSource.value, &UA_TYPES[UA_TYPES_NODEID]) );
		EXPECT_EQ( NodeId{*static_cast<const UA_NodeId*>(nodeIdAtSource.value.data)}, Node(4321) ) << "in the source's namespace";
		const UA_NodeId own = UA_NODEID_NUMERIC( _own, 7 );
		EXPECT_EQ( client->Write(*nodeIdNode, &own, UA_TYPES[UA_TYPES_NODEID]), UA_STATUSCODE_BADNODEIDINVALID ) << "the endpoint's own namespace has no index at the source";
		EXPECT_EQ( NodeId{*static_cast<const UA_NodeId*>(AtSource(nodeId).value.data)}, Node(4321) ) << "never sent";
	}

	//A client with the status and timestamp bits writes a DataValue whose status and source timestamp go to the source as given, which
	//gates them again (spec *Writes*):  the source's variable carries both bits here, so the endpoint's AccessLevel, the source's, lets them.
	TEST_F( GatewayWritesTests, StatusAndTimestampsGoAsGiven ){
		constexpr UA_UInt32 stamped{ 9063 };
		double initial{ 1 };
		AddSourceVariable( stamped, "writeStamped", UA_NS0ID_DOUBLE, &initial, UA_TYPES[UA_TYPES_DOUBLE], UA_ACCESSLEVELMASK_READ|UA_ACCESSLEVELMASK_WRITE|UA_ACCESSLEVELMASK_STATUSWRITE|UA_ACCESSLEVELMASK_TIMESTAMPWRITE );
		let itemsBefore = Items();
		let groupId = CreateGroup( "gatewayWritesStamped" );
		absl::Cleanup purge = [&]{ Purge( groupId, itemsBefore ); };
		let index = AddNode( groupId, stamped );
		auto host = Host( groupId );
		WaitSubscribed( host, 1 );
		WaitPath( host, index );
		auto client = Client();
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = ServedRoot( *client, "writeStamped" ); return node.has_value(); }) );
		auto open = EnforcedHistGroups();
		ASSERT_TRUE( WaitUntil([&]{ let user = UserAccessLevel( *client, *node ); return (user & UA_ACCESSLEVELMASK_WRITE) && (user & UA_ACCESSLEVELMASK_STATUSWRITE) && (user & UA_ACCESSLEVELMASK_TIMESTAMPWRITE); }) ) << "Administer's bits, the source's AccessLevel letting them";
		double value{ 42 };
		UA_DataValue given; UA_DataValue_init( &given );
		UA_Variant_setScalar( &given.value, &value, &UA_TYPES[UA_TYPES_DOUBLE] );
		given.hasValue = true;
		given.status = UA_STATUSCODE_UNCERTAINLASTUSABLEVALUE;
		given.hasStatus = true;
		given.sourceTimestamp = UADateTime{ Since()-1h }.UA();
		given.hasSourceTimestamp = true;
		EXPECT_EQ( client->Write(*node, given), UA_STATUSCODE_GOOD );
		let atSource = AtSource( stamped );
		EXPECT_EQ( Double(atSource), value );
		EXPECT_EQ( atSource.status, UA_STATUSCODE_UNCERTAINLASTUSABLEVALUE ) << "the status as given";
		EXPECT_TRUE( atSource.hasSourceTimestamp );
		EXPECT_EQ( atSource.sourceTimestamp, given.sourceTimestamp ) << "the source timestamp as given";
	}

	//A Write with an IndexRange is refused Bad_WriteNotSupported:  the forward carries a whole value.
	TEST_F( GatewayWritesTests, AnIndexRangeIsRefused ){
		const OneNodeGroup group{ "gatewayWritesRange", RpmManual };
		WaitPath( group.Host, group.Index );
		auto client = Client();
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = Served( *client, "pumpManual", "motorRpm" ); return node.has_value(); }) );
		auto open = EnforcedHistGroups();
		ASSERT_TRUE( WaitWritable(*client, *node) ) << "the grant reaches the gateway's authorizer";
		let before = Current( RpmManual ).AsNumber<double>();
		const double value = before+1;
		EXPECT_EQ( client->Write(*node, &value, UA_TYPES[UA_TYPES_DOUBLE], "0"), UA_STATUSCODE_BADWRITENOTSUPPORTED );
		EXPECT_EQ( Current(RpmManual).AsNumber<double>(), before ) << "never sent";
	}

	//A write on an unenforced resource is refused, as #237 ruled for edits:  the UserAccessLevel carries no Write on the group as it
	//ships, nor on an enforced group granted Read alone, and the Write answers Bad_UserAccessDenied with the source untouched.
	TEST_F( GatewayWritesTests, AnUnenforcedRowRefusesAWrite ){
		const OneNodeGroup group{ "gatewayWritesRefused", RpmManual };
		WaitPath( group.Host, group.Index );
		auto client = Client();
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = Served( *client, "pumpManual", "motorRpm" ); return node.has_value(); }) );
		let refused = [&]( sv when ){
			let user = UserAccessLevel( *client, *node );
			EXPECT_TRUE( user & UA_ACCESSLEVELMASK_READ ) << when;
			EXPECT_FALSE( user & UA_ACCESSLEVELMASK_WRITE ) << when;
			let before = Current( RpmManual ).AsNumber<double>();
			const double value = before+1;
			EXPECT_EQ( client->Write(*node, &value, UA_TYPES[UA_TYPES_DOUBLE]), UA_STATUSCODE_BADUSERACCESSDENIED ) << when;
			EXPECT_EQ( Current(RpmManual).AsNumber<double>(), before ) << when << ":  the source untouched";
		};
		refused( "unenforced" );
		auto open = EnforcedHistGroups();
		ScopeHistGroup( group.Id, Access::ERights::Read );
		absl::Cleanup unscope = [&]{ try{ UnscopeHistGroup( group.Id ); }catch( const std::exception& e ){ ADD_FAILURE() << "the group's resource wasn't deleted:  " << e.what(); } };
		ASSERT_TRUE( WaitUntil([&]{ let user = UserAccessLevel( *client, *node ); return (user & UA_ACCESSLEVELMASK_READ) && !(user & UA_ACCESSLEVELMASK_WRITE); }) ) << "the group's row reaches the gateway's authorizer";
		refused( "enforced, Read alone" );
	}

	//A write while the connection is down answers the client's status, not Good:  the forward connects as the collector would and the
	//connect's refusal is the answer.  The member serves from its row, as GatewayNodesTests' dead connection does, its AccessLevel the
	//source's as the row has it.
	TEST_F( GatewayWritesTests, ADownConnectionAnswersTheClientsStatus ){
		const string slug{ "gatewayWritesDead" };
		PurgeServerCnnctn( slug );
		let connection = GetConnection( slug, "opc.tcp://127.0.0.1:1", "" ).Id;
		absl::Cleanup purgeConnection = [=]{ PurgeServerCnnctn( connection ); };
		let groupId = Id( Mutate("createHistGroup( name:$name, slug:$slug, serverConnectionId:$connection ){ id }", {{"name", "Dead 5"}, {"slug", slug}, {"connection", connection}}) );
		absl::Cleanup purgeGroup = [=]{ try{ Mutate( "purgeHistGroup( id:$id )", {{"id", groupId}} ); }catch( const std::exception& e ){ WARNT( ELogTags::Test, "purgeHistGroup({}):  {}", groupId, e.what() ); } };
		let index = Id( Mutate("createHistGroupNode( histGroupId:$g, node:$node ){ id }", {{"g", groupId}, {"node", serialize(Ex(Rpm2))}}) );
		auto client = Client();
		optional<UA_UInt16> ns; optional<NodeId> undescribed;
		ASSERT_TRUE( WaitUntil([&]{ ns = Mapped( PumpsUri, slug ); undescribed = ns ? client->Translate( NodeId::ObjectsFolder(), {{_own, slug}, {*ns, Ƒ("i={}", Rpm2)}} ) : nullopt; return undescribed.has_value(); }) ) << "under the connection folder by its id";
		HistNodeAttributes written;
		written.DisplayName = "Motor speed";
		written.DataType = ExNodeId{ jvalue{jobject{{"nsu", _ns0Uri}, {"i", UA_NS0ID_DOUBLE}}} };
		written.ValueRank = -1;
		written.AccessLevel = UA_ACCESSLEVELMASK_READ|UA_ACCESSLEVELMASK_WRITE;
		written.Path = vector<HistPathSegment>{ {string{PumpsUri}, "pump2"}, {string{PumpsUri}, "motorRpm"} };
		DS()->ExecuteSync( UpdateHistNodeAttributesSql(index, written) );
		Mutate( "deleteHistGroup( id:$id )", {{"id", groupId}} );
		ASSERT_TRUE( WaitUntil([&]{ return client->Read(*undescribed).status==UA_STATUSCODE_BADNODEIDUNKNOWN; }) );
		Mutate( "restoreHistGroup( id:$id )", {{"id", groupId}} );
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = Served( *client, "pump2", "motorRpm", slug ); return node.has_value(); }) ) << "at its path, from the row";
		auto open = EnforcedHistGroups();
		ASSERT_TRUE( WaitWritable(*client, *node) ) << "the grant reaches the gateway's authorizer, and the row's AccessLevel lets writes";
		const double value{ 1 };
		let sc = client->Write( *node, &value, UA_TYPES[UA_TYPES_DOUBLE] );
		EXPECT_TRUE( UA_StatusCode_isBad(sc) ) << UA_StatusCode_name( sc );
		EXPECT_NE( sc, UA_STATUSCODE_BADUSERACCESSDENIED ) << "refused by the gate, not the connection";
		EXPECT_NE( sc, UA_STATUSCODE_BADNOTWRITABLE );
		EXPECT_NE( sc, UA_STATUSCODE_BADTIMEOUT ) << "the client timed out before the connect failed";
		EXPECT_NE( sc, UA_STATUSCODE_BADWRITENOTSUPPORTED ) << "the write never reached the forward";
	}
}