#include <jde/fwk/process/execution.h>
#include <jde/tests/Capture.h>
#include <jde/opc/openProtocol/logTags.h>
#include "utils/DriverResults.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	using OpenProtocol::Controller;
	using OpenProtocol::LocalTime;
	using OpenProtocol::Mid0015;
	using OpenProtocol::Mid0035;
	using OpenProtocol::Mid0052;
	using OpenProtocol::Mid0071;
	using OpenProtocol::Mid0074;
	using OpenProtocol::Mid0076;

	//docs/open-protocol/plan.md Phase 7 (#469):  the Open Protocol driver's `Selected` and `Alarm`
	//(drivers/openProtocol/OpenProtocolDriver.h) - the parameter set, the job, the VIN and the alarm the library's test controller
	//reports under their four subscriptions, read as a web session's client reads the row's server and collected by a group.
	struct DriverStateTests : DriverResultFixture{
	protected:
		using State = flat_map<NodeId,Value>;
		static constexpr StatusCode NoCommunication{ UA_STATUSCODE_BADNOCOMMUNICATION };
		static constexpr array<sv,9> Paths{ "Selected.Pset", "Selected.Job", "Selected.Vin", "Selected.IdentifierPart2", "Selected.IdentifierPart3", "Selected.IdentifierPart4", "Alarm.Code", "Alarm.Active", "Alarm.Time" };
		//`_Op.Revisions` once the connection's seven requests are answered, each subscription under the MID it pushes.
		static constexpr sv Subscribed{ "0001:6 0015:3 0035:6 0052:2 0061:7 0071:3 0080:1" };
		static constexpr LocalTime Raised{ local_days{year{2026}/7/4}+10h+15min+30s };//on the controller's clock.
		static constexpr TimePoint RaisedUtc{ sys_days{year{2026}/7/4}+14h+15min+30s };//in New York's summer.

		Ω Pset( uint16_t id )ι->Mid0015{ return { .Pset=id, .PsetChangeTime=LocalTime{local_days{year{2026}/1/15}+8h}, .PsetName="M8 bracket" }; }
		Ω Job( uint16_t id )ι->Mid0035{ return { .JobId=id, .JobStatus=0, .JobBatchMode=0, .JobBatchSize=8, .JobBatchCounter=2 }; }
		Ω Vin( string vin )ι->Mid0052{ return { .Vin=move(vin), .IdentifierPart2="part 2", .IdentifierPart3="", .IdentifierPart4="part 4" }; }
		Ω Alarm( string code, optional<LocalTime> at )ι->Mid0071{ return { .ErrorCode=move(code), .ControllerReady=1, .ToolReady=0, .Time=at, .ToolHealth=1, .AlarmText="Tool overheated" }; }
		//Every variable of `Selected` and `Alarm` as a web session's client reads them, with their timestamps.
		Ω ReadState( const DriverRow& row )ε->State{
			flat_set<NodeId> nodes;
			for( let path : Paths )
				nodes.emplace( DriverNode(path) );
			return BlockAwait( ReadValueAwait{move(nodes), BlockAwait(UAClient::GetClient(row.Slug, SessionCredential())), UA_TIMESTAMPSTORETURN_BOTH} );
		}
		Ω Text( const Value& v )ι->string{ return v.hasValue && !v.IsEmpty() ? serialize( v.ValueJson(nullptr) ) : "no value"; }
		Ω Statuses( const State& read )ι->string{
			string y;
			for( let path : Paths )
				y += Ƒ( "{}{} {} {}", y.empty() ? "" : ", ", path, Text(Of(read, path)), UA_StatusCode_name(Of(read, path).status) );
			return y;
		}
		Ω Until( const DriverRow& row, function<bool(const State&)> is )ι->bool{
			return WaitUntil( [&]{
				try{
					return is( ReadState(row) );
				}
				catch( const std::exception& ){
					return false;
				}
			}, 15s );
		}
		//Until `path` holds `json` under `status`.
		Ω Wait( const DriverRow& row, sv path, StatusCode status, sv json )ι->bool{
			return Until( row, [=]( const State& read ){ let& v = Of( read, path ); return v.status==status && Text( v )==json; } );
		}
		//Until each of the nine reads `status`.
		Ω WaitAll( const DriverRow& row, StatusCode status )ι->bool{
			return Until( row, [=]( const State& read ){ return std::ranges::all_of( Paths, [&]( sv path ){ return Of( read, path ).status==status; } ); } );
		}
		Ω WaitRevisions( const DriverRow& row, sv text )ι->bool{ return WaitOp( row, "Revisions", [=]( let& v ){ return OpText( v )==text; } ); }
	};

	//`Selected` and `Alarm` are fixed, as the rest of the row's address space is, and each of their variables reads
	//Bad_WaitingForInitialData until its report.  Their subscriptions are asked at the connect behind the results' and the
	//clock's read.  A parameter set, a job, a VIN with its identifier parts and an alarm each land as they are pushed, sourced
	//at their receipt, the alarm's own time read in the row's zone;  an alarm acknowledged on the controller is no longer active,
	//and an older one acknowledged leaves the alarm held active.
	TEST_F( DriverStateTests, SelectedAndAlarmFollowTheirPushes ){
		Controller station{ *Executor(), ControllerPort(), Station() };
		const DriverRow row{ "driver-state", NewYork };
		ASSERT_TRUE( WaitRevisions(row, Subscribed) ) << OpText( Op(row, "Revisions") );

		let children = [&]( sv object ){
			vector<string> names;
			let node = Socket().QuerySync( "node( opc: $opc, id: $id ){ children{ name nodeClass } }", {{"opc", row.Slug}, {"id", DriverNode(object).ToJson()}} ).as_object();
			for( let& child : Json::AsArray(node, "children") ){
				names.push_back( Json::AsString(child.as_object(), "name") );
				EXPECT_EQ( Json::FindNumber<uint>(child.as_object(), "nodeClass"), UA_NODECLASS_VARIABLE ) << names.back();
			}
			std::ranges::sort( names );
			return names;
		};
		EXPECT_EQ( children("Selected"), (vector<string>{"IdentifierPart2", "IdentifierPart3", "IdentifierPart4", "Job", "Pset", "Vin"}) );
		EXPECT_EQ( children("Alarm"), (vector<string>{"Active", "Code", "Time"}) );
		let attributes = Socket().QuerySync( "node( opc: $opc, id: $id ){ accessLevel userAccessLevel }", {{"opc", row.Slug}, {"id", DriverNode("Selected.Pset").ToJson()}} ).as_object();
		EXPECT_EQ( Json::FindNumber<uint>(attributes, "accessLevel"), optional<uint>{UA_ACCESSLEVELMASK_READ} ) << serialize( attributes );
		EXPECT_EQ( Json::FindNumber<uint>(attributes, "userAccessLevel"), optional<uint>{UA_ACCESSLEVELMASK_READ} ) << serialize( attributes );

		vector<uint16_t> asked;
		for( let& frame : station.Frames() ){
			if( let mid = frame.Head.Mid; mid!=9997 && mid!=9999 )
				asked.push_back( mid );
		}
		EXPECT_EQ( asked, (vector<uint16_t>{1, 60, 80, 14, 34, 51, 70}) ) << "the results first, then the clock, then the four";
		{
			let read = ReadState( row );
			constexpr array<uint16_t,9> types{ UA_TYPES_UINT16, UA_TYPES_UINT16, UA_TYPES_STRING, UA_TYPES_STRING, UA_TYPES_STRING, UA_TYPES_STRING, UA_TYPES_STRING, UA_TYPES_BOOLEAN, UA_TYPES_DATETIME };
			for( uint i=0; i<Paths.size(); ++i ){
				let& v = Of( read, Paths[i] );
				EXPECT_EQ( v.status, Waiting ) << Paths[i] << ": " << UAException::Message( v.status );
				EXPECT_TRUE( Is(v, types[i]) ) << Paths[i] << " holds no " << UA_TYPES[types[i]].typeName;
			}
		}

		let before = Ms( Clock::now() );
		station.Select( Pset(7) );
		ASSERT_TRUE( Wait(row, "Selected.Pset", UA_STATUSCODE_GOOD, "7") ) << Statuses( ReadState(row) );
		{
			let read = ReadState( row );
			let source = SourceTime( Of(read, "Selected.Pset") );
			ASSERT_TRUE( source );
			EXPECT_GE( *source, before ) << "its receipt, not the time the set last changed";
			EXPECT_LE( *source, Clock::now() );
			for( let path : Paths | std::views::drop(1) )
				EXPECT_EQ( Of(read, path).status, Waiting ) << path << ":  no report of its own yet";
		}
		station.Select( Job(12) );
		ASSERT_TRUE( Wait(row, "Selected.Job", UA_STATUSCODE_GOOD, "12") ) << Statuses( ReadState(row) );
		station.Select( Vin("VIN0123456789") );
		ASSERT_TRUE( Wait(row, "Selected.Vin", UA_STATUSCODE_GOOD, "\"VIN0123456789\"") ) << Statuses( ReadState(row) );
		{
			let read = ReadState( row );
			for( let& [path, part] : {std::pair{"Selected.IdentifierPart2"sv, "\"part 2\""sv}, std::pair{"Selected.IdentifierPart3"sv, "\"\""sv}, std::pair{"Selected.IdentifierPart4"sv, "\"part 4\""sv}} ){
				let& v = Of( read, path );
				EXPECT_EQ( v.status, UA_STATUSCODE_GOOD ) << path << ":  an empty text is a value";
				EXPECT_EQ( Text(v), part ) << path;
				EXPECT_EQ( SourceTime(v), SourceTime(Of(read, "Selected.Vin")) ) << path << ":  one report, one source timestamp";
			}
		}

		station.Raise( Alarm("E404", Raised) );
		ASSERT_TRUE( Wait(row, "Alarm.Active", UA_STATUSCODE_GOOD, "true") ) << Statuses( ReadState(row) );
		let raised = ReadState( row );
		EXPECT_EQ( Text(Of(raised, "Alarm.Code")), "\"E404\"" );
		EXPECT_EQ( Of(raised, "Alarm.Time").status, UA_STATUSCODE_GOOD );
		EXPECT_EQ( Time(Of(raised, "Alarm.Time")), optional<TimePoint>{RaisedUtc} ) << "the wire's time in the row's zone";
		ASSERT_TRUE( SourceTime(Of(raised, "Alarm.Time")) );
		EXPECT_GE( *SourceTime(Of(raised, "Alarm.Time")), before ) << "its receipt, not the alarm's own time";

		station.Raise( Alarm("E405", Raised+1min) );
		ASSERT_TRUE( Wait(row, "Alarm.Code", UA_STATUSCODE_GOOD, "\"E405\"") ) << Statuses( ReadState(row) );
		let second = ReadState( row );
		let older = OpenProtocol::Encode( {.Revision=2}, Mid0074{.ErrorCode="E404"} );
		ASSERT_TRUE( older.has_value() ) << older.error().What;
		station.SendRaw( *older );
		station.Select( Pset(8) );//behind it on the link:  the set written, the acknowledgement has been read.
		ASSERT_TRUE( Wait(row, "Selected.Pset", UA_STATUSCODE_GOOD, "8") ) << Statuses( ReadState(row) );
		{
			let read = ReadState( row );
			EXPECT_EQ( Text(Of(read, "Alarm.Active")), "true" ) << "E404 acknowledged, E405 still active";
			EXPECT_EQ( Text(Of(read, "Alarm.Code")), "\"E405\"" );
			for( let path : {"Alarm.Code", "Alarm.Active", "Alarm.Time"} )
				EXPECT_EQ( SourceTime(Of(read, path)), SourceTime(Of(second, path)) ) << path << ":  nothing written";
		}

		station.AcknowledgeAlarm();
		ASSERT_TRUE( Wait(row, "Alarm.Active", UA_STATUSCODE_GOOD, "false") ) << Statuses( ReadState(row) );
		{
			let read = ReadState( row );
			EXPECT_EQ( Of(read, "Alarm.Code").status, UA_STATUSCODE_GOOD );
			EXPECT_EQ( Text(Of(read, "Alarm.Code")), "\"E405\"" ) << "MID 0074's";
			EXPECT_EQ( Time(Of(read, "Alarm.Time")), optional<TimePoint>{RaisedUtc+1min} );
			EXPECT_EQ( SourceTime(Of(read, "Alarm.Time")), SourceTime(Of(second, "Alarm.Time")) ) << "MID 0074 carries no time:  as the alarm left it";
		}
		station.Select( Pset(9) );
		EXPECT_TRUE( Wait(row, "Selected.Pset", UA_STATUSCODE_GOOD, "9") ) << Statuses( ReadState(row) );
		EXPECT_EQ( OpText(Op(row, "Revisions")), Subscribed );
	}

	//The link lost, each variable that read Good reads Bad_NoCommunication over the value it had, from the loss on.  The link
	//back, a controller that pushes nothing as a subscription is accepted leaves them so, and a push to each clears it, though
	//not another alarm's acknowledgement;  one that pushes what it has clears them at the connect, the alarm by MID 0076's
	//status.
	TEST_F( DriverStateTests, ALostLinkIsBadNoCommunication ){
		Controller station{ *Executor(), ControllerPort(), Station() };
		const DriverRow row{ "driver-state-link", NewYork };
		ASSERT_TRUE( WaitRevisions(row, Subscribed) ) << OpText( Op(row, "Revisions") );
		station.Select( Pset(7) );
		station.Select( Job(12) );
		station.Select( Vin("VIN0123456789") );
		station.Raise( Alarm("E404", Raised) );
		ASSERT_TRUE( WaitAll(row, UA_STATUSCODE_GOOD) ) << Statuses( ReadState(row) );
		let good = ReadState( row );
		let kept = [&]( const State& read, sv when ){
			for( let path : Paths ){
				EXPECT_EQ( Of(read, path).status, NoCommunication ) << path << " " << when << ": " << UAException::Message( Of(read, path).status );
				EXPECT_EQ( Text(Of(read, path)), Text(Of(good, path)) ) << path << " " << when << ":  the last value";
			}
		};

		station.Edit( []( Controller::Script& script ){ script.PushOnSubscribe = false; } );
		let lost = Ms( Clock::now() );
		ASSERT_TRUE( Sever(station, row) );
		ASSERT_TRUE( WaitAll(row, NoCommunication) ) << Statuses( ReadState(row) );
		{
			let read = ReadState( row );
			kept( read, "with the link down" );
			let at = SourceTime( Of(read, "Selected.Pset") );
			ASSERT_TRUE( at );
			EXPECT_GE( *at, lost );
			EXPECT_LE( *at, Clock::now() );
			for( let path : Paths )
				EXPECT_EQ( SourceTime(Of(read, path)), at ) << path << ":  the loss's time";
		}
		Mend( station );
		ASSERT_TRUE( WaitConnected(row) );
		ASSERT_TRUE( WaitRevisions(row, Subscribed) ) << OpText( Op(row, "Revisions") );
		kept( ReadState(row), "with the link back and nothing reported" );

		station.Select( Pset(7) );//as it was:  a report all the same.
		ASSERT_TRUE( Wait(row, "Selected.Pset", UA_STATUSCODE_GOOD, "7") ) << Statuses( ReadState(row) );
		EXPECT_EQ( Of(ReadState(row), "Selected.Job").status, NoCommunication ) << "each one's own report";
		let other = OpenProtocol::Encode( {.Revision=2}, Mid0074{.ErrorCode="E403"} );
		ASSERT_TRUE( other.has_value() ) << other.error().What;
		station.SendRaw( *other );
		station.Select( Job(12) );//behind it on the link.
		ASSERT_TRUE( Wait(row, "Selected.Job", UA_STATUSCODE_GOOD, "12") ) << Statuses( ReadState(row) );
		{
			let read = ReadState( row );
			for( let path : {"Alarm.Code", "Alarm.Active", "Alarm.Time"} )
				EXPECT_EQ( Of(read, path).status, NoCommunication ) << path << ":  E403 acknowledged says nothing of E404";
			EXPECT_EQ( Text(Of(read, "Alarm.Code")), "\"E404\"" );
		}
		station.Select( Vin("VIN0123456789") );
		station.Raise( Alarm("E405", Raised+1min) );
		ASSERT_TRUE( WaitAll(row, UA_STATUSCODE_GOOD) ) << Statuses( ReadState(row) );
		EXPECT_EQ( Text(Of(ReadState(row), "Alarm.Code")), "\"E405\"" );

		station.Edit( []( Controller::Script& script ){ script.PushOnSubscribe = true; } );
		ASSERT_TRUE( Sever(station, row) );
		ASSERT_TRUE( WaitAll(row, NoCommunication) ) << Statuses( ReadState(row) );
		Mend( station );
		ASSERT_TRUE( WaitAll(row, UA_STATUSCODE_GOOD) ) << "pushed as each subscription was accepted:  " << Statuses( ReadState(row) );
		let back = ReadState( row );
		EXPECT_EQ( Text(Of(back, "Selected.Pset")), "7" );
		EXPECT_EQ( Text(Of(back, "Selected.Job")), "12" );
		EXPECT_EQ( Text(Of(back, "Selected.Vin")), "\"VIN0123456789\"" );
		EXPECT_EQ( Text(Of(back, "Alarm.Active")), "true" ) << "MID 0076's status";
		EXPECT_EQ( Text(Of(back, "Alarm.Code")), "\"E405\"" );
		EXPECT_EQ( Time(Of(back, "Alarm.Time")), optional<TimePoint>{RaisedUtc+1min} );
		EXPECT_EQ( OpCount(Op(row, "Reconnects")), optional<uint>{2} );
	}

	//A controller that pushes nothing as a subscription is accepted leaves each variable Bad_WaitingForInitialData until it
	//reports, though it has a set, a job, a VIN and an alarm.  The link lost, a variable with no report yet says so still.
	TEST_F( DriverStateTests, NothingReportedIsWaiting ){
		auto quiet = Station();
		quiet.PushOnSubscribe = false;
		Controller station{ *Executor(), ControllerPort(), move(quiet) };
		station.Select( Pset(7) );//each kept, with no client to push to.
		station.Select( Job(12) );
		station.Select( Vin("VIN0123456789") );
		station.Raise( Alarm("E404", Raised) );
		const DriverRow row{ "driver-state-quiet", NewYork };
		ASSERT_TRUE( WaitRevisions(row, Subscribed) ) << OpText( Op(row, "Revisions") );

		station.Select( Pset(8) );
		ASSERT_TRUE( Wait(row, "Selected.Pset", UA_STATUSCODE_GOOD, "8") ) << Statuses( ReadState(row) );
		{
			let read = ReadState( row );
			for( let path : Paths | std::views::drop(1) )
				EXPECT_EQ( Of(read, path).status, Waiting ) << path << ": " << UAException::Message( Of(read, path).status );
		}
		ASSERT_TRUE( Sever(station, row) );
		ASSERT_TRUE( Wait(row, "Selected.Pset", NoCommunication, "8") ) << Statuses( ReadState(row) );
		{
			let read = ReadState( row );
			for( let path : Paths | std::views::drop(1) )
				EXPECT_EQ( Of(read, path).status, Waiting ) << path << ":  nothing to go stale, " << UAException::Message( Of(read, path).status );
		}
		station.Edit( []( Controller::Script& script ){ script.PushOnSubscribe = true; } );
		Mend( station );
		ASSERT_TRUE( WaitAll(row, UA_STATUSCODE_GOOD) ) << Statuses( ReadState(row) );
		let read = ReadState( row );
		EXPECT_EQ( Text(Of(read, "Selected.Pset")), "8" );
		EXPECT_EQ( Text(Of(read, "Selected.Job")), "12" );
		EXPECT_EQ( Text(Of(read, "Selected.IdentifierPart4")), "\"part 4\"" );
		EXPECT_EQ( Text(Of(read, "Alarm.Code")), "\"E404\"" );
		EXPECT_EQ( Text(Of(read, "Alarm.Active")), "true" );
		EXPECT_EQ( Time(Of(read, "Alarm.Time")), optional<TimePoint>{RaisedUtc} );
	}

	//A group over `Selected.Pset` stores each change, the Bad_NoCommunication the link's loss puts over the last, and the
	//report that clears it, in the order they came.
	TEST_F( DriverStateTests, AGroupStoresTheChangesAndTheStatus ){
		Controller station{ *Executor(), ControllerPort(), Station() };
		const DriverRow row{ "driver-state-group", NewYork };
		ASSERT_TRUE( WaitRevisions(row, Subscribed) ) << OpText( Op(row, "Revisions") );
		const ResultGroup group{ row, "driver-state-group", {{"Selected.Pset", {}}} };
		let from = Clock::now();//past the member's first value, the driver's start.
		station.Select( Pset(3) );
		ASSERT_TRUE( Wait(row, "Selected.Pset", UA_STATUSCODE_GOOD, "3") ) << Statuses( ReadState(row) );
		station.Select( Pset(5) );
		ASSERT_TRUE( Wait(row, "Selected.Pset", UA_STATUSCODE_GOOD, "5") ) << Statuses( ReadState(row) );
		ASSERT_TRUE( Sever(station, row) );
		ASSERT_TRUE( Wait(row, "Selected.Pset", NoCommunication, "5") ) << Statuses( ReadState(row) );
		Mend( station );//the set comes again as its subscription is accepted.
		ASSERT_TRUE( Wait(row, "Selected.Pset", UA_STATUSCODE_GOOD, "5") ) << Statuses( ReadState(row) );

		ASSERT_NO_THROW( WaitStored(group.Host, group.Members[0], from, 4) );
		let records = Records( group, 0, from );
		ASSERT_EQ( records.size(), 4u );
		const array<std::pair<StatusCode,uint16_t>,4> stored{{ {UA_STATUSCODE_GOOD, 3}, {UA_STATUSCODE_GOOD, 5}, {NoCommunication, 5}, {UA_STATUSCODE_GOOD, 5} }};
		for( uint i=0; i<stored.size(); ++i ){
			SCOPED_TRACE( i );
			EXPECT_EQ( records[i].status, stored[i].first ) << UAException::Message( records[i].status );
			ASSERT_TRUE( Is(records[i], UA_TYPES_UINT16) );
			EXPECT_EQ( records[i].Get<UA_UInt16>(0), stored[i].second ) << "the status over the last value";
			if( i )
				EXPECT_GT( SourceTime(records[i]), SourceTime(records[i-1]) ) << "increasing";
		}
	}

	//What a controller lacks reads Bad_NotSupported:  a subscription it refuses, said once and in `_Op.LastError`, the link
	//standing, and accepted at a later connection, waiting for its report again;  the identifier parts of a VIN pushed at
	//revision 1, though its subscription was accepted at 2, which `_Op.Revisions` follows;  and the time of an alarm sent with
	//none.  MID 0076's status of no alarm reads Active false, and with no time leaves the time as the alarm left it.  The
	//controller numbers no messages here, so each push is acknowledged by its own MID.
	TEST_F( DriverStateTests, WhatAControllerLacksIsNotSupported ){
		auto plain = Station();
		plain.Identity.SequenceNumberSupport = 0;
		plain.Refuses[34] = 99;
		plain.Revisions[51] = 1;
		plain.Lowers.insert( 51 );
		const Logging::Capture said{ OpenProtocol::LogTag, ELogLevel::Information };
		Controller station{ *Executor(), ControllerPort(), move(plain) };
		const DriverRow row{ "driver-state-lacks", NewYork };
		ASSERT_TRUE( WaitRevisions(row, "0001:6 0015:3 0052:2 0061:7 0071:3 0080:1") ) << OpText( Op(row, "Revisions") );
		EXPECT_TRUE( OpText(Op(row, "LastError")).contains("MID 0034") ) << OpText( Op(row, "LastError") );
		EXPECT_EQ( OpFlag(Op(row, "Connected")), true );
		EXPECT_EQ( OpFlag(Op(row, "LinkLevel")), false );
		let refused = ReadState( row );
		for( let path : Paths )
			EXPECT_EQ( Of(refused, path).status, path=="Selected.Job" ? NotSupported : Waiting ) << path << ": " << UAException::Message( Of(refused, path).status );

		station.Select( Vin("VIN0123456789") );
		ASSERT_TRUE( Wait(row, "Selected.Vin", UA_STATUSCODE_GOOD, "\"VIN0123456789\"") ) << Statuses( ReadState(row) );
		EXPECT_TRUE( WaitRevisions(row, "0001:6 0015:3 0052:1 0061:7 0071:3 0080:1") ) << "the push's own:  " << OpText( Op(row, "Revisions") );
		let vin = ReadState( row );
		for( let path : {"Selected.IdentifierPart2", "Selected.IdentifierPart3", "Selected.IdentifierPart4"} ){
			EXPECT_EQ( Of(vin, path).status, NotSupported ) << path << ": " << UAException::Message( Of(vin, path).status );
			EXPECT_EQ( SourceTime(Of(vin, path)), SourceTime(Of(vin, "Selected.Vin")) ) << path;
		}

		station.Raise( Alarm("E100", nullopt) );
		ASSERT_TRUE( Wait(row, "Alarm.Active", UA_STATUSCODE_GOOD, "true") ) << Statuses( ReadState(row) );
		EXPECT_EQ( Of(ReadState(row), "Alarm.Time").status, NotSupported ) << "sent with no time";
		let none = OpenProtocol::Encode( {.Revision=3}, Mid0076{.AlarmStatus=0, .ControllerReady=1, .ToolReady=1, .Time=Raised, .ToolHealth=1} );
		ASSERT_TRUE( none.has_value() ) << none.error().What;
		station.SendRaw( *none );
		ASSERT_TRUE( Wait(row, "Alarm.Active", UA_STATUSCODE_GOOD, "false") ) << Statuses( ReadState(row) );
		{
			let read = ReadState( row );
			EXPECT_EQ( Of(read, "Alarm.Code").status, UA_STATUSCODE_GOOD );
			EXPECT_EQ( Text(Of(read, "Alarm.Code")), "\"\"" ) << "as the status has it";
			EXPECT_EQ( Of(read, "Alarm.Time").status, UA_STATUSCODE_GOOD );
			EXPECT_EQ( Time(Of(read, "Alarm.Time")), optional<TimePoint>{RaisedUtc} );
		}
		let timeless = OpenProtocol::Encode( {.Revision=3}, Mid0076{.AlarmStatus=0, .ControllerReady=1, .ToolReady=1, .ToolHealth=1} );
		ASSERT_TRUE( timeless.has_value() ) << timeless.error().What;
		let sent = ReadState( row );
		station.SendRaw( *timeless );
		station.Select( Pset(3) );//behind it on the link.
		ASSERT_TRUE( Wait(row, "Selected.Pset", UA_STATUSCODE_GOOD, "3") ) << Statuses( ReadState(row) );
		{
			let read = ReadState( row );
			EXPECT_NE( SourceTime(Of(read, "Alarm.Active")), SourceTime(Of(sent, "Alarm.Active")) ) << "the status written";
			EXPECT_EQ( Of(read, "Alarm.Time").status, UA_STATUSCODE_GOOD ) << "no alarm, not an alarm with no time";
			EXPECT_EQ( Time(Of(read, "Alarm.Time")), optional<TimePoint>{RaisedUtc} );
			EXPECT_EQ( SourceTime(Of(read, "Alarm.Time")), SourceTime(Of(sent, "Alarm.Time")) ) << "as the alarm left it";
		}

		//The next connection is refused the same, and says nothing more of it.
		station.Drop();
		ASSERT_TRUE( WaitOp(row, "Reconnects", []( let& v ){ return OpCount( v )==1u; }) );
		ASSERT_TRUE( WaitRevisions(row, "0001:6 0015:3 0052:1 0061:7 0071:3 0080:1") ) << OpText( Op(row, "Revisions") );
		ASSERT_TRUE( Wait(row, "Selected.Vin", UA_STATUSCODE_GOOD, "\"VIN0123456789\"") ) << "pushed again at the connect:  " << Statuses( ReadState(row) );
		let again = ReadState( row );
		EXPECT_EQ( Of(again, "Selected.Job").status, NotSupported );
		EXPECT_EQ( SourceTime(Of(again, "Selected.Job")), SourceTime(Of(refused, "Selected.Job")) ) << "as the first refusal left it";
		EXPECT_EQ( SourceTime(Of(again, "Selected.IdentifierPart2")), SourceTime(Of(vin, "Selected.IdentifierPart2")) ) << "Bad_NotSupported already";
		EXPECT_EQ( said.Find([&]( const Logging::Entry& e ){ return e.Message().contains( "MID 0034" ) && e.Message().contains( "Bad_NotSupported" ) && e.Message().contains( row.Slug ); }).size(), 1u );

		//Accepted at the next, as by a controller given jobs since, the job waits for its report again.
		station.Edit( []( Controller::Script& script ){ script.Refuses.erase( 34 ); } );
		station.Drop();
		ASSERT_TRUE( WaitOp(row, "Reconnects", []( let& v ){ return OpCount( v )==2u; }) );
		ASSERT_TRUE( WaitRevisions(row, "0001:6 0015:3 0035:6 0052:1 0061:7 0071:3 0080:1") ) << OpText( Op(row, "Revisions") );
		ASSERT_TRUE( Until(row, []( const State& read ){ return Of( read, "Selected.Job" ).status==Waiting; }) ) << Statuses( ReadState(row) );
		let accepted = ReadState( row );
		for( let path : {"Selected.IdentifierPart2", "Selected.IdentifierPart3", "Selected.IdentifierPart4"} ){
			EXPECT_EQ( Of(accepted, path).status, NotSupported ) << path << ":  a report's, not the refusal's";
			EXPECT_EQ( SourceTime(Of(accepted, path)), SourceTime(Of(vin, path)) ) << path;
		}
		EXPECT_EQ( said.Find([&]( const Logging::Entry& e ){ return e.Message().contains( "MID 0034" ) && e.Message().contains( "Bad_WaitingForInitialData" ) && e.Message().contains( row.Slug ); }).size(), 1u );
		station.Select( Job(12) );
		EXPECT_TRUE( Wait(row, "Selected.Job", UA_STATUSCODE_GOOD, "12") ) << Statuses( ReadState(row) );
	}
}