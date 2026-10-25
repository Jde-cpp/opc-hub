//Ported from apps/OpcGateway/tests/LogTests.cpp - everything in that file that needs only Jde.App.Shared and a
//filesystem:  ProtoLog's daily file, the archive rounds it triggers, and reading both back through the QL await.
//Remote/Subscribe/LogTagsIntrospection stayed behind; they need a live app client and socket.
//Unlike the rest of this suite these are integration tests - ProtoLog flushes on a worker and the archive round is
//asynchronous - so they poll rather than assert immediately.
#include <gtest/gtest.h>
#include <absl/cleanup/cleanup.h>
#include <fstream>
#include <thread>
#include <boost/uuid/uuid_io.hpp>
#include <jde/fwk/chrono.h>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/fwk/io/FileAwait.h>
#include <jde/fwk/io/records/Scan.h>
#include <jde/fwk/io/protobuf.h>
#include <jde/fwk/log/MemoryLog.h>
#include <jde/fwk/str.h>
#include <jde/ql/ql.h>
#include <jde/app/log/ArchiveQuery.h>
#include <jde/app/log/DailyLoadAwait.h>
#include <jde/app/log/LogQLAwait.h>
#include <jde/app/log/ProtoLog.h>
#include <jde/app/log/Records.h>
#include "helpers.h"

#define let const auto

namespace Jde::App::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };
	using Log::FileEntry;
	using Log::EStop;

	//A file's records as the record layer holds them (Log.proto's FileEntry):  an archive's, or a daily file's.
	Ω records( const fs::path& file )ε->vector<FileEntry>{ return Log::ParseRecords( BlockAwait(IO::ReadAwait(file)), file ); }
	Ω contents( const fs::path& file )ι->string{
		std::ifstream f{ file, std::ios::binary };
		return string{ std::istreambuf_iterator<char>{f}, {} };
	}
	Ω save( const fs::path& file, sv bytes )ι->void{
		std::ofstream f{ file, std::ios::binary | std::ios::trunc };
		f.write( bytes.data(), bytes.size() );
	}
	//How many entries name the template.
	Ω count( const vector<FileEntry>& records, const uuid& id )ι->uint{
		uint y{};
		for( let& r : records )
			y += r.has_entry() && Protobuf::ToGuid( r.entry().template_id() )==id;
		return y;
	}
	//How many string records hold the id:  a file holds each once.
	Ω stringCount( const vector<FileEntry>& records, const uuid& id )ι->uint{
		uint y{};
		for( let& r : records )
			y += r.has_str() && Protobuf::ToGuid( r.str().id() )==id;
		return y;
	}
	//The string record with the id, which an archive holds once.
	Ω stringValue( const vector<FileEntry>& records, const uuid& id )ι->optional<string>{
		for( let& r : records ){
			if( r.has_str() && Protobuf::ToGuid(r.str().id())==id )
				return r.str().value();
		}
		return nullopt;
	}
	Ω waitUntil( const std::function<bool()>& done, Duration limit=20s )ι->bool{
		for( let deadline = Clock::now()+limit; Clock::now()<deadline; std::this_thread::sleep_for(25ms) ){
			if( done() )
				return true;
		}
		return done();
	}

	struct LogTests : ::testing::Test{
	protected:
		α Log()ι->App::ProtoLog&{ return Logging::GetLogger<App::ProtoLog>(); }
		//One archive per local day, so each test owns a date of its own.
		α DayArchivePath( uint day )ι->fs::path{ return App::ArchiveDayFile( Log().Root(), {std::chrono::year{2025}, std::chrono::month{1}, std::chrono::day{(unsigned)day}} ); }
		//An entry dated other than today flags ProtoLog for archiving; today-dated filler flips the day back and fills
		//the flush buffer, so the round runs without waiting out the delay.
		//Poll, never sleep a fixed interval.  A round rewrites *today's* archive whole, and that file grows with every run
		//the suite makes into the same tree - so a 1s sleep that passed on a clean checkout fails once the tree has some
		//history behind it.  Diagnosed after ~100 runs in one day left 20MB there and turned this from flaky to certain.
		α WaitForFile( const fs::path& path, Duration limit=20s )ι->bool{
			for( let deadline = Clock::now()+limit; Clock::now()<deadline; std::this_thread::sleep_for(25ms) ){
				if( fs::exists(path) )
					return true;
			}
			return fs::exists( path );
		}
		α Round( uint day, sv text, uint filler, optional<std::pair<uint32,uint32>> external={} )ι->uuid{
			Logging::Entry e{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, string{text} };
			e.Time = Chrono::ToTimePoint( 2025, 1, (uint8)day, 12 );
			if( external )
				Log().Write( e, external->first, external->second );
			else
				Log().Write( e );
			for( uint i=0; i<filler; ++i ){
				Log().Write( {SRCE_CUR, ELogLevel::Information, ELogTags::Test, Ƒ("{} filler {}", text, i)} );
				if( filler<=100 && fs::exists(DayArchivePath(day)) )
					break;
			}
			return e.Id();
		}
		//T4: the day CorruptArchiveFailsTheQueryNotTheProcess plants an unparseable archive in.  Swept here, not only by that
		//test's stack destructor: the failure it pins is std::terminate, and neither that nor any other crash runs a
		//destructor - after which every later run of anything that walks the archives (DailyFileStringFilter has no time
		//bound, so it walks all of them) fails on the file left behind, until someone deletes the directory by hand.  The
		//tree outlives the run and nothing else ever cleans it (C8), so one poisoned day is permanent.
		α CorruptDir()ι->fs::path{ return Log().Root()/"2025"/"1"/"20"; }
		α SetUp()->void override{
			ASSERT_NO_THROW( Log() ); //config/App.Tests.jsonnet configures /logging/proto.
			std::error_code ec;
			fs::remove_all( CorruptDir(), ec );
		}
	};

	//T5: was `ASSERT_TRUE( entries.size() )`, which held before this test wrote anything - the merged view carries a string
	//record per template, plus everything the earlier tests of this run and every earlier run left in the daily file.  It
	//could not fail, whatever DailyLoadAwait returned.  What it means to assert is that the entry just written is in there.
	TEST_F( LogTests, Exists ){
		//unique per run: the daily file outlives the suite, so a fixed message would be answered for by an earlier run's entry.
		Logging::Entry e{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, Ƒ("DailyLoadAwait test message {}", ToIsoString(Clock::now())) };
		Log().Write( e );
		using enum Log::Proto::FileEntry::ValueCase;
		bool found{};
		//Query mode merges the unflushed buffer with the file, so this holds whether or not the write above happened to flush.
		for( let& fe : BlockAwait( App::DailyLoadAwait(Log()) ) )
			found = found || (fe.value_case()==kEntry && Protobuf::ToGuid(fe.entry().template_id())==e.Id());
		EXPECT_TRUE( found ) << "the entry just written is not in the merged buffer+file view";
	}

	TEST_F( LogTests, Archive ){
		let archiveFile = DayArchivePath( 1 );
		if( fs::exists(archiveFile) )
			fs::remove( archiveFile );
		let id = Round( 1, "Test message", 100 );
		DBG( "archiveFile: {}", archiveFile.string() );
		ASSERT_TRUE( WaitForFile(archiveFile) );
		vector<FileEntry> archived;
		ASSERT_NO_THROW( archived = records(archiveFile) );
		EXPECT_GE( count(archived, id), 1u );
		//The record layer's file:  a FileStart one generation up, then the daily file's mark, each string once, the entries.
		let head = Log::HeadOf( contents(archiveFile) );
		ASSERT_TRUE( head );
		EXPECT_GE( head->Start.generation(), 1u );
		ASSERT_TRUE( head->Merged );
		EXPECT_NE( head->Merged->daily(), 0 );
	}

	TEST_F( LogTests, ArchiveExternal ){
		let archiveFile = DayArchivePath( 2 );
		if( fs::exists(archiveFile) )
			fs::remove( archiveFile );
		let id = Round( 2, "External test message", 100, std::pair<uint32,uint32>{123,456} );
		ASSERT_TRUE( WaitForFile(archiveFile) );
		vector<FileEntry> archive;
		ASSERT_NO_THROW( archive = records(archiveFile) );
		optional<App::Log::Proto::LogEntryFile> external;
		for( let& r : archive ){
			if( r.has_entry() && r.entry().app_pk()==123 && r.entry().app_instance_pk()==456 && !external )
				external = r.entry();
		}
		ASSERT_TRUE( external );
		ASSERT_EQ( Protobuf::ToGuid(external->template_id()), id );
		EXPECT_EQ( stringValue(archive, id), "External test message" ) << "External entry's template string not archived.";
	}

	// Regression, two ways a round could write an entry it had already archived:
	//   1. the old ArchiveFileAwait::Save appended the fully-merged archive to the very file it had just merged from, so every
	//      round wrote back everything already on disk - archive.binpb grew ~x3.7 per round until it no longer parsed
	//      and aborted the suite.
	//   2. the round archived ProtoLog's unflushed buffer as well as the daily file, but deleted only the file, so an
	//      entry still buffered when a round ran was archived again as soon as the next flush wrote it to disk.  Timing
	//      dependent - it needed a flush already in flight when the entry was written, so it only flaked (release more
	//      often than debug).
	// Either way, a second round must not duplicate the first round's entries.
	TEST_F( LogTests, ArchiveReplacesFile ){
		let archiveFile = DayArchivePath( 3 );
		if( fs::exists(archiveFile) )
			fs::remove( archiveFile );
		//the archive is written asynchronously, so retry until `until` lands, then let the round quiesce: the round's
		//remaining entries are still arriving (and a later flush can start another round), so a count sampled the instant
		//an entry appears is still moving.
		auto archived = [&archiveFile]( const uuid& until )ι->vector<FileEntry>{
			vector<FileEntry> y;
			for( int i=0; i<100; ++i ){
				try{
					if( fs::exists(archiveFile) ){
						auto archive = records( archiveFile );
						if( count(archive, until) ){
							if( y.size()==archive.size() )//unchanged over the last interval - settled.
								return archive;
							y = move( archive );
						}
					}
				}
				catch( const Exception& )
				{}
				std::this_thread::sleep_for( 250ms );
			}
			return y;
		};
		let first = Round( 3, "ArchiveReplacesFile first", 200 );
		ASSERT_EQ( count(archived(first), first), 1u ) << "first round's entry not archived exactly once";
		let second = Round( 3, "ArchiveReplacesFile second", 200 );
		let after = archived( second );
		ASSERT_EQ( count(after, second), 1u ) << "second round's entry not archived exactly once";
		//exactly once, not "same as before": a round archives the daily file and nothing else, and holds that file's lock from the read until fs::remove - so an entry has one source, and no round can re-read what an earlier one archived.
		EXPECT_EQ( count(after, first), 1u ) << "the second round wrote the first round's entry again - archive.binpb was appended to, not replaced";
	}

	//#4: the string-dedup cache used to outlive the daily file a round deletes.  AddString suppresses a kStr whose id is
	//already cached, and a round builds its String table only from the kStr records still in the daily file - so a string
	//first emitted before a round was silently absent from every later archive, ArchiveQuery::find returned Str::Empty(),
	//and the log page rendered a blank message/file/function for every historical entry.  Two rounds a day apart sharing
	//one template text is the whole reproduction: the second round's id is cached, so its String never gets written.
	TEST_F( LogTests, ArchiveStringsSurviveRound ){
		constexpr sv text{ "ArchiveStringsSurviveRound shared template" };
		for( let d : {10u, 11u} )
			if( fs::exists(DayArchivePath(d)) )
				fs::remove( DayArchivePath(d) );
		//an entry and the strings it references are written by one rewrite, renamed in whole, so an archive that holds the
		//entry holds whatever strings that write carried - no settle loop needed, only a retry past the round's start.
		auto archivedWith = []( const fs::path& file, const uuid& id )ι->optional<vector<FileEntry>>{
			for( uint i=0; i<100; ++i ){
				try{
					if( fs::exists(file) ){
						auto archive = records( file );
						if( count(archive, id) )
							return archive;
					}
				}
				catch( const Exception& )
				{}
				std::this_thread::sleep_for( 250ms );
			}
			return nullopt;
		};

		let id = Round( 10, text, 200 );//both rounds share the text, so both share this md5 template id.
		let first = archivedWith( DayArchivePath(10), id );
		ASSERT_TRUE( first ) << "first round's entry never archived";
		ASSERT_EQ( stringValue(*first, id), string{text} );//sanity: the first round was never the broken one.

		ASSERT_EQ( Round(11, text, 200), id );
		let second = archivedWith( DayArchivePath(11), id );
		ASSERT_TRUE( second ) << "second round's entry never archived";
		EXPECT_EQ( stringValue(*second, id), string{text} ) << "the second round archived the entry with no String for its template - the dedup cache outlived the daily file the first round deleted";
	}

	//#3: _today used to be seeded from Clock::now(), so a service that started and stopped within one day never saw a day
	//change - _needsArchive was never set, nothing archived, and log.binpb accumulated across every restart cycle forever.
	//It is now seeded from the daily file, so a restart on a later day arms the round the previous run never ran.  These
	//construct their own ProtoLog over a scratch root rather than using the registered logger: the seeding happens exactly
	//once, in the constructor, so a restart is the only way to observe it.
	struct DailyFileDayTests : ::testing::Test{
	protected:
		fs::path _root;
		α SetUp()->void override{
			_root = Logging::GetLogger<App::ProtoLog>().Root()/"daily-file-day";
			fs::remove_all( _root );
			fs::create_directories( _root );
		}
		α TearDown()->void override{ fs::remove_all( _root ); }
		//delay PT24H so the scratch logger never flushes or arms a timer of its own - these assert the seed, not a round.
		α Make()ε->up<App::ProtoLog>{ return mu<App::ProtoLog>( jobject{{"path", _root.string()}, {"delay", "PT24H"}, {"timeZone", "America/New_York"}} ); }
		//content is irrelevant to the rule under test - the seed comes from the file's last-write time, never from a parse.
		α WriteDailyFile( TimePoint written )->void{
			let file = _root/"log.binpb";
			{ std::ofstream os{ file, std::ios::binary }; os << "unparsed"; }
			fs::last_write_time( file, Chrono::ToClock<fs::file_time_type::clock, Clock>(written) );//ToClock: from_sys is libc++-only, MSVC's file_clock has just from_utc.
		}
	};

	TEST_F( DailyFileDayTests, NoDailyFileSeedsToday ){
		let log = Make();
		EXPECT_EQ( log->Today(), Chrono::LocalYMD(Clock::now(), log->TimeZone()) );
	}

	TEST_F( DailyFileDayTests, StaleDailyFileSeedsItsOwnDay ){
		let written = Clock::now()-std::chrono::days{3};
		WriteDailyFile( written );
		let log = Make();
		let staleDay = Chrono::LocalYMD( written, log->TimeZone() );
		EXPECT_EQ( log->Today(), staleDay );//the restart that used to lose the round: seeded to today, every entry matched, nothing ever archived.
		EXPECT_NE( log->Today(), Chrono::LocalYMD(Clock::now(), log->TimeZone()) ) << "3 days is not a day change";
	}

	//a restart on the same day must not arm a round - the file is current and re-archiving it would split today's entries
	//across the daily file and today's archive for no reason.
	TEST_F( DailyFileDayTests, CurrentDailyFileSeedsToday ){
		WriteDailyFile( Clock::now() );
		let log = Make();
		EXPECT_EQ( log->Today(), Chrono::LocalYMD(Clock::now(), log->TimeZone()) );
	}

	//M5: with the daily file unwritable the flush fails and the whole batch is prepended, and nothing bounded what that grew to -
	//an O(n) memmove per prepend, for as long as the disk stayed unwritable, with a fresh Save (coroutine, lock, failing
	//IO::WriteAwait) started by *every* subsequent log line.
	TEST_F( DailyFileDayTests, AnUnwritableDailyFileTrimsToTheNewest ){
		fs::create_directories( _root/"log.binpb" );//a directory where the file belongs - every append fails, whatever the uid.
		//Destroyed normally since M9 gave ProtoLog a destructor - which this exercises as much as anything: the failure path arms a
		//timer, so tearing one down here is exactly the use-after-free M9 was about.
		let log = mu<App::ProtoLog>( jobject{{"path", _root.string()}, {"delay", "PT24H"}, {"timeZone", "America/New_York"}, {"maxBuffer", 40'000}} );

		constexpr uint count{ 2'000 };
		let text = []( uint i ){ return Ƒ("M5 cap probe {} - unique text, so each entry emits a string record of its own", i); };
		for( uint i=0; i<count; ++i )
			log->Write( {SRCE_CUR, ELogLevel::Information, ELogTags::Test, text(i)} );

		EXPECT_LE( log->BufferSize(), 45'000u ) << "the buffer grew past its cap while the daily file was unwritable";
		ASSERT_GT( log->BufferSize(), 0u ) << "entries stopped being buffered at all";

		//The trim is by whole records, the newest kept.
		vector<App::Log::Proto::FileEntry> buffered;
		ASSERT_NO_THROW( buffered = log->Entries() );
		flat_set<uuid> templates;
		for( let& fe : buffered ){
			if( fe.value_case()==App::Log::Proto::FileEntry::kEntry )
				templates.emplace( Protobuf::ToGuid(fe.entry().template_id()) );
		}
		EXPECT_TRUE( templates.contains(Logging::Entry::GenerateId(text(count-1))) ) << "the newest entry was not among the ones kept";
		EXPECT_FALSE( templates.contains(Logging::Entry::GenerateId(text(0))) ) << "the oldest entry survived - the trim kept the wrong end";
	}

	//Touch reports whether the string's record still has to be written:  once per buffer, which Clear starts over.
	TEST( ProtoLogCacheTests, TouchEmitsOncePerBuffer ){
		App::ProtoLogCache cache;
		let id = Logging::Entry::GenerateId( "hot" );
		ASSERT_TRUE( cache.Touch(cache.Strings, id) ) << "a first use is not cached, so its record is emitted";
		EXPECT_FALSE( cache.Touch(cache.Strings, id) );
		EXPECT_TRUE( cache.Touch(cache.Args, id) ) << "the kinds are cached apart";
		cache.Clear();
		EXPECT_TRUE( cache.Touch(cache.Strings, id) ) << "a flush took the buffer, so the next run carries the string again";
	}

	//L1(a): Save moved the batch out of _toSave before it enqueued on the daily file's lock, so for as long as another holder kept
	//that lock - a round, another query - the batch was in neither the buffer nor the file, and a query that snapshotted the buffer
	//and then read the file found it in neither.  The window is the whole time the lock is held, not a few instructions, so holding
	//it here is the finding rather than a contrivance.
	TEST_F( DailyFileDayTests, AFlushWaitingOnTheDailyFileLockIsStillVisible ){
		let log = mu<App::ProtoLog>( jobject{{"path", _root.string()}, {"delay", "PT24H"}, {"timeZone", "America/New_York"}} );
		Logging::Entry marker{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, string{"L1 in-flight probe"} };
		{
			auto lock = log->TryLockDailyFile();//held for this scope: the flush below parks on it.
			ASSERT_TRUE( lock );
			log->Write( marker );
			//Past _delaySize, so the write triggers Save() - which runs inline to its first suspend, so by the time this loop ends
			//the batch has left _toSave and is parked on the lock.
			for( uint i=0; i<400 && log->BufferSize(); ++i )
				log->Write( {SRCE_CUR, ELogLevel::Information, ELogTags::Test, Ƒ("L1 filler {}", i)} );
			ASSERT_EQ( log->BufferSize(), 0u ) << "no flush was triggered, so there is nothing in flight to see";

			bool found{};
			for( let& fe : log->Entries() )
				found = found || (fe.value_case()==Log::Proto::FileEntry::kEntry && Protobuf::ToGuid(fe.entry().template_id())==marker.Id());
			EXPECT_TRUE( found ) << "a batch waiting on the daily file's lock was in neither the buffer nor the file";
		}
	}

	//M9: ProtoLog had no destructor, so tearing one down with its flush timer armed left StartTimer's continuation pointing at
	//freed memory - the cancelled wait completes on the io thread and resumes into it.  ASan reported it as a heap-use-after-free
	//in DurationTimer::await_resume about `delay` after the write.  A short delay so the timer is genuinely in flight, and a loop
	//so the destructor is exercised against a live one repeatedly rather than once.
	TEST_F( DailyFileDayTests, DestructorOutlastsTheFlushTimer ){
		for( uint i=0; i<20; ++i ){
			let log = mu<App::ProtoLog>( jobject{{"path", (_root/std::to_string(i)).string()}, {"delay", "PT0.2S"}, {"timeZone", "America/New_York"}} );
			log->Write( {SRCE_CUR, ELogLevel::Information, ELogTags::Test, Ƒ("M9 destructor probe {}", i)} );//arms the timer.
		}
		SUCCEED() << "20 build/write/tear-down cycles with a timer armed, clean under ASan";
	}

	//#7: _dailyFileStart is what LogQLAwait's read-local check (`!end || *end > DailyFileStart()`) consults to decide
	//whether today's log can hold anything in range.  It has to be a *lower* bound on everything reachable locally - the
	//daily file and the unflushed buffer both - because reading a file with nothing in range costs one read, while skipping
	//one that does costs the entries.  The seeding cases need no writes, so they reuse the scratch-logger harness.
	struct DailyFileStartTests : DailyFileDayTests{};

	TEST_F( DailyFileStartTests, NoDailyFileStartsAtMax ){
		EXPECT_EQ( Make()->DailyFileStart(), TimePoint::max() );//nothing local at all - the one case where skipping is right.
	}

	TEST_F( DailyFileStartTests, ExistingDailyFileClaimsTheWidestBound ){
		WriteDailyFile( Clock::now() );
		EXPECT_EQ( Make()->DailyFileStart(), TimePoint::min() ) << "a file left by a previous run holds entries of unknown age - a bounded query has to read it";
	}

	//The bug proper.  A flush moves entries out of the buffer and into the daily file; both are local, so the bound must
	//survive it.  Parking it at max() made `*_endTime > max` false for every finite bound, so a query with any time filter
	//skipped the whole of today's log and answered out of the archives alone.
	TEST_F( LogTests, DailyFileStartSurvivesFlush ){
		//Retried, because one thing legitimately does release the bound: an archive round.  The tests above leave 2025-dated
		//entries behind, so the first flush here can find _needsArchive still armed, drain, and correctly reset to max() -
		//after which the filler still to come lowers it past `when`.  That is the fix working, not failing.  A retry is
		//sound rather than a way of hiding flakiness: the bug it guards is deterministic per flush, so before the fix every
		//attempt fails, and the round can only intervene once because the drain clears the flag.
		//The same round, or one still running from the test before, removes the daily file, and the filler's flushes then
		//make another, which can stay smaller than the one removed:  a flush is told by the file's FileStart as well as its size.
		auto mark = [this]{//the daily file's size, and its FileStart's ts, which names it:  0 until it has a whole FileStart.
			let bytes = contents( Log().DailyFile() );
			let head = Log::HeadOf( bytes );
			return std::pair<uint,int64_t>{ bytes.size(), head ? head->Start.ts() : 0 };
		};
		bool held{};
		for( uint attempt=0; attempt<5 && !held; ++attempt ){
			let when = Clock::now();
			let before = mark();
			//by size, not by waiting out /logging/proto/delay (PT1M in App.Tests.jsonnet); _delaySize is 8096 bytes.  All
			//dated now, so nothing here arms a round of its own.
			for( uint i=0; i<400; ++i )
				Log().Write( {SRCE_CUR, ELogLevel::Information, ELogTags::Test, Ƒ("DailyFileStartSurvivesFlush {} filler {}", attempt, i)} );
			bool flushed{};
			for( uint i=0; i<100 && !flushed; ++i ){
				let now = mark();
				flushed = now.second && (now.second!=before.second || now.first>before.first);
				if( !flushed )
					std::this_thread::sleep_for( 50ms );
			}
			ASSERT_TRUE( flushed ) << "no flush landed - the filler did not exceed _delaySize";
			//T7: sampled across a window once the flush has landed, not once at the instant it does.  The bug parks the bound
			//on the executor thread in the statement after the write that grows the file, so a single sample taken the moment
			//the size changes can be read *before* the park and see the bound the fix would have left - a green that says
			//nothing about which version is running.  One sample catching it released is the failure, and nothing lowers it
			//again once this attempt's filler is written, so a window can only add detections, never take one away.
			held = true;
			for( uint i=0; i<25 && held; ++i ){
				let start = Log().DailyFileStart();//one read: two calls could straddle a flush and compare different values.
				held = start!=TimePoint::max() && start<=when;
				std::this_thread::sleep_for( 10ms );
			}
		}
		EXPECT_TRUE( held ) << "a flush released the bound - it moves entries from the buffer into the daily file, and both are local, so every time-bounded logs() query would now skip the daily file";
	}

	//#9: the attribution check read ProgramPK/InstancePK, which nothing ever assigned - indeterminate, so any forwarded
	//entry whose ids happened to match the garbage was silently written as one of ours, without its app_pk/app_instance_pk,
	//differently on every process start.  They hold this process's own pks now.
	TEST_F( LogTests, ForwardedEntryAttribution ){
		constexpr App::ProgramPK app{ 4242 };
		constexpr App::ProgInstPK inst{ 8484 };
		//unique per run: the read below covers the daily file, which outlives the suite, and an earlier run's entry for the
		//same text would answer for this one - including a run from before the fix, whose answers were the wrong ones.
		let run = ToIsoString( Clock::now() );
		using Attribution = std::pair<int32,int32>;//app_pk, app_instance_pk:  {0,0} is one of our own entries.
		//via DailyLoadAwait, not Log().Entries(): the buffer alone loses the entry whenever the write that added it crossed
		//_delaySize and flushed, which is a coin toss depending on how full the earlier tests left the buffer.  Query mode
		//merges the buffer with the file, so the entry is found either way.
		auto write = [this,&run]( App::ProgramPK a, App::ProgInstPK i, sv what )ε->optional<Attribution>{
			Logging::Entry e{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, Ƒ("ForwardedEntryAttribution {} {}", what, run) };
			Log().Write( e, a, i );
			for( let& fe : BlockAwait( App::DailyLoadAwait(Log()) ) ){
				if( fe.has_entry() && Protobuf::ToGuid(fe.entry().template_id())==e.Id() )
					return Attribution{ fe.entry().app_pk(), fe.entry().app_instance_pk() };
			}
			return nullopt;
		};
		EXPECT_EQ( write(app, inst, "unset"), (Attribution{app, inst}) ) << "with our own pks unset nothing forwarded is ours";
		Log().SetAppPKs( app, inst );
		EXPECT_EQ( write(app, inst, "self"), (Attribution{0, 0}) ) << "both pks match - one of our own entries coming back";
		EXPECT_EQ( write(app, inst+1, "sibling"), (Attribution{app, inst+1}) ) << "another instance of the same program is not us - matching the program pk alone must not drop attribution";
		EXPECT_EQ( write(0, inst, "unattributed"), (Attribution{0, 0}) ) << "no app pk to preserve";
		Log().SetAppPKs( 0, 0 );//the fixture shares the process-wide logger.
	}

	//#13(c): the archive-read lambda in the old ArchiveLoadAwait::LoadArchives was marked ι but calls Protobuf::Deserialize and
	//ArchiveQuery::Append, both ε.  One corrupt or truncated archive.binpb - a torn write, a file left half-written by a
	//killed process - took the whole process down with std::terminate instead of failing the query that read it.
	TEST_F( LogTests, CorruptArchiveFailsTheQueryNotTheProcess ){
		let dir = CorruptDir();//a day of its own; ArchiveDayFiles() skips directories that are not a valid date.
		fs::create_directories( dir );
		{
			std::ofstream os{ dir/"archive.binpb", std::ios::binary };
			os << "not a protobuf";
		}
		//removed however this exits - every other test in the suite reads the archives too, and the tree outlives the run.
		//SetUp sweeps it too, for the exits that run no destructor at all - which is the very failure this pins.
		absl::Cleanup cleanup = [dir]{ std::error_code ec; fs::remove_all( dir, ec ); };
		//T4: the exception, not EXPECT_ANY_THROW.  A QL parse error, a renamed view, a read that fails for any other reason
		//- all of them are throws, and under any of them this reports green while the terminate it exists for goes unguarded.
		//What it has to be is the read of that file refusing it as no archive of the record layer's, which names the layer.
		try{
			//no limit, so IsComplete() is false and LogQLAwait walks every archive file, this one included.
			BlockAwait( App::LogQLAwait{move(QL::Parse(string{"logs{ entries{templateId time} }"}, {}, {}).Queries()[0])} );
			ADD_FAILURE() << "the corrupt archive was read without complaint";
		}
		catch( const Jde::Exception& e ){
			EXPECT_NE( string{e.what()}.find("record layer"), string::npos ) << "it threw, but not on the corrupt archive: " << e.what();
		}
		catch( const std::exception& e ){
			ADD_FAILURE() << "expected a Jde::Exception from the archive read, got: " << e.what();
		}
	}

	TEST_F( LogTests, GraphQL ){
		let now = ToIsoString( Clock::now() );
		Logging::Entry eNow{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, string{now} };
		Logging::Entry eHour{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, ToIsoString(eNow.Time - 1h) };
		eHour.Time = eNow.Time - 1h;
		TRACE( "prev.Time: {}, id: {}", ToIsoString(eHour.Time), Jde::ToString(eHour.Id()) );
		Log().Write( eHour );
		const string start{ ToIsoString(eHour.Time+1s) };
		TRACE( "filter: time: {}, id: {}", start, Jde::ToString(eNow.Id()) );
		Log().Write( eNow );
		constexpr auto q = "logs( time: {gt: $start} ){ entries{templateId argIds level tags line time userId fileId functionId} strings{id value} }";
		jobject vars{ {"start", start} };
		let logs = BlockAwait( App::LogQLAwait{move(QL::Parse(q, vars, {}).Queries()[0])} );
		optional<jobject> jNow;
		for( let& log : logs.at("entries").as_array() ){
			let id = ToUuid( log.at("templateId").as_string() );
			if( id==eNow.Id() )
				jNow = log.as_object();
			ASSERT_FALSE( id==eHour.Id() ) << "Found hour log entry which should be excluded.";
		}
		ASSERT_TRUE( jNow );
	}

	//#5: the daily-file overload of ArchiveQuery::Append filtered each entry before populating the string maps, but Test()
	//resolves "text"/"message"/"args" through those very maps - so every entry was compared against "" and any such filter
	//returned nothing from today's log.  Nothing surviving meant nothing was ever added, so it could not self-correct.
	//The time filter in GraphQL above never caught it: time is read off the entry, not out of a map.
	TEST_F( LogTests, DailyFileStringFilter ){
		//unique per run: the daily file and the archives outlive the suite, so a fixed marker would match older entries too.
		let marker = Ƒ( "DailyFileStringFilter {}", ToIsoString(Clock::now()) );
		Logging::Entry e{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, string{marker} };
		Log().Write( e );
		//no time filter, so LogQLAwait reads the daily file (buffer included) - this is the ArchiveQuery(Filter&,...) path.
		constexpr auto q = "logs( text: {eq: $text} ){ entries{templateId time} }";
		jobject vars{ {"text", marker} };
		let logs = BlockAwait( App::LogQLAwait{move(QL::Parse(q, vars, {}).Queries()[0])} );
		uint matched{};
		for( let& log : logs.at("entries").as_array() )
			matched += ToUuid( log.at("templateId").as_string() )==e.Id();
		EXPECT_EQ( matched, 1u ) << "a text filter found nothing in the daily file - Append tested entries against string maps it had not filled yet";
	}


	//#309: the daily file is the record layer's - a FileStart, then a run per flush ending in a checkpoint, no length before
	//any record - and holds each string its entries name once, so a read of the whole file resolves its entries.
	//Scratch loggers over a root of their own, as DailyFileDayTests, with a short delay so the timer flushes.
	struct DailyFileLayerTests : DailyFileDayTests{
	protected:
		α Make( sv delay )ε->up<App::ProtoLog>{ return mu<App::ProtoLog>( jobject{{"path", _root.string()}, {"delay", string{delay}}, {"timeZone", "America/New_York"}} ); }
		α File()Ι->fs::path{ return _root/"log.binpb"; }
		α Size()Ι->uint{ std::error_code ec; return fs::exists(File(), ec) ? (uint)fs::file_size(File(), ec) : 0; }
		//A write, and the flush that lands it:  the file has grown.
		α Flush( App::ProtoLog& log, const Logging::Entry& e )ι->bool{
			let before = Size();
			log.Write( e );
			return waitUntil( [&]{ return Size()>before; } );
		}
		α Scan()Ε->IO::Records::Scanned{
			IO::Records::Decoder<FileEntry> decoder;
			return IO::Records::ScanNow( ms<IO::OpenFile>(File()), Log::StartField, Log::CheckpointField, decoder );
		}
		//Whether every string a run's entries name is in the run.
		Ω SelfContained( const vector<FileEntry>& run )ι->bool{
			flat_set<uuid> strings;
			for( let& r : run ){
				if( r.has_str() )
					strings.emplace( Protobuf::ToGuid(r.str().id()) );
			}
			bool y{ true };
			for( let& r : run ){
				if( r.has_entry() )
					ForEachStringId( r.entry(), [&]( EStringKind, const string& idBytes ){ y = y && strings.contains( Protobuf::ToGuid(idBytes) ); } );
			}
			return y;
		}
	};

	TEST_F( DailyFileLayerTests, TheDailyFileIsTheRecordLayers ){
		let log = Make( "PT0.2S" );
		let a = entry( Clock::now(), ELogLevel::Information, 1, "layer {}", {"a"} );
		let b = entry( a.Time+1s, ELogLevel::Information, 2, "layer {}", {"b"} );//the same template, a second time.
		ASSERT_TRUE( Flush(*log, a) );
		ASSERT_TRUE( Flush(*log, b) );
		let bytes = contents( File() );
		EXPECT_NE( (uint8_t)bytes[0], 0u ) << "a length precedes the first record";
		let first = IO::Records::Cursor{ bytes, 0 }.Next();
		FileEntry start;
		ASSERT_TRUE( first && start.ParseFromArray(first->Frame.data(), (int)first->Frame.size()) );
		EXPECT_TRUE( start.has_file_start() ) << "the file doesn't open with Log.proto's file_start";
		let scanned = Scan();
		EXPECT_EQ( scanned.Stop, EStop::End );
		EXPECT_EQ( scanned.Size, bytes.size() );
		ASSERT_EQ( scanned.Runs.size(), 2u ) << "a flush is a run";
		let all = Log::ParseRecords( bytes, File() );
		EXPECT_TRUE( SelfContained(all) ) << "an entry names a string the file doesn't hold";
		EXPECT_EQ( stringCount(all, a.Id()), 1u ) << "the second run wrote the template the first holds";
		EXPECT_EQ( count(all, a.Id()), 2u );
		let loaded = BlockAwait( App::DailyLoadAwait(*log) );
		EXPECT_EQ( count(loaded, a.Id()), 2u );
		EXPECT_EQ( stringValue(loaded, a.Id()), "layer {}" ) << "a query doesn't resolve the second run's entry";
	}

	//A process that opens a daily file another left knows its strings from the scan, and doesn't write them again.
	TEST_F( DailyFileLayerTests, AProcessRestartDoesntWriteTheFilesStringsAgain ){
		let a = entry( Clock::now(), ELogLevel::Information, 1, "restart {}", {"a"} );
		let b = entry( a.Time+1s, ELogLevel::Information, 2, "restart {}", {"b"} );
		{
			let log = Make( "PT0.2S" );
			ASSERT_TRUE( Flush(*log, a) );
		}
		let log = Make( "PT0.2S" );
		ASSERT_TRUE( Flush(*log, b) );
		let all = records( File() );
		EXPECT_EQ( stringCount(all, a.Id()), 1u ) << "the template was written again after the restart";
		EXPECT_EQ( count(all, a.Id()), 2u );
	}

	//A daily file removed under the process starts over:  its runs write the strings the old file held, the first's and
	//the ones after it.
	TEST_F( DailyFileLayerTests, ARemovedDailyFileStartsOverWithItsStrings ){
		let log = Make( "PT0.2S" );
		let a = entry( Clock::now(), ELogLevel::Information, 1, "removed {}", {"a"} );
		let x = entry( a.Time+1s, ELogLevel::Information, 2, "only the old file at first {}", {"x"} );
		let b = entry( a.Time+2s, ELogLevel::Information, 3, "removed {}", {"b"} );
		let c = entry( a.Time+3s, ELogLevel::Information, 4, "only the old file at first {}", {"c"} );
		ASSERT_TRUE( Flush(*log, a) );
		ASSERT_TRUE( Flush(*log, x) );
		fs::remove( File() );
		ASSERT_TRUE( Flush(*log, b) );
		ASSERT_TRUE( Flush(*log, c) );
		let all = records( File() );
		EXPECT_EQ( count(all, a.Id()), 1u );
		EXPECT_EQ( stringValue(all, a.Id()), "removed {}" ) << "the new file's first run lacks the template the removed one held";
		EXPECT_EQ( stringValue(all, x.Id()), "only the old file at first {}" ) << "a later run lacks a template only the removed file held";
		EXPECT_TRUE( SelfContained(all) );
	}

	//The ids a file holds are bounded:  past its cap the least recently used half goes, and is written again if named.
	TEST( FileStringsTests, KeepsTheMostRecentlyUsedUpToItsCap ){
		let id = []( uint i ){ return Logging::Entry::GenerateId( std::to_string(i) ); };
		Log::FileStrings strings;
		strings.Add( id(0) );
		for( uint i=1; i<=Log::FileStrings::Cap; ++i ){
			strings.Add( id(i) );
			if( i%1000==0 )
				EXPECT_TRUE( strings.Holds(id(0)) );
		}
		EXPECT_LE( strings.Size(), Log::FileStrings::Cap );
		EXPECT_TRUE( strings.Holds(id(0)) ) << "the most recently used was forgotten";
		EXPECT_FALSE( strings.Holds(id(1)) ) << "the least recently used was kept";
		EXPECT_TRUE( strings.Holds(id(Log::FileStrings::Cap)) );
	}

	//A daily file torn inside its last append:  a query serves the whole records before the tear, the tear is reported,
	//the file is kept, and the next append drops the torn bytes and writes the salvaged records again.
	TEST_F( DailyFileLayerTests, ATornDailyFileLosesOnlyTheTornRecord ){
		let e1 = entry( Clock::now()-3s, ELogLevel::Information, 1, "torn {}", {"one"} );
		let e2 = entry( Clock::now()-2s, ELogLevel::Information, 2, "torn {}", {"two"} );
		let e3 = entry( Clock::now()-1s, ELogLevel::Information, 3, "torn {}", {"three"} );
		string bytes;
		Log::Appender first{ bytes, 0 };
		IO::Proto::FileStart start;
		start.set_ts( 1 );
		first.Start( start );
		for( auto& r : fileEntries({e1}) )
			first.Add( move(r) );
		first.Seal();
		Log::Appender second{ bytes, 0 };
		for( auto& r : fileEntries({e2, e3}) )
			second.Add( move(r) );
		let torn = bytes.substr( 0, bytes.size()-3 );//inside e3, the append's last record, before its checkpoint.
		save( File(), torn );
		if( !Logging::FindLogger<Logging::MemoryLog>() )
			Logging::AddLogger( mu<Logging::MemoryLog>() );
		Logging::ClearMemory();

		let log = Make( "PT0.2S" );
		let loaded = BlockAwait( App::DailyLoadAwait(*log) );
		uint lines[4]{};
		for( let& r : loaded ){
			if( r.has_entry() && r.entry().line()<4 )
				++lines[r.entry().line()];
		}
		EXPECT_EQ( lines[1], 1u );
		EXPECT_EQ( lines[2], 1u ) << "the whole record before the tear was lost with the torn append";
		EXPECT_EQ( lines[3], 0u ) << "the torn record was served";
		EXPECT_EQ( stringValue(loaded, Logging::Entry::GenerateId("two")), "two" );
		let warned = Logging::Find( [this]( const Logging::Entry& e ){ return e.Level>=ELogLevel::Warning && e.Message().contains(File().string()); } );
		EXPECT_GE( warned.size(), 1u ) << "the tear went unreported";
		EXPECT_EQ( Size(), torn.size() ) << "a read changed the file";

		let e4 = entry( Clock::now(), ELogLevel::Information, 4, "torn {}", {"four"} );
		log->Write( e4 );
		ASSERT_TRUE( waitUntil([&]{ return Scan().Runs.size()==2; }) ) << "no append landed";//not the size:  the torn tail is cut before the append is written.
		let scanned = Scan();
		EXPECT_EQ( scanned.Stop, EStop::End ) << "the append left the torn bytes";
		EXPECT_EQ( scanned.Size, Size() );
		let after = records( File() );
		uint after4[5]{};
		for( let& r : after ){
			if( r.has_entry() && r.entry().line()<5 )
				++after4[r.entry().line()];
		}
		EXPECT_EQ( after4[1], 1u );
		EXPECT_EQ( after4[2], 1u ) << "the salvaged record wasn't written again";
		EXPECT_EQ( after4[3], 0u );
		EXPECT_EQ( after4[4], 1u );
	}

	//The first append to a new daily file fails part-way, its records whole and its checkpoint cut.  Nothing was committed
	//at 0, so the retry's Verify refuses the bytes and the file is scanned again.  The records it finds torn are the batch
	//the flush put back in the buffer:  salvaging them as well wrote each twice.
	TEST_F( DailyFileLayerTests, AFailedFirstAppendIsWrittenOnceByItsRetry ){
		let e1 = entry( Clock::now()-2s, ELogLevel::Information, 1, "failed {}", {"one"} );
		let e2 = entry( Clock::now()-1s, ELogLevel::Information, 2, "failed {}", {"two"} );
		auto open = [this]( const Log::DailyFile& was ){
			Log::Opening opening{ File(), was };
			while( opening.Next()==Log::ENext::More )
				IO::Records::FeedNow( opening );
			return opening.Take();
		};
		auto file = open( {} );
		let failed = file.Prepare( fileEntries({e1, e2}) );
		save( File(), sv{failed.Bytes}.substr(0, failed.Bytes.size()-3) );//inside the run's checkpoint.
		EXPECT_THROW( file.Prepare(fileEntries({e1, e2})), IO::IOException );
		ASSERT_FALSE( file.Opened );

		file = open( file );
		EXPECT_TRUE( file.Salvaged.empty() ) << "the failed append's records were salvaged, though the buffer holds them again";
		auto retry = file.Prepare( fileEntries({e1, e2}) );
		save( File(), contents(File()).substr(0, retry.Offset)+retry.Bytes );
		file.Commit( move(retry) );
		uint lines[3]{};
		for( let& r : records(File()) ){
			if( r.has_entry() && r.entry().line()<3 )
				++lines[r.entry().line()];
		}
		EXPECT_EQ( lines[1], 1u );
		EXPECT_EQ( lines[2], 1u );
	}

	//#235: a round that fails at day k is tried again by the next flush, and doesn't write days before k a second time.
	TEST_F( DailyFileLayerTests, ARoundThatFailsAtADayIsRetriedWithoutDuplicates ){
		let log = Make( "PT0.2S" );
		let five = App::ArchiveDayFile( _root, {std::chrono::year{2025}, std::chrono::month{3}, std::chrono::day{5}} );
		let six = App::ArchiveDayFile( _root, {std::chrono::year{2025}, std::chrono::month{3}, std::chrono::day{6}} );
		let obstruction = fs::path{ six }.concat( ".tmp" );
		fs::create_directories( obstruction );//a directory where day 6's temp file goes:  its write fails.
		let e5 = entry( Chrono::ToTimePoint(2025, 3, 5, 12), ELogLevel::Information, 5, "day five" );
		let e6 = entry( Chrono::ToTimePoint(2025, 3, 6, 12), ELogLevel::Information, 6, "day six" );
		log->Write( e5 );
		log->Write( e6 );
		ASSERT_TRUE( waitUntil([&]{ return fs::exists(five); }) );
		//the round ends at day 6:  it lets the daily file's lock go.
		ASSERT_TRUE( waitUntil([&]{ return (bool)log->TryLockDailyFile(); }) );
		EXPECT_FALSE( fs::exists(six) );
		EXPECT_TRUE( fs::exists(File()) ) << "the daily file was removed with a day unarchived";
		EXPECT_EQ( count(records(five), e5.Id()), 1u );
		let fiveWritten = fs::last_write_time( five );
		let fiveSize = fs::file_size( five );
		fs::remove_all( obstruction );
		//the retry:  a flush on the same day as the last entry, so only the re-arm brings the round.
		let again = entry( Chrono::ToTimePoint(2025, 3, 6, 13), ELogLevel::Information, 7, "day six again" );
		log->Write( again );
		ASSERT_TRUE( waitUntil([&]{ return fs::exists(six); }) );
		ASSERT_TRUE( waitUntil([&]{ return !fs::exists(File()); }) ) << "the retried round didn't remove the daily file";
		EXPECT_EQ( fs::last_write_time(five), fiveWritten ) << "day 5 was written a second time";
		EXPECT_EQ( fs::file_size(five), fiveSize );
		EXPECT_EQ( count(records(five), e5.Id()), 1u );
		let sixth = records( six );
		EXPECT_EQ( count(sixth, e6.Id()), 1u );
		EXPECT_EQ( count(sixth, again.Id()), 1u );
		//the file the round removed held the template:  the next file writes it again.
		let after = entry( Chrono::ToTimePoint(2025, 3, 6, 14), ELogLevel::Information, 8, "day six again" );
		ASSERT_TRUE( Flush(*log, after) );
		EXPECT_EQ( stringValue(records(File()), after.Id()), "day six again" ) << "the new daily file lacks the template the archived one held";
	}

	//A round that fails waits out its backoff, Delay() the first time:  a flush before then leaves it alone, so a cause
	//that lasts doesn't have every flush read the daily file again.  With the timer an hour off, only the size trigger
	//flushes.
	TEST_F( DailyFileLayerTests, AFailedRoundWaitsOutItsBackoff ){
		let log = Make( "PT1H" );
		let five = App::ArchiveDayFile( _root, {std::chrono::year{2025}, std::chrono::month{3}, std::chrono::day{5}} );
		let six = App::ArchiveDayFile( _root, {std::chrono::year{2025}, std::chrono::month{3}, std::chrono::day{6}} );
		let obstruction = fs::path{ six }.concat( ".tmp" );
		fs::create_directories( obstruction );//a directory where day 6's temp file goes:  its write fails.
		auto fill = [&]( uint32_t from ){//past the size trigger, on day 6.
			for( uint32_t i=from; i<from+400; ++i )
				log->Write( entry(Chrono::ToTimePoint(2025, 3, 6, 12)+std::chrono::milliseconds{i}, ELogLevel::Information, i, "backoff {}", {std::to_string(i)}) );
		};
		log->Write( entry(Chrono::ToTimePoint(2025, 3, 5, 12), ELogLevel::Information, 5, "day five") );
		fill( 1000 );
		ASSERT_TRUE( waitUntil([&]{ return fs::exists(five); }) );
		ASSERT_TRUE( waitUntil([&]{ return (bool)log->TryLockDailyFile(); }) );
		ASSERT_FALSE( fs::exists(six) );
		fs::remove_all( obstruction );

		let before = Size();
		fill( 2000 );
		ASSERT_TRUE( waitUntil([&]{ return Size()>before; }) ) << "no flush landed";
		EXPECT_FALSE( waitUntil([&]{ return fs::exists(six); }, 1s) ) << "a flush inside the backoff ran the round again";
		EXPECT_TRUE( fs::exists(File()) );
	}

	//A refused daily file that is repaired in place, emptied here, is scanned again by the next flush, which writes to it.
	TEST_F( DailyFileLayerTests, ARefusedDailyFileRepairedInPlaceIsWrittenAgain ){
		save( File(), "not a protobuf" );//another program's:  the scan refuses it.
		let log = Make( "PT0.2S" );
		EXPECT_TRUE( BlockAwait(App::DailyLoadAwait(*log)).empty() );//its open:  the process holds the file refused.
		fs::resize_file( File(), 0 );
		let e = entry( Clock::now(), ELogLevel::Information, 1, "written once repaired" );
		log->Write( e );
		ASSERT_TRUE( waitUntil([&]{ return Size()>0 && (bool)log->TryLockDailyFile(); }) ) << "the repaired file is still refused";
		let all = records( File() );
		EXPECT_EQ( count(all, e.Id()), 1u );
		EXPECT_EQ( stringValue(all, e.Id()), "written once repaired" ) << "the rescan left the strings the refused file held";
	}

	//A day's archive that isn't the record layer's, any garbage file, is left as it is:  the round drops the day's
	//entries with an error, as the historian drops records for a file it leaves alone, archives the other days and
	//removes the daily file, so a bad day holds nothing else back.
	TEST_F( DailyFileLayerTests, AGarbageArchiveDropsItsDaysEntries ){
		let five = App::ArchiveDayFile( _root, {std::chrono::year{2025}, std::chrono::month{3}, std::chrono::day{5}} );
		let six = App::ArchiveDayFile( _root, {std::chrono::year{2025}, std::chrono::month{3}, std::chrono::day{6}} );
		fs::create_directories( five.parent_path() );
		save( five, "not a protobuf" );
		if( !Logging::FindLogger<Logging::MemoryLog>() )
			Logging::AddLogger( mu<Logging::MemoryLog>() );
		Logging::ClearMemory();
		let log = Make( "PT0.2S" );
		let e5 = entry( Chrono::ToTimePoint(2025, 3, 5, 12), ELogLevel::Information, 5, "day five" );
		let e6 = entry( Chrono::ToTimePoint(2025, 3, 6, 12), ELogLevel::Information, 6, "day six" );
		log->Write( e5 );
		log->Write( e6 );
		ASSERT_TRUE( waitUntil([&]{ return fs::exists(six); }) );
		ASSERT_TRUE( waitUntil([&]{ return !fs::exists(File()); }) ) << "the round didn't remove the daily file";
		EXPECT_EQ( contents(five), "not a protobuf" ) << "the garbage file was written to";
		EXPECT_EQ( count(records(six), e6.Id()), 1u );
		let said = Logging::Find( [&]( const Logging::Entry& e ){ return e.Level==ELogLevel::Error && e.Message().contains(five.string()); } );
		EXPECT_EQ( said.size(), 1u ) << "the dropped day went unsaid";
	}

	//A day's archive whose head is the layer's but which can't be read through past it:  the round drops the day's entries,
	//and its error says how many.  It counted them after they had gone into the rewrite, so it always said 0.
	TEST_F( DailyFileLayerTests, AnUnreadableArchiveSaysHowManyEntriesItDrops ){
		let five = App::ArchiveDayFile( _root, {std::chrono::year{2025}, std::chrono::month{3}, std::chrono::day{5}} );
		let six = App::ArchiveDayFile( _root, {std::chrono::year{2025}, std::chrono::month{3}, std::chrono::day{6}} );
		fs::create_directories( five.parent_path() );
		string bytes;
		Log::Appender archive{ bytes, 0 };
		IO::Proto::FileStart start;
		start.set_ts( 1 );
		start.set_generation( 1 );
		archive.Start( start );
		FileEntry merged;
		merged.mutable_merged()->set_daily( 1 );
		archive.Add( move(merged) );
		save( five, bytes+"not a protobuf" );
		if( !Logging::FindLogger<Logging::MemoryLog>() )
			Logging::AddLogger( mu<Logging::MemoryLog>() );
		Logging::ClearMemory();
		let log = Make( "PT0.2S" );
		log->Write( entry(Chrono::ToTimePoint(2025, 3, 5, 12), ELogLevel::Information, 5, "day five") );
		log->Write( entry(Chrono::ToTimePoint(2025, 3, 6, 12), ELogLevel::Information, 6, "day six") );
		ASSERT_TRUE( waitUntil([&]{ return fs::exists(six); }) );
		ASSERT_TRUE( waitUntil([&]{ return !fs::exists(File()); }) ) << "the round didn't remove the daily file";
		let said = Logging::Find( [&]( const Logging::Entry& e ){ return e.Level==ELogLevel::Error && e.Message().contains(five.string()) && e.Message().contains("entries for"); } );
		ASSERT_EQ( said.size(), 1u ) << "the dropped day went unsaid";
		EXPECT_TRUE( said.front().Message().contains("its 1 entries") ) << said.front().Message();
	}
}