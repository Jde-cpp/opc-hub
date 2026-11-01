//Servers phase 5 (#386, docs/servers/plan.md):  the endpoint's write-through - a client's Write of a served variable held for the
//source's answer (spec *Writes*).  A write under an enforced Update grant lands at the OpcServer before the client is answered and the
//served value follows from the source;  a structure and a NodeId go back into the source's namespaces, and one the endpoint can't map
//back is refused;  a client's status and source timestamp go to the source as given;  a variable read-only at the source is read-only
//here;  a Write with an IndexRange is refused;  a write cancelled before it's sent never is, and one out when the endpoint stops is
//logged, not posted;  a write on the unenforced group as it ships, and on an enforced one granted Read alone, is refused;  and a write
//whose connection is down, its source never having given the node's id, is answered Bad_NoCommunication, not Good.
#include <absl/cleanup/cleanup.h>
#include <jde/fwk/log/MemoryLog.h>
#include <jde/opc/uatypes/DateTime.h>
#include "../src/hist/HistRows.h"
#include "utils/EndpointFixture.h"
#define let const auto

namespace Jde::Opc::Gateway::Tests{
	constexpr sv _ns0Uri{ "http://opcfoundation.org/UA/" };

	struct GatewayWritesTests : EndpointFixture{
		Ω AtSource( UA_UInt32 node )ι->Value{ return Opc::ReadValue( Server::GetUAServer(), Node(node), UA_TIMESTAMPSTORETURN_BOTH ); }//the source's node, both timestamps.
		//The suite's user may write the node:  the grant has reached the gateway's authorizer and the source lets writes.
		Ω WaitWritable( UATestClient& client, const NodeId& node )ι->bool{ return WaitUntil( [&]{ return UserAccessLevel( client, node ) & UA_ACCESSLEVELMASK_WRITE; } ); }
		//The source's variable whose writes it holds while `Hold`:  a callback value source, its context the one HeldSource, which a repeat
		//of a test finds still there.  Release answers the held write, outside the source's lock:  the set takes the service lock, which
		//the callbacks hold when they take it.
		struct HeldSource final{ std::mutex Mutex; vector<double> Written; double Current{}; bool Hold{}; const UA_DataValue* Held{}; };
		static constexpr UA_UInt32 HeldNode{ 9064 };
		Ω Held()ι->HeldSource&{ static HeldSource y; return y; }
		Ω AddHeldVariable()ε->HeldSource&{
			auto& source = Held();
			{
				lg _{ source.Mutex };
				source.Written.clear(); source.Current = 0; source.Hold = false; source.Held = nullptr;
			}
			auto ua = Server::GetUAServer().Ptr();
			if( UA_NodeClass c; !UA_Server_readNodeClass(ua, Node(HeldNode), &c) )
				return source;
			double initial{};
			auto attributes = UA_VariableAttributes_default;
			UA_Variant_setScalar( &attributes.value, &initial, &UA_TYPES[UA_TYPES_DOUBLE] );
			attributes.dataType = UA_NODEID_NUMERIC( 0, UA_NS0ID_DOUBLE );
			attributes.displayName = UA_LOCALIZEDTEXT( (char*)"", (char*)"writeHeld" );
			attributes.accessLevel = UA_ACCESSLEVELMASK_READ|UA_ACCESSLEVELMASK_WRITE;
			UAε( UA_Server_addVariableNode(ua, Node(HeldNode), UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES), UA_QUALIFIEDNAME(_ns, (char*)"writeHeld"), UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attributes, &source, nullptr) );
			UAε( UA_Server_setVariableNode_callbackValueSource(ua, Node(HeldNode), UA_CallbackValueSource{
				.read=[]( UA_Server*, const UA_NodeId*, void*, const UA_NodeId*, void* context, UA_Boolean, const UA_NumericRange*, UA_DataValue* value )->UA_StatusCode{
					auto& s = *static_cast<HeldSource*>( context );
					lg _{ s.Mutex };
					value->hasValue = true;
					return UA_Variant_setScalarCopy( &value->value, &s.Current, &UA_TYPES[UA_TYPES_DOUBLE] );
				},
				.write=[]( UA_Server*, const UA_NodeId*, void*, const UA_NodeId*, void* context, const UA_NumericRange*, const UA_DataValue* value )->UA_StatusCode{
					if( !value->hasValue || !UA_Variant_hasScalarType(&value->value, &UA_TYPES[UA_TYPES_DOUBLE]) )
						return UA_STATUSCODE_BADTYPEMISMATCH;
					auto& s = *static_cast<HeldSource*>( context );
					lg _{ s.Mutex };
					s.Current = *static_cast<const double*>( value->value.data );
					s.Written.push_back( s.Current );
					if( !s.Hold )
						return UA_STATUSCODE_GOOD;
					s.Held = value;
					return UA_STATUSCODE_GOODCOMPLETESASYNCHRONOUSLY;
				}
			}) );
			return source;
		}
		Ω Hold( bool hold=true )ι->void{ auto& s = Held(); lg _{ s.Mutex }; s.Hold = hold; }
		Ω Holding()ι->bool{ auto& s = Held(); lg _{ s.Mutex }; return s.Held; }
		Ω Release()ι->void{
			const UA_DataValue* value{};
			{
				auto& s = Held();
				lg _{ s.Mutex };
				s.Hold = false;
				value = std::exchange( s.Held, nullptr );
			}
			if( value )
				UA_Server_setAsyncWriteResult( Server::GetUAServer().Ptr(), value, UA_STATUSCODE_GOOD );
		}
		//One request writing `values` to `node` in order, its answer into `answered`, which outlives the client:  a client's delete
		//answers what's still out.
		struct Answered final{ bool Done{}; UA_StatusCode Service{}; };
		Ω SendWrites( UATestClient& client, const NodeId& node, std::initializer_list<double> values, Answered& answered )ε->UA_UInt32{
			vector<UA_WriteValue> items( values.size() );
			auto value = values.begin();
			for( auto& item : items ){
				UA_WriteValue_init( &item );
				item.nodeId = node;//views:  not cleared.
				item.attributeId = UA_ATTRIBUTEID_VALUE;
				UA_Variant_setScalar( &item.value.value, const_cast<double*>(&*value++), &UA_TYPES[UA_TYPES_DOUBLE] );
				item.value.hasValue = true;
			}
			UA_WriteRequest request; UA_WriteRequest_init( &request );
			request.nodesToWrite = items.data();
			request.nodesToWriteSize = items.size();
			UA_UInt32 requestId{};
			UAε( UA_Client_sendAsyncWriteRequest(client, &request, []( UA_Client*, void* userdata, UA_UInt32, UA_WriteResponse* response ){
				auto& a = *static_cast<Answered*>( userdata );
				a.Service = response->responseHeader.serviceResult;
				a.Done = true;
			}, &answered, &requestId) );
			return requestId;
		}
		//A group serving HeldNode, its node writable for the suite's user once `open` enforces the groups.
		Ω ServeHeld( UATestClient& client, sv slug )ε->std::tuple<uint32,NodeId>{
			let groupId = CreateGroup( slug );
			let index = AddNode( groupId, HeldNode );
			auto host = Host( groupId );
			WaitSubscribed( host, 1 );
			WaitPath( host, index );
			optional<NodeId> node;
			THROW_IF( !WaitUntil([&]{ node = ServedRoot( client, "writeHeld" ); return node.has_value(); }), "writeHeld isn't served." );
			return { groupId, *node };
		}
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

	//The source's AccessLevel is the endpoint's (spec *Writes*):  a variable the source won't let anyone write is read-only here, whatever
	//the group grants - enforced, the suite's user Administer's - and its Write is refused before the callback, the source untouched.
	//Bad_NotWritable:  since open62541 1.5.10 a node whose own AccessLevel lacks Write is refused before the user's level is asked,
	//where 1.5.9 masked the user's by it and answered Bad_UserAccessDenied.
	TEST_F( GatewayWritesTests, ASourceReadOnlyVariableStaysReadOnly ){
		constexpr UA_UInt32 readOnly{ 9065 };
		double initial{ 5 };
		AddSourceVariable( readOnly, "writeReadOnly", UA_NS0ID_DOUBLE, &initial, UA_TYPES[UA_TYPES_DOUBLE], UA_ACCESSLEVELMASK_READ );
		let itemsBefore = Items();
		let groupId = CreateGroup( "gatewayWritesReadOnly" );
		absl::Cleanup purge = [&]{ Purge( groupId, itemsBefore ); };
		let index = AddNode( groupId, readOnly );
		auto host = Host( groupId );
		WaitSubscribed( host, 1 );
		WaitPath( host, index );
		auto client = Client();
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = ServedRoot( *client, "writeReadOnly" ); return node.has_value(); }) );
		auto open = EnforcedHistGroups();
		ASSERT_TRUE( WaitUntil([&]{ return UserAccessLevel( *client, *node ) & UA_ACCESSLEVELMASK_HISTORYWRITE; }) ) << "the grant reaches the gateway's authorizer:  the history's edits, the gateway's own";
		UA_Byte access{};
		ASSERT_EQ( UA_Client_readAccessLevelAttribute(*client, *node, &access), UA_STATUSCODE_GOOD );
		EXPECT_FALSE( access & UA_ACCESSLEVELMASK_WRITE ) << "the source's AccessLevel";
		EXPECT_FALSE( UserAccessLevel(*client, *node) & UA_ACCESSLEVELMASK_WRITE ) << "masked by it";
		const double value{ 6 };
		EXPECT_EQ( client->Write(*node, &value, UA_TYPES[UA_TYPES_DOUBLE]), UA_STATUSCODE_BADNOTWRITABLE );
		EXPECT_EQ( Double(AtSource(readOnly)), initial ) << "the source untouched";
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

	//A write open62541 cancels before its forward starts is dropped, never sent (spec *Writes*):  one request writes the node twice, the
	//source holds the first, and the client cancels the request, which open62541 answers Bad_RequestCancelledByClient.  Once the
	//source answers the first, its audit line says the operation was cancelled, and a third write reaches the source with only the
	//first before it.
	TEST_F( GatewayWritesTests, ACancelledWriteIsNeverSent ){
		if( !Logging::FindLogger<Logging::MemoryLog>() )
			Logging::AddLogger( mu<Logging::MemoryLog>() );
		auto& source = AddHeldVariable();
		let itemsBefore = Items();
		Answered answered;
		auto client = Client();
		let [groupId, node] = ServeHeld( *client, "gatewayWritesCancelled" );
		absl::Cleanup purge = [&]{ Purge( groupId, itemsBefore ); };
		auto open = EnforcedHistGroups();
		ASSERT_TRUE( WaitWritable(*client, node) ) << "the grant reaches the gateway's authorizer";
		absl::Cleanup unhold = Release;
		Hold();
		const double first{ 11 }, second{ 12 }, third{ 13 };
		let requestId = SendWrites( *client, node, {first, second}, answered );
		ASSERT_TRUE( WaitUntil(Holding) ) << "the source holds the first";
		EXPECT_EQ( UA_Client_cancelByRequestId(*client, requestId, nullptr), UA_STATUSCODE_GOOD );
		ASSERT_TRUE( WaitUntil([&]{ UA_Client_run_iterate( *client, 10 ); return answered.Done; }) );
		EXPECT_EQ( answered.Service, UA_STATUSCODE_BADREQUESTCANCELLEDBYCLIENT ) << UA_StatusCode_name( answered.Service );
		let cancelled = [&]{ return Logging::Find( [&](const Logging::Entry& e){ let m = e.Message(); return m.contains( node.ToString() ) && m.contains( "answered after open62541 cancelled it" ); } ).size(); };
		let before = cancelled();
		Release();
		EXPECT_TRUE( WaitUntil([&]{ return cancelled()>before; }) ) << "the first's answer, logged as cancelled:  no reason named, since the cancel gives none";
		EXPECT_EQ( client->Write(node, &third, UA_TYPES[UA_TYPES_DOUBLE]), UA_STATUSCODE_GOOD );
		lg _{ source.Mutex };
		EXPECT_EQ( source.Written, (vector<double>{first, third}) ) << "the second dropped:  cancelled before it was sent";
	}

	//A forward still out when the endpoint stops is answered after its server is gone (spec *Writes*):  the source holds the write
	//while the endpoint stops, then answers it, and the answer is logged, never posted to the stopped server.  The endpoint restarts.
	TEST_F( GatewayWritesTests, AWriteOutAtStopIsLoggedNotPosted ){
		if( !Logging::FindLogger<Logging::MemoryLog>() )
			Logging::AddLogger( mu<Logging::MemoryLog>() );
		AddHeldVariable();
		let itemsBefore = Items();
		Answered answered;
		auto client = Client();
		let [groupId, node] = ServeHeld( *client, "gatewayWritesStopped" );
		absl::Cleanup purge = [&]{ Purge( groupId, itemsBefore ); };
		auto open = EnforcedHistGroups();
		ASSERT_TRUE( WaitWritable(*client, node) ) << "the grant reaches the gateway's authorizer";
		absl::Cleanup unhold = Release;
		Hold();
		SendWrites( *client, node, {21}, answered );
		ASSERT_TRUE( WaitUntil(Holding) ) << "the source holds it";
		GatewayServer::Stop();
		Release();
		let logged = []{ return Logging::Find( [](const Logging::Entry& e){ return e.Message().contains( "answered after the endpoint stopped" ); } ).size()>0; };
		EXPECT_TRUE( WaitUntil(logged) ) << "the source's answer logged, not posted to the stopped server";
		ASSERT_NO_THROW( GatewayServer::Listen(Ssl()) );
		ASSERT_NO_THROW( GatewayServer::Start() );
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

	//A write while the connection is down answers the client's status, not Good:  the source never gave the node's id, so the forward
	//answers Bad_NoCommunication without a connect.  The member serves from its row, as GatewayNodesTests' dead connection does, its
	//AccessLevel the source's as the row has it.
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
		EXPECT_EQ( sc, UA_STATUSCODE_BADNOCOMMUNICATION ) << UA_StatusCode_name( sc );
	}
}