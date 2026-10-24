//Historian 5c (#224):  a gateway group collecting, read and edited (spec *Collection*, *Reads*, *Edits*) - the collector's items
//feeding the library, history with `group` in its three modes, and the edits with `group` under a grant on an enforced
//resource.  And the differential test the plan sets:  a group over the embedded OpcServer's historizing pump nodes, with the
//thresholds their HA Configurations give, read beside a pass-through read of the server's own history of the same range,
//through a connection drop that the group marks as a gap and the server, which never lost a value, doesn't.
#include <absl/cleanup/cleanup.h>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/fwk/log/MemoryLog.h>
#include "utils/HistGroupFixture.h"
#include "../src/ql/HistGroupInfoQLAwait.h"

namespace Jde::Opc::Gateway::Tests{
	struct HistCollectorTests : HistGroupFixture{};

	//The differential test (plan *Differential test*):  a group over RpmManual and Status1 with the thresholds their HA
	//Configurations give stores what OpcServer stores itself, so a group read and a pass-through read of the same range
	//agree:  raw, at times and aggregated.  Then the collector's connection drops while a value is written:  the group marks
	//the gap with Bad_DataLost before the value that ends it, the server holds the value alone, and Status1, which didn't
	//change, comes back with nothing new.
	TEST_F( HistCollectorTests, AgreesWithThePassThrough ){
		let itemsBefore = Items();
		let group = CreateGroup( "differential" );
		absl::Cleanup purge = [=]{ Purge( group, itemsBefore ); };
		let rpm = AddNode( group, RpmManual, {{"exceptionDeviation", 10}, {"exceptionDeviationFormat", 0}} );
		let status = AddNode( group, Status1 );
		auto host = Host( group );
		ASSERT_NO_THROW( WaitSubscribed(host, 2) );
		ASSERT_NO_THROW( WaitDescribed(host, rpm) );//its band
		ASSERT_NO_THROW( WaitStored(host, rpm, Joined, 1) );
		ASSERT_NO_THROW( WaitStored(host, status, Joined, 1) );

		//A jump off the value the node holds now, so both hosts' bands start from the same stored value, then changes inside
		//the 10 rpm band and past it.  Status1 flips from its value now at every write:  an item reports a change of value or
		//status, so a write that repeats the value reaches OpcServer's history and not the group's, which is sampling.
		let base = BaseRpm();
		const vector<double> rpms{ base, base+4, base+11, base+15, base+30, base+35, base-100 };
		let on = !*(const UA_Boolean*)Current( Status1 ).value.data;
		let t0 = Since();
		for( uint i=0; i<rpms.size(); ++i ){
			WriteRpm( rpms[i], t0+milliseconds{10*i} );
			WriteStatus( i%2==0 ? on : !on, t0+milliseconds{10*i+5} );
		}
		let rpmStored = WaitStored( host, rpm, t0, 4 );
		let statusStored = WaitStored( host, status, t0, rpms.size() );
		const Request range{ .Start=t0, .End=t0+1s, .Limit=100 };
		let groupRpm = ReadAll( Group(group), {Ex(RpmManual)}, range );
		let serverRpm = ReadAll( Opc(), {Node(RpmManual).ToJson()}, range );
		ASSERT_EQ( groupRpm.size(), 4u ) << "the band dropped the three inside it";
		EXPECT_TRUE( Agree(groupRpm, serverRpm) );
		EXPECT_EQ( groupRpm[1].Value, jvalue{base+11} );
		let groupStatus = ReadAll( Group(group), {Ex(Status1)}, range );
		EXPECT_EQ( groupStatus.size(), rpms.size() ) << "a Boolean stores every change";
		EXPECT_TRUE( Agree(groupStatus, ReadAll(Opc(), {Node(Status1).ToJson()}, range)) );
		for( let& row : groupRpm ){
			EXPECT_EQ( row.Node, Ex(RpmManual) ) << "a node comes back as the read named it";
			EXPECT_FALSE( row.Heartbeat );
		}
		//A node named by the server's index reads the same, spelled the same way back.
		let byIndex = ReadAll( Group(group), {Node(RpmManual).ToJson()}, range );
		EXPECT_TRUE( Agree(byIndex, groupRpm) );
		ASSERT_FALSE( byIndex.empty() );
		EXPECT_EQ( byIndex[0].Node, jvalue{Node(RpmManual).ToJson()} );

		//At times, stepped from the records either side, and aggregated over 20 ms intervals.
		const Request atTimes{ .Limit=100, .Times={t0+15ms, t0+25ms, t0+41ms, t0+60ms, t0+500ms} };
		let groupAt = ReadAll( Group(group), {Ex(RpmManual)}, atTimes );
		EXPECT_EQ( groupAt.size(), atTimes.Times.size() );
		EXPECT_TRUE( Agree(groupAt, ReadAll(Opc(), {Node(RpmManual).ToJson()}, atTimes)) );
		for( let aggregate : {"Count", "Maximum", "Average"} ){
			SCOPED_TRACE( aggregate );
			const Request processed{ .Start=t0, .End=t0+80ms, .Limit=100, .Interval=20, .Aggregate=aggregate };
			let groupProcessed = ReadAll( Group(group), {Ex(RpmManual)}, processed );
			EXPECT_EQ( groupProcessed.size(), 4u );
			EXPECT_TRUE( Agree(groupProcessed, ReadAll(Opc(), {Node(RpmManual).ToJson()}, processed)) );
		}

		//The collector's connection drops, and a value is written while it is down:  the rebuild's first value is later than
		//the last the group had, so the group stores Bad_DataLost at the break, then the value.  Status1 comes back unchanged.
		auto collector = Collector();
		ASSERT_TRUE( collector );
		let broke = Clock::now();
		UAClient::ConnectionLost( sp<UAClient>{collector} );
		let missed = Ms( Clock::now() );
		WriteRpm( base+500, missed );
		ASSERT_NO_THROW( WaitStored(host, rpm, t0, 6) ) << "no gap marker and value after the reconnect";
		const Request after{ .Start=t0, .End=missed+1s, .Limit=100 };
		let groupAfter = ReadAll( Group(group), {Ex(RpmManual)}, after );
		let serverAfter = ReadAll( Opc(), {Node(RpmManual).ToJson()}, after );
		ASSERT_EQ( groupAfter.size(), 6u );
		let& marker = groupAfter[4];
		EXPECT_EQ( marker.Status, UA_STATUSCODE_BADDATALOST );
		EXPECT_FALSE( marker.Value.is_number() ) << "a marker holds no value:  " << serialize( marker.Value );
		ASSERT_TRUE( marker.Source );
		EXPECT_GE( *marker.Source, Ms(broke) );
		EXPECT_LE( *marker.Source, missed );
		EXPECT_EQ( groupAfter[5].Value, jvalue{base+500} );
		EXPECT_EQ( groupAfter[5].Source, missed );
		vector<Row> unmarked;
		for( let& row : groupAfter ){
			if( row.Status!=UA_STATUSCODE_BADDATALOST )
				unmarked.push_back( row );
		}
		EXPECT_TRUE( Agree(unmarked, serverAfter) ) << "apart from the gap marker";
		std::this_thread::sleep_for( 1s );//a publishing interval or two for anything else the rebuild delivers.
		EXPECT_EQ( Stored(host, status, t0).size(), statusStored.size() ) << "Status1's value after the break repeats the last it delivered:  nothing was lost";
		EXPECT_EQ( rpmStored.size(), 4u );
	}

	//history with `group`:  pages by the library's continuation either way, answers Bad_NodeIdUnknown for a node that isn't a
	//member, and refuses a member named twice, a group the gateway doesn't host, an aggregate it doesn't serve, and a call that
	//names both a group and a server, or neither.
	TEST_F( HistCollectorTests, ReadsAGroupsHistory ){
		let [group, rpm, host, _] = OneNodeGroup{ "reads", RpmManual };
		let t0 = Since();
		let base = BaseRpm();
		for( uint i=0; i<5; ++i )
			WriteRpm( base+i*100, t0+milliseconds{10*i} );
		ASSERT_NO_THROW( WaitStored(host, rpm, t0, 5) );

		const Request forward{ .Start=t0, .End=t0+1s, .Limit=2 };
		let first = Read( Group(group), {Ex(RpmManual)}, forward );
		ASSERT_EQ( first.Values.size(), 2u );
		ASSERT_FALSE( first.Continuation.empty() );
		EXPECT_EQ( first.Values[0].Source, t0 );
		let all = ReadAll( Group(group), {Ex(RpmManual)}, forward );
		ASSERT_EQ( all.size(), 5u );
		let reverse = ReadAll( Group(group), {Ex(RpmManual)}, {.Start=t0+1s, .End=t0, .Limit=2} );
		ASSERT_EQ( reverse.size(), 5u );
		EXPECT_EQ( reverse.front().Value, all.back().Value );
		let bounded = ReadAll( Group(group), {Ex(RpmManual)}, {.Start=t0+5ms, .End=t0+25ms, .Limit=100, .Bounds=true} );
		ASSERT_EQ( bounded.size(), 4u );
		EXPECT_TRUE( bounded.front().Bound && bounded.back().Bound );
		EXPECT_EQ( bounded.front().Source, t0 );
		EXPECT_EQ( bounded.back().Source, t0+30ms );
		EXPECT_THROW( Read(Group(group), {Ex(RpmManual)}, {.Start=t0, .End=t0+2s, .Limit=2}, first.Continuation), GatewayErrorResponse ) << "another read's continuation";

		let mixed = Read( Group(group), {Ex(Rpm2), Ex(RpmManual)}, {.Start=t0, .End=t0+1s, .Limit=100} );
		EXPECT_EQ( mixed.Values.size(), 5u );
		ASSERT_EQ( mixed.Statuses.size(), 2u );
		EXPECT_EQ( mixed.Statuses[0].second, UA_STATUSCODE_BADNODEIDUNKNOWN );
		EXPECT_EQ( mixed.Statuses[1].second, UA_STATUSCODE_GOOD );
		let empty = Read( Group(group), {Ex(RpmManual)}, {.Start=t0-24h*400, .End=t0-24h*399, .Limit=100} );//before the group was made.
		EXPECT_TRUE( empty.Values.empty() );
		ASSERT_EQ( empty.Statuses.size(), 1u );
		EXPECT_EQ( empty.Statuses[0].second, UA_STATUSCODE_GOODNODATA );

		EXPECT_THROW( Read(Group(group), {Ex(RpmManual), Node(RpmManual).ToJson()}, forward), GatewayErrorResponse ) << "one member by two spellings";
		EXPECT_THROW( Read(Group(group), {Ex(RpmManual), Ex(RpmManual)}, forward), GatewayErrorResponse ) << "one node twice";
		EXPECT_THROW( Read(Group(999'999), {Ex(RpmManual)}, forward), GatewayErrorResponse ) << "no such group";
		EXPECT_THROW( Read(Group(group), {Ex(RpmManual)}, {.Start=t0, .End=t0+1s, .Limit=100, .Interval=10, .Aggregate="Range"}), GatewayErrorResponse ) << "an aggregate the library doesn't serve";
		auto both = Group( group );
		both["opc"] = OpcServerSlug;
		EXPECT_THROW( Read(both, {Ex(RpmManual)}, forward), GatewayErrorResponse );
		EXPECT_THROW( Read({}, {Ex(RpmManual)}, forward), GatewayErrorResponse );
	}

	//The edits with `group`:  refused while nothing enforces hist_groups, whatever the caller may do elsewhere, and with the
	//resource enforced and granted, each answers Part 11's results, a value typed as the group stored its node, and a node
	//that isn't a member Bad_NodeIdUnknown.  The modified read returns what each edit replaced, by the caller.
	TEST_F( HistCollectorTests, EditsNeedAnEnforcedGrant ){
		let [group, rpm, host, _] = OneNodeGroup{ "edits", RpmManual };
		let t0 = Since();
		let base = BaseRpm();
		WriteRpm( base, t0 );
		WriteRpm( base+100, t0+20ms );
		ASSERT_NO_THROW( WaitStored(host, rpm, t0, 2) );

		EnforceHistGroups( false );
		EXPECT_THROW( Update("createHistory", group, {{RpmManual, t0+10ms, 1}}), GatewayErrorResponse ) << "an edit nothing enforces";
		auto open = EnforcedHistGroups();

		let inserted = Update( "createHistory", group, {{RpmManual, t0+10ms, 1}, {RpmManual, t0, 2}, {Rpm2, t0+10ms, 3}} );
		EXPECT_EQ( inserted.Values, (vector<StatusCode>{UA_STATUSCODE_GOODENTRYINSERTED, UA_STATUSCODE_BADENTRYEXISTS, UA_STATUSCODE_BADNODEIDUNKNOWN}) );
		EXPECT_EQ( Codes(inserted.Statuses), (vector<StatusCode>{UA_STATUSCODE_GOOD, UA_STATUSCODE_BADNODEIDUNKNOWN}) );
		EXPECT_EQ( Update("updateHistory", group, {{RpmManual, t0+20ms, 250}, {RpmManual, t0+30ms, 9}}).Values, (vector<StatusCode>{UA_STATUSCODE_GOODENTRYREPLACED, UA_STATUSCODE_BADNOENTRYEXISTS}) );
		EXPECT_EQ( Update("upsertHistory", group, {{RpmManual, t0, 7}, {RpmManual, t0+40ms, 8}}).Values, (vector<StatusCode>{UA_STATUSCODE_GOODENTRYREPLACED, UA_STATUSCODE_GOODENTRYINSERTED}) );
		const Request range{ .Start=t0, .End=t0+50ms, .Limit=100 };
		vector<jvalue> values;
		for( let& row : ReadAll(Group(group), {Ex(RpmManual)}, range) )
			values.push_back( row.Value );
		EXPECT_EQ( values, (vector<jvalue>{7.0, 1.0, 250.0, 8.0}) ) << "a json integer takes the Double the group stored";

		EXPECT_EQ( PurgeAt(group, RpmManual, {t0+10ms, t0+11ms}).Values, (vector<StatusCode>{UA_STATUSCODE_GOOD, UA_STATUSCODE_BADNOENTRYEXISTS}) );
		EXPECT_EQ( Codes(PurgeRange(group, RpmManual, t0+20ms, t0+40ms).Statuses), (vector<StatusCode>{UA_STATUSCODE_GOOD}) ) << "a group's range holds both ends";
		EXPECT_EQ( Codes(PurgeRange(group, RpmManual, t0+20ms, t0+40ms).Statuses), (vector<StatusCode>{UA_STATUSCODE_BADNODATA}) );
		values.clear();
		for( let& row : ReadAll(Group(group), {Ex(RpmManual)}, range) )
			values.push_back( row.Value );
		EXPECT_EQ( values, (vector<jvalue>{7.0}) );

		let modified = ReadAll( Group(group), {Ex(RpmManual)}, {.Start=t0, .End=t0+50ms, .Limit=100, .Modified=true} );
		EXPECT_EQ( modified.size(), 7u ) << "an insert, a replace, an upsert's two, and three deletes";
		try{//a value that can't take the node's type refuses the call, naming the node and the value.
			Update( "createHistory", group, {{RpmManual, t0+45ms, "not a number"}} );
			ADD_FAILURE() << "a value that can't be a Double was taken";
		}
		catch( const GatewayErrorResponse& e ){
			EXPECT_NE( string{e.what()}.find("\"not a number\" isn't a Double"), string::npos ) << e.what();
		}
	}
	//A node that leaves and joins again within the second its item outlives it (DeleteMonitoring) joins that item, which sends
	//no initial value:  the collector reads the node's value instead, and the new member's history goes on with it, once,
	//after the Bad_DataLost a rejoin's break writes (spec *Gaps*), the first membership's value before them, since a read
	//of the node joins its indexes (#326, spec *Reads*).
	TEST_F( HistCollectorTests, ARejoinReadsTheItemsValue ){
		let [group, first, host, itemsBefore] = OneNodeGroup{ "rejoin", RpmManual };
		Mutate( "purgeHistGroupNode( id:$id )", {{"id", first}} );
		let again = AddNode( group, RpmManual );
		ASSERT_NE( again, first ) << "a rejoin is a new row, and a new index";
		ASSERT_NO_THROW( WaitSubscribed(host, 1) );
		let stored = WaitStored( host, again, Joined, 3 );
		ASSERT_EQ( stored.size(), 3u );
		EXPECT_TRUE( stored[0].Value.has_value() ) << "the first membership's value, under the current index";
		EXPECT_EQ( stored[0].Value.node_index(), again );
		EXPECT_EQ( stored[1].Value.status(), UA_STATUSCODE_BADDATALOST );
		EXPECT_FALSE( stored[1].Value.has_value() );
		EXPECT_TRUE( stored[2].Value.has_value() );
		std::this_thread::sleep_for( 1s );//a publishing interval or two:  the item's next value is a change, not its value now again.
		EXPECT_EQ( Stored(host, again, Joined).size(), 3u );
		EXPECT_EQ( Items(), itemsBefore+1 ) << "the one item, joined, not a second";
	}
	//A namespace the server registers at the end moves no member's index:  neither the pass that sees it, here a member's join,
	//nor a rebuild's check makes the group's items again (spec *Collector contract*).
	TEST_F( HistCollectorTests, AnAppendedNamespaceMovesNothing ){
		if( !Logging::FindLogger<Logging::MemoryLog>() )
			Logging::AddLogger( mu<Logging::MemoryLog>() );
		let itemsBefore = Items();
		let group = CreateGroup( "appended" );
		absl::Cleanup purge = [=]{ Purge( group, itemsBefore ); };
		let status = AddNode( group, Status1 );
		auto host = Host( group );
		ASSERT_NO_THROW( WaitSubscribed(host, 1) );
		let logged = [&]( sv text ){
			return Logging::Find( [&]( const Logging::Entry& e ){ let m = e.Message(); return m.contains( host->Guid() ) && m.contains( text ); } );
		};
		let passes = [&]( uint count ){//each create's INFO
			Stopwatch sw;
			while( logged("items created on").size()<count )
				sw.CheckTimeout( 15s, 10ms );
			return logged( "items created on" );
		};
		ASSERT_NO_THROW( passes(1) );
		auto ua = Server::FindUAServer();
		UA_Server_addNamespace( ua->Ptr(), "urn:jde:tests:appended:join" );
		AddNode( group, RpmManual );
		ASSERT_NO_THROW( WaitSubscribed(host, 2) );
		vector<Logging::Entry> created;
		ASSERT_NO_THROW( created = passes(2) );
		ASSERT_EQ( created.size(), 2u );
		EXPECT_TRUE( created[1].Message().contains("1 of 1 items") ) << created[1].Message();

		//The rebuild's values wait for the check, so once the value written while down is stored, the check has decided.
		UA_Server_addNamespace( ua->Ptr(), "urn:jde:tests:appended:rebuild" );
		auto collector = Collector();
		ASSERT_TRUE( collector );
		let on = !*(const UA_Boolean*)Current( Status1 ).value.data;
		UAClient::ConnectionLost( sp<UAClient>{collector} );
		let missed = Ms( Clock::now() );
		WriteStatus( on, missed );
		ASSERT_NO_THROW( WaitStored(host, status, missed, 1) );
		EXPECT_TRUE( logged("namespaces moved").empty() );
		EXPECT_EQ( logged("items created on").size(), 2u ) << "the rebuild's items kept";
	}
	//Each member's DataType, read once its item is made, is the type the group's edits take, and goes into its join's NodeAdded
	//while no flush has taken that (spec *Edits*).
	TEST_F( HistCollectorTests, MembersTakeTheirDataType ){
		let itemsBefore = Items();
		let group = CreateGroup( "types" );
		absl::Cleanup purge = [=]{ Purge( group, itemsBefore ); };
		let rpm = AddNode( group, RpmManual );
		let status = AddNode( group, Status1 );
		auto host = Host( group );
		ASSERT_NO_THROW( WaitSubscribed(host, 2) );
		let declared = [&]( uint32 index )->const UA_DataType* {
			for( let& r : host->Group()->Buffer() ){
				if( let added = std::get_if<Opc::Hist::NodeAdded>(&r); added && added->Index==index )
					return added->DataType;
			}
			return nullptr;
		};
		Stopwatch sw;
		while( !declared(rpm) || !declared(status) )
			ASSERT_NO_THROW( sw.CheckTimeout(15s, 10ms) );
		EXPECT_EQ( declared(rpm), &UA_TYPES[UA_TYPES_DOUBLE] );
		EXPECT_EQ( declared(status), &UA_TYPES[UA_TYPES_BOOLEAN] );
		EXPECT_EQ( host->Group()->DataType(status), &UA_TYPES[UA_TYPES_BOOLEAN] );
	}
	//An abstract DataType of namespace 0, as Number, declares none, and so does BaseDataType, whose built-in type, Variant,
	//says nothing of a value's:  the group's edits take the type of the values it stored, which either's may change (spec
	//*Record format*).
	TEST_F( HistCollectorTests, AnAbstractDataTypeDeclaresNone ){
		constexpr UA_UInt32 Any{ 9024 }, Count{ 9026 };//added here, of BaseDataType and Number, each holding an Int32.
		for( let& [id, dataType, name] : {std::tuple{Any, UA_NS0ID_BASEDATATYPE, "Any"}, std::tuple{Count, UA_NS0ID_NUMBER, "Count"}} ){
			auto attributes = UA_VariableAttributes_default;
			UA_Int32 zero{};
			UA_Variant_setScalar( &attributes.value, &zero, &UA_TYPES[UA_TYPES_INT32] );
			attributes.dataType = UA_NODEID_NUMERIC( 0, dataType );
			attributes.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
			ASSERT_EQ( UA_Server_addVariableNode(Server::GetUAServer().Ptr(), Node(id), UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
				UA_QUALIFIEDNAME(_ns, (char*)name), UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attributes, nullptr, nullptr), UA_STATUSCODE_GOOD ) << name;
		}
		absl::Cleanup remove = [=]{
			for( let id : {Any, Count} )
				UA_Server_deleteNode( Server::GetUAServer().Ptr(), Node(id), true );
		};
		let itemsBefore = Items();
		let group = CreateGroup( "abstract" );
		absl::Cleanup purge = [=]{ Purge( group, itemsBefore ); };
		let any = AddNode( group, Any );
		let count = AddNode( group, Count );
		auto host = Host( group );
		ASSERT_NO_THROW( WaitSubscribed(host, 2) );
		for( let [id, index] : {std::pair{Any, any}, std::pair{Count, count}} ){
			ASSERT_NO_THROW( WaitDescribed(host, index) );
			ASSERT_NO_THROW( WaitStored(host, index, Joined, 1) );
			let buffer = host->Group()->Buffer();
			let join = std::ranges::find_if( buffer, [=]( let& r ){ let added = std::get_if<Opc::Hist::NodeAdded>( &r ); return added && added->Index==index; } );
			ASSERT_NE( join, buffer.end() );
			let declared = std::get<Opc::Hist::NodeAdded>( *join ).DataType;
			EXPECT_EQ( declared, nullptr ) << "member " << index << " " << declared->typeName;
			EXPECT_EQ( host->Group()->DataType(index), &UA_TYPES[UA_TYPES_INT32] ) << "member " << index << ":  the type of the value it stored";
			const UA_Double half{ 1.5 };
			Write( id, &half, UA_TYPES[UA_TYPES_DOUBLE], Since() );
			ASSERT_NO_THROW( WaitStored(host, index, Joined, 2) );
			EXPECT_EQ( host->Group()->DataType(index), &UA_TYPES[UA_TYPES_DOUBLE] ) << "member " << index;
		}
	}
	//A percent-of-range band takes the InstrumentRange or EURange the server gives the node, read once its item is made, and
	//a format edit takes the other;  a node without the range its format needs stores every change (spec *Compression*).
	TEST_F( HistCollectorTests, RangesFromTheServer ){
		let itemsBefore = Items();
		let group = CreateGroup( "ranges" );
		absl::Cleanup purge = [=]{ Purge( group, itemsBefore ); };
		let pump3 = AddNode( group, Rpm3, {{"exceptionDeviation", 1}, {"exceptionDeviationFormat", 2}} );
		let pump4 = AddNode( group, Rpm4, {{"exceptionDeviation", 1}, {"exceptionDeviationFormat", 2}} );
		auto host = Host( group );
		ASSERT_NO_THROW( WaitSubscribed(host, 2) );
		let thresholds = [&]( uint32 index ){ return host->Group()->FindThresholds( index ).value(); };
		Stopwatch sw;
		while( !thresholds(pump3).Range )
			ASSERT_NO_THROW( sw.CheckTimeout(15s, 10ms) );
		EXPECT_EQ( thresholds(pump3).ExceptionDeviation, 1 );
		EXPECT_EQ( thresholds(pump3).Range->High, 1500 ) << "its InstrumentRange";
		EXPECT_FALSE( thresholds(pump4).ExceptionDeviation ) << "no InstrumentRange:  every change is stored";
		Mutate( "updateHistGroupNode( id:$id, exceptionDeviationFormat:3 )", {{"id", pump3}} );
		EXPECT_EQ( thresholds(pump3).Range->High, 3000 ) << "its EURange";
		Mutate( "updateHistGroupNode( id:$id, exceptionDeviationFormat:3 )", {{"id", pump4}} );
		EXPECT_EQ( thresholds(pump4).ExceptionDeviation, 1 );
		ASSERT_TRUE( thresholds(pump4).Range );
		EXPECT_EQ( thresholds(pump4).Range->High, 3000 );
		EXPECT_FALSE( host->FindThresholds(pump4)->Range ) << "the rows', which keep no range";
	}
	//A description Describe lost, rather than was refused, the watch asks for again (Redescribe):  here a percent-of-range band
	//waits while the server refuses the translate of a node's two range properties, and takes the InstrumentRange once asked again.
	TEST_F( HistCollectorTests, ALostDescriptionIsAskedAgain ){
		auto& config = *UA_Server_getConfig( Server::GetUAServer().Ptr() );
		let limit = config.maxNodesPerTranslateBrowsePathsToNodeIds;
		config.maxNodesPerTranslateBrowsePathsToNodeIds = 1;
		absl::Cleanup restore = [&]{ config.maxNodesPerTranslateBrowsePathsToNodeIds = limit; };
		let itemsBefore = Items();
		let group = CreateGroup( "redescribe" );
		absl::Cleanup purge = [=]{ Purge( group, itemsBefore ); };
		let pump3 = AddNode( group, Rpm3, {{"exceptionDeviation", 1}, {"exceptionDeviationFormat", 2}} );
		auto host = Host( group );
		ASSERT_NO_THROW( WaitSubscribed(host, 1) );
		Stopwatch sw;
		optional<HistDescription> described;
		while( !(described = host->Description(pump3)) || !described->Enumeration )//its DataType, read in the same Describe.
			ASSERT_NO_THROW( sw.CheckTimeout(15s, 10ms) );
		EXPECT_FALSE( described->Ranged ) << "the translate was refused";
		let thresholds = [&]{ return host->Group()->FindThresholds( pump3 ).value(); };
		EXPECT_FALSE( thresholds().ExceptionDeviation ) << "its band waits";
		config.maxNodesPerTranslateBrowsePathsToNodeIds = limit;
		host->Redescribe();
		ASSERT_NO_THROW( WaitDescribed(host, pump3) );
		EXPECT_EQ( thresholds().ExceptionDeviation, 1 );
		ASSERT_TRUE( thresholds().Range );
		EXPECT_EQ( thresholds().Range->High, 1500 ) << "its InstrumentRange";
	}
	//An enumeration's values arrive as Int32, which the library can't tell from a quantity, so its node takes no band
	//(spec *Compression*):  ServerStatus.State, of namespace 0's ServerState, a variable of Enumeration itself, which
	//open62541's kind for it says is one as well, and the IA examples' DeviceHealth, whose DeviceHealthEnumeration only the
	//server's definition of it says is one.
	TEST_F( HistCollectorTests, EnumerationsTakeNoBand ){
		constexpr UA_UInt32 Mode{ 9029 };//added here:  of Enumeration itself, i=29, holding an Int32.
		auto attributes = UA_VariableAttributes_default;
		UA_Int32 one{ 1 };
		UA_Variant_setScalar( &attributes.value, &one, &UA_TYPES[UA_TYPES_INT32] );
		attributes.dataType = UA_NODEID_NUMERIC( 0, UA_NS0ID_ENUMERATION );
		attributes.accessLevel = UA_ACCESSLEVELMASK_READ;
		ASSERT_EQ( UA_Server_addVariableNode(Server::GetUAServer().Ptr(), Node(Mode), UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
			UA_QUALIFIEDNAME(_ns, (char*)"Mode"), UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attributes, nullptr, nullptr), UA_STATUSCODE_GOOD );
		absl::Cleanup remove = [=]{ UA_Server_deleteNode( Server::GetUAServer().Ptr(), Node(Mode), true ); };
		let itemsBefore = Items();
		let group = CreateGroup( "enumerations" );
		absl::Cleanup purge = [=]{ Purge( group, itemsBefore ); };
		const jobject band{ {"exceptionDeviation", 1}, {"exceptionDeviationFormat", 0} };
		let state = AddNode( group, jobject{{"nsu", "http://opcfoundation.org/UA/"}, {"i", UA_NS0ID_SERVER_SERVERSTATUS_STATE}}, band );
		let health = AddNode( group, jobject{{"nsu", "http://opcfoundation.org/UA/IA/Examples/"}, {"i", 6002}}, band );
		let mode = AddNode( group, Mode, band );
		let rpm = AddNode( group, RpmManual, band );
		auto host = Host( group );
		ASSERT_NO_THROW( WaitSubscribed(host, 4) );
		let thresholds = [&]( uint32 index ){ return host->Group()->FindThresholds( index ).value(); };
		for( let [index, enumeration] : {std::pair{state, true}, std::pair{health, true}, std::pair{mode, true}, std::pair{rpm, false}} ){
			HistDescription described;
			ASSERT_NO_THROW( described = WaitDescribed(host, index) );
			EXPECT_EQ( described.Enumeration, enumeration ) << "member " << index;
			EXPECT_EQ( thresholds(index).ExceptionDeviation.has_value(), !enumeration ) << "member " << index;
			EXPECT_EQ( host->FindThresholds(index)->ExceptionDeviation, 1 ) << "the row keeps it";
		}
	}

	//The web's gate on a group's history (#335):  historyGroups says which group on the connection holds each node, spelled
	//as a read spells it back, among the groups the caller may read, and historyCapabilities what the caller may do with one -
	//the library's aggregates, and the edits, which need a grant on an enforced resource as the edits themselves do.
	TEST_F( HistCollectorTests, GroupsAndCapabilities ){
		if( !Logging::FindLogger<Logging::MemoryLog>() )
			Logging::AddLogger( mu<Logging::MemoryLog>() );
		//A capability the caller lacks used to log an AccessException, at Debug under Access|Exception, which the config's
		//loggers leave out.  The memory logger takes that tag alone, Information otherwise:  at its own default, Trace, the
		//recomputed filter would log every exception as it is made, before the code under test could silence it.
		auto& memory = Logging::GetLogger<Logging::MemoryLog>();
		let denial = ELogTags::Access | ELogTags::Exception;
		let memoryDefault = memory.DefaultLevel();
		memory.SetDefaultLevel( ELogLevel::Information );
		memory.SetLevel( denial, ELogLevel::Debug );
		absl::Cleanup restoreMemory = [&]{//the clear recomputes the filter with the memory logger at Information, and its default goes back without one.
			memory.ClearLevel( denial );
			memory.SetDefaultLevel( memoryDefault );
		};
		let itemsBefore = Items();
		let group = CreateGroup( "gate" );
		absl::Cleanup purge = [=]{ Purge( group, itemsBefore ); };
		AddNode( group, RpmManual );
		let twin = CreateGroup( "gate-twin" );
		absl::Cleanup purgeTwin = [=]{ Purge( twin, itemsBefore ); };
		AddNode( twin, Status1 );
		ASSERT_NO_THROW( WaitSubscribed(Host(group), 1) );//a node spelled by index resolves once the collector has the server's namespaces.
		ASSERT_NO_THROW( WaitSubscribed(Host(twin), 1) );
		let query = [&]( sv q, jobject vars ){ return Socket().QuerySync( string{q}, move(vars) ); };

		let groups = query( "historyGroups( opc:$opc, nodes:$nodes ){ groups{ id name nodes } }", {{"opc", OpcServerSlug}, {"nodes", jarray{Node(RpmManual).ToJson(), Ex(Rpm2), Node(Status1).ToJson()}}} );
		let& rows = Json::AsArray( groups.as_object(), "groups" );
		ASSERT_EQ( rows.size(), 2u ) << serialize( groups );
		for( let& row : rows ){
			let& o = row.as_object();
			let id = Json::AsNumber<uint32>( o, "id" );
			EXPECT_TRUE( id==group || id==twin ) << serialize( row );
			EXPECT_EQ( Json::AsSV(o, "name"), id==group ? "Collector gate" : "Collector gate-twin" );
			EXPECT_EQ( Json::AsArray(o, "nodes"), (jarray{ id==group ? Node(RpmManual).ToJson() : Node(Status1).ToJson() }) ) << "spelled as asked, and none of Rpm2, which no group holds";
		}
		EXPECT_TRUE( Json::AsArray(query("historyGroups( opc:$opc, nodes:$nodes ){ groups{ id } }", {{"opc", OpcServerSlug}, {"nodes", Ex(Rpm2)}}).as_object(), "groups").empty() );
		try{//no nodes is the caller's mistake, which answered 500, a fault of the gateway's (historian-group-history-web #9).
			query( "historyGroups( opc:$opc ){ groups{ id } }", {{"opc", OpcServerSlug}} );
			ADD_FAILURE() << "historyGroups without nodes was answered";
		}
		catch( const GatewayErrorResponse& e ){
			EXPECT_EQ( e.HttpStatus(), EHttpStatus::BadRequest ) << e.what();
		}

		let capabilities = [&]( uint32 g ){ return query( "historyCapabilities( group:$group ){ aggregates update delete }", {{"group", g}} ).as_object(); };
		EnforceHistGroups( false );
		auto open = capabilities( group );
		jarray names;
		for( let& aggregate : Opc::Hist::Aggregates )
			names.emplace_back( aggregate.Name );
		EXPECT_EQ( Json::AsArray(open, "aggregates"), names );
		EXPECT_FALSE( Json::AsBool(open, "update") ) << "nothing enforces hist_groups";
		EXPECT_FALSE( Json::AsBool(open, "delete") );
		let denials = Logging::Find( [&]( const Logging::Entry& e ){ let m = e.Message(); return m.contains( Ƒ("histGroups[{}]", Host(group)->Guid()) ) && m.contains( "nothing enforces it" ); } );//the row is keyed by the guid (#357).
		EXPECT_TRUE( denials.empty() ) << "a capability the caller lacks is an answer, not a denial to log:  " << denials.front().Message();
		auto reopen = EnforcedHistGroups();
		auto granted = capabilities( group );
		EXPECT_TRUE( Json::AsBool(granted, "update") );
		EXPECT_TRUE( Json::AsBool(granted, "delete") );
		//A caller Authorize doesn't know, as a session whose user is gone, is refused with the 401 the web signs in again on, not
		//answered with no groups as a caller without the grant is (historian-group-history-web #4).
		constexpr Jde::UserPK unknown{ (Jde::UserPK::Type)0x08'0000 };
		auto ql = QL::Parse( "historyGroups( opc:$opc, nodes:$nodes ){ groups{ id } }", {{"opc", OpcServerSlug}, {"nodes", jarray{Node(RpmManual).ToJson()}}}, Schemas(), true );
		try{
			BlockAwait<HistGroupInfoQLAwait,jvalue>( HistGroupInfoQLAwait{move(ql.Queries().front()), QL::Creds{unknown}} );
			ADD_FAILURE() << "an unknown caller was answered";
		}
		catch( const Exception& e ){
			EXPECT_EQ( e.HttpStatus(), EHttpStatus::Unauthorized ) << e.what();
		}
		try{
			capabilities( 999999 );
			ADD_FAILURE() << "a group the gateway doesn't host";
		}
		catch( const GatewayErrorResponse& e ){
			EXPECT_NE( string{e.what()}.find("isn't one this gateway historizes"), string::npos ) << e.what();
		}

		//The rule historyGroups adds, and the two edit rights told apart (historian-group-history-web #10):  a group's own resource
		//stands in for the slug's root (ScopeHistGroup).  The twin's grants the caller no Read, so it isn't listed;  the group's grants
		//Read and Update, not Delete.
		let acl = AppClient()->Acl();
		absl::Cleanup unscope = [=]{
			for( let g : {group, twin} ){
				try{
					UnscopeHistGroup( g );
				}
				catch( const std::exception& e ){
					ADD_FAILURE() << "group " << g << "'s resource wasn't deleted:  " << e.what();
				}
			}
		};
		ScopeHistGroup( twin, Access::ERights::None );
		ScopeHistGroup( group, Access::ERights::Read | Access::ERights::Update );
		ASSERT_TRUE( WaitUntil([&]{//the gateway's authorizer hears of it over its subscription.
			auto allowed = acl->CheckEnforced( "gateway", "histGroups", Host(group)->Guid(), Access::ERights::Update, User(), SRCE_CUR );
			if( !allowed )
				allowed.error().SetLevel( ELogLevel::NoLog );
			return allowed.has_value();
		}) ) << "the group's grant didn't reach the gateway's authorizer";

		let listed = query( "historyGroups( opc:$opc, nodes:$nodes ){ groups{ id } }", {{"opc", OpcServerSlug}, {"nodes", jarray{Node(RpmManual).ToJson(), Node(Status1).ToJson()}}} );
		vector<uint32> ids;
		for( let& row : Json::AsArray(listed.as_object(), "groups") )
			ids.push_back( Json::AsNumber<uint32>(row.as_object(), "id") );
		EXPECT_EQ( ids, (vector<uint32>{group}) ) << "the twin, which the caller may not read, is none of theirs:  " << serialize( listed );
		auto scoped = capabilities( group );
		EXPECT_TRUE( Json::AsBool(scoped, "update") ) << serialize( scoped );
		EXPECT_FALSE( Json::AsBool(scoped, "delete") ) << "Update granted alone:  " << serialize( scoped );
	}
}