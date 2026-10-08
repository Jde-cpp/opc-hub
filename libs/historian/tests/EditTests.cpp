//Edits (#206):  Part 11's HistoryUpdate on a group - insert, replace, update, delete over a range and at times - written
//as Modification records to the day's modifications file once the buffer is flushed, fsynced before the caller is
//answered, applied by raw reads and served by modified reads; and the start values that edits and late records keep
//current:  each node's newest record, the walk back for a file made after its day, and the later files' preambles.
#include <jde/fwk/log/MemoryLog.h>
#include <jde/opc/UAException.h>
#include "reads.h"

#define let const auto

namespace Jde::Opc::Hist::Tests{
	using namespace std::chrono;
	using Proto::HistoryRecord;

	Ω generation( const fs::path& file )ε->uint32_t{
		let start = ReadStart( file );
		if( !start )
			ADD_FAILURE() << file << " opens with no FileStart.";
		return start ? start->generation() : std::numeric_limits<uint32_t>::max();
	}
	Ω types( const vector<ReadValue>& values )ι->vector<Proto::UpdateType>{
		vector<Proto::UpdateType> y;
		for( let& v : values )
			y.push_back( v.Modification ? v.Modification->Type : Proto::UPDATE_TYPE_UNSPECIFIED );
		return y;
	}
	Ω statuses( const EditResult& r )ι->vector<StatusCode>{ return r.Results; }

	//March 7 archived, March 8 live:  speed 1@+1s, 2@+2s and temp 10@+2s on the 7th, speed 3@+2min on the 8th in its file and
	//4@+3min still in the buffer, as ReadTests' Reads has them.
	struct Edits : GatewayFiles{
		α TwoDays()ε->void{
			Pump = AddGroup();
			Speed = Join( *Pump, "Pump1.Speed" );
			Temp = Join( *Pump, "Pump1.Temp" );
			DataChange( *Pump, Speed, 1, T0+1s );
			DataChange( *Pump, Speed, 2, T0+2s );
			DataChange( *Pump, Temp, 10, T0+2s );
			EXPECT_TRUE( Flush(*Pump) );
			Time->AdvanceTo( Eighth+1min );
			Settle( *Pump );
			DataChange( *Pump, Speed, 3, Eighth+2min );
			EXPECT_TRUE( Flush(*Pump) );
			Time->AdvanceTo( Eighth+3min );
			DataChange( *Pump, Speed, 4, Eighth+3min );
		}
		α Edit( vector<EditDetails> details )ε->vector<EditResult>{ return BlockAny( Pump->Edit(move(details), Admin) ); }
		α All( ReadRequest request, vector<uint>* pages=nullptr )ε->vector<ReadValue>{ return readAll( *Pump, move(request), pages ); }
		α Mods( Day day )Ι->fs::path{ return Path()/DayDirectory( day )/( Pump->Name()+".mods.binpb" ); }
		//The start value of index's last preamble record in day's file, as a reader takes it:  NoStart for a record with
		//none, and Unlisted when the file has no such record.
		using StartValue = optional<optional<double>>;
		const StartValue NoStart{ optional<double>{} };
		const StartValue Unlisted{};
		α Start( Day day, NodeIndex index )ε->StartValue{
			StartValue y;
			for( let& r : readFile(File(*Pump, day)) ){
				if( isPreamble(r, index, day) )
					y = r.node_added().has_start() ? optional<double>{ r.node_added().start().value().double_value() } : nullopt;
			}
			return y;
		}
		const TimePoint T0{ Time->Now() };
		const TimePoint Eighth{ sys_days{March8} };
		sp<Group> Pump;
		NodeIndex Speed{}, Temp{};
	};

	//Each entry's values against the day's records, the edits before applied:  INSERT refuses a time that holds one,
	//REPLACE one that holds none, UPDATE does either, and the deletes take every record at their times.  The buffer is
	//flushed first, so a value still in it can be replaced.  No value file is rewritten for an edit.
	TEST_F( Edits, InsertReplaceUpdateDelete ){
		TwoDays();
		auto results = Edit( {UpdateData{Speed, UA_PERFORMUPDATETYPE_INSERT, {Reading(1.5, T0+1500ms), Reading(1.1, T0+1s)}}} );
		ASSERT_EQ( results.size(), 1 );
		EXPECT_EQ( results[0].Status, UA_STATUSCODE_GOOD );
		EXPECT_EQ( statuses(results[0]), (vector<StatusCode>{UA_STATUSCODE_GOODENTRYINSERTED, UA_STATUSCODE_BADENTRYEXISTS}) );
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(T0), .End=ticks(Eighth+4min)})), (vector<double>{1, 1.5, 2, 3, 4}) );
		EXPECT_TRUE( fs::exists(Mods(March7)) );
		EXPECT_FALSE( fs::exists(Mods(March8)) );
		EXPECT_TRUE( Pump->Buffer().empty() );//flushed first.

		results = Edit( {UpdateData{Speed, UA_PERFORMUPDATETYPE_REPLACE, {Reading(2.5, T0+2s), Reading(9, T0+2500ms)}}, UpdateData{Speed, UA_PERFORMUPDATETYPE_UPDATE, {Reading(4.5, Eighth+3min), Reading(5, Eighth+4min)}}} );
		ASSERT_EQ( results.size(), 2 );
		EXPECT_EQ( statuses(results[0]), (vector<StatusCode>{UA_STATUSCODE_GOODENTRYREPLACED, UA_STATUSCODE_BADNOENTRYEXISTS}) );
		EXPECT_EQ( statuses(results[1]), (vector<StatusCode>{UA_STATUSCODE_GOODENTRYREPLACED, UA_STATUSCODE_GOODENTRYINSERTED}) );
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(T0), .End=ticks(Eighth+5min)})), (vector<double>{1, 1.5, 2.5, 3, 4.5, 5}) );

		results = Edit( {DeleteAtTime{Speed, {ticks(T0+1s), ticks(T0+1250ms)}}, DeleteRaw{Temp, ticks(T0), ticks(T0+1s)}} );
		ASSERT_EQ( results.size(), 2 );
		EXPECT_EQ( statuses(results[0]), (vector<StatusCode>{UA_STATUSCODE_GOOD, UA_STATUSCODE_BADNOENTRYEXISTS}) );
		EXPECT_EQ( results[1].Status, UA_STATUSCODE_BADNODATA );
		EXPECT_TRUE( results[1].Results.empty() );
		results = Edit( {DeleteRaw{Speed, ticks(T0+1500ms), ticks(Eighth+3min)}} );
		EXPECT_EQ( results[0].Status, UA_STATUSCODE_GOOD );
		EXPECT_EQ( doubles(All({.Nodes={Speed, Temp}, .Start=ticks(T0), .End=ticks(Eighth+5min)})), (vector<double>{10, 5}) );
		EXPECT_EQ( Edit({DeleteRaw{Speed, ticks(T0+1500ms), ticks(Eighth+3min)}})[0].Status, UA_STATUSCODE_BADNODATA );
		EXPECT_EQ( generation(File(*Pump, March7)), 1 );
		EXPECT_EQ( generation(File(*Pump, March8)), 0 );

		//The modified values:  by the time they target, then the order the edits were made, each the value it replaced, or
		//inserted, with who made it and when.
		let modified = All( {.Nodes={Speed, Temp}, .Start=ticks(T0), .End=ticks(Eighth+5min), .Modified=true} );
		EXPECT_EQ( doubles(modified), (vector<double>{1, 1.5, 1.5, 2, 2.5, 3, 4, 4.5, 5}) );
		using enum Proto::UpdateType;
		EXPECT_EQ( types(modified), (vector<Proto::UpdateType>{UPDATE_TYPE_DELETE, UPDATE_TYPE_INSERT, UPDATE_TYPE_DELETE, UPDATE_TYPE_REPLACE, UPDATE_TYPE_DELETE, UPDATE_TYPE_DELETE, UPDATE_TYPE_UPDATE, UPDATE_TYPE_DELETE, UPDATE_TYPE_UPDATE}) );
		EXPECT_EQ( bounds(modified), vector<bool>(9, false) );
		for( let& v : modified ){
			ASSERT_TRUE( v.Modification );
			EXPECT_EQ( v.Modification->UserName, "admin" );
			EXPECT_EQ( v.Modification->IdentityId, 7 );
			EXPECT_EQ( v.Modification->Time, ticks(Time->Now()) );
			EXPECT_EQ( v.Value.node_index(), Speed );
		}
		EXPECT_EQ( modified[1].Value.source_ts(), ticks(T0+1500ms) );
		EXPECT_EQ( modified[6].Value.server_ts(), ticks(Eighth+3min+5ms) );//the original, as collected.
	}

	//A modified read pages by time and count as a raw one does, reads back in reverse, and returns no bounds.
	TEST_F( Edits, ModifiedReadsPage ){
		TwoDays();
		Edit( {UpdateData{Speed, UA_PERFORMUPDATETYPE_INSERT, {Reading(1.5, T0+1500ms)}}, UpdateData{Temp, UA_PERFORMUPDATETYPE_REPLACE, {Reading(11, T0+2s)}}} );
		Edit( {DeleteRaw{Speed, ticks(T0), ticks(Eighth+3min)}} );
		vector<uint> pages;
		let all = All( {.Nodes={Speed, Temp}, .Start=ticks(T0), .End=ticks(Eighth+5min), .Modified=true, .Limit=2}, &pages );
		EXPECT_EQ( doubles(all), (vector<double>{1, 1.5, 1.5, 10, 2, 3, 4}) );//at one time, in the order the edits were made.
		EXPECT_EQ( pages, (vector<uint>{2, 2, 2, 1}) );
		pages.clear();
		let reverse = All( {.Nodes={Speed, Temp}, .Start=ticks(Eighth+5min), .End=ticks(T0), .Modified=true, .Limit=3}, &pages );
		EXPECT_EQ( doubles(reverse), (vector<double>{4, 3, 2, 10, 1.5, 1.5, 1}) );
		EXPECT_EQ( pages, (vector<uint>{3, 3, 1}) );
		EXPECT_EQ( doubles(All({.Nodes={Temp}, .End=ticks(Eighth+5min), .Modified=true})), (vector<double>{10}) );
		EXPECT_TRUE( All({.Nodes={Temp}, .Start=ticks(T0+3s), .Modified=true}).empty() );
		let none = Pump->Read( {.Nodes={Temp}, .Start=ticks(Eighth), .End=ticks(Eighth+1h), .Modified=true} );
		EXPECT_TRUE( none.NoData );
		EXPECT_THROW( Pump->Read({.Nodes={Speed}, .Start=ticks(T0), .End=ticks(Eighth), .Bounds=true, .Modified=true}), UAException );
		//Its continuation is its own:  a raw read refuses it.
		auto page = Pump->Read( {.Nodes={Speed, Temp}, .Start=ticks(T0), .End=ticks(Eighth+5min), .Modified=true, .Limit=1} );
		ASSERT_FALSE( page.Continuation.empty() );
		EXPECT_THROW( Pump->Read({.Nodes={Speed, Temp}, .Start=ticks(T0), .End=ticks(Eighth+5min), .Limit=1, .Continuation=page.Continuation}), UAException );
	}

	//A range delete over many records, with no other node's at their times, empties each time in turn:  a read over them,
	//and the next edit's planner, step past each in one frame.
	TEST_F( Edits, LongDelete ){
		Pump = AddGroup();
		Speed = Join( *Pump, "Pump1.Speed" );
		constexpr uint count{ 50'000 };
		for( uint i=0; i<=count; ++i )
			DataChange( *Pump, Speed, (double)i, T0+i*1ms );
		EXPECT_TRUE( Flush(*Pump) );
		let range = DeleteRaw{ Speed, ticks(T0+1ms), ticks(T0+(count-1)*1ms) };
		EXPECT_EQ( Edit({range})[0].Status, UA_STATUSCODE_GOOD );
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(T0), .End=ticks(T0+1h)})), (vector<double>{0, (double)count}) );
		EXPECT_EQ( Edit({range})[0].Status, UA_STATUSCODE_BADNODATA );
	}

	//A stored NaN is the record its modification's original copies, so an edit of it applies.
	TEST_F( Edits, NaN ){
		TwoDays();
		let nan = std::numeric_limits<double>::quiet_NaN();
		DataChange( *Pump, Temp, nan, Eighth+3min );
		DataChange( *Pump, Temp, nan, Eighth+4min );
		EXPECT_EQ( statuses(Edit({UpdateData{Temp, UA_PERFORMUPDATETYPE_REPLACE, {Reading(11, Eighth+3min)}}})[0]), (vector<StatusCode>{UA_STATUSCODE_GOODENTRYREPLACED}) );
		EXPECT_EQ( statuses(Edit({DeleteAtTime{Temp, {ticks(Eighth+4min)}}})[0]), (vector<StatusCode>{UA_STATUSCODE_GOOD}) );
		EXPECT_EQ( doubles(All({.Nodes={Speed, Temp}, .Start=ticks(Eighth), .End=ticks(Eighth+5min)})), (vector<double>{3, 4, 11}) );
		let modified = All( {.Nodes={Temp}, .Start=ticks(Eighth), .End=ticks(Eighth+5min), .Modified=true} );
		using enum Proto::UpdateType;
		EXPECT_EQ( types(modified), (vector<Proto::UpdateType>{UPDATE_TYPE_REPLACE, UPDATE_TYPE_DELETE}) );
		for( let v : doubles(modified) )
			EXPECT_TRUE( std::isnan(v) ) << v;
	}

	//What an edit refuses outright:  a node that isn't a member, a value with no source timestamp or one no day holds, a
	//range the wrong way round, REMOVE, and a value no file can hold.
	TEST_F( Edits, Refusals ){
		TwoDays();
		Value unstamped{ Reading(1) };
		Value diagnostic{ Reading(1, T0+5s) };
		UA_DiagnosticInfo info; UA_DiagnosticInfo_init( &info );
		UA_Variant_clear( &diagnostic.value );
		UA_Variant_setScalarCopy( &diagnostic.value, &info, &UA_TYPES[UA_TYPES_DIAGNOSTICINFO] );
		let results = Edit( {
			UpdateData{999, UA_PERFORMUPDATETYPE_INSERT, {Reading(1, T0+5s)}},
			UpdateData{Speed, UA_PERFORMUPDATETYPE_INSERT, {unstamped, Reading(1, TimePoint{sys_days{1600y/January/1}}), diagnostic}},
			UpdateData{Speed, UA_PERFORMUPDATETYPE_REMOVE, {Reading(1, T0+5s)}},
			DeleteRaw{Speed, ticks(T0+2s), ticks(T0+1s)},
			DeleteAtTime{Temp, {ticks(TimePoint{sys_days{1600y/January/1}})}}
		} );
		ASSERT_EQ( results.size(), 5 );
		EXPECT_EQ( results[0].Status, UA_STATUSCODE_BADNODEIDUNKNOWN );
		EXPECT_EQ( results[1].Status, UA_STATUSCODE_GOOD );
		EXPECT_EQ( statuses(results[1]), (vector<StatusCode>{UA_STATUSCODE_BADINVALIDTIMESTAMPARGUMENT, UA_STATUSCODE_BADINVALIDTIMESTAMPARGUMENT, UA_STATUSCODE_BADNOTSUPPORTED}) );
		EXPECT_EQ( results[2].Status, UA_STATUSCODE_BADHISTORYOPERATIONUNSUPPORTED );
		EXPECT_EQ( results[3].Status, UA_STATUSCODE_BADINVALIDARGUMENT );
		EXPECT_EQ( statuses(results[4]), (vector<StatusCode>{UA_STATUSCODE_BADINVALIDTIMESTAMPARGUMENT}) );
		EXPECT_FALSE( fs::exists(Mods(March7)) );
		EXPECT_EQ( doubles(All({.Nodes={Speed, Temp}, .Start=ticks(T0), .End=ticks(Eighth+5min)})), (vector<double>{1, 2, 10, 3, 4}) );
	}

	//An edit that targets a day with no value file makes it first, with its preamble alone, an archive for a day past;
	//the day's start values come from the walk back, and the later files' start values the record reaches are corrected,
	//an archive's by its rewrite.
	TEST_F( Edits, InsertMakesTheDayFile ){
		TwoDays();
		const TimePoint fifth{ sys_days{2026y/March/5}+1h };
		let results = Edit( {UpdateData{Speed, UA_PERFORMUPDATETYPE_INSERT, {Reading(0.5, fifth)}}} );
		EXPECT_EQ( statuses(results[0]), (vector<StatusCode>{UA_STATUSCODE_GOODENTRYINSERTED}) );
		let day = 2026y/March/5;
		ASSERT_TRUE( fs::exists(File(*Pump, day)) );
		EXPECT_TRUE( fs::exists(Mods(day)) );
		EXPECT_EQ( generation(File(*Pump, day)), 1 );
		EXPECT_EQ( Start(day, Speed), NoStart );//nothing before it.
		EXPECT_EQ( Start(day, Temp), NoStart );
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(sys_days{day}), .End=ticks(Eighth+5min)})), (vector<double>{0.5, 1, 2, 3, 4}) );
		EXPECT_EQ( Pump->Earliest(), DayStart(day, utc()) );
		EXPECT_EQ( generation(File(*Pump, March7)), 2 );//its start value for speed was none, and is now the 5th's record.
		EXPECT_EQ( Start(March7, Speed), StartValue{0.5} );
		EXPECT_EQ( Start(March7, Temp), NoStart );
		EXPECT_EQ( generation(File(*Pump, March8)), 0 );//its start value, the 7th's last, is after the record.
		EXPECT_EQ( Start(March8, Speed), StartValue{2} );
		let sixth = All( {.Nodes={Speed}, .Start=ticks(sys_days{March6}+1h), .End=ticks(sys_days{March6}+2h), .Bounds=true} );
		ASSERT_EQ( sixth.size(), 2 );
		EXPECT_TRUE( isBound(sixth[0], Speed, 0.5, fifth) );
		EXPECT_TRUE( isBound(sixth[1], Speed, 1, T0+1s) );
	}

	//Deleting the record a later live file's start value copies appends a corrected preamble record to it, which a reader
	//takes in the first's place:  the record before it, or none when there is none left.
	TEST_F( Edits, DeleteMovesTheStartValueBack ){
		TwoDays();
		EXPECT_EQ( Start(March8, Speed), StartValue{2} );
		EXPECT_EQ( statuses(Edit({DeleteAtTime{Speed, {ticks(T0+2s)}}})[0]), (vector<StatusCode>{UA_STATUSCODE_GOOD}) );
		EXPECT_EQ( Start(March8, Speed), StartValue{1} );
		EXPECT_EQ( Start(March8, Temp), StartValue{10} );
		auto v = All( {.Nodes={Speed}, .Start=ticks(Eighth+1min), .End=ticks(Eighth+1min+30s), .Bounds=true} );
		ASSERT_EQ( v.size(), 2 );
		EXPECT_TRUE( isBound(v[0], Speed, 1, T0+1s) );
		EXPECT_TRUE( isBound(v[1], Speed, 3, Eighth+2min) );
		EXPECT_EQ( Edit({DeleteRaw{Speed, ticks(T0), ticks(T0+1s)}})[0].Status, UA_STATUSCODE_GOOD );
		EXPECT_EQ( Start(March8, Speed), NoStart );
		v = All( {.Nodes={Speed}, .Start=ticks(Eighth+1min), .End=ticks(Eighth+1min+30s), .Bounds=true} );
		ASSERT_EQ( v.size(), 2 );
		EXPECT_TRUE( notFound(v[0], Speed, Eighth+1min) );
		EXPECT_EQ( generation(File(*Pump, March8)), 0 );
	}

	//An edit at or after a node's newest record moves it, which the next file made takes as its start value, after a
	//restart too:  the newest is read through the edits when a modifications file is among the files the start reads.
	TEST_F( Edits, EditsMoveTheNewest ){
		TwoDays();
		let flow = Join( *Pump, "Pump1.Flow" );//whose values bring the new days, so neither edited node gets the stop's marker.
		Edit( {DeleteRaw{Speed, ticks(Eighth+3min), ticks(Eighth+3min)}} );//the newest, flushed first.
		Time->AdvanceTo( sys_days{March9}+1min );
		Settle( *Pump );
		DataChange( *Pump, Temp, 11, sys_days{March9}+2min );
		DataChange( *Pump, flow, 100, sys_days{March9}+2min );
		EXPECT_TRUE( Flush(*Pump) );
		EXPECT_EQ( Start(March9, Speed), StartValue{3} );
		Edit( {UpdateData{Speed, UA_PERFORMUPDATETYPE_INSERT, {Reading(7, sys_days{March9}+10min)}}} );
		Edit( {UpdateData{Temp, UA_PERFORMUPDATETYPE_REPLACE, {Reading(11.5, sys_days{March9}+2min)}}} );
		Edit( {DeleteRaw{Speed, ticks(T0), ticks(T0+2s)}} );//before the newest:  it stays.
		let members = vector<Member>{ {Node("Pump1.Speed"), {}, Speed}, {Node("Pump1.Temp"), {}, Temp}, {Node("Pump1.Flow"), {}, flow} };
		let name = Pump->Name();
		Restart();
		Pump = Rejoin( name, members );
		EXPECT_EQ( doubles(All({.Nodes={Speed, Temp}, .Start=ticks(T0), .End=ticks(sys_days{March9}+1h)})), (vector<double>{10, 3, 11.5, 7}) );
		Time->AdvanceTo( sys_days{March10}+1min );
		Settle( *Pump );
		DataChange( *Pump, flow, 101, sys_days{March10}+2min );
		EXPECT_TRUE( Flush(*Pump) );
		EXPECT_EQ( Start(March10, Speed), StartValue{7} );
		EXPECT_EQ( Start(March10, Temp), StartValue{11.5} );
		auto v = All( {.Nodes={Speed, Temp}, .Start=ticks(sys_days{March10}+1min), .End=ticks(sys_days{March10}+1min+30s), .Bounds=true} );
		ASSERT_EQ( v.size(), 4 );
		EXPECT_TRUE( isBound(v[0], Temp, 11.5, sys_days{March9}+2min) );
		EXPECT_TRUE( isBound(v[1], Speed, 7, sys_days{March9}+10min) );
	}

	//A correction reaches an archive by its rewrite, and goes on through each later file whose start value is at or before
	//the change, stopping at the first whose start value is after it.
	TEST_F( Edits, ArchivePreambleIsCorrectedInPlace ){
		TwoDays();
		Time->AdvanceTo( sys_days{March9}+1min );
		Settle( *Pump );
		DataChange( *Pump, Temp, 11, sys_days{March9}+2min );
		EXPECT_TRUE( Flush(*Pump) );
		EXPECT_EQ( generation(File(*Pump, March8)), 1 );
		EXPECT_EQ( Start(March9, Speed), StartValue{4} );
		EXPECT_EQ( Edit({DeleteRaw{Speed, ticks(Eighth), ticks(Eighth+4min)}})[0].Status, UA_STATUSCODE_GOOD );//the 8th's, which the 9th copied.
		EXPECT_EQ( Start(March9, Speed), StartValue{2} );
		EXPECT_EQ( generation(File(*Pump, March8)), 1 );//its own start value is before the change.
		EXPECT_EQ( Edit({DeleteAtTime{Speed, {ticks(T0+2s)}}})[0].Results[0], UA_STATUSCODE_GOOD );//the 7th's, which both copy now.
		EXPECT_EQ( generation(File(*Pump, March8)), 2 );
		EXPECT_EQ( Start(March8, Speed), StartValue{1} );
		EXPECT_EQ( Start(March8, Temp), StartValue{10} );
		EXPECT_EQ( Start(March9, Speed), StartValue{1} );
		EXPECT_EQ( generation(File(*Pump, March9)), 0 );
		auto v = All( {.Nodes={Speed}, .Start=ticks(sys_days{March9}+1min), .End=ticks(sys_days{March9}+2min), .Bounds=true} );
		ASSERT_EQ( v.size(), 2 );
		EXPECT_TRUE( isBound(v[0], Speed, 1, T0+1s) );
		EXPECT_TRUE( notFound(v[1], Speed, sys_days{March9}+2min) );
		//The 8th's archive still reads whole, its records with their modifications.
		EXPECT_EQ( doubles(All({.Nodes={Speed, Temp}, .Start=ticks(T0), .End=ticks(sys_days{March9}+1h)})), (vector<double>{1, 10, 11}) );
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(Eighth), .End=ticks(sys_days{March9}), .Modified=true})), (vector<double>{3, 4}) );
	}

	//A late record corrects the later files' start values the same way, and only where the record is after the one they
	//copy.
	TEST_F( Edits, LateRecordCorrectsTheStartValue ){
		TwoDays();
		DataChange( *Pump, Speed, 1.5, T0+1500ms );//before the 7th's last, which the 8th copies:  nothing to correct.
		EXPECT_TRUE( Flush(*Pump) );
		EXPECT_EQ( generation(File(*Pump, March7)), 2 );
		EXPECT_EQ( Start(March8, Speed), StartValue{2} );
		EXPECT_EQ( readFile(File(*Pump, March8)).size(), 5 );//FileStart, two preamble records, 3 and 4.
		DataChange( *Pump, Speed, 2.5, T0+2500ms );
		EXPECT_TRUE( Flush(*Pump) );
		EXPECT_EQ( Start(March8, Speed), StartValue{2.5} );
		EXPECT_EQ( Start(March8, Temp), StartValue{10} );
		auto v = All( {.Nodes={Speed, Temp}, .Start=ticks(Eighth+1min), .End=ticks(Eighth+1min+30s), .Bounds=true} );
		ASSERT_EQ( v.size(), 4 );
		EXPECT_TRUE( isBound(v[0], Temp, 10, T0+2s) );
		EXPECT_TRUE( isBound(v[1], Speed, 2.5, T0+2500ms) );
	}

	//A file made after its day, by a late record, takes its start values from the walk back through the files, the edits
	//applied, since the node's newest record is after the day; and the record corrects the later files whose start value
	//it is now, an archive's by its rewrite, stopping at the first whose start value is after it.
	TEST_F( Edits, LateFileWalksBack ){
		Pump = AddGroup();
		Speed = Join( *Pump, "Pump1.Speed" );
		Temp = Join( *Pump, "Pump1.Temp" );
		constexpr Day March3{ 2026y/March/3 }, March4{ 2026y/March/4 }, March5{ 2026y/March/5 };
		DataChange( *Pump, Speed, 0.25, sys_days{March4}+1h );
		DataChange( *Pump, Speed, 0.5, sys_days{March6}+1h );
		EXPECT_TRUE( Flush(*Pump) );//the 4th and the 6th as archives from the start, and the 7th live with the two joins.
		EXPECT_EQ( Start(March4, Speed), NoStart );
		EXPECT_EQ( Start(March6, Speed), StartValue{0.25} );
		EXPECT_EQ( Start(March7, Speed), StartValue{0.5} );
		Time->AdvanceTo( Eighth+1min );
		Settle( *Pump );
		DataChange( *Pump, Speed, 2, Eighth+2min );
		DataChange( *Pump, Temp, 10, Eighth+2min );
		EXPECT_TRUE( Flush(*Pump) );
		EXPECT_EQ( Start(March8, Speed), StartValue{0.5} );

		DataChange( *Pump, Speed, 0.4, sys_days{March5}+1h );//the 5th's, which has no file.
		EXPECT_TRUE( Flush(*Pump) );
		EXPECT_EQ( generation(File(*Pump, March5)), 1 );
		EXPECT_EQ( Start(March5, Speed), StartValue{0.25} );//the walk back:  the newest is after the day.
		EXPECT_EQ( Start(March5, Temp), NoStart );
		EXPECT_EQ( generation(File(*Pump, March6)), 2 );
		EXPECT_EQ( Start(March6, Speed), StartValue{0.4} );
		EXPECT_EQ( Start(March7, Speed), StartValue{0.5} );//after the record:  the walk stopped here.
		EXPECT_EQ( Start(March8, Speed), StartValue{0.5} );

		Edit( {DeleteAtTime{Speed, {ticks(sys_days{March6}+1h)}}} );
		EXPECT_EQ( Start(March7, Speed), StartValue{0.4} );
		EXPECT_EQ( Start(March8, Speed), StartValue{0.4} );
		Edit( {DeleteAtTime{Speed, {ticks(sys_days{March5}+1h)}}} );
		EXPECT_EQ( Start(March6, Speed), StartValue{0.25} );
		EXPECT_EQ( Start(March7, Speed), StartValue{0.25} );
		EXPECT_EQ( Start(March8, Speed), StartValue{0.25} );
		Edit( {UpdateData{Speed, UA_PERFORMUPDATETYPE_INSERT, {Reading(0.3, sys_days{March4}+2h)}}} );
		for( let day : {March5, March6, March7, March8} )
			EXPECT_EQ( Start(day, Speed), StartValue{0.3} ) << DayDirectory( day ).string();
		DataChange( *Pump, Speed, 0.1, sys_days{March3}+1h );//a file made after its day, with nothing before it.
		EXPECT_TRUE( Flush(*Pump) );
		EXPECT_EQ( Start(March3, Speed), NoStart );
		EXPECT_EQ( Start(March4, Speed), StartValue{0.1} );
		EXPECT_EQ( Start(March5, Speed), StartValue{0.3} );
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(sys_days{March3}), .End=ticks(Eighth+1h)})), (vector<double>{0.1, 0.25, 0.3, 2}) );
	}

	//A walk back that can't read a file through fails the day whose file it would start, as any other I/O error does:  an
	//edit's value there, and a late record, which the next flush writes once the file reads.
	TEST_F( Edits, UnreadableWalkBackFailsTheDay ){
		Pump = AddGroup();
		Speed = Join( *Pump, "Pump1.Speed" );
		constexpr Day March4{ 2026y/March/4 }, March5{ 2026y/March/5 };
		DataChange( *Pump, Speed, 0.25, sys_days{March4}+1h );
		DataChange( *Pump, Speed, 1, T0+1s );
		EXPECT_TRUE( Flush(*Pump) );
		let fourth = File( *Pump, March4 );
		let aside = fs::path{ fourth.string()+".aside" };
		fs::rename( fourth, aside );
		fs::create_directory( fourth );

		EXPECT_EQ( statuses(Edit({UpdateData{Speed, UA_PERFORMUPDATETYPE_INSERT, {Reading(0.3, sys_days{March5}+2h)}}})[0]), (vector<StatusCode>{UA_STATUSCODE_BADUNEXPECTEDERROR}) );
		DataChange( *Pump, Speed, 0.4, sys_days{March5}+1h );
		EXPECT_FALSE( Flush(*Pump) );
		EXPECT_FALSE( fs::exists(File(*Pump, March5)) );

		fs::remove( fourth );
		fs::rename( aside, fourth );
		EXPECT_TRUE( Flush(*Pump) );
		EXPECT_EQ( Start(March5, Speed), StartValue{0.25} );
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(sys_days{March4}), .End=ticks(T0+1min)})), (vector<double>{0.25, 0.4, 1}) );
	}

	//A correction whose walk back can't read a file through is left undone, as one whose file can't be prepared is:  the
	//edit stands, and the next record or edit that reaches the start value corrects it.
	TEST_F( Edits, UnreadableWalkBackLeavesTheCorrection ){
		Pump = AddGroup();
		Speed = Join( *Pump, "Pump1.Speed" );
		constexpr Day March4{ 2026y/March/4 };
		DataChange( *Pump, Speed, 0.25, sys_days{March4}+1h );
		DataChange( *Pump, Speed, 0.5, sys_days{March6}+1h );
		DataChange( *Pump, Speed, 1, T0+1s );
		EXPECT_TRUE( Flush(*Pump) );
		EXPECT_EQ( Start(March7, Speed), StartValue{0.5} );
		let fourth = File( *Pump, March4 );
		let aside = fs::path{ fourth.string()+".aside" };
		fs::rename( fourth, aside );
		fs::create_directory( fourth );

		//The 7th copies the deleted record, and the walk back from it reads past the 6th to the 4th.
		EXPECT_EQ( statuses(Edit({DeleteAtTime{Speed, {ticks(sys_days{March6}+1h)}}})[0]), (vector<StatusCode>{UA_STATUSCODE_GOOD}) );
		EXPECT_EQ( Start(March7, Speed), StartValue{0.5} );

		fs::remove( fourth );
		fs::rename( aside, fourth );
		EXPECT_EQ( statuses(Edit({UpdateData{Speed, UA_PERFORMUPDATETYPE_INSERT, {Reading(0.6, sys_days{March6}+2h)}}})[0]), (vector<StatusCode>{UA_STATUSCODE_GOODENTRYINSERTED}) );
		EXPECT_EQ( Start(March7, Speed), StartValue{0.6} );
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(sys_days{March4}), .End=ticks(T0+1min)})), (vector<double>{0.25, 0.6, 1}) );
	}

	//A start that can't read the newest of a node an edit names keeps the newest the files fold for it, and the group
	//starts:  the other nodes the edits name are read through.
	TEST_F( Edits, UnreadableWalkBackAtStart ){
		Pump = AddGroup();
		Speed = Join( *Pump, "Pump1.Speed" );
		Temp = Join( *Pump, "Pump1.Temp" );
		let flow = Join( *Pump, "Pump1.Flow" );//whose values bring the new day, so neither edited node gets the stop's marker.
		constexpr Day March4{ 2026y/March/4 };
		DataChange( *Pump, Speed, 0.25, sys_days{March4}+1h );
		DataChange( *Pump, Speed, 0.5, T0+1s );
		DataChange( *Pump, Temp, 10, T0+1s );
		EXPECT_TRUE( Flush(*Pump) );
		Edit( {DeleteAtTime{Speed, {ticks(T0+1s)}}, UpdateData{Temp, UA_PERFORMUPDATETYPE_REPLACE, {Reading(11, T0+1s)}}} );//a modifications file the start reads.
		let members = vector<Member>{ {Node("Pump1.Speed"), {}, Speed}, {Node("Pump1.Temp"), {}, Temp}, {Node("Pump1.Flow"), {}, flow} };
		let name = Pump->Name();
		let fourth = File( *Pump, March4 );
		let aside = fs::path{ fourth.string()+".aside" };
		Restart();
		fs::rename( fourth, aside );
		fs::create_directory( fourth );
		Pump = Rejoin( name, members );//Speed's walk back passes its deleted record for the 4th.

		fs::remove( fourth );
		fs::rename( aside, fourth );
		Time->AdvanceTo( Eighth+1min );
		Settle( *Pump );
		DataChange( *Pump, flow, 100, Eighth+2min );
		EXPECT_TRUE( Flush(*Pump) );
		EXPECT_EQ( Start(March8, Speed), StartValue{0.5} );//folded from the 7th's value file:  the edits unread.
		EXPECT_EQ( Start(March8, Temp), StartValue{11} );
	}

	//A start reads through the edits only the newest of each node they name:  one they don't name keeps the newest the
	//files fold.  Here that is Speed's start value in the 7th's preamble, which a walk back, reading records alone, would
	//drop, since the 4th's file that held the record is purged.
	TEST_F( Edits, StartWalksOnlyTheNodesEditsName ){
		Pump = AddGroup();
		Speed = Join( *Pump, "Pump1.Speed" );
		Temp = Join( *Pump, "Pump1.Temp" );
		let flow = Join( *Pump, "Pump1.Flow" );//whose values bring the new day, so neither node gets the stop's marker.
		constexpr Day March4{ 2026y/March/4 };
		DataChange( *Pump, Speed, 0.25, sys_days{March4}+1h );
		DataChange( *Pump, Temp, 10, T0+1s );
		EXPECT_TRUE( Flush(*Pump) );
		Edit( {UpdateData{Temp, UA_PERFORMUPDATETYPE_REPLACE, {Reading(11, T0+1s)}}} );
		let members = vector<Member>{ {Node("Pump1.Speed"), {}, Speed}, {Node("Pump1.Temp"), {}, Temp}, {Node("Pump1.Flow"), {}, flow} };
		let name = Pump->Name();
		let fourth = File( *Pump, March4 );
		Restart();
		ASSERT_TRUE( fs::remove(fourth) );
		Pump = Rejoin( name, members );

		Time->AdvanceTo( Eighth+1min );
		Settle( *Pump );
		DataChange( *Pump, flow, 100, Eighth+2min );
		EXPECT_TRUE( Flush(*Pump) );
		EXPECT_EQ( Start(March8, Speed), StartValue{0.25} );
		EXPECT_EQ( Start(March8, Temp), StartValue{11} );
	}

	//The correction walk keeps no archive it reads only to stop at:  it reads that file's preamble alone, so a read of
	//the day after scans the file as it is then, here an older copy put back.
	TEST_F( Edits, WalkKeepsNoArchive ){
		Pump = AddGroup();
		Speed = Join( *Pump, "Pump1.Speed" );
		Temp = Join( *Pump, "Pump1.Temp" );
		constexpr Day March3{ 2026y/March/3 }, March5{ 2026y/March/5 };
		DataChange( *Pump, Speed, 0.25, sys_days{March3}+1h );
		DataChange( *Pump, Speed, 0.5, sys_days{March5}+1h );
		DataChange( *Pump, Temp, 9, sys_days{March6}+1h );
		DataChange( *Pump, Temp, 10, T0+1s );
		EXPECT_TRUE( Flush(*Pump) );
		let sixth = File( *Pump, March6 );
		let older = fs::path{ sixth.string()+".older" };
		fs::copy_file( sixth, older );
		DataChange( *Pump, Temp, 9.5, sys_days{March6}+2h );//the 6th rewritten, longer.
		EXPECT_TRUE( Flush(*Pump) );
		let members = vector<Member>{ {Node("Pump1.Speed"), {}, Speed}, {Node("Pump1.Temp"), {}, Temp} };
		let name = Pump->Name();
		Restart();
		Pump = Rejoin( name, members );

		Edit( {UpdateData{Speed, UA_PERFORMUPDATETYPE_INSERT, {Reading(0.3, sys_days{March3}+2h)}}} );//corrects the 5th, and stops at the 6th.
		EXPECT_EQ( Start(March5, Speed), StartValue{0.3} );
		EXPECT_EQ( Start(March6, Speed), StartValue{0.5} );
		fs::rename( older, sixth );
		EXPECT_EQ( doubles(All({.Nodes={Temp}, .Start=ticks(sys_days{March6}), .End=ticks(sys_days{March7})})), (vector<double>{9}) );
	}

	//An edit's records end with a checkpoint, so one a crash cuts off is dropped whole by the next start, and the next
	//edit's append cuts it from the file.
	TEST_F( Edits, TornEditIsDropped ){
		TwoDays();
		Edit( {UpdateData{Speed, UA_PERFORMUPDATETYPE_INSERT, {Reading(1.5, T0+1500ms)}}} );
		let whole = fs::file_size( Mods(March7) );
		{
			std::ofstream f{ Mods(March7), std::ios::binary | std::ios::app };
			f.write( "\0\0\0\0\0", 5 );
		}
		let members = vector<Member>{ {Node("Pump1.Speed"), {}, Speed}, {Node("Pump1.Temp"), {}, Temp} };
		let name = Pump->Name();
		Restart();
		Pump = Rejoin( name, members );
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(T0), .End=ticks(Eighth+5min)})), (vector<double>{1, 1.5, 2, 3, 4}) );
		EXPECT_EQ( fs::file_size(Mods(March7)), whole+5 );
		Edit( {UpdateData{Speed, UA_PERFORMUPDATETYPE_INSERT, {Reading(1.75, T0+1750ms)}}} );
		EXPECT_GT( fs::file_size(Mods(March7)), whole );
		EXPECT_EQ( readFile(Mods(March7)).size(), 5 );//FileStart, two preamble records, two modifications:  the tail is gone.
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(T0), .End=ticks(Eighth+5min)})), (vector<double>{1, 1.5, 1.75, 2, 3, 4}) );
	}

	//A day whose file can't be made fails its values, and the other days stand.
	TEST_F( Edits, UnwritableDayFailsItsValues ){
		TwoDays();
		save( Path()/"2026"/"3"/"9", "in the way" );
		let results = Edit( {UpdateData{Speed, UA_PERFORMUPDATETYPE_INSERT, {Reading(5, sys_days{March9}+1h), Reading(1.5, T0+1500ms)}}, DeleteRaw{Speed, ticks(sys_days{March9}), ticks(sys_days{March9}+2h)}} );
		EXPECT_EQ( statuses(results[0]), (vector<StatusCode>{UA_STATUSCODE_BADUNEXPECTEDERROR, UA_STATUSCODE_GOODENTRYINSERTED}) );
		EXPECT_EQ( results[1].Status, UA_STATUSCODE_BADNODATA );//no file holds the day.
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(T0), .End=ticks(sys_days{March10})})), (vector<double>{1, 1.5, 2, 3, 4}) );
		fs::remove( Path()/"2026"/"3"/"9" );
	}

	//A range delete over a day the flush before couldn't write answers Bad_UnexpectedError, whether the day has a file or
	//none yet, and whether or not another entry failed the day first:  the day's records wait in the buffer, out of its
	//reach.
	TEST_F( Edits, DeleteOverUnflushedDayFails ){
		TwoDays();//the 8th's 4 is still in the buffer.
		constexpr Day March5{ 2026y/March/5 };
		let eighth = File( *Pump, March8 );
		let aside = fs::path{ eighth.string()+".aside" };
		fs::rename( eighth, aside );
		fs::create_directory( eighth );//so the flush can't append the 4,
		save( Path()/"2026"/"3"/"5", "in the way" );//nor make the 5th.
		DataChange( *Pump, Speed, 0.5, sys_days{March5}+1h );
		let results = Edit( {
			UpdateData{Speed, UA_PERFORMUPDATETYPE_INSERT, {Reading(3.5, Eighth+150s)}},//fails the 8th first.
			DeleteRaw{Speed, ticks(Eighth), ticks(Eighth+5min)},
			DeleteRaw{Speed, ticks(sys_days{March5}), ticks(sys_days{March5}+2h)}
		} );
		ASSERT_EQ( results.size(), 3 );
		EXPECT_EQ( statuses(results[0]), (vector<StatusCode>{UA_STATUSCODE_BADUNEXPECTEDERROR}) );
		EXPECT_EQ( results[1].Status, UA_STATUSCODE_BADUNEXPECTEDERROR );
		EXPECT_EQ( results[2].Status, UA_STATUSCODE_BADUNEXPECTEDERROR );

		fs::remove( eighth );
		fs::rename( aside, eighth );
		fs::remove( Path()/"2026"/"3"/"5" );
		EXPECT_TRUE( Flush(*Pump) );
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(sys_days{March5}), .End=ticks(Eighth+5min)})), (vector<double>{0.5, 1, 2, 3, 4}) );
	}

	//A range delete when the days can't be listed answers Bad_UnexpectedError:  which of them hold its records isn't known.
	TEST_F( Edits, UnlistableDaysFailTheDelete ){
		TwoDays();
		EXPECT_TRUE( Flush(*Pump) );//the 4, so the edit's flush writes nothing.
		let year = Path()/"2026";
		fs::permissions( year, fs::perms::none, fs::perm_options::replace );
		std::error_code ec;
		if( fs::directory_iterator{year, ec}; !ec ){
			fs::permissions( year, fs::perms::owner_all, fs::perm_options::replace );
			GTEST_SKIP() << "This user can list a directory it can't read.";
		}
		let result = Edit( {DeleteRaw{Speed, ticks(T0), ticks(Eighth+5min)}} )[0];
		fs::permissions( year, fs::perms::owner_all, fs::perm_options::replace );
		EXPECT_EQ( result.Status, UA_STATUSCODE_BADUNEXPECTEDERROR );
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(T0), .End=ticks(Eighth+5min)})), (vector<double>{1, 2, 3, 4}) );
	}

	//A day's modifications file is scanned once, by the first read that serves it, not by every page:  here its torn tail,
	//which each scan says, is said once over a read of three pages.
	TEST_F( Edits, ModsScannedOnce ){
		TwoDays();
		Edit( {UpdateData{Speed, UA_PERFORMUPDATETYPE_INSERT, {Reading(1.5, T0+1500ms)}}} );
		let members = vector<Member>{ {Node("Pump1.Speed"), {}, Speed}, {Node("Pump1.Temp"), {}, Temp} };
		let name = Pump->Name();
		Restart();
		{
			std::ofstream f{ Mods(March7), std::ios::binary | std::ios::app };
			f.write( "\0\0\0\0\0", 5 );
		}
		Pump = Rejoin( name, members );
		if( !Logging::FindLogger<Logging::MemoryLog>() )
			Logging::AddLogger( mu<Logging::MemoryLog>() );
		Logging::ClearMemory();
		vector<uint> pages;
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(T0), .End=ticks(Eighth), .Limit=1}, &pages)), (vector<double>{1, 1.5, 2}) );
		EXPECT_GE( pages.size(), 3 );
		let path = Mods( March7 ).string();
		let said = Logging::Find( [&]( const Logging::Entry& e ){ return e.Message().contains(path) && e.Message().contains("reads serve only"); } );
		EXPECT_EQ( said.size(), 1 );
	}

	//A removed group refuses an edit, and so does one whose historian has stopped.
	TEST_F( Edits, RemovedGroupRefuses ){
		TwoDays();
		auto other = AddGroup();
		let flow = Join( *other, "Pump2.Flow" );
		Library->RemoveGroup( other->Name() );
		EXPECT_THROW( BlockAny(other->Edit({UpdateData{flow, UA_PERFORMUPDATETYPE_INSERT, {Reading(1, T0+5s)}}}, Admin)), Exception );
		auto group = Pump;
		Library.reset();
		EXPECT_THROW( BlockAny(group->Edit({UpdateData{Speed, UA_PERFORMUPDATETYPE_INSERT, {Reading(1, T0+5s)}}}, Admin)), Exception );
		Pump.reset();
	}

	//OpcServer's shape:  the one group, with the indexes it issued.
	TEST_F( ServerFiles, Edits ){
		let speed = Historize( "Pump1.Speed" );
		let first = Time->Now();
		SetValue( speed, 1 );
		EXPECT_TRUE( Flush(*Server) );
		const Writer operator1{ {{9}}, "operator" };
		let results = BlockAny( Server->Edit({UpdateData{speed, UA_PERFORMUPDATETYPE_REPLACE, {Reading(1.5, first)}}, UpdateData{speed, UA_PERFORMUPDATETYPE_INSERT, {Reading(2, first+1s)}}}, operator1) );
		ASSERT_EQ( results.size(), 2 );
		EXPECT_EQ( results[0].Results, (vector<StatusCode>{UA_STATUSCODE_GOODENTRYREPLACED}) );
		EXPECT_EQ( results[1].Results, (vector<StatusCode>{UA_STATUSCODE_GOODENTRYINSERTED}) );
		EXPECT_EQ( doubles(readAll(*Server, {.Nodes={speed}, .Start=ticks(first), .End=ticks(first+1min)})), (vector<double>{1.5, 2}) );
		let modified = readAll( *Server, {.Nodes={speed}, .Start=ticks(first), .End=ticks(first+1min), .Modified=true} );
		EXPECT_EQ( doubles(modified), (vector<double>{1, 2}) );
		ASSERT_TRUE( modified[0].Modification );
		EXPECT_EQ( modified[0].Modification->UserName, "operator" );
		EXPECT_EQ( modified[0].Modification->IdentityId, 9 );
		let mods = readFile( Path()/DayDirectory(March7)/"server.mods.binpb" );
		ASSERT_EQ( mods.size(), 4 );
		EXPECT_EQ( mods[0].file_start().next_node_index(), 2 );
		EXPECT_TRUE( isPreamble(mods[1], speed, March7) );
		EXPECT_FALSE( mods[1].node_added().has_start() );
	}
}