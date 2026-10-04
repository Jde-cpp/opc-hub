//Day files and durability (#228):  the lock on hist.path, the two-slot .flushed file, the buffer's flush at 8 KB and at
//`delay`, what a full buffer drops and how its gap is marked, and each live file's runs.
#include "hosts.h"
#include <fstream>
#include <thread>
#include <jde/fwk/io/crc.h>
#include <jde/fwk/log/MemoryLog.h>
#include "../src/io/DayFiles.h"

#define let const auto

namespace Jde::Opc::Hist::Tests{
	using namespace std::chrono;
	using Proto::HistoryRecord;

	namespace{
		constexpr Day March6{ 2026y/March/6 }, March7{ 2026y/March/7 }, March8{ 2026y/March/8 }, March9{ 2026y/March/9 };
		Ξ utc()ι->const time_zone&{ return *locate_zone( "UTC" ); }
		Ξ ticks( TimePoint t )ι->Ticks{ return UADateTime{ t }.UA(); }
		Ω contents( const fs::path& file )ι->string{
			std::ifstream f{ file, std::ios::binary };
			return string{ std::istreambuf_iterator<char>{f}, {} };
		}
		Ω save( const fs::path& file, sv bytes )ι->void{
			std::ofstream f{ file, std::ios::binary | std::ios::trunc };
			f.write( bytes.data(), bytes.size() );
		}
		//A file's records, all of which its scan keeps, with their times absolute and its checkpoints left out.
		Ω readFile( const fs::path& file )ε->vector<HistoryRecord>{
			let scanned = Scan( file );
			EXPECT_EQ( scanned.Size, scanned.FileSize ) << file;
			std::ifstream in{ file, std::ios::binary };
			google::protobuf::io::IstreamInputStream stream{ &in };
			Reader reader{ stream, 0, scanned.Size, 0 };
			vector<HistoryRecord> y;
			for( HistoryRecord r; reader.Next(r); ){
				if( !r.has_checkpoint() )
					y.push_back( r );
			}
			EXPECT_EQ( reader.Stop(), EStop::End ) << file;
			return y;
		}
		//The runs a group holds for a file are those a scan of it rebuilds.
		Ω expectRuns( const vector<Run>& actual, const fs::path& file )ε->void{
			let expected = Scan( file ).Runs;
			ASSERT_EQ( actual.size(), expected.size() ) << file;
			for( uint i=0; i<actual.size(); ++i ){
				SCOPED_TRACE( Ƒ("run {} of {}", i, actual.size()) );
				EXPECT_EQ( actual[i].Offset, expected[i].Offset );
				EXPECT_EQ( actual[i].End, expected[i].End );
				EXPECT_EQ( actual[i].Chain, expected[i].Chain );
				EXPECT_EQ( actual[i].First, expected[i].First );
				EXPECT_EQ( actual[i].Last, expected[i].Last );
			}
		}
		Ω isValue( const HistoryRecord& r, NodeIndex index, double v, TimePoint source )ι->bool{
			return r.has_value() && r.value().node_index()==index && r.value().value().double_value()==v && r.value().source_ts()==ticks( source );
		}
		Ω isPreamble( const HistoryRecord& r, NodeIndex index, Day day )ι->bool{
			return r.has_node_added() && r.node_added().node_index()==index && r.node_added().ts()==StartOf( day, utc() ) && !r.node_added().has_identity_id();
		}
	}

	//The gateway's shape with `delay` at its default, a minute.
	struct GatewayFiles : GatewayHost{
		GatewayFiles()ι:GatewayHost{ 1min }{}
		α File( const Group& group, Day day )Ι->fs::path{ return Path()/DayDirectory( day )/( group.Name()+".binpb" ); }
		//The gateway starting again with a group's hist_group_nodes rows.
		α Rejoin( const string& name, vector<Member> members )ε->sp<Group>{
			return _group = Library->AddGroup( {.Name=name, .Indexes=EIndexes::Host, .PublishingInterval=500ms}, move(members) );
		}
		//A file where 2026's directory goes, so no day file of this year can be made, and its removal.
		α Block()Ι->void{ save( Path()/"2026", "in the way" ); }
		α Unblock()Ι->void{ fs::remove( Path()/"2026" ); }
	};
	//OpcServer's shape with `delay` at a minute.
	struct ServerFiles : ServerHost{
		ServerFiles()ι:ServerHost{ 1min }{}
		α File( Day day )Ι->fs::path{ return Path()/DayDirectory( day )/"server.binpb"; }
		//OpcServer starting again with the nodes its nodesets historize.
		α Start( vector<Member> members )ε->void{
			Restart();
			_group = Server = Library->AddGroup( {.Name="server", .Indexes=EIndexes::Issued}, move(members) );
		}
	};

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
		for( ; Time->Pending()==1; ++count )
			DataChange( *group, speed, (double)count, Time->Now()+count*1ms );
		EXPECT_GT( count, 150 );//a value is some 34 bytes of a file.
		EXPECT_LT( count, 300 );
		DataChange( *group, speed, (double)count, Time->Now()+count*1ms );
		++count;
		EXPECT_EQ( Time->Pending(), 2 );//asked for once.
		EXPECT_FALSE( fs::exists(file) );

		EXPECT_EQ( Time->Advance(0s), 1 );
		Settle( *group );
		EXPECT_TRUE( group->Buffer().empty() );
		EXPECT_EQ( std::ranges::count_if(readFile(file), &HistoryRecord::has_value), count );
		EXPECT_EQ( Time->Pending(), 1 );
		EXPECT_EQ( Time->Advance(59s), 0 );
		EXPECT_EQ( Time->Advance(1s), 1 );
		Settle( *group );
		EXPECT_EQ( group->Flushed(), Time->Now() );
	}

	//A flush sorts what it took by source time and writes each record to its own day's file, so one flush can touch
	//several.  A late record lands in its own day's file, after later ones:  a run of its own, which the group lists.
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

	//A node's first value after it joins carries the time the value last changed, which can fall in a day whose file was
	//made before the node joined.  That file gets a preamble record for the node with the value, so it still maps every
	//index it holds.
	TEST_F( GatewayFiles, MapsEachIndexAFileHolds ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		let first = Time->Now();
		DataChange( *group, speed, 1, first );
		EXPECT_TRUE( Flush(*group) );
		Time->AdvanceTo( sys_days{March8}+10min );
		let flow = Join( *group, "Pump1.Flow" );
		let changed = sys_days{March7}+23h;
		DataChange( *group, flow, 3, changed );
		EXPECT_TRUE( Flush(*group) );

		let yesterday = readFile( File(*group, March7) );
		ASSERT_EQ( yesterday.size(), 6 );
		EXPECT_TRUE( isPreamble(yesterday[4], flow, March7) );
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
		EXPECT_EQ( Time->Pending(), 1 );
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
		for( uint i=0; Time->Pending()==1; ++i )//the buffer's size counts again.
			DataChange( *group, speed, i, Time->Now() );
		EXPECT_EQ( Time->Pending(), 2 );
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
		let start = Time->Now();
		let at = [&]( uint n ){ return start+n*10ms; };
		DataChange( *pump1, flow, 0, at(0) );//the oldest of all, and Flow's only one.
		constexpr uint count{ 60 };
		for( uint n=1; n<=count; ++n ){
			DataChange( n%2 ? *pump1 : *pump2, n%2 ? speed : other, n, at(n) );
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
						EXPECT_TRUE( Flush(*group) );
						EXPECT_TRUE( std::ranges::any_of(group->Runs(March7), [at]( let& run ){ return run.First<=at && at<=run.Last; }) );
					}
				});
			}
		}
		EXPECT_TRUE( Settle(*group) );
		EXPECT_TRUE( group->Buffer().empty() );
		let file = File( *group, March7 );
		EXPECT_EQ( std::ranges::count_if(readFile(file), &HistoryRecord::has_value), callers*each );
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
