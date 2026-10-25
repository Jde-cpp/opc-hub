//Servers phase 4 (#385, docs/servers/plan.md):  the endpoint's history - the library's UAHistory over the gateway's groups through
//GatewayHistoryHost.  A raw and a processed HistoryRead on a served node agree with the QL `history` with `group` over the same
//range, the spec's differential, Median served from the endpoint's own namespace and the archive start written;  a structure-typed
//member and a NodeId-typed one read from history in the gateway's namespaces, each page remapped by the table at read time;  a
//HistoryUpdate under an enforced grant, then a modified read shows what it replaced, by the session's user, and the QL agrees;  a
//HistoryUpdate's NodeId stored in the source's namespace, and one the endpoint can't map back refused, a QualifiedName's and a
//structure's member's the same way;  a DeleteRawModified's range gone from the history;  and a user without Update is refused, on
//the unenforced group as it ships and on an enforced one granted Read alone.
#include <absl/cleanup/cleanup.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/opc/server/HAConfiguration.h>
#include <jde/opc/server/UAHistory.h>
#include "utils/EndpointFixture.h"
#define let const auto

namespace Jde::Opc::Gateway::Tests{
	struct GatewayHistoryTests : EndpointFixture{
		struct Reading final{ UA_StatusCode Status; vector<Value> Values; };
		struct Info final{ UA_DateTime Time; UA_HistoryUpdateType Type; string User; };
		struct Modified final{ UA_StatusCode Status; vector<Value> Values; vector<Info> Infos; };
		Ω Ticks( TimePoint t )ι->UA_DateTime{ return UADateTime{ t }.UA(); }
		Ω Double( const Value& v )ι->optional<double>{ return v.hasValue && UA_Variant_hasScalarType(&v.value, &UA_TYPES[UA_TYPES_DOUBLE]) ? optional<double>{ *(UA_Double*)v.value.data } : nullopt; }
		Ω Doubles( const vector<Value>& values )ι->vector<optional<double>>{
			vector<optional<double>> y;
			for( let& v : values )
				y.push_back( Double(v) );
			return y;
		}
		Ω SourceTime( const Value& v )ι->optional<TimePoint>{ return v.hasSourceTimestamp ? optional<TimePoint>{ UADateTime{v.sourceTimestamp}.Time() } : nullopt; }
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
		//A variable of the embedded OpcServer under Objects, in the pumps' namespace, for a member of a type the pumps don't have.
		Ω AddSourceVariable( UA_UInt32 node, const char* name, UA_UInt32 dataType, void* value, const UA_DataType& type )ε->void{
			auto ua = Server::GetUAServer().Ptr();
			if( UA_NodeClass c; !UA_Server_readNodeClass(ua, Node(node), &c) )//a repeat of this test.
				return;
			auto attributes = UA_VariableAttributes_default;
			UA_Variant_setScalar( &attributes.value, value, &type );
			attributes.dataType = UA_NODEID_NUMERIC( 0, dataType );
			attributes.displayName = UA_LOCALIZEDTEXT( (char*)"", (char*)name );
			attributes.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
			UAε( UA_Server_addVariableNode(ua, Node(node), UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES), UA_QUALIFIEDNAME(_ns, (char*)name), UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attributes, nullptr, nullptr) );
		}
		//UA_Client_HistoryRead_raw, every page.
		Ω Raw( UATestClient& client, const NodeId& node, TimePoint start, TimePoint end )ι->Reading{
			Reading y{};
			let onPage = []( UA_Client*, const UA_NodeId*, UA_Boolean, const UA_ExtensionObject* data, void* context )->UA_Boolean {
				auto& values = *static_cast<vector<Value>*>( context );
				if( data->encoding==UA_EXTENSIONOBJECT_DECODED && data->content.decoded.type==&UA_TYPES[UA_TYPES_HISTORYDATA] ){
					let& history = *static_cast<const UA_HistoryData*>( data->content.decoded.data );
					for( uint i=0; i<history.dataValuesSize; ++i )
						values.emplace_back( history.dataValues[i] );
				}
				return true;
			};
			y.Status = UA_Client_HistoryRead_raw( client, &node, onPage, Ticks(start), Ticks(end), UA_STRING_NULL, false, 0, UA_TIMESTAMPSTORETURN_BOTH, &y.Values );
			return y;
		}
		//UA_Client_HistoryRead_modified, every page's values and ModificationInfos.
		Ω ReadModified( UATestClient& client, const NodeId& node, TimePoint start, TimePoint end )ι->Modified{
			Modified y{};
			let onPage = []( UA_Client*, const UA_NodeId*, UA_Boolean, const UA_ExtensionObject* data, void* context )->UA_Boolean {
				auto& y = *static_cast<Modified*>( context );
				if( data->encoding==UA_EXTENSIONOBJECT_DECODED && data->content.decoded.type==&UA_TYPES[UA_TYPES_HISTORYMODIFIEDDATA] ){
					let& history = *static_cast<const UA_HistoryModifiedData*>( data->content.decoded.data );
					for( uint i=0; i<history.dataValuesSize; ++i )
						y.Values.emplace_back( history.dataValues[i] );
					for( uint i=0; i<history.modificationInfosSize; ++i )
						y.Infos.push_back( {history.modificationInfos[i].modificationTime, history.modificationInfos[i].updateType, ToString(history.modificationInfos[i].userName)} );
				}
				return true;
			};
			y.Status = UA_Client_HistoryRead_modified( client, &node, onPage, Ticks(start), Ticks(end), UA_STRING_NULL, false, 0, UA_TIMESTAMPSTORETURN_BOTH, &y );
			return y;
		}
		//A processed read, which the client's helpers don't send:  one node's pages, the continuation point followed to the last.
		Ω Processed( UATestClient& client, const NodeId& node, const NodeId& function, TimePoint start, TimePoint end, Duration interval )ε->Reading{
			UA_NodeId type = function;//shallow, as the request's array.
			UA_ReadProcessedDetails details; UA_ReadProcessedDetails_init( &details );
			details.startTime = Ticks( start );
			details.endTime = Ticks( end );
			details.processingInterval = duration<double,std::milli>{ interval }.count();
			details.aggregateTypeSize = 1;
			details.aggregateType = &type;
			details.aggregateConfiguration.useServerCapabilitiesDefaults = true;
			UA_HistoryReadValueId id; UA_HistoryReadValueId_init( &id );
			id.nodeId = node;//shallow
			absl::Cleanup freed = [&]{ UA_ByteString_clear( &id.continuationPoint ); };
			Reading y{ UA_STATUSCODE_GOOD, {} };
			for( uint pages{}; ; ++pages ){
				THROW_IF( pages==100, "A read that never ends." );
				UA_HistoryReadRequest request; UA_HistoryReadRequest_init( &request );
				request.historyReadDetails.encoding = UA_EXTENSIONOBJECT_DECODED;
				request.historyReadDetails.content.decoded.type = &UA_TYPES[UA_TYPES_READPROCESSEDDETAILS];
				request.historyReadDetails.content.decoded.data = &details;
				request.timestampsToReturn = UA_TIMESTAMPSTORETURN_BOTH;
				request.nodesToReadSize = 1;
				request.nodesToRead = &id;
				auto response = UA_Client_Service_historyRead( client, request );
				absl::Cleanup cleared = [&]{ UA_HistoryReadResponse_clear( &response ); };
				if( response.responseHeader.serviceResult || response.resultsSize!=1 ){
					y.Status = response.responseHeader.serviceResult ? response.responseHeader.serviceResult : UA_STATUSCODE_BADUNEXPECTEDERROR;
					return y;
				}
				let& result = response.results[0];
				y.Status = result.statusCode;
				if( result.historyData.encoding==UA_EXTENSIONOBJECT_DECODED && result.historyData.content.decoded.type==&UA_TYPES[UA_TYPES_HISTORYDATA] ){
					let& history = *static_cast<const UA_HistoryData*>( result.historyData.content.decoded.data );
					for( uint i=0; i<history.dataValuesSize; ++i )
						y.Values.emplace_back( history.dataValues[i] );
				}
				UA_ByteString_clear( &id.continuationPoint );
				if( UA_StatusCode_isBad(y.Status) || !result.continuationPoint.length )
					return y;
				UA_ByteString_copy( &result.continuationPoint, &id.continuationPoint );
			}
		}
		//A value as a HistoryUpdate carries it, keyed by its SourceTimestamp, and open62541's helpers:  the one value's result, or the
		//entry's status when that isn't Good.
		Ω Sample( const void* value, const UA_DataType& type, TimePoint source )ι->Value{
			UA_Variant v; UA_Variant_init( &v );
			UA_Variant_setScalarCopy( &v, value, &type );
			Value y{ move(v) };
			y.sourceTimestamp = Ticks( source );
			y.hasSourceTimestamp = true;
			return y;
		}
		Ω Sample( double value, TimePoint source )ι->Value{ return Sample( &value, UA_TYPES[UA_TYPES_DOUBLE], source ); }
		Ω Insert( UATestClient& client, const NodeId& node, Value v )ι->UA_StatusCode{ return UA_Client_HistoryUpdate_insert( client, &node, &v ); }
		Ω Insert( UATestClient& client, const NodeId& node, double value, TimePoint source )ι->UA_StatusCode{ return Insert( client, node, Sample(value, source) ); }
		Ω Replace( UATestClient& client, const NodeId& node, double value, TimePoint source )ι->UA_StatusCode{ auto v = Sample( value, source ); return UA_Client_HistoryUpdate_replace( client, &node, &v ); }
		//One value's UpdateData as the service answers it:  the entry's status, which the helpers hide behind Bad_UnexpectedError for an
		//entry refused whole, since that carries no value results.
		Ω ReplaceEntry( UATestClient& client, const NodeId& node, double value, TimePoint source )ι->UA_StatusCode{
			auto v = Sample( value, source );
			UA_UpdateDataDetails details; UA_UpdateDataDetails_init( &details );
			details.nodeId = node;//shallow
			details.performInsertReplace = UA_PERFORMUPDATETYPE_REPLACE;
			details.updateValuesSize = 1;
			details.updateValues = &v;
			UA_ExtensionObject entry; UA_ExtensionObject_init( &entry );
			entry.encoding = UA_EXTENSIONOBJECT_DECODED;
			entry.content.decoded.type = &UA_TYPES[UA_TYPES_UPDATEDATADETAILS];
			entry.content.decoded.data = &details;
			UA_HistoryUpdateRequest request; UA_HistoryUpdateRequest_init( &request );
			request.historyUpdateDetailsSize = 1;
			request.historyUpdateDetails = &entry;
			auto response = UA_Client_Service_historyUpdate( client, request );
			let status = response.responseHeader.serviceResult ? response.responseHeader.serviceResult : response.resultsSize==1 ? response.results[0].statusCode : UA_STATUSCODE_BADUNEXPECTEDERROR;
			UA_HistoryUpdateResponse_clear( &response );
			return status;
		}
		//What the endpoint's read and the QL's must agree on:  each value's source time, status and value, in order.
		Ω Agree( const vector<Value>& ua, const vector<Row>& ql )ι->::testing::AssertionResult{
			if( ua.size()!=ql.size() )
				return ::testing::AssertionFailure() << "the endpoint read " << ua.size() << " values, the QL " << ql.size();
			for( uint i=0; i<ua.size(); ++i ){
				let& u = ua[i]; let& q = ql[i];
				let number = q.Value.is_object() && q.Value.as_object().contains("v") ? &q.Value.as_object().at("v") : &q.Value;//a value with a status that isn't Good is {v, sc}.
				let value = number->is_number() ? optional<double>{ number->to_number<double>() } : nullopt;
				if( SourceTime(u)!=q.Source || u.status!=q.Status || Double(u)!=value )
					return ::testing::AssertionFailure() << "value " << i << ":  the endpoint's " << (SourceTime(u) ? ToIsoString(*SourceTime(u)) : "no source") << " " << Double(u).value_or(NAN) << " status " << u.status
						<< ", the QL's " << (q.Source ? ToIsoString(*q.Source) : "no source") << " " << serialize(q.Value) << " status " << q.Status;
			}
			return ::testing::AssertionSuccess();
		}
		//An Argument's dataType, decoded, or as the historian keeps a structure:  an ExtensionObject of its binary encoding.
		Ω ArgumentDataType( const Value& v )ι->optional<NodeId>{
			if( v.hasValue && UA_Variant_hasScalarType(&v.value, &UA_TYPES[UA_TYPES_ARGUMENT]) )
				return NodeId{ v.Get<UA_Argument>(0).dataType };
			if( !v.hasValue || !UA_Variant_hasScalarType(&v.value, &UA_TYPES[UA_TYPES_EXTENSIONOBJECT]) || v.Get<UA_ExtensionObject>(0).encoding!=UA_EXTENSIONOBJECT_ENCODED_BYTESTRING )
				return nullopt;
			UA_Argument a; UA_Argument_init( &a );
			if( UA_decodeBinary(&v.Get<UA_ExtensionObject>(0).content.encoded.body, &a, &UA_TYPES[UA_TYPES_ARGUMENT], nullptr) )
				return nullopt;
			NodeId y{ a.dataType };
			UA_Argument_clear( &a );
			return y;
		}
		//A served variable's StartOfArchive, through the client.
		Ω StartOfArchive( UATestClient& client, const NodeId& variable )ε->optional<UA_DateTime>{
			auto property = client.Translate( variable, {{0, string{Opc::Server::HAConfiguration::BrowseName}}, {0, "StartOfArchive"}} );
			if( !property )
				return nullopt;
			auto v = client.Read( *property );
			return v.hasValue && v.IsScalar() && v.value.type->typeKind==UA_DATATYPEKIND_DATETIME ? optional<UA_DateTime>{ *(UA_DateTime*)v.value.data } : nullopt;//a UtcTime decodes as its DateTime.
		}
	};

	//A raw and a processed HistoryRead on a served node agree with the QL `history` with `group` over the same range (spec *History*, the
	//redesign note's differential), Median among the aggregates, which HistoryServerCapabilities lists from the endpoint's own namespace;
	//and the node's StartOfArchive is written, for a node served while the endpoint runs.
	TEST_F( GatewayHistoryTests, ReadsAgreeWithTheQL ){
		const OneNodeGroup group{ "gatewayHistoryReads", RpmManual };
		WaitPath( group.Host, group.Index );
		let t0 = Since();
		let base = BaseRpm();
		for( uint i=0; i<6; ++i )
			WriteRpm( base+i*100, t0+milliseconds{10*i} );//each past RpmManual's band of 10.
		ASSERT_NO_THROW( WaitStored(group.Host, group.Index, t0, 6) );
		auto client = Client();
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = Served( *client, "pumpManual", "motorRpm" ); return node.has_value(); }) );
		let end = t0+1s;
		let raw = Raw( *client, *node, t0, end );
		ASSERT_TRUE( UA_StatusCode_isGood(raw.Status) ) << UA_StatusCode_name( raw.Status );
		EXPECT_EQ( raw.Values.size(), 6u );
		EXPECT_TRUE( Agree(raw.Values, ReadAll(Group(group.Id), {Ex(RpmManual)}, {.Start=t0, .End=end, .Limit=100})) );
		for( let& v : raw.Values )
			EXPECT_TRUE( v.hasServerTimestamp );
		const NodeId average{ 0, UA_NS0ID_AGGREGATEFUNCTION_AVERAGE };
		let processed = Processed( *client, *node, average, t0, t0+60ms, 20ms );
		ASSERT_TRUE( UA_StatusCode_isGood(processed.Status) ) << UA_StatusCode_name( processed.Status );
		EXPECT_EQ( processed.Values.size(), 3u );
		EXPECT_TRUE( Agree(processed.Values, ReadAll(Group(group.Id), {Ex(RpmManual)}, {.Start=t0, .End=t0+60ms, .Limit=100, .Interval=20, .Aggregate="Average"})) );
		let median = Processed( *client, *node, Opc::Server::UAHistory::Median(), t0, t0+60ms, 20ms );
		ASSERT_TRUE( UA_StatusCode_isGood(median.Status) ) << UA_StatusCode_name( median.Status );
		EXPECT_TRUE( Agree(median.Values, ReadAll(Group(group.Id), {Ex(RpmManual)}, {.Start=t0, .End=t0+60ms, .Limit=100, .Interval=20, .Aggregate="Median"})) );
		let functions = client->Browse( NodeId{0, UA_NS0ID_HISTORYSERVERCAPABILITIES_AGGREGATEFUNCTIONS} );
		EXPECT_TRUE( std::ranges::any_of(functions, [](let& f){ return f.Id==Opc::Server::UAHistory::Median(); }) ) << functions.size() << " aggregate(s), none Median";
		EXPECT_TRUE( ServerCnnctn::SlugError(ToSV(Opc::Server::UAHistory::Median().identifier.string), SRCE_CUR) ) << "no connection folder, ns=1;s=<slug>, can take Median's id";
		EXPECT_TRUE( std::ranges::any_of(functions, [](let& f){ return f.BrowseName=="Average"; }) );
		let start = StartOfArchive( *client, *node );
		ASSERT_TRUE( start );
		EXPECT_GT( *start, 0 ) << "written when the node was served, the endpoint already running";
		EXPECT_LE( *start, UA_DateTime_now() );
	}

	//A structure-typed member and a NodeId-typed one read from history in the gateway's namespaces (spec *Namespaces*, *History*):  a
	//Range, namespace 0's structure, decodes as the structure with the values written;  a NodeId in the pumps' namespace comes in the
	//endpoint's index for it, the page remapped by the table at read time.  The two are added to the embedded OpcServer here, under Objects.
	TEST_F( GatewayHistoryTests, MembersReadFromHistoryInTheGatewaysNamespaces ){
		constexpr UA_UInt32 range{ 9051 }, nodeId{ 9052 };//past GatewayValuesTests' 9041-9045.
		UA_Range r{ 0, 100 };
		UA_NodeId id = Node( RpmManual );
		AddSourceVariable( range, "historyRange", UA_NS0ID_RANGE, &r, UA_TYPES[UA_TYPES_RANGE] );
		AddSourceVariable( nodeId, "historyNodeId", UA_NS0ID_NODEID, &id, UA_TYPES[UA_TYPES_NODEID] );
		let itemsBefore = Items();
		let groupId = CreateGroup( "gatewayHistoryTypes" );
		absl::Cleanup purge = [&]{ Purge( groupId, itemsBefore ); };
		let rangeIndex = AddNode( groupId, range ), nodeIdIndex = AddNode( groupId, nodeId );
		auto host = Host( groupId );
		WaitSubscribed( host, 2 );
		WaitPath( host, rangeIndex );
		WaitPath( host, nodeIdIndex );
		ASSERT_NO_THROW( WaitStored(host, rangeIndex, Joined, 1) );
		ASSERT_NO_THROW( WaitStored(host, nodeIdIndex, Joined, 1) );
		let t0 = Since();
		const array<UA_Range,2> ranges{ UA_Range{0, 1500}, UA_Range{0, 3000} };
		for( uint i=0; i<ranges.size(); ++i )
			Write( range, &ranges[i], UA_TYPES[UA_TYPES_RANGE], t0+milliseconds{10*i} );
		const UA_NodeId written = UA_NODEID_NUMERIC( _ns, 1234 );
		Write( nodeId, &written, UA_TYPES[UA_TYPES_NODEID], t0 );
		ASSERT_NO_THROW( WaitStored(host, rangeIndex, t0, ranges.size()) );
		ASSERT_NO_THROW( WaitStored(host, nodeIdIndex, t0, 1) );
		auto client = Client();
		optional<NodeId> rangeNode, nodeIdNode;
		ASSERT_TRUE( WaitUntil([&]{ rangeNode = ServedRoot( *client, "historyRange" ); nodeIdNode = ServedRoot( *client, "historyNodeId" ); return rangeNode && nodeIdNode; }) );
		let rangeRead = Raw( *client, *rangeNode, t0, t0+1s );
		ASSERT_TRUE( UA_StatusCode_isGood(rangeRead.Status) ) << UA_StatusCode_name( rangeRead.Status );
		ASSERT_EQ( rangeRead.Values.size(), ranges.size() );
		for( uint i=0; i<ranges.size(); ++i ){
			let& v = rangeRead.Values[i];
			ASSERT_TRUE( v.hasValue && UA_Variant_hasScalarType(&v.value, &UA_TYPES[UA_TYPES_RANGE]) ) << "decoded as the structure";
			EXPECT_EQ( static_cast<const UA_Range*>(v.value.data)->high, ranges[i].high );
			EXPECT_EQ( SourceTime(v), t0+milliseconds{10*i} );
		}
		let nodeIdRead = Raw( *client, *nodeIdNode, t0, t0+1s );
		ASSERT_TRUE( UA_StatusCode_isGood(nodeIdRead.Status) ) << UA_StatusCode_name( nodeIdRead.Status );
		ASSERT_EQ( nodeIdRead.Values.size(), 1u );
		let& v = nodeIdRead.Values[0];
		ASSERT_TRUE( v.hasValue && UA_Variant_hasScalarType(&v.value, &UA_TYPES[UA_TYPES_NODEID]) );
		let ns = Mapped( PumpsUri );
		ASSERT_TRUE( ns );
		EXPECT_EQ( NodeId{*static_cast<const UA_NodeId*>(v.value.data)}, NodeId(*ns, 1234) ) << "the pumps' namespace, mapped";
		EXPECT_EQ( Opc::Hist::ToUA(Stored(host, nodeIdIndex, t0).at(0).Value).Get<UA_NodeId>(0).namespaceIndex, _ns ) << "stored in the source's";
	}

	//A HistoryUpdate on a served node under a grant on an enforced resource (spec *Authorization*, #237):  a replace and an insert, by
	//open62541's helpers, then a raw read shows the history as edited, the QL agrees, and a modified read shows what each edit
	//replaced, or inserted, with the session's user.
	TEST_F( GatewayHistoryTests, AHistoryUpdateThenAModifiedRead ){
		const OneNodeGroup group{ "gatewayHistoryEdits", RpmManual };
		WaitPath( group.Host, group.Index );
		let t0 = Since();
		let base = BaseRpm();
		WriteRpm( base, t0 );
		WriteRpm( base+100, t0+20ms );
		ASSERT_NO_THROW( WaitStored(group.Host, group.Index, t0, 2) );
		auto client = Client();
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = Served( *client, "pumpManual", "motorRpm" ); return node.has_value(); }) );
		auto open = EnforcedHistGroups();
		ASSERT_TRUE( WaitUntil([&]{ return UserAccessLevel( *client, *node ) & UA_ACCESSLEVELMASK_HISTORYWRITE; }) ) << "the grant reaches the gateway's authorizer";
		let before = UA_DateTime_now();
		EXPECT_EQ( Replace(*client, *node, base+250, t0+20ms), UA_STATUSCODE_GOODENTRYREPLACED );
		EXPECT_EQ( Insert(*client, *node, base+50, t0+10ms), UA_STATUSCODE_GOODENTRYINSERTED );
		EXPECT_EQ( Insert(*client, *node, base+51, t0+10ms), UA_STATUSCODE_BADENTRYEXISTS );
		let raw = Raw( *client, *node, t0, t0+1s );
		ASSERT_TRUE( UA_StatusCode_isGood(raw.Status) ) << UA_StatusCode_name( raw.Status );
		EXPECT_EQ( Doubles(raw.Values), (vector<optional<double>>{base, base+50, base+250}) );
		EXPECT_TRUE( Agree(raw.Values, ReadAll(Group(group.Id), {Ex(RpmManual)}, {.Start=t0, .End=t0+1s, .Limit=100})) );
		let modified = ReadModified( *client, *node, t0, t0+1s );
		ASSERT_TRUE( UA_StatusCode_isGood(modified.Status) ) << UA_StatusCode_name( modified.Status );
		EXPECT_EQ( Doubles(modified.Values), (vector<optional<double>>{base+50, base+100}) ) << "an INSERT's is the value inserted, a REPLACE's the one replaced";
		ASSERT_EQ( modified.Infos.size(), 2u );
		EXPECT_EQ( modified.Infos[0].Type, UA_HISTORYUPDATETYPE_INSERT );
		EXPECT_EQ( modified.Infos[1].Type, UA_HISTORYUPDATETYPE_REPLACE );
		let user = AppClient()->Acl()->UserName( User() );
		for( let& info : modified.Infos ){
			EXPECT_EQ( info.User, user );
			EXPECT_GE( info.Time, before );
			EXPECT_LE( info.Time, UA_DateTime_now() );
		}
	}

	//A HistoryUpdate's NodeId goes back into the source's namespace (spec *History*):  one in the endpoint's index for the pumps' namespace
	//is stored in the source's and read back in the endpoint's, and one in the endpoint's own namespace, which no source index names,
	//is refused with Bad_NodeIdInvalid and not stored.
	TEST_F( GatewayHistoryTests, AHistoryUpdateGoesBackToTheSourcesNamespaces ){
		constexpr UA_UInt32 nodeId{ 9052 };//MembersReadFromHistoryInTheGatewaysNamespaces's.
		UA_NodeId id = Node( RpmManual );
		AddSourceVariable( nodeId, "historyNodeId", UA_NS0ID_NODEID, &id, UA_TYPES[UA_TYPES_NODEID] );
		let itemsBefore = Items();
		let groupId = CreateGroup( "gatewayHistoryUnmap" );
		absl::Cleanup purge = [&]{ Purge( groupId, itemsBefore ); };
		let index = AddNode( groupId, nodeId );
		auto host = Host( groupId );
		WaitSubscribed( host, 1 );
		WaitPath( host, index );
		ASSERT_NO_THROW( WaitStored(host, index, Joined, 1) );
		auto client = Client();
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = ServedRoot( *client, "historyNodeId" ); return node.has_value(); }) );
		auto open = EnforcedHistGroups();
		ASSERT_TRUE( WaitUntil([&]{ return UserAccessLevel( *client, *node ) & UA_ACCESSLEVELMASK_HISTORYWRITE; }) ) << "the grant reaches the gateway's authorizer";
		let ns = Mapped( PumpsUri );
		ASSERT_TRUE( ns );
		let t0 = Since();
		const UA_NodeId mapped = UA_NODEID_NUMERIC( *ns, 4321 ), own = UA_NODEID_NUMERIC( _own, 4322 );
		EXPECT_EQ( Insert(*client, *node, Sample(&mapped, UA_TYPES[UA_TYPES_NODEID], t0)), UA_STATUSCODE_GOODENTRYINSERTED );
		EXPECT_EQ( Insert(*client, *node, Sample(&own, UA_TYPES[UA_TYPES_NODEID], t0+10ms)), UA_STATUSCODE_BADNODEIDINVALID );
		let stored = Stored( host, index, t0 );
		ASSERT_EQ( stored.size(), 1u ) << "the refused value isn't stored";
		EXPECT_EQ( NodeId{Opc::Hist::ToUA(stored[0].Value).Get<UA_NodeId>(0)}, NodeId(_ns, 4321) ) << "stored in the source's";
		let raw = Raw( *client, *node, t0, t0+1s );
		ASSERT_TRUE( UA_StatusCode_isGood(raw.Status) ) << UA_StatusCode_name( raw.Status );
		ASSERT_EQ( raw.Values.size(), 1u );
		let& v = raw.Values[0];
		ASSERT_TRUE( v.hasValue && UA_Variant_hasScalarType(&v.value, &UA_TYPES[UA_TYPES_NODEID]) );
		EXPECT_EQ( NodeId{*static_cast<const UA_NodeId*>(v.value.data)}, NodeId(*ns, 4321) ) << "read back in the endpoint's";
	}

	//The way back reaches what the way out does (spec *Namespaces*):  a QualifiedName, and a NodeId inside a structure - an Argument's
	//dataType, namespace 0's structure, decoded - each stored in the source's index and read back in the endpoint's;  an Argument whose
	//dataType is in the endpoint's own namespace is refused.
	TEST_F( GatewayHistoryTests, AHistoryUpdateGoesBackInsideAStructure ){
		constexpr UA_UInt32 qualifiedId{ 9053 }, argumentId{ 9054 };
		UA_QualifiedName initial = UA_QUALIFIEDNAME( _ns, (char*)"initial" );
		UA_Argument source; UA_Argument_init( &source );
		source.name = UA_STRING( (char*)"rpm" );
		source.dataType = Node( RpmManual );
		source.valueRank = UA_VALUERANK_SCALAR;
		AddSourceVariable( qualifiedId, "historyQualifiedName", UA_NS0ID_QUALIFIEDNAME, &initial, UA_TYPES[UA_TYPES_QUALIFIEDNAME] );
		AddSourceVariable( argumentId, "historyArgument", UA_NS0ID_ARGUMENT, &source, UA_TYPES[UA_TYPES_ARGUMENT] );
		const OneNodeGroup qualified{ "gatewayHistoryQualified", qualifiedId }, argument{ "gatewayHistoryArgument", argumentId };
		WaitPath( qualified.Host, qualified.Index );
		WaitPath( argument.Host, argument.Index );
		auto client = Client();
		optional<NodeId> qualifiedNode, argumentNode;
		ASSERT_TRUE( WaitUntil([&]{ qualifiedNode = ServedRoot( *client, "historyQualifiedName" ); argumentNode = ServedRoot( *client, "historyArgument" ); return qualifiedNode && argumentNode; }) );
		auto open = EnforcedHistGroups();
		ASSERT_TRUE( WaitUntil([&]{ return (UserAccessLevel(*client, *qualifiedNode) & UserAccessLevel(*client, *argumentNode)) & UA_ACCESSLEVELMASK_HISTORYWRITE; }) ) << "the grant reaches the gateway's authorizer";
		let ns = Mapped( PumpsUri );
		ASSERT_TRUE( ns );
		let t0 = Since();
		const UA_QualifiedName name{ *ns, UA_STRING((char*)"edited") };
		UA_Argument edited = source;//shallow:  its strings are literals.
		edited.dataType = UA_NODEID_NUMERIC( *ns, 77 );
		UA_Argument own = edited;
		own.dataType = UA_NODEID_NUMERIC( _own, 77 );
		EXPECT_EQ( Insert(*client, *qualifiedNode, Sample(&name, UA_TYPES[UA_TYPES_QUALIFIEDNAME], t0)), UA_STATUSCODE_GOODENTRYINSERTED );
		EXPECT_EQ( Insert(*client, *argumentNode, Sample(&edited, UA_TYPES[UA_TYPES_ARGUMENT], t0)), UA_STATUSCODE_GOODENTRYINSERTED );
		EXPECT_EQ( Insert(*client, *argumentNode, Sample(&own, UA_TYPES[UA_TYPES_ARGUMENT], t0+10ms)), UA_STATUSCODE_BADNODEIDINVALID );
		let storedName = Stored( qualified.Host, qualified.Index, t0 );
		ASSERT_EQ( storedName.size(), 1u );
		let nameValue = Opc::Hist::ToUA( storedName[0].Value );
		ASSERT_TRUE( UA_Variant_hasScalarType(&nameValue.value, &UA_TYPES[UA_TYPES_QUALIFIEDNAME]) );
		EXPECT_EQ( nameValue.Get<UA_QualifiedName>(0).namespaceIndex, _ns ) << "stored in the source's";
		let storedArgument = Stored( argument.Host, argument.Index, t0 );
		ASSERT_EQ( storedArgument.size(), 1u ) << "the refused one isn't stored";
		EXPECT_EQ( ArgumentDataType(Opc::Hist::ToUA(storedArgument[0].Value)), NodeId(_ns, 77) ) << "stored in the source's, " << _ns << ", the endpoint's " << *ns;
		let nameRead = Raw( *client, *qualifiedNode, t0, t0+1s );
		ASSERT_TRUE( UA_StatusCode_isGood(nameRead.Status) ) << UA_StatusCode_name( nameRead.Status );
		ASSERT_EQ( nameRead.Values.size(), 1u );
		ASSERT_TRUE( nameRead.Values[0].hasValue && UA_Variant_hasScalarType(&nameRead.Values[0].value, &UA_TYPES[UA_TYPES_QUALIFIEDNAME]) );
		EXPECT_EQ( nameRead.Values[0].Get<UA_QualifiedName>(0).namespaceIndex, *ns ) << "read back in the endpoint's";
		let argumentRead = Raw( *client, *argumentNode, t0, t0+1s );
		ASSERT_TRUE( UA_StatusCode_isGood(argumentRead.Status) ) << UA_StatusCode_name( argumentRead.Status );
		ASSERT_EQ( argumentRead.Values.size(), 1u );
		EXPECT_EQ( ArgumentDataType(argumentRead.Values[0]), NodeId(*ns, 77) ) << "read back in the endpoint's";
	}

	//A DeleteRawModified through the endpoint (spec *History*):  its range, start held and end not, is gone from the raw read, the QL
	//agrees, and the same range again finds nothing to delete.
	TEST_F( GatewayHistoryTests, ADeleteRawThroughTheEndpoint ){
		const OneNodeGroup group{ "gatewayHistoryDelete", RpmManual };
		WaitPath( group.Host, group.Index );
		let t0 = Since();
		let base = BaseRpm();
		for( uint i=0; i<3; ++i )
			WriteRpm( base+i*100, t0+milliseconds{10*i} );
		ASSERT_NO_THROW( WaitStored(group.Host, group.Index, t0, 3) );
		auto client = Client();
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = Served( *client, "pumpManual", "motorRpm" ); return node.has_value(); }) );
		auto open = EnforcedHistGroups();
		ASSERT_TRUE( WaitUntil([&]{ return UserAccessLevel( *client, *node ) & UA_ACCESSLEVELMASK_HISTORYWRITE; }) ) << "the grant reaches the gateway's authorizer";
		EXPECT_EQ( UA_Client_HistoryUpdate_deleteRaw(*client, &*node, Ticks(t0+10ms), Ticks(t0+20ms)), UA_STATUSCODE_GOOD );
		let raw = Raw( *client, *node, t0, t0+1s );
		ASSERT_TRUE( UA_StatusCode_isGood(raw.Status) ) << UA_StatusCode_name( raw.Status );
		EXPECT_EQ( Doubles(raw.Values), (vector<optional<double>>{base, base+200}) );
		EXPECT_TRUE( Agree(raw.Values, ReadAll(Group(group.Id), {Ex(RpmManual)}, {.Start=t0, .End=t0+1s, .Limit=100})) );
		EXPECT_EQ( UA_Client_HistoryUpdate_deleteRaw(*client, &*node, Ticks(t0+10ms), Ticks(t0+20ms)), UA_STATUSCODE_BADNODATA );
	}

	//An edit needs Update granted on a resource that is enforced:  refused on the unenforced group, as it ships, whatever the session may
	//do elsewhere, and on an enforced one whose own row grants Read alone, where the reads still answer.  UserAccessLevel leaves
	//HistoryWrite out both times, so a client sees the history as read-only before it tries.
	TEST_F( GatewayHistoryTests, AUserWithoutUpdateIsRefused ){
		const OneNodeGroup group{ "gatewayHistoryRefused", RpmManual };
		WaitPath( group.Host, group.Index );
		let t0 = Since();
		let base = BaseRpm();
		WriteRpm( base, t0 );
		ASSERT_NO_THROW( WaitStored(group.Host, group.Index, t0, 1) );
		auto client = Client();
		optional<NodeId> node;
		ASSERT_TRUE( WaitUntil([&]{ node = Served( *client, "pumpManual", "motorRpm" ); return node.has_value(); }) );
		let refused = [&]( sv when ){
			let user = UserAccessLevel( *client, *node );
			EXPECT_TRUE( user & UA_ACCESSLEVELMASK_HISTORYREAD ) << when;
			EXPECT_FALSE( user & UA_ACCESSLEVELMASK_HISTORYWRITE ) << when;
			EXPECT_EQ( ReplaceEntry(*client, *node, base+1, t0), UA_STATUSCODE_BADUSERACCESSDENIED ) << when;
			let raw = Raw( *client, *node, t0, t0+1s );
			EXPECT_TRUE( UA_StatusCode_isGood(raw.Status) ) << when << ":  " << UA_StatusCode_name( raw.Status );
			EXPECT_EQ( Doubles(raw.Values), (vector<optional<double>>{base}) ) << when;
		};
		refused( "unenforced" );
		auto open = EnforcedHistGroups();
		ScopeHistGroup( group.Id, Access::ERights::Read );
		absl::Cleanup unscope = [&]{ try{ UnscopeHistGroup( group.Id ); }catch( const std::exception& e ){ ADD_FAILURE() << "the group's resource wasn't deleted:  " << e.what(); } };
		ASSERT_TRUE( WaitUntil([&]{ let user = UserAccessLevel( *client, *node ); return (user & UA_ACCESSLEVELMASK_HISTORYREAD) && !(user & UA_ACCESSLEVELMASK_HISTORYWRITE); }) ) << "the group's row reaches the gateway's authorizer";
		refused( "enforced, Read alone" );
	}
}