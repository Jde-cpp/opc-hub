//Day files and durability (#228):  the lock on hist.path, the two-slot .flushed file, the buffer's flush at 8 KB and at
//`delay`, what a full buffer drops and how its gap is marked, and each live file's runs.
#include "dayFiles.h"
#include <fstream>
#include <future>
#include <thread>
#include <jde/fwk/io/crc.h>
#include <jde/fwk/log/MemoryLog.h>
#ifndef _WIN32
	#include <fcntl.h>
	#include <sys/stat.h>
	#include <unistd.h>
#endif

#define let const auto

namespace Jde::Opc::Hist::Tests{
	using namespace std::chrono;
	using Proto::HistoryRecord;

	namespace{
		//A name at file's path that resolves to nothing, so the file reads as missing and can't be made, as when a parent of
		//it is renamed away.  False where this user can't make a symlink.
		Ω dangle( const fs::path& file )ι->bool{
			std::error_code ec;
			fs::create_symlink( "missing/target", file, ec );
			return !ec;
		}
	}

	//A record goes in the file of its source time's day in timeZone, under <yyyy>/<m>/<d> as the log names a day.
	TEST( DayTests, FiledByDayInTimeZone ){
		let& newYork = *locate_zone( "America/New_York" );
		let t = ticks( sys_days{2026y/March/8}+3h );//22:00 on the 7th in New York, whose clocks go forward on the 8th.
		EXPECT_EQ( DayOf(t, utc()), March8 );
		EXPECT_EQ( DayOf(t, newYork), March7 );
		EXPECT_EQ( StartOf(March8, utc()), ticks(sys_days{2026y/March/8}) );
		EXPECT_EQ( StartOf(March8, newYork), ticks(sys_days{2026y/March/8}+5h) );
		EXPECT_EQ( StartOf(March9, newYork), ticks(sys_days{2026y/March/9}+4h) );
		EXPECT_EQ( DayOf(StartOf(March9, newYork), newYork), March9 );
		EXPECT_EQ( DayOf(StartOf(March9, newYork)-1, newYork), March8 );
		EXPECT_EQ( DayDirectory(March7), fs::path{"2026"}/"3"/"7" );
	}

	//UA has no date before 1601 or after 9999, which a source's clock can still send.
	TEST( DayTests, TimesOutsideUAsRange ){
		EXPECT_EQ( DayOf(0, utc()), Day{1601y/January/1} );
		EXPECT_EQ( DayOf(-1, utc()), Day{1601y/January/1} );
		EXPECT_EQ( DayOf(std::numeric_limits<Ticks>::min(), utc()), Day{1601y/January/1} );
		EXPECT_EQ( DayOf(std::numeric_limits<Ticks>::max(), utc()), Day{9999y/December/31} );
	}

	//The files' day, by UA ticks, is Clock.cpp's:  Havana's midnight that DST skips and the one it repeats, as
	//ClockTests.MidnightSkippedAndRepeated has them.
	TEST( DayTests, TicksFollowTheClock ){
		let& tz = *locate_zone( "America/Havana" );
		for( let day : {Day{2026y/March/8}, Day{2026y/November/1}} ){
			let start = StartOf( day, tz );
			EXPECT_EQ( start, ticks(DayStart(day, tz)) );
			EXPECT_EQ( DayOf(start, tz), day );
			EXPECT_EQ( DayOf(start-1, tz), Day{sys_days{day}-days{1}} );
		}
	}

	struct FlushedTests : ::testing::Test{
		α SetUp()->void override{ fs::create_directories( File.parent_path() ); }
		α TearDown()->void override{
			fs::remove_all( File.parent_path() );
			std::error_code ec;
			fs::remove( File.parent_path().parent_path(), ec );
		}
		Ω Slot( TimePoint time, uint32_t sequence )ι->string{
			string y( Flushed::SlotSize, '\0' );
			let t = (uint64_t)ticks( time );
			for( uint i=0; i<8; ++i )
				y[i] = (char)( t>>(8*i) );
			for( uint i=0; i<4; ++i )
				y[8+i] = (char)( sequence>>(8*i) );
			let crc = IO::Crc::Calc32c( sv{y}.substr(0, 12) );
			for( uint i=0; i<4; ++i )
				y[12+i] = (char)( crc>>(8*i) );
			return y;
		}
		//As a flush writes it.
		Ω Write( Flushed& flushed, TimePoint time )ε->void{
			let slot = flushed.Next( time );
			BlockVoidAwait( slot.Write() );
			flushed.Wrote( slot );
		}
		const TimePoint Time{ sys_days{2026y/March/7}+17h };
		const fs::path File{ fs::current_path()/"hist-tests"/::testing::UnitTest::GetInstance()->current_test_info()->name()/"group.flushed" };
	};

	//Two 16-byte slots, written alternately in place:  the time in UA ticks, a sequence and a CRC-32C, little-endian.
	TEST_F( FlushedTests, AlternatesSlots ){
		{
			Flushed flushed{ File };
			EXPECT_FALSE( flushed.Time() );//no file:  never flushed.
			EXPECT_FALSE( fs::exists(File) );
			Write( flushed, Time );
			EXPECT_EQ( flushed.Time(), Time );
			EXPECT_EQ( contents(File), Slot(Time, 1) );
			Write( flushed, Time+1min );
			EXPECT_EQ( contents(File), Slot(Time, 1)+Slot(Time+1min, 2) );
			Write( flushed, Time+2min );
			EXPECT_EQ( contents(File), Slot(Time+2min, 3)+Slot(Time+1min, 2) );
			EXPECT_EQ( flushed.Time(), Time+2min );
		}
		Flushed read{ File };
		EXPECT_EQ( read.Time(), Time+2min );
		Write( read, Time+3min );//over the older slot.
		EXPECT_EQ( contents(File), Slot(Time+2min, 3)+Slot(Time+3min, 4) );
	}

	//A write torn at any byte loses that flush's time, not the file:  the reader falls back to the other slot, and the
	//next write goes over the torn one.
	TEST_F( FlushedTests, TornSlotFallsBack ){
		let before = Slot( Time, 1 )+Slot( Time+1min, 2 );
		let written = Slot( Time+2min, 3 );
		for( uint torn=0; torn<Flushed::SlotSize; ++torn ){
			SCOPED_TRACE( Ƒ("{} bytes of the slot written", torn) );
			save( File, written.substr(0, torn)+before.substr(torn) );
			Flushed flushed{ File };
			EXPECT_EQ( flushed.Time(), Time+1min );
			Write( flushed, Time+3min );
			EXPECT_EQ( contents(File), Slot(Time+3min, 3)+Slot(Time+1min, 2) );
		}
		save( File, written+before.substr(Flushed::SlotSize) );
		EXPECT_EQ( Flushed{File}.Time(), Time+2min );//whole.

		for( uint torn=0; torn<Flushed::SlotSize; ++torn ){//the second slot's first write, which grows the file.
			save( File, Slot(Time, 1)+Slot(Time+1min, 2).substr(0, torn) );
			EXPECT_EQ( Flushed{File}.Time(), Time ) << torn;
		}
	}

	TEST_F( FlushedTests, NoValidSlot ){
		if( !Logging::FindLogger<Logging::MemoryLog>() )
			Logging::AddLogger( mu<Logging::MemoryLog>() );
		for( let& bytes : {string{}, string(32, '\0'), string(32, 'x'), Slot(Time, 1).substr(0, 9)} ){
			save( File, bytes );
			Logging::ClearMemory();
			Flushed flushed{ File };
			EXPECT_FALSE( flushed.Time() );
			let warned = Logging::Find( [this]( const Logging::Entry& e ){ return e.Message().contains(File.string()); } );
			ASSERT_EQ( warned.size(), 1 );
			EXPECT_EQ( warned[0].Level, ELogLevel::Warning );
			Write( flushed, Time );
			EXPECT_EQ( Flushed{File}.Time(), Time );
		}
	}

	//A file that opens but can't be read, here a directory, is an error, not a group that never flushed:  libc++ reports
	//the failed read as the end of the file.
	TEST_F( FlushedTests, UnreadableThrows ){
		fs::create_directories( File );
		EXPECT_THROW( Flushed{File}, Exception );
		fs::remove( File );
	}

	//The sequence wraps, so the later slot is the one ahead by less than half its range.
	TEST_F( FlushedTests, SequenceWraps ){
		let last = std::numeric_limits<uint32_t>::max();
		save( File, Slot(Time+1min, 0)+Slot(Time, last) );
		Flushed flushed{ File };
		EXPECT_EQ( flushed.Time(), Time+1min );
		Write( flushed, Time+2min );
		EXPECT_EQ( contents(File), Slot(Time+1min, 0)+Slot(Time+2min, 1) );
		save( File, Slot(Time, last-1)+Slot(Time+1min, last) );
		Flushed wrapped{ File };
		Write( wrapped, Time+2min );
		EXPECT_EQ( contents(File), Slot(Time+2min, 0)+Slot(Time+1min, last) );
	}

	//Another historian on the same hist.path, a second gateway or an OpcServer whose path collides:  it runs without its
	//historian, saying so at Critical, and the one that holds the lock is untouched.
	TEST_F( GatewayFiles, HeldLockDisables ){
		if( !Logging::FindLogger<Logging::MemoryLog>() )
			Logging::AddLogger( mu<Logging::MemoryLog>() );
		Logging::ClearMemory();
		EXPECT_TRUE( Library->Enabled() );
		{
			Historian second{ Config(1min), Time };
			EXPECT_FALSE( second.Enabled() );
			let logged = Logging::Find( []( const Logging::Entry& e ){ return e.Level==ELogLevel::Critical; } );
			ASSERT_EQ( logged.size(), 1 );
			EXPECT_TRUE( logged[0].Message().contains(Path().string()) ) << logged[0].Message();
			EXPECT_THROW( second.AddGroup({.Name="pump1"}), Exception );
			EXPECT_FALSE( second.FindGroup("pump1") );
			EXPECT_THROW( second.RemoveGroup("pump1"), Exception );
		}
		auto group = AddGroup();//the second's end didn't drop the first's lock.
		{
			Historian third{ Config(1min), Time };
			EXPECT_FALSE( third.Enabled() );
		}
		DataChange( *group, Join(*group, "Pump1.Speed"), 1750, Time->Now() );
		EXPECT_TRUE( Flush(*group) );
		Library.reset();
		Historian next{ Config(1min), Time };//the lock goes with its holder.
		EXPECT_TRUE( next.Enabled() );
	}

	//A hist.path that can't be made is no reason to exit either.
	TEST_F( GatewayFiles, UnusablePathDisables ){
		save( Path()/"taken", "a file" );
		Historian blocked{ Settings{Path()/"taken"/"hist"}, Time };
		EXPECT_FALSE( blocked.Enabled() );
		EXPECT_THROW( blocked.AddGroup({.Name="pump1"}), Exception );
	}

	//`delay` after the last flush, the buffer is written:  a new day's file with its preamble, then the records sorted by
	//source time, and the flush's time in .flushed.
	TEST_F( GatewayFiles, DelayFlushes ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		let joined = Time->Now();
		DataChange( *group, speed, 1750, joined-2s );
		let file = File( *group, March7 );
		EXPECT_EQ( Time->Advance(59s), 0 );
		EXPECT_FALSE( fs::exists(file) );
		EXPECT_EQ( group->Buffer().size(), 2 );
		EXPECT_FALSE( group->Flushed() );
		EXPECT_GT( Library->Buffered(), 0 );

		EXPECT_EQ( Time->Advance(1s), 1 );
		Settle( *group );
		EXPECT_TRUE( group->Buffer().empty() );
		EXPECT_EQ( Library->Buffered(), 0 );
		EXPECT_EQ( group->Flushed(), joined+1min );
		EXPECT_EQ( Flushed{Path()/(group->Name()+".flushed")}.Time(), joined+1min );
		let records = readFile( file );
		ASSERT_EQ( records.size(), 4 );
		ASSERT_TRUE( records[0].has_file_start() );
		EXPECT_EQ( records[0].file_start().ts(), StartOf(March7, utc()) );
		EXPECT_EQ( records[0].file_start().generation(), 0 );
		EXPECT_EQ( records[0].file_start().next_node_index(), 0 );//the host's indexes.
		EXPECT_TRUE( isPreamble(records[1], speed, March7) );
		EXPECT_FALSE( records[1].node_added().has_start() );
		EXPECT_TRUE( isValue(records[2], speed, 1750, joined-2s) );
		EXPECT_EQ( records[2].value().server_ts(), ticks(joined-2s+5ms) );
		ASSERT_TRUE( records[3].has_node_added() );
		EXPECT_EQ( records[3].node_added().ts(), ticks(joined) );
		EXPECT_EQ( records[3].node_added().user_name(), "admin" );
		expectRuns( group->Runs(March7), file );
		EXPECT_EQ( group->Runs(March7).size(), 2 );//the preamble, and the flush.

		EXPECT_EQ( Time->Advance(1min), 1 );//every `delay`, with nothing buffered too.
		Settle( *group );
		EXPECT_EQ( group->Flushed(), joined+2min );
		EXPECT_EQ( readFile(file).size(), 4 );
	}

	//A buffer that reaches 8 KB is written without waiting for `delay`, which then counts from that flush.
	TEST_F( GatewayFiles, SizeFlushes ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		let file = File( *group, March7 );
		Time->Advance( 30s );
		uint count{};
		for( ; Time->Pending()==2; ++count )//`delay`'s and midnight's.
			DataChange( *group, speed, (double)count, Time->Now()+count*1ms );
		EXPECT_GT( count, 150 );//a value is some 34 bytes of a file.
		EXPECT_LT( count, 300 );
		DataChange( *group, speed, (double)count, Time->Now()+count*1ms );
		++count;
		EXPECT_EQ( Time->Pending(), 3 );//asked for once.
		EXPECT_FALSE( fs::exists(file) );

		EXPECT_EQ( Time->Advance(0s), 1 );
		Settle( *group );
		EXPECT_TRUE( group->Buffer().empty() );
		EXPECT_EQ( std::ranges::count_if(readFile(file), &HistoryRecord::has_value), count );
		EXPECT_EQ( Time->Pending(), 2 );
		EXPECT_EQ( Time->Advance(59s), 0 );
		EXPECT_EQ( Time->Advance(1s), 1 );
		Settle( *group );
		EXPECT_EQ( group->Flushed(), Time->Now() );
	}

	//A flush sorts what it took by source time and writes each record to its own day's file, so one flush can touch
	//several.  A late record lands in its own day's file, after later ones:  a run of its own, which the group lists.
	//Yesterday's, made here for a day already past, is its archive from the start, with no runs.
	TEST_F( GatewayFiles, SortedIntoDayFiles ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		let now = Time->Now();
		DataChange( *group, speed, 3, now-1s );
		DataChange( *group, speed, 1, now-18h );//yesterday's, late.
		DataChange( *group, speed, 2, now-3s );
		DataChange( *group, speed, 4, now+8h );//tomorrow's, from a clock ahead.
		EXPECT_TRUE( Flush(*group) );

		let yesterday = readFile( File(*group, March6) );
		ASSERT_EQ( yesterday.size(), 3 );
		EXPECT_EQ( yesterday[0].file_start().ts(), StartOf(March6, utc()) );
		EXPECT_EQ( yesterday[0].file_start().generation(), 1 );
		EXPECT_TRUE( isPreamble(yesterday[1], speed, March6) );
		EXPECT_TRUE( isValue(yesterday[2], speed, 1, now-18h) );

		let today = readFile( File(*group, March7) );
		ASSERT_EQ( today.size(), 5 );
		EXPECT_TRUE( isPreamble(today[1], speed, March7) );
		EXPECT_TRUE( isValue(today[2], speed, 2, now-3s) );
		EXPECT_TRUE( isValue(today[3], speed, 3, now-1s) );
		EXPECT_EQ( today[4].node_added().ts(), ticks(now) );

		let tomorrow = readFile( File(*group, March8) );
		ASSERT_EQ( tomorrow.size(), 3 );
		EXPECT_TRUE( isPreamble(tomorrow[1], speed, March8) );
		EXPECT_TRUE( isValue(tomorrow[2], speed, 4, now+8h) );

		DataChange( *group, speed, 2.5, now-2s );
		EXPECT_TRUE( Flush(*group) );
		let runs = group->Runs( March7 );
		ASSERT_EQ( runs.size(), 3 );
		EXPECT_EQ( runs[1].First, ticks(now-3s) );
		EXPECT_EQ( runs[1].Last, ticks(now) );
		EXPECT_EQ( runs[2].First, ticks(now-2s) );//inside the run before it.
		EXPECT_EQ( runs[2].Last, ticks(now-2s) );
		for( let day : {March6, March7, March8} )
			expectRuns( group->Runs(day), File(*group, day) );
		EXPECT_TRUE( group->Runs(March9).empty() );
	}

	//A new file's preamble gives each member its last stored value before the day, which a restart reads back from the
	//newest file.  OpcServer's FileStart carries the index it issues next.
	TEST_F( ServerFiles, StartValues ){
		let speed = Historize( "Pump1.Speed" );
		let flow = Historize( "Pump1.Flow" );
		let first = Time->Now();
		SetValue( speed, 1 );
		Time->AdvanceTo( sys_days{March8}+30s );
		let second = Time->Now();
		SetValue( speed, 2 );
		EXPECT_TRUE( Flush(*Server) );
		auto records = readFile( File(March8) );
		ASSERT_EQ( records.size(), 4 );
		EXPECT_EQ( records[0].file_start().next_node_index(), 3 );
		EXPECT_TRUE( isPreamble(records[1], speed, March8) );
		let& start = records[1].node_added().start();
		EXPECT_EQ( start.node_index(), 0 );//its record's.
		EXPECT_EQ( start.source_ts(), ticks(first) );
		EXPECT_EQ( start.server_ts(), ticks(first) );
		EXPECT_EQ( start.value().double_value(), 1 );
		EXPECT_TRUE( isPreamble(records[2], flow, March8) );
		EXPECT_FALSE( records[2].node_added().has_start() );//nothing stored yet.
		EXPECT_TRUE( isValue(records[3], speed, 2, second) );

		Start( {{Node("Pump1.Speed")}, {Node("Pump1.Flow")}} );
		EXPECT_TRUE( Server->Buffer().empty() );//both are in the newest file's preamble.
		EXPECT_EQ( Server->Find(Node("Pump1.Flow")), flow );
		Time->AdvanceTo( sys_days{March9}+30s );
		SetValue( flow, 5 );
		EXPECT_TRUE( Flush(*Server) );
		records = readFile( File(March9) );
		ASSERT_EQ( records.size(), 4 );
		EXPECT_EQ( records[1].node_added().start().source_ts(), ticks(second) );
		EXPECT_EQ( records[1].node_added().start().value().double_value(), 2 );
		EXPECT_FALSE( records[2].node_added().has_start() );
		EXPECT_TRUE( isValue(records[3], flow, 5, Time->Now()) );
	}

	//A day file a restart can't tell is there may be the newest, and say an index is taken:  adding the group throws
	//rather than restoring it from an older file, which would issue that index again.
	TEST_F( ServerFiles, NewestFileUnreadable ){
		Historize( "Pump1.Speed" );
		EXPECT_TRUE( Flush(*Server) );
		Time->AdvanceTo( sys_days{March8}+30s );
		let tank = Historize( "Tank1.Level" );
		EXPECT_TRUE( Flush(*Server) );
		let dir = File( March8 ).parent_path();
		fs::permissions( dir, fs::perms::none, fs::perm_options::replace );
		std::error_code ec;
		let unreadable = !fs::exists( File(March8), ec ) && ec;
		if( unreadable )
			EXPECT_THROW( Start({{Node("Pump1.Speed")}, {Node("Tank1.Level")}, {Node("Pump9.New")}}), Exception );
		fs::permissions( dir, fs::perms::owner_all, fs::perm_options::replace );
		if( !unreadable )
			GTEST_SKIP() << "This user can stat a file in a directory it can't search.";

		Start( {{Node("Pump1.Speed")}, {Node("Tank1.Level")}, {Node("Pump9.New")}} );
		EXPECT_EQ( Server->Find(Node("Tank1.Level")), tank );
		EXPECT_EQ( Server->Find(Node("Pump9.New")), tank+1 );
	}

	//A restart takes each node's newest stored value from every file down to today's, not from a future-dated one's
	//preamble alone, so the next day's file starts from it.
	TEST_F( ServerFiles, StartValuesAfterAFutureFile ){
		let speed = Historize( "Pump1.Speed" );
		let flow = Historize( "Pump1.Flow" );
		SetValue( speed, 1 );
		Server->Enqueue( flow, Reading(9, Time->Now()+days{2}) );//a writer's clock two days ahead.
		EXPECT_TRUE( Flush(*Server) );
		EXPECT_EQ( readFile(File(March9))[1].node_added().start().value().double_value(), 1 );
		Time->Advance( 1h );
		let second = Time->Now();
		SetValue( speed, 2 );
		EXPECT_TRUE( Flush(*Server) );

		Start( {{Node("Pump1.Speed")}, {Node("Pump1.Flow")}} );
		Time->AdvanceTo( sys_days{March8}+30s );
		SetValue( speed, 3 );
		EXPECT_TRUE( Flush(*Server) );
		let records = readFile( File(March8) );
		ASSERT_GE( records.size(), 3 );
		ASSERT_TRUE( isPreamble(records[1], speed, March8) );
		let& start = records[1].node_added().start();
		EXPECT_EQ( start.value().double_value(), 2 );
		EXPECT_EQ( start.source_ts(), ticks(second) );
		ASSERT_TRUE( isPreamble(records[2], flow, March8) );
		EXPECT_FALSE( records[2].node_added().has_start() );//its newest is after the day, which step 5's walk back covers.
	}

	//A later day purged under a running host is forgotten:  a membership change after doesn't make its file again.
	TEST_F( ServerFiles, PurgedLaterDayStaysPurged ){
		let speed = Historize( "Pump1.Speed" );
		Server->Enqueue( speed, Reading(1, Time->Now()+days{1}) );
		EXPECT_TRUE( Flush(*Server) );
		ASSERT_TRUE( fs::exists(File(March8)) );
		fs::remove_all( File(March8).parent_path() );
		let flow = Historize( "Pump1.Flow" );
		EXPECT_TRUE( Flush(*Server) );
		EXPECT_FALSE( fs::exists(File(March8)) );
		EXPECT_EQ( readFile(File(March7)).back().node_added().node_index(), flow );
		Historize( "Tank1.Level" );
		EXPECT_TRUE( Flush(*Server) );
		EXPECT_FALSE( fs::exists(File(March8)) );
	}

	//A host clock set back, as on a box that boots before its time sync, stamps a membership change before the group's
	//files.  Its copies go only to the files after the last flush, not into every file there is, and the last flush stays.
	TEST_F( ServerFiles, ClockSetBack ){
		let speed = Historize( "Pump1.Speed" );
		let flow = Historize( "Pump1.Flow" );
		SetValue( flow, 1 );
		Server->Enqueue( speed, Reading(2, Time->Now()+days{1}) );//a writer's clock a day ahead.
		EXPECT_TRUE( Flush(*Server) );
		let flushed = Server->Flushed();
		let today = readFile( File(March7) ).size();
		let tomorrow = readFile( File(March8) ).size();

		Time = ms<ManualClock>( sys_days{2000y/January/1} );
		Start( {{Node("Pump1.Speed")}} );//Flow has left the nodesets.
		EXPECT_TRUE( Flush(*Server) );
		EXPECT_EQ( readFile(File(March7)).size(), today );
		let later = readFile( File(March8) );
		ASSERT_EQ( later.size(), tomorrow+1 );
		EXPECT_EQ( later.back().node_removed().node_index(), flow );
		EXPECT_EQ( later.back().node_removed().ts(), StartOf(March8, utc()) );
		EXPECT_EQ( Server->Flushed(), flushed );
	}

	//A restart walks only the day directories the historian names:  a padded or stray one neither stops it nor stands in
	//for a real day, here the 7th, which 263 narrowed to, ahead of the newest file's day.
	TEST_F( ServerFiles, StrayDirectoriesSkipped ){
		let speed = Historize( "Pump1.Speed" );
		Server->Enqueue( speed, Reading(1, Time->Now()+days{13}) );//March 20.
		EXPECT_TRUE( Flush(*Server) );
		fs::create_directories( Path()/"2026"/"04" );//with no 2026/4.
		fs::create_directories( Path()/"2026"/"3"/"263" );
		Start( {{Node("Pump1.Speed")}} );
		Server->Remove( speed );
		EXPECT_TRUE( Flush(*Server) );
		let later = readFile( File(2026y/March/20) );
		ASSERT_TRUE( later.back().has_node_removed() );//its copy, which only a walk that found March 20 makes.
		EXPECT_EQ( later.back().node_removed().node_index(), speed );
	}

	//DateTime's MaxValue, past every day, counts as no timestamp:  a source time is dropped, filing the value by its
	//server time, and a server time is the arrival's.  No 9999 file is made to stay the newest.
	TEST_F( GatewayFiles, TimeNoDayHolds ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		let value = [&]( double v, UA_DateTime source, UA_DateTime server ){
			UA_DataValue dv{};
			UA_Variant_setScalarCopy( &dv.value, &v, &UA_TYPES[UA_TYPES_DOUBLE] );
			dv.hasValue = true;
			dv.sourceTimestamp = source;
			dv.hasSourceTimestamp = true;
			dv.sourcePicoseconds = 5;
			dv.hasSourcePicoseconds = true;
			dv.serverTimestamp = server;
			dv.hasServerTimestamp = true;
			return Value{ move(dv) };
		};
		constexpr auto max = std::numeric_limits<UA_DateTime>::max();
		let server = ticks( Time->Now()-1s );
		for( uint i=0; i<20; ++i )
			EXPECT_TRUE( group->Enqueue(speed, value(i, max, server)) );
		EXPECT_TRUE( group->Enqueue(speed, value(20, max, max)) );
		EXPECT_TRUE( Flush(*group) );
		EXPECT_FALSE( fs::exists(File(*group, 9999y/December/31)) );
		let records = readFile( File(*group, March7) );
		uint values{};
		for( let& r : records ){
			if( !r.has_value() )
				continue;
			EXPECT_FALSE( r.value().has_source_ts() );
			EXPECT_EQ( r.value().source_picoseconds(), 0 );
			EXPECT_EQ( r.value().server_ts(), values<20 ? server : ticks(Time->Now()) );
			++values;
		}
		EXPECT_EQ( values, 21 );
		EXPECT_EQ( group->Runs(March7).size(), 2 );//the preamble, and the one flush.
	}

	//A file made before a membership change, for a day still to come, takes the change too, at the start of its day:  so
	//it maps its own nodes, and a restart, which reads the newest file, finds the group as it is.
	TEST_F( ServerFiles, MembershipReachesLaterFiles ){
		let speed = Historize( "Pump1.Speed" );
		Server->Enqueue( speed, Reading(1, Time->Now()+days{1}) );//a writer's clock a day ahead.
		EXPECT_TRUE( Flush(*Server) );
		EXPECT_EQ( readFile(File(March8)).size(), 3 );
		let flow = Historize( "Pump1.Flow" );
		Server->Remove( speed );
		EXPECT_TRUE( Flush(*Server) );

		let today = readFile( File(March7) );
		ASSERT_EQ( today.size(), 5 );
		EXPECT_EQ( today[3].node_added().node_index(), flow );
		EXPECT_EQ( today[3].node_added().ts(), ticks(Time->Now()) );
		EXPECT_EQ( today[4].node_removed().node_index(), speed );
		let tomorrow = readFile( File(March8) );
		ASSERT_EQ( tomorrow.size(), 5 );
		EXPECT_TRUE( isPreamble(tomorrow[3], flow, March8) );
		EXPECT_EQ( tomorrow[4].node_removed().node_index(), speed );
		EXPECT_EQ( tomorrow[4].node_removed().ts(), StartOf(March8, utc()) );

		Start( {{Node("Pump1.Flow")}, {Node("Tank1.Level")}} );
		EXPECT_EQ( Server->Find(Node("Pump1.Flow")), flow );
		EXPECT_EQ( Server->Find(Node("Tank1.Level")), 3 );
		EXPECT_TRUE( Records<NodeRemoved>().empty() );//Speed left before the restart.
		ASSERT_EQ( Records<NodeAdded>().size(), 1 );
	}

	//The same when the file for the day to come is made by the flush that writes the change:  the node that left is
	//mapped there for its value, and removed again, so a restart doesn't find it a member.
	TEST_F( ServerFiles, LeftNodeStaysOutOfANewFile ){
		let speed = Historize( "Pump1.Speed" );
		Server->Enqueue( speed, Reading(1, Time->Now()+days{1}) );
		let flow = Historize( "Pump1.Flow" );
		Server->Remove( speed );
		EXPECT_TRUE( Flush(*Server) );
		let tomorrow = readFile( File(March8) );
		ASSERT_EQ( tomorrow.size(), 5 );
		EXPECT_TRUE( isPreamble(tomorrow[1], flow, March8) );
		EXPECT_TRUE( isPreamble(tomorrow[2], speed, March8) );
		EXPECT_EQ( tomorrow[3].node_removed().node_index(), speed );
		EXPECT_EQ( tomorrow[3].node_removed().ts(), StartOf(March8, utc()) );
		EXPECT_TRUE( isValue(tomorrow[4], speed, 1, Time->Now()+days{1}) );

		Start( {{Node("Pump1.Flow")}} );
		EXPECT_EQ( Server->Find(Node("Pump1.Flow")), flow );
		EXPECT_FALSE( Server->Find(Node("Pump1.Speed")) );
		EXPECT_TRUE( Records<NodeRemoved>().empty() );//Speed left before the restart.
		EXPECT_TRUE( Records<NodeAdded>().empty() );
	}

	//A node's first value after it joins carries the time the value last changed, which can fall in a day whose file was
	//made before the node joined.  That file gets a preamble record for the node with the value, so it still maps every
	//index it holds:  here yesterday's, an archive by then, which takes the record with its preamble.
	TEST_F( GatewayFiles, MapsEachIndexAFileHolds ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		let first = Time->Now();
		DataChange( *group, speed, 1, first );
		EXPECT_TRUE( Flush(*group) );
		Time->AdvanceTo( sys_days{March8}+10min );
		Settle( *group );//midnight's rewrite, which a late record then follows.
		let flow = Join( *group, "Pump1.Flow" );
		let changed = sys_days{March7}+23h;
		DataChange( *group, flow, 3, changed );
		EXPECT_TRUE( Flush(*group) );

		let yesterday = readFile( File(*group, March7) );
		ASSERT_EQ( yesterday.size(), 6 );
		EXPECT_EQ( yesterday[0].file_start().generation(), 2 );
		EXPECT_TRUE( isPreamble(yesterday[2], flow, March7) );
		EXPECT_TRUE( isValue(yesterday[5], flow, 3, changed) );
		let today = readFile( File(*group, March8) );
		ASSERT_EQ( today.size(), 4 );
		EXPECT_EQ( today[1].node_added().start().source_ts(), ticks(first) );
		EXPECT_TRUE( isPreamble(today[2], flow, March8) );
		EXPECT_EQ( today[2].node_added().start().source_ts(), ticks(changed) );//written to yesterday's file first.
		EXPECT_EQ( today[3].node_added().user_name(), "admin" );
		expectRuns( group->Runs(March7), File(*group, March7) );
	}

	//While a file can't be written, its records go back to the buffer as they were, .flushed stands still, and only
	//`delay` tries again.
	TEST_F( GatewayFiles, UnwritableIsRetried ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		DataChange( *group, speed, 0, Time->Now() );
		let buffered = Library->Buffered();
		Block();
		EXPECT_FALSE( Flush(*group) );
		let back = group->Buffer();
		ASSERT_EQ( back.size(), 2 );
		EXPECT_TRUE( std::holds_alternative<NodeAdded>(back[0]) );
		EXPECT_TRUE( std::holds_alternative<DataValue>(back[1]) );
		EXPECT_EQ( Library->Buffered(), buffered );
		EXPECT_FALSE( group->Flushed() );

		for( uint i=1; i<=300; ++i )//past 8 KB.
			DataChange( *group, speed, i, Time->Now()+i*1ms );
		EXPECT_EQ( Time->Pending(), 2 );
		EXPECT_EQ( Time->Advance(1min), 1 );
		Settle( *group );
		let held = group->Buffer();
		ASSERT_EQ( held.size(), 302 );
		for( uint i=0; i<=300; ++i )//in the order they arrived.
			ASSERT_EQ( get<DataValue>(held[i+1]).Data.Get<double>(0), i );

		Unblock();
		EXPECT_EQ( Time->Advance(1min), 1 );
		Settle( *group );
		EXPECT_TRUE( group->Buffer().empty() );
		EXPECT_EQ( Library->Buffered(), 0 );
		EXPECT_EQ( readFile(File(*group, March7)).size(), 304 );
		EXPECT_EQ( group->Flushed(), Time->Now() );
		DataChange( *group, speed, 301, Time->Now() );
		for( uint i=0; Time->Pending()==2; ++i )//the buffer's size counts again.
			DataChange( *group, speed, i, Time->Now() );
		EXPECT_EQ( Time->Pending(), 3 );
	}

	//A day that can't be written, an old one a late record names, holds back only its own records:  the others are
	//written, the flush at 8 KB still runs, and it leaves that day to `delay`.  .flushed waits for it.
	TEST_F( GatewayFiles, UnwritableDayHoldsOnlyItself ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		DataChange( *group, speed, 1, Time->Now() );
		EXPECT_TRUE( Flush(*group) );
		let flushed = group->Flushed();
		save( Path()/"2025", "in the way" );
		let late = sys_days{2025y/December/31}+12h;
		DataChange( *group, speed, 0, late );
		DataChange( *group, speed, 2, Time->Now()+1s );
		EXPECT_FALSE( Flush(*group) );
		auto back = group->Buffer();
		ASSERT_EQ( back.size(), 1 );
		EXPECT_EQ( get<DataValue>(back[0]).Data.Get<double>(0), 0 );
		EXPECT_EQ( group->Flushed(), flushed );
		let today = File( *group, March7 );
		EXPECT_TRUE( isValue(readFile(today).back(), speed, 2, Time->Now()+1s) );

		fs::remove( Path()/"2025" );//writable again, but `delay` isn't up.
		uint i{};
		for( ; Time->Pending()==2; ++i )
			DataChange( *group, speed, 3+i, Time->Now()+2s+i*1ms );
		EXPECT_EQ( Time->Advance(0s), 1 );
		Settle( *group );
		back = group->Buffer();
		ASSERT_EQ( back.size(), 1 );
		EXPECT_EQ( get<DataValue>(back[0]).Data.Get<double>(0), 0 );
		EXPECT_TRUE( isValue(readFile(today).back(), speed, 3+i-1, Time->Now()+2s+(i-1)*1ms) );
		EXPECT_EQ( group->Flushed(), flushed );

		EXPECT_EQ( Time->Advance(1min), 1 );
		Settle( *group );
		EXPECT_TRUE( group->Buffer().empty() );
		EXPECT_EQ( group->Flushed(), Time->Now() );
		EXPECT_TRUE( isValue(readFile(File(*group, 2025y/December/31)).back(), speed, 0, late) );
	}

	//A group that can write and takes the buffers past maxBuffer is flushed rather than trimmed, even before 8 KB.
	TEST_F( GatewayFiles, CapFlushesAWritableGroup ){
		auto config = Config( 1min );
		config.MaxBuffer = 20'000;//below the floor the hist block sets, so the cap comes long before 8 KB.
		Restart( move(config) );
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		constexpr uint count{ 1'000 };
		uint flushes{};
		for( uint i=0; i<count; ++i ){
			DataChange( *group, speed, i, Time->Now()+i*1ms );
			if( Time->Pending()>2 ){//the executor's, which runs it at once.
				EXPECT_EQ( Time->Advance(0s), 1 );
				Settle( *group );
				++flushes;
			}
		}
		EXPECT_GT( flushes, 1 );
		EXPECT_TRUE( Flush(*group) );
		uint values{};
		for( let& r : readFile(File(*group, March7)) ){
			if( r.has_value() ){
				EXPECT_TRUE( isValue(r, speed, values, Time->Now()+values*1ms) );
				++values;
			}
		}
		EXPECT_EQ( values, count );
	}

	//Passing maxBuffer trims on the clock's zero-delay hop, not in Enqueue, and to an eighth below the cap, so the next
	//value doesn't trim again.  A group that drops is warned of once a streak, which another group's flush doesn't end.
	TEST_F( GatewayFiles, TrimOffTheCollectionPath ){
		if( !Logging::FindLogger<Logging::MemoryLog>() )
			Logging::AddLogger( mu<Logging::MemoryLog>() );
		auto config = Config( 1min );
		config.MaxBuffer = 4'000;
		Restart( move(config) );
		auto stuck = AddGroup();
		auto healthy = AddGroup();
		let speed = Join( *stuck, "Pump1.Speed" );
		let flow = Join( *healthy, "Pump2.Flow" );
		fs::create_directories( File(*stuck, March7).parent_path() );
		if( !dangle(File(*stuck, March7)) )
			GTEST_SKIP() << "This user can't make a symlink.";
		EXPECT_FALSE( Flush(*stuck) );
		EXPECT_TRUE( Flush(*healthy) );
		Logging::ClearMemory();
		let warnings = []{
			return Logging::Find( []( const Logging::Entry& e ){ return e.Level==ELogLevel::Warning && e.Message().contains("drops its oldest values"); } ).size();
		};

		uint n{};
		let fill = [&]{//bounded, for a trim that ran in Enqueue and so never let it past.
			for( uint i=0; i<1'000 && Library->Buffered()<=4'000; ++i, ++n )
				DataChange( *stuck, speed, n, Time->Now()+n*1ms );
		};
		fill();
		EXPECT_GT( Library->Buffered(), 4'000 );//Enqueue left it to the hop.
		EXPECT_EQ( Time->Advance(0s), 1 );
		EXPECT_LE( Library->Buffered(), 3'500 );
		EXPECT_EQ( warnings(), 1u );
		for( uint minute=0; minute<5; ++minute ){
			fill();
			DataChange( *healthy, flow, minute, Time->Now() );
			Time->Advance( 1min );
			Settle( *stuck );
			EXPECT_TRUE( Settle(*healthy) );
		}
		EXPECT_EQ( warnings(), 1u );
	}

	//Values dropped while a flush's are out are a gap of their own:  when that flush fails and some of its values are
	//dropped in turn, each gap keeps its marker, so no read carries a value across the later one.  A FIFO where the day
	//file goes holds the flush in its scan until the gap is made, and a second, failing group lends the buffers the room
	//that makes the second drop partial.
	TEST_F( GatewayFiles, GapsWhileAFlushIsOut ){
#ifdef _WIN32
		GTEST_SKIP() << "No FIFO to hold the flush.";
#else
		uint r;//what one value takes of the buffers.
		{
			auto probe = AddGroup();
			let index = Join( *probe, "Probe" );
			let before = Library->Buffered();
			DataChange( *probe, index, 0, Time->Now() );
			r = Library->Buffered()-before;
		}
		auto config = Config( 1min );
		config.MaxBuffer = 24*r;
		Restart( move(config) );
		auto group = AddGroup();
		auto other = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		let flow = Join( *other, "Pump2.Flow" );
		let start = Time->Now();
		let at = [&]( uint n ){ return start+n*10ms; };
		uint n{};
		for( ; n<18; ++n )
			DataChange( *group, speed, n, at(n) );
		let file = File( *group, March7 );
		let otherFile = File( *other, March7 );
		fs::create_directories( file.parent_path() );
		ASSERT_EQ( ::mkfifo(file.c_str(), 0600), 0 );
		ASSERT_TRUE( dangle(otherFile) );
		EXPECT_FALSE( Flush(*other) );//so it trims rather than flushes.

		std::jthread flushing{ [&]{ EXPECT_FALSE( Flush(*group) ); } };//its scan waits on the FIFO for a writer.
		bool released{};
		let release = [&]{
			for( let deadline = steady_clock::now()+10s; !released && steady_clock::now()<deadline; std::this_thread::sleep_for(1ms) ){
				if( let fd = ::open(file.c_str(), O_WRONLY | O_NONBLOCK); fd!=-1 ){
					::close( fd );
					released = true;
				}
			}
		};
		struct Finally{ std::function<void()> F; ~Finally(){ F(); } } guard{ release };//before the join.
		for( let deadline = steady_clock::now()+10s; !group->Buffer().empty() && steady_clock::now()<deadline; )
			std::this_thread::sleep_for( 1ms );
		ASSERT_TRUE( group->Buffer().empty() );

		let gap = n;
		for( uint i=0; i<10; ++i, ++n )
			DataChange( *group, speed, n, at(n) );
		for( uint v=0; Library->Buffered()<=26*r; ++v )
			DataChange( *other, flow, v, Time->Now() );
		EXPECT_EQ( Time->Advance(0s), 1 );//the trim, which drops the oldest:  this group's newest values.
		fs::remove( otherFile );
		EXPECT_TRUE( Flush(*other) );//the room.
		release();
		flushing.join();
		fs::remove( file );
		EXPECT_TRUE( Flush(*group) );

		vector<HistoryRecord> markers;
		for( let& record : readFile(file) ){
			if( record.has_value() && record.value().status()==UA_STATUSCODE_BADDATALOST )
				markers.push_back( record );
		}
		ASSERT_EQ( markers.size(), 2 );
		EXPECT_EQ( markers[0].value().source_ts(), ticks(at(0)) );
		EXPECT_EQ( markers[1].value().source_ts(), ticks(at(gap)) );
		EXPECT_EQ( Library->Buffered(), 0 );
#endif
	}

	//AddGroup reads its files outside the historian's lock, holding only the name:  FindGroup doesn't wait on a scan, here
	//held by a FIFO where the new group's newest file goes, and the name is free again once that fails.
	TEST_F( GatewayFiles, AddGroupScansOutsideTheLock ){
#ifdef _WIN32
		GTEST_SKIP() << "No FIFO to hold the scan.";
#else
		let other = AddGroup();
		let file = Path()/DayDirectory( March7 )/"pump1.binpb";
		fs::create_directories( file.parent_path() );
		ASSERT_EQ( ::mkfifo(file.c_str(), 0600), 0 );
		std::jthread adding{ [&]{ EXPECT_THROW( Library->AddGroup({.Name="pump1", .Indexes=EIndexes::Host}), Exception ); } };
		std::this_thread::sleep_for( 100ms );//into its scan, which waits on the FIFO for a writer.
		auto found = std::async( std::launch::async, [&]{ return Library->FindGroup(other->Name()); } );
		let answered = found.wait_for( 2s )==std::future_status::ready;
		if( answered )//so the lock is free:  the name is held meanwhile.
			EXPECT_THROW( Library->AddGroup({.Name="pump1", .Indexes=EIndexes::Host}), Exception );
		bool released{};
		for( let deadline = steady_clock::now()+10s; !released && steady_clock::now()<deadline; std::this_thread::sleep_for(1ms) ){
			if( let fd = ::open(file.c_str(), O_WRONLY | O_NONBLOCK); fd!=-1 ){
				::close( fd );
				released = true;
			}
		}
		adding.join();
		EXPECT_TRUE( answered );
		EXPECT_EQ( found.get(), other );
		fs::remove( file );
		EXPECT_TRUE( Library->AddGroup({.Name="pump1", .Indexes=EIndexes::Host}) );
#endif
	}

	//The historian's end runs every group's last flush at once, and waits only up to StopLimit:  one held in its scan by
	//a FIFO where its day file goes is given up on with an error, while the other's is written.
	TEST_F( GatewayFiles, EndWaitsOnlySoLong ){
#ifdef _WIN32
		GTEST_SKIP() << "No FIFO to hold the flush.";
#else
		if( !Logging::FindLogger<Logging::MemoryLog>() )
			Logging::AddLogger( mu<Logging::MemoryLog>() );
		auto config = Config( 1min );
		config.StopLimit = 200ms;
		Restart( move(config) );
		auto stuck = AddGroup();
		auto healthy = AddGroup();
		DataChange( *stuck, Join(*stuck, "Pump1.Speed"), 1, Time->Now() );
		DataChange( *healthy, Join(*healthy, "Pump2.Speed"), 2, Time->Now() );
		let file = File( *stuck, March7 );
		let written = File( *healthy, March7 );
		let name = stuck->Name();
		fs::create_directories( file.parent_path() );
		ASSERT_EQ( ::mkfifo(file.c_str(), 0600), 0 );
		stuck.reset();
		healthy.reset();
		_group.reset();
		Logging::ClearMemory();

		let start = steady_clock::now();
		Library.reset();
		let took = steady_clock::now()-start;
		bool released{};//the FIFO's writer, which ends the stuck scan.
		for( let deadline = steady_clock::now()+10s; !released && steady_clock::now()<deadline; std::this_thread::sleep_for(1ms) ){
			if( let fd = ::open(file.c_str(), O_WRONLY | O_NONBLOCK); fd!=-1 ){
				::close( fd );
				released = true;
			}
		}
		EXPECT_TRUE( released );
		EXPECT_GE( took, 200ms );
		EXPECT_LT( took, 5s );
		EXPECT_EQ( readFile(written).size(), 4 );
		let logged = Logging::Find( [&]( const Logging::Entry& e ){ return e.Level==ELogLevel::Error && e.Message().contains("stopped with a flush still out") && e.Message().contains(name); } );
		EXPECT_EQ( logged.size(), 1 );
#endif
	}

	namespace{
		Ω diagnostic( TimePoint at )ι->Value{//a value with no file form, from a node typed BaseDataType.
			UA_DataValue dv{};
			UA_DiagnosticInfo info{};
			UA_Variant_setScalarCopy( &dv.value, &info, &UA_TYPES[UA_TYPES_DIAGNOSTICINFO] );
			dv.hasValue = true;
			dv.sourceTimestamp = ticks( at );
			dv.hasSourceTimestamp = true;
			return Value{ move(dv) };
		}
	}
	//The once-per-node warning of a value with no file form is said once, though its day fails and is retried.
	TEST_F( GatewayFiles, NoFileFormWarnedOnceThoughRetried ){
		if( !Logging::FindLogger<Logging::MemoryLog>() )
			Logging::AddLogger( mu<Logging::MemoryLog>() );
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		Block();
		Logging::ClearMemory();
		ASSERT_TRUE( group->Enqueue(speed, diagnostic(Time->Now())) );
		EXPECT_FALSE( Flush(*group) );
		EXPECT_FALSE( Flush(*group) );
		let warned = Logging::Find( []( const Logging::Entry& e ){ return e.Level==ELogLevel::Warning && e.Message().contains("has no file form"); } );
		EXPECT_EQ( warned.size(), 1u );
	}

	//Nor is it lost when the value it was for is dropped before a flush says it:  the node's next such value carries it.
	TEST_F( GatewayFiles, NoFileFormWarningOutlivesADrop ){
		auto config = Config( 1min );
		config.MaxBuffer = 4'000;
		Restart( move(config) );
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		Block();
		EXPECT_FALSE( Flush(*group) );//so it trims rather than flushes.
		ASSERT_TRUE( group->Enqueue(speed, diagnostic(Time->Now())) );
		ASSERT_TRUE( Records<DataValue>().back().Unsupported );
		for( uint i=1; Library->Buffered()<=4'000; ++i )
			DataChange( *group, speed, i, Time->Now()+i*1ms );
		EXPECT_EQ( Time->Advance(0s), 1 );//the trim:  the diagnostic goes, and a later value is written back.
		for( let& value : Records<DataValue>() )
			ASSERT_FALSE( value.Data.value.type==&UA_TYPES[UA_TYPES_DIAGNOSTICINFO] );
		ASSERT_TRUE( group->Enqueue(speed, diagnostic(Time->Now()+1min)) );
		EXPECT_TRUE( Records<DataValue>().back().Unsupported );
	}

	//maxBuffer caps every group's buffer together.  Past it, the oldest values across the groups go first.  When writing
	//resumes, a node that lost several gets a Bad_DataLost at the source time of the first, then the newest of them,
	//written back, and a node that lost one simply gets it back.
	TEST_F( GatewayFiles, FullBufferDropsTheOldest ){
		auto config = Config( 1min );
		config.MaxBuffer = 4'000;
		Restart( move(config) );
		auto pump1 = AddGroup();
		auto pump2 = AddGroup();
		let speed = Join( *pump1, "Pump1.Speed" );
		let flow = Join( *pump1, "Pump1.Flow" );
		let other = Join( *pump2, "Pump2.Speed" );
		Block();
		EXPECT_FALSE( Flush(*pump1) );//so each knows its files are unwritable, and trims rather than flushes.
		EXPECT_FALSE( Flush(*pump2) );
		let start = Time->Now();
		let at = [&]( uint n ){ return start+n*10ms; };
		DataChange( *pump1, flow, 0, at(0) );//the oldest of all, and Flow's only one.
		constexpr uint count{ 60 };
		for( uint n=1; n<=count; ++n ){
			DataChange( n%2 ? *pump1 : *pump2, n%2 ? speed : other, n, at(n) );
			Time->Advance( 0s );//the trim's hop.
			ASSERT_LE( Library->Buffered(), 4'000 );
		}
		vector<double> left;
		for( let& group : {pump1, pump2} ){
			for( let& r : group->Buffer() ){
				if( let value = get_if<DataValue>(&r) )
					left.push_back( value->Data.Get<double>(0) );
			}
		}
		std::ranges::sort( left );
		ASSERT_GT( left.size(), 4 );
		let kept = (uint)left.front();//everything older was dropped, whichever group held it.
		ASSERT_GT( kept, 4 );
		for( uint i=0; i<left.size(); ++i )
			ASSERT_EQ( left[i], kept+i );
		EXPECT_EQ( left.back(), count );

		Unblock();
		EXPECT_TRUE( Flush(*pump1) );
		EXPECT_TRUE( Flush(*pump2) );
		let stored = [&]( const Group& group, NodeIndex index ){
			vector<HistoryRecord> y;
			for( let& r : readFile(File(group, March7)) ){
				if( r.has_value() && r.value().node_index()==index )
					y.push_back( r );
			}
			return y;
		};
		for( let& [group, index, odd] : {tuple{pump1, speed, 1u}, tuple{pump2, other, 0u}} ){
			SCOPED_TRACE( index );
			let first = odd ? 1u : 2u;
			let newest = kept-1-( (kept-1)%2==odd ? 0 : 1 );//the last this node lost.
			let values = stored( *group, index );
			ASSERT_EQ( values.size(), 2+(count-newest)/2 );
			EXPECT_FALSE( values[0].value().has_value() );
			EXPECT_EQ( values[0].value().status(), UA_STATUSCODE_BADDATALOST );
			EXPECT_EQ( values[0].value().source_ts(), ticks(at(first)) );
			EXPECT_TRUE( isValue(values[1], index, newest, at(newest)) );
			for( uint i=2; i<values.size(); ++i )
				EXPECT_TRUE( isValue(values[i], index, newest+2*(i-1), at(newest+2*(i-1))) );
		}
		let flows = stored( *pump1, flow );
		ASSERT_EQ( flows.size(), 1 );//back, with no marker.
		EXPECT_TRUE( isValue(flows[0], flow, 0, at(0)) );
		expectRuns( pump1->Runs(March7), File(*pump1, March7) );
		EXPECT_EQ( Library->Buffered(), 0 );//drops, failed flushes and returns, all counted back out.
	}

	//A flush that a crash cut short leaves a torn tail, which the scan at the next start leaves alone and the first
	//append drops.
	TEST_F( GatewayFiles, TornTailDroppedByFirstAppend ){
		auto group = AddGroup();
		let name = group->Name();
		let speed = Join( *group, "Pump1.Speed" );
		DataChange( *group, speed, 1, Time->Now() );
		let file = File( *group, March7 );
		group.reset();
		Restart();
		let whole = contents( file );
		save( file, whole+string(20, '\x7f') );
		group = Rejoin( name, {{Node("Pump1.Speed"), {}, speed}} );
		EXPECT_TRUE( group->Buffer().empty() );
		EXPECT_EQ( fs::file_size(file), whole.size()+20 );
		DataChange( *group, speed, 2, Time->Now() );
		EXPECT_TRUE( Flush(*group) );
		EXPECT_EQ( contents(file).substr(0, whole.size()), whole );
		let records = readFile( file );
		ASSERT_EQ( records.size(), 5 );
		EXPECT_TRUE( isValue(records[4], speed, 2, Time->Now()) );
		expectRuns( group->Runs(March7), file );
	}

	//A file that reads as missing to a flush that then fails is still the one it was when it comes back:  the next append
	//goes on its end, not over it from byte 0.
	TEST_F( GatewayFiles, ReturningFileIsAppendedTo ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		for( uint i=0; i<100; ++i )
			DataChange( *group, speed, i, Time->Now()+i*1ms );
		EXPECT_TRUE( Flush(*group) );
		let file = File( *group, March7 );
		let before = readFile( file ).size();
		fs::rename( file, Path()/"away" );
		if( !dangle(file) ){
			fs::rename( Path()/"away", file );
			GTEST_SKIP() << "This user can't make a symlink.";
		}
		DataChange( *group, speed, 100, Time->Now()+1s );
		EXPECT_FALSE( Flush(*group) );
		fs::remove( file );
		fs::rename( Path()/"away", file );
		EXPECT_TRUE( Flush(*group) );
		let records = readFile( file );
		ASSERT_EQ( records.size(), before+1 );
		EXPECT_TRUE( isValue(records.back(), speed, 100, Time->Now()+1s) );
		expectRuns( group->Runs(March7), file );
	}

	//A file made in place of one that went missing is written to until that one is put back over it.  It is then scanned
	//again, rather than cut at the stand-in's size.
	TEST_F( GatewayFiles, ReplacedFileIsScannedAgain ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		for( uint i=0; i<100; ++i )
			DataChange( *group, speed, i, Time->Now()+i*1ms );
		EXPECT_TRUE( Flush(*group) );
		let file = File( *group, March7 );
		let before = readFile( file ).size();
		fs::rename( file, Path()/"away" );
		DataChange( *group, speed, 100, Time->Now()+1s );
		EXPECT_TRUE( Flush(*group) );
		EXPECT_EQ( readFile(file).size(), 3 );//the stand-in:  its preamble and the value.
		ASSERT_LT( fs::file_size(file), fs::file_size(Path()/"away") );

		fs::rename( Path()/"away", file );
		DataChange( *group, speed, 101, Time->Now()+2s );
		EXPECT_FALSE( Flush(*group) );
		EXPECT_TRUE( Flush(*group) );
		let records = readFile( file );
		ASSERT_EQ( records.size(), before+1 );
		EXPECT_TRUE( isValue(records.back(), speed, 101, Time->Now()+2s) );
		expectRuns( group->Runs(March7), file );
	}

	//A file put where a group's first append to its day failed may be anything, so it is scanned rather than cut:  here
	//another program's, which is left as it is.
	TEST_F( GatewayFiles, FileAfterFailedFirstAppendIsScanned ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		DataChange( *group, speed, 1, Time->Now() );
		let file = File( *group, March7 );
		fs::create_directories( file.parent_path() );
		if( !dangle(file) )
			GTEST_SKIP() << "This user can't make a symlink.";
		EXPECT_FALSE( Flush(*group) );
		fs::remove( file );
		const string foreign( 64, 'n' );
		save( file, foreign );
		EXPECT_FALSE( Flush(*group) );
		EXPECT_FALSE( Flush(*group) );//the scan's:  another program's.
		EXPECT_EQ( contents(file), foreign );
	}

	//What an append that failed part-way left is cut off by the next, which goes where it went.
	TEST_F( GatewayFiles, FailedAppendIsCut ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		DataChange( *group, speed, 1, Time->Now() );
		EXPECT_TRUE( Flush(*group) );
		let file = File( *group, March7 );
		let whole = contents( file );
		let before = readFile( file ).size();
		fs::permissions( file, fs::perms::owner_read, fs::perm_options::replace );
		DataChange( *group, speed, 2, Time->Now()+1s );
		let failed = !Flush( *group );
		fs::permissions( file, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace );
		if( !failed )
			GTEST_SKIP() << "A read-only file doesn't stop this user writing it.";
		save( file, whole+string(8, '\x7f') );//as far as the failed append got.
		EXPECT_TRUE( Flush(*group) );
		let records = readFile( file );
		ASSERT_EQ( records.size(), before+1 );
		EXPECT_TRUE( isValue(records.back(), speed, 2, Time->Now()+1s) );
		expectRuns( group->Runs(March7), file );
	}

	//A file that holds what a truncation would lose, here another program's, is left as it is.  Its records are dropped
	//rather than held for a file that will never take them, and .flushed doesn't claim them.
	TEST_F( GatewayFiles, RefusedFileIsLeftAlone ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		DataChange( *group, speed, 1, Time->Now() );
		let file = File( *group, March7 );
		fs::create_directories( file.parent_path() );
		const string foreign( 64, 'n' );
		save( file, foreign );
		EXPECT_FALSE( Flush(*group) );
		EXPECT_TRUE( group->Buffer().empty() );
		EXPECT_FALSE( group->Flushed() );
		EXPECT_EQ( contents(file), foreign );

		fs::remove( file );
		DataChange( *group, speed, 2, Time->Now() );
		EXPECT_TRUE( Flush(*group) );
		let records = readFile( file );
		ASSERT_EQ( records.size(), 3 );
		EXPECT_TRUE( isValue(records[2], speed, 2, Time->Now()) );
		EXPECT_EQ( group->Flushed(), Time->Now() );
	}

	//A refused file repaired in place, here emptied, is scanned again by the next append, which then writes to it.
	TEST_F( GatewayFiles, RefusedFileRepairedInPlace ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		DataChange( *group, speed, 1, Time->Now() );
		let file = File( *group, March7 );
		fs::create_directories( file.parent_path() );
		save( file, string(64, 'n') );
		EXPECT_FALSE( Flush(*group) );
		DataChange( *group, speed, 2, Time->Now() );
		EXPECT_FALSE( Flush(*group) );//still refused, unchanged.

		save( file, {} );
		DataChange( *group, speed, 3, Time->Now() );
		EXPECT_TRUE( Flush(*group) );
		let records = readFile( file );
		ASSERT_EQ( records.size(), 3 );
		EXPECT_TRUE( isPreamble(records[1], speed, March7) );
		EXPECT_TRUE( isValue(records[2], speed, 3, Time->Now()) );
		expectRuns( group->Runs(March7), file );
	}

	//Purging a day is a directory delete, under a running historian too:  the next record for it makes its file again.
	TEST_F( GatewayFiles, PurgedDayStartsAgain ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		DataChange( *group, speed, 1, Time->Now() );
		EXPECT_TRUE( Flush(*group) );
		fs::remove_all( Path()/"2026" );
		DataChange( *group, speed, 2, Time->Now() );
		EXPECT_TRUE( Flush(*group) );
		let records = readFile( File(*group, March7) );
		ASSERT_EQ( records.size(), 3 );
		EXPECT_TRUE( records[0].has_file_start() );
		EXPECT_TRUE( isPreamble(records[1], speed, March7) );
		EXPECT_TRUE( isValue(records[2], speed, 2, Time->Now()) );
		expectRuns( group->Runs(March7), File(*group, March7) );
	}

	//A String that isn't UTF-8 is stored without it, so its file stays whole:  a restart appends to it, and the next day's
	//preamble takes the stored value as its start.
	TEST_F( GatewayFiles, NotUtf8StoredWithoutIt ){
		auto group = AddGroup();
		let name = group->Name();
		let label = Join( *group, "Pump1.Label" );
		let speed = Join( *group, "Pump1.Speed" );
		EXPECT_TRUE( group->Enqueue(label, Text("25\xB0" "C", Time->Now())) );
		EXPECT_TRUE( Flush(*group) );
		let file = File( *group, March7 );
		auto records = readFile( file );
		ASSERT_EQ( records.size(), 6 );//its preambles, the NodeAddeds, and the value.
		EXPECT_FALSE( records[5].value().has_value() );
		EXPECT_EQ( records[5].value().status(), UA_STATUSCODE_BADENCODINGERROR );

		group.reset();
		Restart();
		group = Rejoin( name, {{Node("Pump1.Label"), {}, label}, {Node("Pump1.Speed"), {}, speed}} );
		EXPECT_TRUE( group->Buffer().empty() );
		Time->Advance( 1s );
		EXPECT_TRUE( group->Enqueue(label, Text("26\xB0" "C", Time->Now())) );
		EXPECT_TRUE( Flush(*group) );
		records = readFile( file );
		ASSERT_EQ( records.size(), 7 );
		EXPECT_EQ( records[6].value().source_ts(), ticks(Time->Now()) );
		EXPECT_EQ( records[6].value().status(), UA_STATUSCODE_BADENCODINGERROR );

		Time->AdvanceTo( sys_days{March8}+30s );
		DataChange( *group, speed, 1, Time->Now() );
		EXPECT_TRUE( Flush(*group) );
		records = readFile( File(*group, March8) );
		ASSERT_EQ( records.size(), 4 );
		ASSERT_TRUE( isPreamble(records[1], label, March8) );
		EXPECT_FALSE( records[1].node_added().start().has_value() );
		EXPECT_EQ( records[1].node_added().start().status(), UA_STATUSCODE_BADENCODINGERROR );
		EXPECT_TRUE( isValue(records[3], speed, 1, Time->Now()) );
	}

	//A removed group that has written all it buffered ends when the historian lets its name go:  neither the flush its
	//Close asked for nor a host that kept its handle writes the files a new group of its name now owns.
	TEST_F( GatewayFiles, RemovedGroupEndsWhenItsNameIsLetGo ){
		auto old = Library->AddGroup( {.Name="pump1", .Indexes=EIndexes::Host, .PublishingInterval=500ms} );
		Library->RemoveGroup( "pump1" );
		auto group = Library->AddGroup( {.Name="pump1", .Indexes=EIndexes::Host, .PublishingInterval=500ms} );
		Time->Advance( 0s );//the flush the old one's Close asked for.
		EXPECT_TRUE( Flush(*old) );//nothing to wait for.
		EXPECT_FALSE( old->Flushed() );
		EXPECT_FALSE( fs::exists(Path()/"pump1.flushed") );
		EXPECT_TRUE( Flush(*group) );
		EXPECT_EQ( group->Flushed(), Time->Now() );
	}

	//What a removed group buffered is still written, and then `delay` no longer runs for it.
	TEST_F( GatewayFiles, RemovedGroupIsWritten ){
		auto group = AddGroup();
		let name = group->Name();
		let speed = Join( *group, "Pump1.Speed" );
		DataChange( *group, speed, 1, Time->Now() );
		Library->RemoveGroup( name, Admin );
		EXPECT_FALSE( fs::exists(File(*group, March7)) );
		EXPECT_EQ( Time->Advance(0s), 1 );
		Settle( *group );
		let records = readFile( File(*group, March7) );
		ASSERT_EQ( records.size(), 4 );//no member is left for a preamble.
		EXPECT_EQ( records[1].node_added().node_index(), speed );
		EXPECT_TRUE( isValue(records[2], speed, 1, Time->Now()) );
		EXPECT_EQ( records[3].node_removed().user_name(), "admin" );
		EXPECT_EQ( Time->Pending(), 0 );

		let again = Library->AddGroup( {.Name=name} );//its files say that every member left.
		EXPECT_TRUE( again->Buffer().empty() );
		EXPECT_FALSE( again->Find(Node("Pump1.Speed")) );
	}

	//Until then it holds its files, which a new group of its name would write too.
	TEST_F( GatewayFiles, RemovedGroupKeepsItsNameUntilWritten ){
		auto group = AddGroup();
		let name = group->Name();
		Join( *group, "Pump1.Speed" );
		Block();
		Library->RemoveGroup( name, Admin );
		EXPECT_EQ( Time->Advance(0s), 1 );
		Settle( *group );
		EXPECT_THROW( Library->AddGroup({.Name=name}), Exception );
		Unblock();
		EXPECT_EQ( Time->Advance(1min), 1 );
		Settle( *group );
		EXPECT_EQ( readFile(File(*group, March7)).size(), 3 );
		EXPECT_NO_THROW( Library->AddGroup({.Name=name}) );
	}

	//The host's end writes what each group holds.  A group it left behind takes nothing more, and writes nothing more
	//under a lock that is gone.
	TEST_F( GatewayFiles, StopsWithItsHistorian ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		DataChange( *group, speed, 1, Time->Now() );
		let file = File( *group, March7 );
		Library.reset();
		EXPECT_EQ( readFile(file).size(), 4 );
		EXPECT_EQ( Time->Pending(), 0 );
		EXPECT_FALSE( DataChange(*group, speed, 2, Time->Now()) );
		EXPECT_THROW( Join(*group, "Pump1.Flow"), Exception );
		EXPECT_TRUE( Flush(*group) );
		EXPECT_EQ( readFile(file).size(), 4 );
	}

	//As a host runs it, on the process's executor:  `delay` flushes from a timer's thread while values arrive, and the
	//historian's end writes what is left.
	TEST_F( GatewayFiles, SystemClockFlushes ){
		Settings config{ Path()/"system" };
		config.Delay = 20ms;
		auto library = mu<Historian>( move(config), SystemClock() );
		auto group = library->AddGroup( {.Name="pump1"} );
		let speed = group->Add( {Node("Pump1.Speed"), {}, 101}, Admin );
		uint count{};
		for( let start = steady_clock::now(); !group->Flushed() && steady_clock::now()-start<10s; ++count ){
			EXPECT_TRUE( group->Enqueue(speed, Reading((double)count, Clock::now())) );
			std::this_thread::sleep_for( 1ms );
		}
		ASSERT_TRUE( group->Flushed() );
		library.reset();
		uint stored{};
		for( let& entry : fs::recursive_directory_iterator{Path()/"system"} ){//one day's file, or two around midnight.
			if( entry.path().extension()==".binpb" )
				stored += std::ranges::count_if( readFile(entry.path()), &HistoryRecord::has_value );
		}
		EXPECT_EQ( stored, count );
	}

	//One flush runs at a time, and holds no thread while its write is out.  A caller that asks during one waits for the
	//next, which takes what was buffered when it began:  so whatever a caller enqueued is in the file when its flush
	//returns, however many ask at once.
	TEST_F( GatewayFiles, FlushesFollowOneAnother ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		EXPECT_TRUE( Settle(*group) );//none is running.
		let start = Time->Now();
		constexpr uint callers{ 4 }, each{ 25 };
		std::atomic<uint> next{};
		{
			vector<std::jthread> threads;
			for( uint t=0; t<callers; ++t ){
				threads.emplace_back( [&]{
					for( uint i=0; i<each; ++i ){
						let at = ticks( start+next++*1ms );
						DataChange( *group, speed, 0, UADateTime{at}.Time() );
						EXPECT_TRUE( Flush(*group) );//so what it took, the caller's value with it, is in the file.
						EXPECT_FALSE( std::ranges::any_of(group->Buffer(), [at]( let& r ){
							let value = std::get_if<DataValue>( &r );
							return value && value->Data.sourceTimestamp==at;
						}) );
					}
				});
			}
		}
		EXPECT_TRUE( Settle(*group) );
		EXPECT_TRUE( group->Buffer().empty() );
		let file = File( *group, March7 );
		vector<Ticks> stored, sent;
		for( let& r : readFile(file) ){
			if( r.has_value() )
				stored.push_back( r.value().source_ts() );
		}
		std::ranges::sort( stored );
		for( uint n=0; n<callers*each; ++n )
			sent.push_back( ticks(start+n*1ms) );
		EXPECT_EQ( stored, sent );//each caller's own value, once.
		expectRuns( group->Runs(March7), file );
	}

	//The collection path only ever takes the buffer's lock, so it runs while a flush writes.
	TEST_F( GatewayFiles, FlushesWhileEnqueuing ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		constexpr uint count{ 5000 };
		{
			std::jthread strand{ [&]{
				for( uint i=0; i<count; ++i )
					DataChange( *group, speed, (double)i, Time->Now() );
			}};
			for( uint i=0; i<20; ++i )
				EXPECT_TRUE( Flush(*group) );
		}
		EXPECT_TRUE( Flush(*group) );
		let file = File( *group, March7 );
		EXPECT_EQ( std::ranges::count_if(readFile(file), &HistoryRecord::has_value), count );
		EXPECT_EQ( Library->Buffered(), 0 );
		expectRuns( group->Runs(March7), file );
	}
}
