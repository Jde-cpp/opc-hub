//Archives (#229):  the midnight rewrite of a day's live file as a merge of its runs, a late record merged into the
//archive it belongs to, and what a start recovers - the temp file of a rewrite cut short, and the live files a midnight
//left behind.  The injected clock runs midnight, in UTC and in a zone across its DST change.
#include "dayFiles.h"
#include <jde/opc/proto/opc.Common.h>

#define let const auto

namespace Jde::Opc::Hist::Tests{
	using namespace std::chrono;
	using Proto::HistoryRecord;

	namespace{
		//An archive's records after its FileStart:  in time order, with no checkpoint among them.
		Ω archived( const fs::path& file, uint32_t generation, Day day, const time_zone& tz=utc() )ε->vector<HistoryRecord>{
			let bytes = contents( file );
			google::protobuf::io::ArrayInputStream in{ bytes.data(), (int)bytes.size() };
			Reader reader{ in, 0, bytes.size(), 0 };
			vector<HistoryRecord> y;
			HistoryRecord r;
			if( !reader.Next(r) || !r.has_file_start() ){
				ADD_FAILURE() << file << " opens with no FileStart.";
				return y;
			}
			EXPECT_EQ( r.file_start().generation(), generation ) << file;
			EXPECT_EQ( r.file_start().ts(), StartOf(day, tz) );
			EXPECT_EQ( r.file_start().crc(), StartCrc(r.file_start()) );
			auto last = r.file_start().ts();
			while( reader.Next(r) ){
				let t = PrimaryTime( r );
				EXPECT_TRUE( t && *t>=last ) << r.ShortDebugString();
				last = t.value_or( last );
				y.push_back( r );
			}
			EXPECT_EQ( reader.Stop(), EStop::End ) << file;
			EXPECT_TRUE( std::ranges::none_of(y, &HistoryRecord::has_checkpoint) ) << file;
			return y;
		}
		//A live file's is 0.
		Ω generation( const fs::path& file )ε->uint32_t{
			let start = ReadStart( file );
			if( !start )
				ADD_FAILURE() << file << " opens with no FileStart.";
			return start ? start->generation() : std::numeric_limits<uint32_t>::max();
		}
		Ω temp( const fs::path& file )ι->fs::path{ return fs::path{ file }+=".tmp"; }
	}

	//`delay` after midnight, by when the day's last flush has landed, yesterday's live file is rewritten as its archive:
	//its runs merged in source-time order, preamble first, ties going to the earlier run, under a FileStart of generation
	//1 and with no checkpoint.  Until then it is live, and today's stays so.
	TEST_F( GatewayFiles, MidnightRewritesTheDay ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		let joined = Time->Now();
		DataChange( *group, speed, 2, joined+2s );
		DataChange( *group, speed, 4, joined+4s );
		EXPECT_TRUE( Flush(*group) );
		DataChange( *group, speed, 3, joined+3s );//late:  a run inside the one before it.
		DataChange( *group, speed, 5, joined+5s );
		EXPECT_TRUE( Flush(*group) );
		DataChange( *group, speed, 4.5, joined+4s );//at a time the file holds, so after the record that is there.
		DataChange( *group, speed, 1, joined+1s );
		EXPECT_TRUE( Flush(*group) );
		let file = File( *group, March7 );
		EXPECT_EQ( group->Runs(March7).size(), 4 );

		Time->AdvanceTo( sys_days{March8}+59s );
		Settle( *group );
		EXPECT_EQ( generation(file), 0 );
		EXPECT_EQ( group->Runs(March7).size(), 4 );
		DataChange( *group, speed, 6, sys_days{March8}-1s );//yesterday's last, which the flush at `delay` lands.
		DataChange( *group, speed, 7, sys_days{March8}+30s );
		Time->AdvanceTo( sys_days{March8}+1min );
		Settle( *group );

		let records = archived( file, 1, March7 );
		ASSERT_EQ( records.size(), 9 );
		EXPECT_TRUE( isPreamble(records[0], speed, March7) );
		EXPECT_EQ( records[1].node_added().ts(), ticks(joined) );
		EXPECT_EQ( records[1].node_added().user_name(), "admin" );
		EXPECT_TRUE( isValue(records[2], speed, 1, joined+1s) );
		EXPECT_TRUE( isValue(records[3], speed, 2, joined+2s) );
		EXPECT_TRUE( isValue(records[4], speed, 3, joined+3s) );
		EXPECT_TRUE( isValue(records[5], speed, 4, joined+4s) );
		EXPECT_TRUE( isValue(records[6], speed, 4.5, joined+4s) );
		EXPECT_TRUE( isValue(records[7], speed, 5, joined+5s) );
		EXPECT_TRUE( isValue(records[8], speed, 6, sys_days{March8}-1s) );
		EXPECT_EQ( records[8].value().server_ts(), ticks(sys_days{March8}-1s+5ms) );
		EXPECT_TRUE( group->Runs(March7).empty() );
		EXPECT_FALSE( fs::exists(temp(file)) );

		let today = readFile( File(*group, March8) );
		ASSERT_EQ( today.size(), 3 );
		EXPECT_EQ( today[0].file_start().generation(), 0 );
		EXPECT_TRUE( isPreamble(today[1], speed, March8) );
		EXPECT_EQ( today[1].node_added().start().value().double_value(), 6 );
		EXPECT_TRUE( isValue(today[2], speed, 7, sys_days{March8}+30s) );
		expectRuns( group->Runs(March8), File(*group, March8) );
	}

	//A record goes in the file of its day in timeZone, and that day's midnight is the zone's:  New York's 7th ends at
	//05:00 UTC, and its 8th, when the clocks go forward, 23 hours later.
	TEST_F( GatewayFiles, MidnightFollowsTheTimeZoneAcrossDST ){
		let& tz = *locate_zone( "America/New_York" );
		auto config = Config( 1min );
		config.TimeZone = &tz;
		Restart( move(config) );
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );//12:00 EST on the 7th.
		let evening = sys_days{March8}+3h;//22:00 EST on the 7th.
		Time->AdvanceTo( evening );
		DataChange( *group, speed, 1, evening );
		EXPECT_TRUE( Flush(*group) );
		let seventh = File( *group, March7 );
		ASSERT_TRUE( fs::exists(seventh) );
		EXPECT_FALSE( fs::exists(File(*group, March8)) );

		Time->AdvanceTo( sys_days{March8}+5h+59s );//past UTC's midnight, and 59 s past New York's.
		Settle( *group );
		EXPECT_EQ( generation(seventh), 0 );
		Time->AdvanceTo( sys_days{March8}+5h+1min );
		Settle( *group );
		auto records = archived( seventh, 1, March7, tz );
		ASSERT_EQ( records.size(), 3 );
		EXPECT_TRUE( isPreamble(records[0], speed, March7, tz) );
		EXPECT_TRUE( isValue(records[2], speed, 1, evening) );

		const TimePoint night{ sys_days{March8}+6h }, late{ sys_days{March9}+3h+30min };//01:00 EST and 23:30 EDT on the 8th.
		DataChange( *group, speed, 2, night );
		Time->AdvanceTo( late );
		DataChange( *group, speed, 3, late );
		EXPECT_TRUE( Flush(*group) );
		let eighth = File( *group, March8 );
		Time->AdvanceTo( sys_days{March9}+4h+59s );//00:00:59 EDT on the 9th.
		Settle( *group );
		EXPECT_EQ( generation(eighth), 0 );
		DataChange( *group, speed, 4, sys_days{March9}+4h+30s );
		Time->AdvanceTo( sys_days{March9}+4h+1min );
		Settle( *group );
		records = archived( eighth, 1, March8, tz );
		ASSERT_EQ( records.size(), 3 );
		EXPECT_TRUE( isPreamble(records[0], speed, March8, tz) );
		EXPECT_EQ( records[0].node_added().start().value().double_value(), 1 );
		EXPECT_TRUE( isValue(records[1], speed, 2, night) );
		EXPECT_TRUE( isValue(records[2], speed, 3, late) );
		let ninth = readFile( File(*group, March9) );
		ASSERT_EQ( ninth.size(), 3 );
		EXPECT_EQ( ninth[0].file_start().generation(), 0 );
		EXPECT_EQ( ninth[0].file_start().ts(), ticks(sys_days{March9}+4h) );
		EXPECT_TRUE( isValue(ninth[2], speed, 4, sys_days{March9}+4h+30s) );
	}

	//In the `delay` after midnight yesterday's file is still live:  one made then is a live file, which the rewrite at
	//`delay` archives.  A group started in that `delay` runs that rewrite too.
	TEST_F( GatewayFiles, YesterdayIsLiveUntilItsRewrite ){
		Time->AdvanceTo( sys_days{March8}+10s );
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		DataChange( *group, speed, 1, sys_days{March8}-10s );
		EXPECT_TRUE( Flush(*group) );
		let file = File( *group, March7 );
		EXPECT_EQ( generation(file), 0 );
		expectRuns( group->Runs(March7), file );
		DataChange( *group, speed, 2, sys_days{March8}-5s );
		EXPECT_TRUE( Flush(*group) );
		EXPECT_EQ( group->Runs(March7).size(), 3 );//appended to.

		Time->AdvanceTo( sys_days{March8}+1min );
		Settle( *group );
		let records = archived( file, 1, March7 );
		ASSERT_EQ( records.size(), 3 );
		EXPECT_TRUE( isValue(records[1], speed, 1, sys_days{March8}-10s) );
		EXPECT_TRUE( isValue(records[2], speed, 2, sys_days{March8}-5s) );
	}

	//A late record for an archived day is merged into place, after any record the archive holds at its time, by a rewrite
	//that increments the generation.  A node the archive doesn't map gets its preamble record there, in the preamble.
	TEST_F( GatewayFiles, LateRecordIsMergedIntoItsArchive ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		let joined = Time->Now();
		DataChange( *group, speed, 1, joined+1s );
		DataChange( *group, speed, 3, joined+3s );
		EXPECT_TRUE( Flush(*group) );
		Time->AdvanceTo( sys_days{March8}+10min );
		Settle( *group );
		let file = File( *group, March7 );
		ASSERT_EQ( generation(file), 1 );

		let flow = Join( *group, "Pump1.Flow" );
		DataChange( *group, speed, 2, joined+2s );
		DataChange( *group, speed, 3.5, joined+3s );
		DataChange( *group, flow, 9, joined+2s );
		EXPECT_TRUE( Flush(*group) );
		let records = archived( file, 2, March7 );
		ASSERT_EQ( records.size(), 8 );
		EXPECT_TRUE( isPreamble(records[0], speed, March7) );
		EXPECT_TRUE( isPreamble(records[1], flow, March7) );
		EXPECT_FALSE( records[1].node_added().has_start() );
		EXPECT_EQ( records[2].node_added().user_name(), "admin" );
		EXPECT_TRUE( isValue(records[3], speed, 1, joined+1s) );
		EXPECT_TRUE( isValue(records[4], speed, 2, joined+2s) );
		EXPECT_TRUE( isValue(records[5], flow, 9, joined+2s) );
		EXPECT_TRUE( isValue(records[6], speed, 3, joined+3s) );
		EXPECT_TRUE( isValue(records[7], speed, 3.5, joined+3s) );
		EXPECT_TRUE( group->Runs(March7).empty() );
		EXPECT_FALSE( fs::exists(temp(file)) );
		EXPECT_EQ( group->Flushed(), Time->Now() );

		let today = readFile( File(*group, March8) );//made by the same flush, from what the archive now holds.
		ASSERT_EQ( today.size(), 4 );
		EXPECT_EQ( today[1].node_added().start().value().double_value(), 3.5 );
		EXPECT_EQ( today[2].node_added().start().value().double_value(), 9 );
		EXPECT_EQ( today[3].node_added().node_index(), flow );
	}

	//A file made for a day already past is its archive from the start.  The clock's flushes rewrite an archive at most
	//once per `delay`:  a late record that arrives sooner is held, with .flushed, while the other days are written.
	TEST_F( GatewayFiles, ArchiveIsRewrittenOncePerDelay ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		let late = sys_days{March6}+12h;
		DataChange( *group, speed, 1, late );
		EXPECT_TRUE( Flush(*group) );
		let file = File( *group, March6 );
		ASSERT_EQ( archived(file, 1, March6).size(), 2 );
		let flushed = group->Flushed();

		Time->Advance( 10s );
		DataChange( *group, speed, 2, late+1s );
		uint count{};
		for( ; Time->Pending()==2; ++count )//to the flush at 8 KB.
			DataChange( *group, speed, count, Time->Now()+count*1ms );
		EXPECT_EQ( Time->Advance(0s), 1 );
		Settle( *group );
		let held = group->Buffer();
		ASSERT_EQ( held.size(), 1 );
		EXPECT_EQ( get<DataValue>(held[0]).Data.Get<double>(0), 2 );
		EXPECT_EQ( generation(file), 1 );
		EXPECT_EQ( group->Flushed(), flushed );
		EXPECT_EQ( std::ranges::count_if(readFile(File(*group, March7)), &HistoryRecord::has_value), count );

		EXPECT_EQ( Time->Advance(1min), 1 );
		Settle( *group );
		EXPECT_TRUE( group->Buffer().empty() );
		let records = archived( file, 2, March6 );
		ASSERT_EQ( records.size(), 3 );
		EXPECT_TRUE( isValue(records[2], speed, 2, late+1s) );
		EXPECT_EQ( group->Flushed(), Time->Now() );
	}

	//A clock set back after an archive's rewrite doesn't stretch its hold past `delay`:  a rewrite stamped after now has
	//expired, so the flush at 8 KB merges the late record at once.
	TEST_F( GatewayFiles, ClockSetBackEndsTheHold ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		let late = sys_days{March6}+12h;
		DataChange( *group, speed, 1, late );
		EXPECT_TRUE( Flush(*group) );
		let file = File( *group, March6 );
		let pending = Time->Pending();

		Time->Step( -1h );
		DataChange( *group, speed, 2, late+1s );
		for( uint i=0; Time->Pending()==pending; ++i )//to the flush at 8 KB.
			DataChange( *group, speed, i, Time->Now()+i*1ms );
		EXPECT_EQ( Time->Advance(0s), 1 );
		Settle( *group );
		EXPECT_TRUE( group->Buffer().empty() );
		EXPECT_EQ( archived(file, 2, March6).size(), 3 );
	}

	//A flush that only held an archive's records holds back no other day's:  the first record for one lets the flush at
	//8 KB run again, which writes it and holds the archive's once more.
	TEST_F( GatewayFiles, DeferredArchiveHoldsOnlyItsOwn ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		let late = sys_days{March6}+12h;
		DataChange( *group, speed, 1, late );
		EXPECT_TRUE( Flush(*group) );
		let file = File( *group, March6 );
		ASSERT_EQ( generation(file), 1 );
		let pending = Time->Pending();

		Time->Advance( 10s );
		uint held{};
		for( ; Time->Pending()==pending; ++held )//to the flush at 8 KB, all for the archive just rewritten.
			DataChange( *group, speed, held, late+1s+held*1ms );
		EXPECT_EQ( Time->Advance(0s), 1 );
		Settle( *group );
		EXPECT_EQ( group->Buffer().size(), held );
		EXPECT_EQ( generation(file), 1 );

		uint count{};
		for( ; Time->Pending()==pending && count<10'000; ++count )//to the flush at 8 KB, for today.
			DataChange( *group, speed, count, Time->Now()+count*1ms );
		ASSERT_EQ( Time->Pending(), pending+1 );
		EXPECT_EQ( Time->Advance(0s), 1 );
		Settle( *group );
		EXPECT_EQ( group->Buffer().size(), held );
		EXPECT_EQ( generation(file), 1 );
		EXPECT_EQ( std::ranges::count_if(readFile(File(*group, March7)), &HistoryRecord::has_value), count );
	}

	//A merge into an archive that the last one wrote takes what that one knew of it, rather than scanning it again:  its
	//size, and the nodes it maps, so a node it maps gets no second NodeAdded.
	TEST_F( GatewayFiles, MergeKeepsWhatItWrote ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		let late = sys_days{March6}+12h;
		DataChange( *group, speed, 1, late );
		EXPECT_TRUE( Flush(*group) );
		let file = File( *group, March6 );
		let flow = Join( *group, "Pump1.Flow" );
		for( uint i=0; i<2; ++i ){
			DataChange( *group, flow, i, late+(i+1)*1s );
			EXPECT_TRUE( Flush(*group) );
		}
		let records = archived( file, 3, March6 );
		EXPECT_EQ( std::ranges::count_if(records, &HistoryRecord::has_value), 3 );
		EXPECT_EQ( std::ranges::count_if(records, [flow]( let& r ){ return r.has_node_added() && r.node_added().node_index()==flow; }), 1 );
	}

	//Unless the buffers are past maxBuffer:  a group that can write is flushed rather than trimmed, its archives' records
	//with the rest.
	TEST_F( GatewayFiles, FullBuffersMergeAtOnce ){
		auto config = Config( 1min );
		config.MaxBuffer = 20'000;//below the floor the hist block sets, so the cap comes long before 8 KB.
		Restart( move(config) );
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		let late = sys_days{March6}+12h;
		DataChange( *group, speed, 1, late );
		EXPECT_TRUE( Flush(*group) );
		let file = File( *group, March6 );
		ASSERT_EQ( generation(file), 1 );

		DataChange( *group, speed, 2, late+1s );
		for( uint i=0; Time->Pending()==2; ++i )
			DataChange( *group, speed, i, Time->Now()+i*1ms );
		EXPECT_EQ( Time->Advance(0s), 1 );
		Settle( *group );
		EXPECT_TRUE( group->Buffer().empty() );
		EXPECT_EQ( archived(file, 2, March6).size(), 3 );
	}

	//A rewrite never holds the day whole:  it reads each run as the merge reaches it, here six that all overlap, and
	//writes its temp file a part at a time.
	TEST_F( GatewayFiles, RewriteGoesInParts ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		let start = Time->Now();
		constexpr uint count{ 60'000 }, step{ 7919 };//a prime, so each millisecond is some value's.
		for( uint i=0; i<count; ++i ){
			DataChange( *group, speed, i, start+(i*step%count)*1ms );
			if( i%10'000==9'999 )
				EXPECT_TRUE( Flush(*group) );
		}
		let file = File( *group, March7 );
		EXPECT_EQ( group->Runs(March7).size(), 7 );
		Time->AdvanceTo( sys_days{March8}+1min );
		Settle( *group );

		EXPECT_GT( fs::file_size(file), 1u<<20 );
		let records = archived( file, 1, March7 );
		ASSERT_EQ( records.size(), count+2 );
		for( uint k=0; k<count; ++k ){
			let& value = records[k+2].value();
			ASSERT_EQ( value.source_ts(), ticks(start+k*1ms) );
			ASSERT_EQ( (uint)value.value().double_value()*step%count, k );
		}
	}

	//A merge closes the file at its end, while its Rewrite still holds it, so the rewrite's temp file can be renamed over
	//it:  Windows opened it without FILE_SHARE_DELETE.
	TEST_F( GatewayFiles, MergeClosesItsFileAtItsEnd ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		let start = Time->Now();
		for( uint i=0; i<2; ++i ){
			DataChange( *group, speed, i, start+i*1s );
			EXPECT_TRUE( Flush(*group) );
		}
		let file = Path()/"merged.binpb";
		fs::copy_file( File(*group, March7), file );
		Merge merge{ ms<ReadHandle>(file), Scan(file).Runs, {} };
		HistoryRecord r;
		uint values{};
		while( merge.Next(r) )
			values += r.has_value();
		EXPECT_EQ( values, 2 );
#ifndef _WIN32
		for( let& fd : fs::directory_iterator{"/proc/self/fd"} ){
			std::error_code ec;
			EXPECT_NE( fs::read_symlink(fd.path(), ec), file );
		}
#endif
		let temp = Path()/"merged.tmp";
		save( temp, "rewritten" );
		std::error_code ec;
		fs::rename( temp, file, ec );
		EXPECT_FALSE( ec ) << ec.message();
	}

	//A node removed and re-added with no writer, both copied to the start of a later day's file, stays a member in that
	//day's archive:  the re-add isn't folded into the preamble record before its NodeRemoved as a corrected start value.
	TEST_F( GatewayFiles, ReAddSurvivesTheRewrite ){
		auto group = AddGroup();
		let name = group->Name();
		let speed = Join( *group, "Pump1.Speed" );
		DataChange( *group, speed, 1, Time->Now()+days{1} );//a source clock a day ahead, which makes tomorrow's file.
		EXPECT_TRUE( Flush(*group) );
		group->Remove( speed );
		group->Add( {Node("Pump1.Speed"), {}, speed} );
		EXPECT_TRUE( Flush(*group) );
		Time->AdvanceTo( sys_days{March9}+1min );//both midnights' rewrites.
		Settle( *group );

		let records = archived( File(*group, March8), 1, March8 );
		let added = std::ranges::count_if( records, [speed]( let& r ){ return r.has_node_added() && r.node_added().node_index()==speed; } );
		EXPECT_EQ( added, 2 );
		Restart();
		let again = Rejoin( name, {{Node("Pump1.Speed"), {}, speed}} );
		EXPECT_EQ( again->Find(Node("Pump1.Speed")), speed );
		EXPECT_TRUE( Records<NodeAdded>().empty() );//restored a member, not added again.
	}

	//A start that can't tell about an older day's file, below the newest, starts all the same:  the day is taken as live,
	//and its rewrite tries again until it can read the file.
	TEST_F( ServerFiles, StartPassesAnUnreadableOlderDay ){
		let speed = Historize( "Pump1.Speed" );
		SetValue( speed, 1 );
		EXPECT_TRUE( Flush(*Server) );
		Time->AdvanceTo( sys_days{March8}+30s );//inside yesterday's `delay`, so its file is still live.
		Settle( *Server );
		SetValue( speed, 2 );
		EXPECT_TRUE( Flush(*Server) );
		let file = File( March7 );
		let dir = file.parent_path();
		fs::permissions( dir, fs::perms::none, fs::perm_options::replace );
		std::error_code ec;
		if( fs::exists(file, ec) || !ec ){
			fs::permissions( dir, fs::perms::owner_all, fs::perm_options::replace );
			GTEST_SKIP() << "This user can stat a file in a directory it can't search.";
		}

		EXPECT_NO_THROW( Start({{Node("Pump1.Speed")}}) );
		EXPECT_EQ( Time->Advance(0s), 1 );//the rewrite, which still can't read it.
		Settle( *Server );
		fs::permissions( dir, fs::perms::owner_all, fs::perm_options::replace );
		EXPECT_EQ( generation(file), 0 );
		Time->Advance( 1min );
		Settle( *Server );
		EXPECT_EQ( archived(file, 1, March7).size(), 3 );
	}

	//A removed group's live files, today's included, are rewritten as their archives by the flush that writes the removal:
	//no midnight comes for a group that is gone, and no start adds it again.  One whose rewrite fails holds the group until
	//`delay` writes it.
	TEST_F( GatewayFiles, RemovalArchivesTheGroup ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		DataChange( *group, speed, 1, Time->Now() );
		EXPECT_TRUE( Flush(*group) );
		Time->AdvanceTo( sys_days{March8}+30s );//inside yesterday's `delay`, so its file is still live.
		Settle( *group );
		DataChange( *group, speed, 2, Time->Now() );
		EXPECT_TRUE( Flush(*group) );
		let yesterday = File( *group, March7 ), today = File( *group, March8 );
		ASSERT_EQ( generation(yesterday), 0 );
		ASSERT_EQ( generation(today), 0 );
		fs::create_directories( temp(today) );//where its rewrite's temp file goes, which no Abandon removes once it isn't empty.
		save( temp(today)/"in the way", "" );

		Library->RemoveGroup( group->Name() );
		EXPECT_EQ( Time->Advance(0s), 1 );
		Settle( *group );
		EXPECT_EQ( std::ranges::count_if(archived(yesterday, 1, March7), &HistoryRecord::has_value), 1 );
		EXPECT_EQ( generation(today), 0 );
		EXPECT_GT( Time->Pending(), 0 );

		fs::remove_all( temp(today) );
		Time->Advance( 2min );//yesterday's midnight timer, a no-op now, then `delay`'s.
		Settle( *group );
		let records = archived( today, 1, March8 );
		EXPECT_EQ( std::ranges::count_if(records, &HistoryRecord::has_value), 1 );
		EXPECT_TRUE( !records.empty() && records.back().has_node_removed() );
		EXPECT_EQ( Time->Pending(), 0 );
	}

	//A start removes the temp file of a rewrite that a crash cut short, and rewrites each file still live from the day
	//before the last flush's through yesterday:  here a midnight the process was down for.
	TEST_F( ServerFiles, StartRewritesWhatAMidnightLeft ){
		let speed = Historize( "Pump1.Speed" );
		SetValue( speed, 1 );
		EXPECT_TRUE( Flush(*Server) );
		Time->Advance( 1h );
		SetValue( speed, 2 );
		EXPECT_TRUE( Flush(*Server) );
		let file = File( March7 );
		save( temp(file), "half a rewrite" );

		Time = ms<ManualClock>( sys_days{March9}+10h );//down over two midnights.
		Start( {{Node("Pump1.Speed")}} );
		EXPECT_FALSE( fs::exists(temp(file)) );
		EXPECT_EQ( generation(file), 0 );
		EXPECT_EQ( Time->Advance(0s), 1 );
		Settle( *Server );
		let records = archived( file, 1, March7 );
		ASSERT_EQ( records.size(), 4 );
		EXPECT_TRUE( isPreamble(records[0], speed, March7) );
		EXPECT_TRUE( records[1].has_node_added() );
		EXPECT_EQ( records[2].value().value().double_value(), 1 );
		EXPECT_EQ( records[3].value().value().double_value(), 2 );
		EXPECT_FALSE( fs::exists(temp(file)) );

		Start( {{Node("Pump1.Speed")}} );//nothing is left to rewrite, and the archive restores the group.
		EXPECT_TRUE( Server->Buffer().empty() );
		EXPECT_EQ( Server->Find(Node("Pump1.Speed")), speed );
		EXPECT_EQ( Time->Advance(0s), 0 );
		SetValue( speed, 3 );
		EXPECT_TRUE( Flush(*Server) );
		let today = readFile( File(March9) );
		ASSERT_EQ( today.size(), 4 );
		EXPECT_EQ( today[1].node_added().start().value().double_value(), 2 );
		EXPECT_EQ( today[2].value().status(), UA_STATUSCODE_BADDATALOST );//the stop's marker.
		EXPECT_EQ( today[3].value().value().double_value(), 3 );
		EXPECT_EQ( generation(file), 1 );
	}

	//A process that ends in the `delay` after midnight leaves yesterday's file live.  The next start rewrites it without
	//waiting out the `delay`, since nothing is buffered for it, and leaves today's live.
	TEST_F( ServerFiles, StartRewritesYesterdayInsideTheDelay ){
		let speed = Historize( "Pump1.Speed" );
		SetValue( speed, 1 );
		Time->AdvanceTo( sys_days{March8}+30s );
		Settle( *Server );
		SetValue( speed, 2 );
		EXPECT_TRUE( Flush(*Server) );
		ASSERT_EQ( generation(File(March7)), 0 );

		Start( {{Node("Pump1.Speed")}} );
		EXPECT_EQ( Time->Advance(0s), 1 );
		Settle( *Server );
		EXPECT_EQ( archived(File(March7), 1, March7).size(), 3 );
		EXPECT_EQ( generation(File(March8)), 0 );
		expectRuns( Server->Runs(March8), File(March8) );
	}

	//A live file holds a corrected start value as a later preamble record of its member, which a reader takes in place
	//of the first.  The rewrite folds it into the preamble:  one record a member, with its last start value, or none.
	TEST_F( GatewayFiles, RewriteFoldsCorrectedStartValues ){
		let name = "pump1"s;
		let file = Path()/DayDirectory( March6 )/( name+".binpb" );
		let start = StartOf( March6, utc() );
		let preamble = [&]( NodeIndex index, sv id, optional<double> value ){
			HistoryRecord y;
			auto& added = *y.mutable_node_added();
			added.set_node_index( index );
			*added.mutable_node() = ProtoUtils::ToExNodeId( Node(id) );
			added.set_ts( start );
			if( value ){
				added.mutable_start()->set_source_ts( start-1 );
				added.mutable_start()->mutable_value()->set_double_value( *value );
			}
			return y;
		};
		string bytes;
		Appender opening{ bytes, 0 };
		HistoryRecord first;
		first.mutable_file_start()->set_ts( start );
		opening.Add( move(first) );
		opening.Add( preamble(101, "Pump1.Speed", 1) );
		opening.Add( preamble(102, "Pump1.Flow", 5) );
		auto chain = opening.Seal();
		Appender values{ bytes, chain };
		HistoryRecord stored;
		stored.mutable_value()->set_node_index( 101 );
		stored.mutable_value()->set_source_ts( start+1 );
		stored.mutable_value()->mutable_value()->set_double_value( 2 );
		values.Add( move(stored) );
		chain = values.Seal();
		Appender corrected{ bytes, chain };
		corrected.Add( preamble(101, "Pump1.Speed", 1.5) );//as a late record before the day leaves it.
		corrected.Add( preamble(102, "Pump1.Flow", nullopt) );//and a delete of the only one.
		corrected.Seal();
		fs::create_directories( file.parent_path() );
		save( file, bytes );

		auto group = Rejoin( name, {{Node("Pump1.Speed"), {}, 101}, {Node("Pump1.Flow"), {}, 102}} );
		EXPECT_TRUE( group->Buffer().empty() );
		EXPECT_EQ( Time->Advance(0s), 1 );//a group that never flushed rewrites what it found too.
		Settle( *group );
		let records = archived( file, 1, March6 );
		ASSERT_EQ( records.size(), 3 );
		EXPECT_TRUE( isPreamble(records[0], 101, March6) );
		EXPECT_EQ( records[0].node_added().start().value().double_value(), 1.5 );
		EXPECT_TRUE( isPreamble(records[1], 102, March6) );
		EXPECT_FALSE( records[1].node_added().has_start() );
		EXPECT_EQ( records[2].value().value().double_value(), 2 );
	}

	//A live file that no longer reads as the historian wrote it is not rewritten, which would drop what follows the
	//damage:  it is left as it is, with no temp file, and scanned again, which refuses it.
	TEST_F( GatewayFiles, DamagedDayIsLeftAsItIs ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		let joined = Time->Now();
		DataChange( *group, speed, 1, joined+1s );
		EXPECT_TRUE( Flush(*group) );
		DataChange( *group, speed, 2, joined+2s );
		EXPECT_TRUE( Flush(*group) );
		let file = File( *group, March7 );
		let runs = group->Runs( March7 );
		ASSERT_EQ( runs.size(), 3 );
		auto bytes = contents( file );
		std::fill_n( bytes.begin()+runs[1].Offset+3, 16, '\0' );//a zeroed sector, before a run a historian sealed.
		save( file, bytes );

		Time->AdvanceTo( sys_days{March8}+1min );
		Settle( *group );
		EXPECT_EQ( contents(file), bytes );
		EXPECT_FALSE( fs::exists(temp(file)) );
		DataChange( *group, speed, 3, joined+3s );
		EXPECT_FALSE( Flush(*group) );//dropped, rather than held for a file that will never take it.
		EXPECT_TRUE( group->Buffer().empty() );
		EXPECT_EQ( contents(file), bytes );
		DataChange( *group, speed, 4, Time->Now() );
		EXPECT_TRUE( Flush(*group) );//today's is written as ever.
		EXPECT_TRUE( isValue(readFile(File(*group, March8)).back(), speed, 4, Time->Now()) );
	}

	//So is an archive that can't be read through:  a merge would drop what follows the damage.
	TEST_F( GatewayFiles, DamagedArchiveIsLeftAsItIs ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		let late = sys_days{March6}+12h;
		DataChange( *group, speed, 1, late );
		DataChange( *group, speed, 2, late+1s );
		EXPECT_TRUE( Flush(*group) );
		let file = File( *group, March6 );
		auto bytes = contents( file );
		ASSERT_EQ( archived(file, 1, March6).size(), 3 );
		std::fill_n( bytes.end()-40, 16, '\xFF' );//over a length or a tag, wherever it falls.
		save( file, bytes );

		DataChange( *group, speed, 3, late+2s );
		EXPECT_FALSE( Flush(*group) );
		EXPECT_TRUE( group->Buffer().empty() );
		EXPECT_EQ( contents(file), bytes );
		EXPECT_FALSE( fs::exists(temp(file)) );
	}

	//A rewrite that can't make its temp file leaves the day as it was, and its records in the buffer for `delay` to
	//try again.  What is in the way is left there:  only a temp file a rewrite made is removed.
	TEST_F( GatewayFiles, FailedRewriteIsRetried ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		let late = sys_days{March6}+12h;
		DataChange( *group, speed, 1, late );
		EXPECT_TRUE( Flush(*group) );
		let file = File( *group, March6 );
		let before = contents( file );
		fs::create_directories( temp(file) );//where the temp file goes.

		DataChange( *group, speed, 2, late+1s );
		for( uint i=0; i<2; ++i ){
			SCOPED_TRACE( Ƒ("attempt {}", i) );
			EXPECT_FALSE( Flush(*group) );
			EXPECT_EQ( group->Buffer().size(), 1 );
			EXPECT_EQ( contents(file), before );
			EXPECT_TRUE( fs::is_directory(temp(file)) );
		}
		fs::remove( temp(file) );
		EXPECT_EQ( Time->Advance(1min), 1 );
		Settle( *group );
		EXPECT_TRUE( group->Buffer().empty() );
		EXPECT_EQ( archived(file, 2, March6).size(), 3 );
	}

	//A midnight rewrite that keeps failing is found by a start however far the flushes have moved on since:  .flushed
	//names the oldest day whose file is still live, here below a later day's archive.
	TEST_F( ServerFiles, StartRewritesAFailingMidnight ){
		let speed = Historize( "Pump1.Speed" );
		SetValue( speed, 1 );
		EXPECT_TRUE( Flush(*Server) );
		let file = File( March7 );
		fs::create_directories( temp(file) );//where the rewrite's temp file goes, which no Abandon removes once it isn't empty.
		save( temp(file)/"in the way", "" );
		for( let day : {March8, March9} ){
			Time->AdvanceTo( sys_days{day}+1h );
			Settle( *Server );
			SetValue( speed, 2 );
			EXPECT_TRUE( Flush(*Server) );
		}
		EXPECT_EQ( generation(file), 0 );
		EXPECT_EQ( generation(File(March8)), 1 );

		fs::remove_all( temp(file) );//the space freed, and the process started again.
		Start( {{Node("Pump1.Speed")}} );
		EXPECT_EQ( Time->Advance(0s), 1 );
		Settle( *Server );
		EXPECT_EQ( archived(file, 1, March7).size(), 3 );
	}

	//A rewrite's rename whose directory can't be fsynced holds .flushed back, through the flushes after it too, until a
	//flush's fsync succeeds:  here a midnight rewrite, which has no records of its own.
	TEST_F( GatewayFiles, UnsyncedRenameHoldsFlushed ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		let start = Time->Now();
		constexpr uint count{ 60'000 };//past a part, so only the rename's fsync opens the directory, not the temp file's writes.
		for( uint i=0; i<count; ++i ){
			DataChange( *group, speed, i, start+i*1ms );
			if( i%10'000==9'999 )
				EXPECT_TRUE( Flush(*group) );
		}
		let flushed = group->Flushed();
		let file = File( *group, March7 );
		let dir = file.parent_path();
		fs::permissions( dir, fs::perms::owner_read, fs::perm_options::remove );//so the fsync's open fails.
		std::error_code ec;
		(void)fs::directory_iterator{ dir, ec };//the open the fsync makes.
		if( !ec ){
			fs::permissions( dir, fs::perms::owner_read, fs::perm_options::add );
			GTEST_SKIP() << "This user opens a directory it can't read.";
		}
		Time->AdvanceTo( sys_days{March8}+1min );
		Settle( *group );
		EXPECT_EQ( generation(file), 1 );
		EXPECT_EQ( group->Flushed(), flushed );

		DataChange( *group, speed, 1, Time->Now() );
		EXPECT_FALSE( Flush(*group) );
		EXPECT_EQ( std::ranges::count_if(readFile(File(*group, March8)), &HistoryRecord::has_value), 1 );
		EXPECT_EQ( group->Flushed(), flushed );

		fs::permissions( dir, fs::perms::owner_read, fs::perm_options::add );
		Time->Advance( 1s );
		EXPECT_TRUE( Flush(*group) );
		EXPECT_EQ( group->Flushed(), Time->Now() );
	}
}
