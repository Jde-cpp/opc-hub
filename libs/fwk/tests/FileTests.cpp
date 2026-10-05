#include <fstream>
#include <latch>
#include <jde/fwk/io/Cache.h>
#include <jde/fwk/io/file.h>
#include <jde/fwk/log/MemoryLog.h>
#include <jde/fwk/io/FileAwait.h>
#include <jde/fwk/co/LockKey.h>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/utils/Vector.h>
#include <jde/fwk/process/thread.h>
#include <jde/fwk/chrono.h>
#include <stdexcept>
//a private header under src/, the way OpenSslTests reaches OpenSslInternal.h - IDrive/NativeDrive are not part of the
//installed surface, and the two platforms spell the concrete type differently.
#ifdef _MSC_VER
	#include "../src/process/os/windows/WindowsDrive.h"
	namespace Jde::IO{ using NativeDriveType = WindowsDrive; }
#else
	#include "../src/process/os/linux/LinuxDrive.h"
	namespace Jde::IO{ using NativeDriveType = Drive::NativeDrive; }
#endif

#define let const auto

using boost::uuids::uuid;
namespace Jde::IO::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };
	Ω file( uint index )->fs::path{
		let path = Settings::FindPath( "/testing/file" );
		return path
			? path->parent_path()/Ƒ( "{}{}{}", path->stem().string(), index, path->extension().string() )
			: fs::temp_directory_path()/Ƒ( "test{}.txt", index );
	}

	//poll until the predicate holds or the 10s deadline passes; returns whether it did.  The ASSERT stays at
	//the call site so a GoogleTest fatal failure returns from the test, not just from here.
	Ŧ waitFor( T predicate )ι->bool{
		let deadline = steady_clock::now()+10s;
		while( !predicate() && steady_clock::now()<deadline )
			std::this_thread::sleep_for( 5ms );
		return predicate();
	}

	struct FileTests : public ::testing::Test{
	protected:
		FileTests(){}
		~FileTests()override{}

		Ω SetUpTestCase()ι->void{
			Execution::Run();//io strand threading issue.
		}
		α SetUp()->void override{
			INFO( "{}", file(0).string() );
			fs::create_directories( file(0).parent_path() );
		}
		α TearDown()->void override{}
	};

	//path and values are co-owned, like writeRaw's 'done': on a timed-out wait testFile returns while a
	//detached write is still queued, and a reference would dangle when it finally records its guids.
	Ω write( fs::path file, uuid guid1, uuid guid2, sp<Vector<uuid>> written, bool createFile, SRCE )->LockKeyAwait::Task{
		auto l = co_await LockKeyAwait{ file.string() };
		[sl]( fs::path file, uuid guid1, uuid guid2, sp<Vector<uuid>> written, CoLockGuard, bool createFile )->VoidAwait::Task {
			try{
				co_await IO::WriteAwait{ file, Ƒ("{}\n{}\n", ToString(guid1), ToString(guid2)), createFile, IO::EWriteMode::Append, Jde::ELogTags::IO, sl };
			}
			catch( Exception& e ){
				e.Log();
				throw;
			}
			written->push_back( guid1 );
			written->push_back( guid2 );
		}( move(file), guid1, guid2, move(written), move(l), createFile );
	}

	Ω read( fs::path file, sp<Vector<uuid>> readValues, SRCE )ι->TAwait<string>::Task{
		let content = co_await IO::ReadAwait{ file, false, sl };
		let guidStrings = Str::Split( content, '\n' );
		ul _{ readValues->Mutex };
		for( auto&& guid : guidStrings ){
			try{
				readValues->push_back_locked( ToUuid(string{guid}) );
			}
			catch( const std::runtime_error& e ){
				THROW( "[{}] Failed to parse GUID from string '{}': {}", file.string(), guid, e.what() );
			}
		}
	}

	Ω testFile( uint fileIndex )->void{
		array<uuid,1024> guids;
		for( uint i=0; i<guids.size(); ++i ){
			auto prefix = boost::endian::endian_reverse( fileIndex );
			( (uint*)guids[i].data() )[0] = prefix;
			auto suffix = boost::endian::endian_reverse( i );
			( (uint*)guids[i].data() )[1] = suffix;
		}

		auto written = ms<Vector<uuid>>();
		let file = Tests::file( fileIndex );
		let exists = fs::exists( file );
		if( exists ){
			INFO( "Removing existing file: {}", file.string() );
			fs::remove( file );
		}
		Thread::SetName( file.filename().string() );
		for( uint i=0; i<guids.size(); i+=2 )
			write( file, guids[i], guids[i+1], written, i==0 );

		//deadlines, not unbounded polls: a throwing WriteAwait/ReadAwait never advances these, and with no
		//ctest TIMEOUT an IO regression would hang the whole run instead of failing this test.
		ASSERT_TRUE( waitFor([&]{ return written->size()>=guids.size(); }) ) << "only " << written->size() << " of " << guids.size() << " guids were written";

		auto readValues = ms<Vector<uuid>>();
		read( file, readValues );
		ASSERT_TRUE( waitFor([&]{ return !readValues->empty(); }) ) << "the read never completed";

		ASSERT_EQ( readValues->size(), guids.size() );
		readValues->visit( [&](const uuid& guid){
			ASSERT_TRUE( find(guids, guid)!=guids.end() );
		});
		//each write appends 'guid1\nguid2\n' as one WriteAwait under the key lock, so the file is a sequence of
		//whole pairs in order - chunk-order corruption (the bug this test guards) breaks that, and until now
		//could only surface as the guid parse THROW above or a count mismatch.
		let ordered = readValues->copy();
		for( uint i=0; i<ordered.size(); i+=2 ){
			let index = []( const uuid& x )ι{ return boost::endian::endian_reverse( ((const uint*)x.data())[1] ); };
			ASSERT_EQ( index(ordered[i])%2, 0u ) << "the pair at " << i << " does not start on a written pair";
			ASSERT_EQ( index(ordered[i+1]), index(ordered[i])+1 ) << "the pair at " << i << " is out of order";
		}
	}

	//done is co-owned: on a timed-out wait the test returns while the detached coroutine is still running, so a
	//reference would dangle when the completion finally stores true.  'error' takes the exception the failure
	//tests assert on - dropping it left the call site with only a content mismatch or a timeout to report.
	Ω writeRaw( fs::path file, string content, sp<std::atomic<bool>> done, sp<up<Exception>> error={}, IO::EWriteMode mode=IO::EWriteMode::Append, bool create=true, SRCE )->LockKeyAwait::Task{
		auto l = co_await LockKeyAwait{ file.string() };
		[sl]( fs::path file, string content, sp<std::atomic<bool>> done, CoLockGuard, IO::EWriteMode mode, sp<up<Exception>> error, bool create )->VoidAwait::Task {
			try{
				co_await IO::WriteAwait{ move(file), move(content), create, mode, Jde::ELogTags::IO, sl };
			}
			catch( Exception& e ){
				if( error )
					*error = e.Move();//Move, not a copy: it keeps the dynamic type the tests check for.
			}
			*done = true;
		}( move(file), move(content), move(done), move(l), mode, move(error), create );
	}

	Ω readRaw( fs::path file, sp<string> content, sp<std::atomic<bool>> done, sp<up<Exception>> error={}, IO::ReadOptions options={}, SRCE )ι->TAwait<string>::Task{
		try{
			*content = co_await IO::ReadAwait{ move(file), options, sl };
		}
		catch( Exception& e ){
			if( error )
				*error = e.Move();
		}
		*done = true;
	}

	Ω waitDone( const std::atomic<bool>& done )ι->bool{ return waitFor( [&done]{ return done.load(); } ); }

	//"" when the operation finished cleanly, otherwise why not: the exception it captured, or that it never
	//finished at all.  A failed io used to reach the call site as a content mismatch with the cause nowhere in
	//the report - the helpers caught and dropped it, and the assertion could only describe the symptom.
	Ω waitError( const sp<std::atomic<bool>>& done, const sp<up<Exception>>& error )ι->string{
		return !waitDone( *done ) ? string{"never completed"}
			: *error ? string{ (*error)->what() }
			: string{};
	}

	// Regression for the ChunksToSend off-by-one: a write whose size is an exact multiple of
	// ChunkByteSize() must still complete. ChunksToSend was computed as size/ChunkByteSize()+1,
	// one more than the number of chunks actually queued whenever the size divides evenly, so the
	// final chunk's completion never satisfied ChunksToSend==ChunksCompleted and the write hung.
	// 256 guards the Windows initial-window truncation: (uint8)ChunksToSend turned any multiple of
	// 256 chunks into a zero-size window, so no chunk ever started and the coroutine hung.
	TEST_F( FileTests, WriteExactChunkMultiple ){
		let chunkSize = IO::ChunkByteSize();
		for( uint chunks : {1u, 2u, 3u, 256u} ){
			let size = chunks*chunkSize;
			let file = Tests::file( 100+chunks );
			if( fs::exists(file) )
				fs::remove( file );
			let content = string( size, (char)('A'+chunks) ); //distinct, deterministic per file
			auto done = ms<std::atomic<bool>>(); auto error = ms<up<Exception>>();
			writeRaw( file, content, done, error );
			let failure = waitError( done, error );
			ASSERT_TRUE( failure.empty() ) << "write of " << size << " bytes (" << chunks << " chunk(s)): " << failure;

			let actual = IO::Load( file );
			ASSERT_EQ( actual.size(), size );
			ASSERT_EQ( actual, content );
		}
	}

	// Regression for the Windows chunk-offset scheme: chunk 0 wrote at EOF (offset -1) while chunks
	// 1+ wrote at their absolute buffer offsets, so a multi-chunk append to a non-empty file
	// overwrote the head of the file and appended only the first chunk's bytes. The second write
	// must land wholly after the first.
	TEST_F( FileTests, AppendMultiChunk ){
		let chunkSize = IO::ChunkByteSize();
		let file = Tests::file( 400 );
		if( fs::exists(file) )
			fs::remove( file );
		let first = string( chunkSize*3+chunkSize/2, 'x' );
		let second = string( chunkSize*3+chunkSize/2, 'y' );
		for( const string* content : {&first, &second} ){ //pointers, not {first,second} - that copies both into the init-list.
			auto done = ms<std::atomic<bool>>(); auto error = ms<up<Exception>>();
			writeRaw( file, *content, done, error );
			let failure = waitError( done, error );
			ASSERT_TRUE( failure.empty() ) << "append of " << content->size() << " bytes: " << failure;
		}
		let actual = IO::Load( file );
		ASSERT_EQ( actual, first+second );
	}

	// Regression for the hardcoded WriteAwait::_append{true}: no caller could ask for truncation, so a
	// read-modify-write (the log archive) appended its merged-in-full contents to the file it had just
	// read, growing it geometrically.  A Truncate write must replace the file, tail included.
	TEST_F( FileTests, TruncateReplacesFile ){
		let file = Tests::file( 500 );
		if( fs::exists(file) )
			fs::remove( file );
		let write = [&file]( str content, IO::EWriteMode mode )->string{
			auto done = ms<std::atomic<bool>>(); auto error = ms<up<Exception>>();
			writeRaw( file, content, done, error, mode );
			return waitError( done, error );
		};
		let long_ = string( IO::ChunkByteSize()*2, 'l' );//multi-chunk: the short write must not leave chunk 1 behind.
		let truncating = write( long_, IO::EWriteMode::Truncate );
		ASSERT_TRUE( truncating.empty() ) << "truncating write: " << truncating;
		ASSERT_EQ( IO::Load( file ), long_ );

		let short_ = string( 16, 's' );
		let second = write( short_, IO::EWriteMode::Truncate );
		ASSERT_TRUE( second.empty() ) << "second truncating write: " << second;
		ASSERT_EQ( IO::Load( file ), short_ ) << "truncate left the previous, longer contents in place";

		let appended = write( short_, IO::EWriteMode::Append );//the default must still append - the daily proto log depends on it.
		ASSERT_TRUE( appended.empty() ) << "append: " << appended;
		ASSERT_EQ( IO::Load( file ), short_+short_ );
	}

	// Regression: a zero-byte operation produced no chunks, so no completion ever arrived — the
	// awaiting coroutine hung and a completion poller was leaked. Empty writes must create the file
	// and resume; reads of empty files must resume with an empty string.
	TEST_F( FileTests, EmptyFile ){
		let file = Tests::file( 200 );
		if( fs::exists(file) )
			fs::remove( file );

		auto written = ms<std::atomic<bool>>(); auto writeError = ms<up<Exception>>();
		writeRaw( file, {}, written, writeError );
		let writeFailure = waitError( written, writeError );
		ASSERT_TRUE( writeFailure.empty() ) << "empty write: " << writeFailure;
		ASSERT_TRUE( fs::exists(file) );
		ASSERT_EQ( fs::file_size(file), 0u );

		auto content = ms<string>( "sentinel" );
		auto readDone = ms<std::atomic<bool>>(); auto readError = ms<up<Exception>>();
		readRaw( file, content, readDone, readError );
		let readFailure = waitError( readDone, readError );
		ASSERT_TRUE( readFailure.empty() ) << "empty read: " << readFailure;
		ASSERT_TRUE( content->empty() ) << "expected empty content, got: " << *content;
	}

	TEST_F( FileTests, CachedEmptyString ){
		let file = Tests::file( 300 );//never created on disk - a cache hit must not open the file.
		Cache::Set<string>( file.string(), "" );//explicit - `{}` is ambiguous between the T and sp<const T> overloads.
		auto content = ms<string>( "sentinel" );
		auto done = ms<std::atomic<bool>>(); auto error = ms<up<Exception>>();
		readRaw( file, content, done, error, {.Cache=true} );
		let failure = waitError( done, error );
		ASSERT_TRUE( failure.empty() ) << "cached empty read: " << failure;
		EXPECT_TRUE( content->empty() ) << "expected empty content, got: " << *content;
		Cache::Clear( file.string() );
	}

	// CachedEmptyString above only ever takes the hit branch - it pre-seeds the entry and the file never exists.
	// The other half, a cache=true read that *misses*, goes through io_uring and then Cache::Sets what it read, was
	// never driven: the populate line sits just past the `_fromCache || r.size()` guard that fwk-max #17 lived on, so
	// a regression that returned early would silently stop populating and nothing would notice.  Deleting the file
	// between the two reads is what proves the second one came from the cache rather than the disk again.
	TEST_F( FileTests, CachedReadPopulates ){
		let file = Tests::file( 900 );
		Cache::Clear( file.string() );
		if( fs::exists(file) )
			fs::remove( file );
		let content = string( IO::ChunkByteSize()*3+IO::ChunkByteSize()/2, 'c' );//several chunks, so the populate happens after a real multi-chunk completion.
		auto written = ms<std::atomic<bool>>(); auto writeError = ms<up<Exception>>();
		writeRaw( file, content, written, writeError );
		let writeFailure = waitError( written, writeError );
		ASSERT_TRUE( writeFailure.empty() ) << "write: " << writeFailure;

		ASSERT_FALSE( Cache::Get<string>(file.string()) ) << "the entry exists before any read - the test proves nothing";
		auto first = ms<string>();
		auto firstDone = ms<std::atomic<bool>>(); auto firstError = ms<up<Exception>>();
		readRaw( file, first, firstDone, firstError, {.Cache=true} );
		let firstFailure = waitError( firstDone, firstError );
		ASSERT_TRUE( firstFailure.empty() ) << "populating read: " << firstFailure;
		ASSERT_EQ( *first, content );
		let cached = Cache::Get<string>( file.string() );
		ASSERT_TRUE( cached ) << "the read completed but never populated the cache";
		EXPECT_EQ( *cached, content );

		fs::remove( file );//...so a second read that still succeeds can only have been served from the entry above.
		auto second = ms<string>( "sentinel" );
		auto secondDone = ms<std::atomic<bool>>(); auto secondError = ms<up<Exception>>();
		readRaw( file, second, secondDone, secondError, {.Cache=true} );
		let secondFailure = waitError( secondDone, secondError );
		ASSERT_TRUE( secondFailure.empty() ) << "cached read after the file was deleted: " << secondFailure;
		EXPECT_EQ( *second, content );

		//and cache=false must still go to disk, which is now gone.
		auto uncached = ms<string>( "sentinel" );
		auto uncachedDone = ms<std::atomic<bool>>(); auto uncachedError = ms<up<Exception>>();
		readRaw( file, uncached, uncachedDone, uncachedError );
		ASSERT_TRUE( waitDone(*uncachedDone) );
		EXPECT_TRUE( *uncachedError ) << "a cache=false read was served from the cache";
		Cache::Clear( file.string() );
	}

	Ω writeWith( fs::path file, variant<string,vector<byte>> content, IO::WriteOptions options, sp<std::atomic<bool>> done, sp<up<Exception>> error, SRCE )->VoidAwait::Task{
		try{
			co_await IO::WriteAwait{ move(file), move(content), options, sl };
		}
		catch( Exception& e ){
			*error = e.Move();
		}
		*done = true;
	}

	// Buffer is a variant<string,vector<byte>> and every write in this suite passes the string alternative, so
	// Data()/Size()'s visit and the chunk indexing were only ever instantiated for one of the two - a byte payload is
	// how ProtoLog::Save writes, and nothing here would catch it breaking.  A non-multiple of the chunk size keeps the
	// final short chunk in play, and the bytes are non-ASCII so a path that round-tripped them through a string would
	// have to preserve them exactly.
	//NativeDrive has no test file at all, so neither the mtime that CreateFolder/Save apply nor the conversion that
	//produces it is pinned - on linux that is to_timespec + utimensat (fwk-max #15 lived here), on windows GetTimes +
	//SetFileTime.  Note the two platforms gate on *different* fields: linux on ModifiedTime, windows on CreatedTime,
	//so a portable test has to set both or it silently exercises nothing on one of them.
	//clock_cast is C++20 chrono - libc++ is still at __cpp_lib_chrono 201611L and does not have it at all, so this has to
	//branch on the library feature, not the compiler.  What libc++ does offer is file_clock::to_sys, the same exact
	//conversion clock_cast would resolve to.  Not Chrono::ToClock (which is what ProtoLog::dailyFileDay and LogTests use):
	//that offsets by two now() readings and lands within a millisecond of the truth, and `when` below sits exactly on the
	//whole second these expectations floor to - a sub-millisecond error either way flips the comparison.
	Ω toSys( fs::file_time_type t )ι->TimePoint{
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
		return std::chrono::clock_cast<Clock>( t );
#else
		return std::chrono::time_point_cast<Duration>( fs::file_time_type::clock::to_sys(t) );
#endif
	}
	TEST( DriveTests, CreateFolderAndSaveApplyTheirTimes ){
		//native separators throughout: /testing/file arrives with forward slashes, and WindowsDrive hands the raw
		//string to CreateDirectory/CreateFileW, which reject a mixed path with ERROR_INVALID_NAME.
		auto rootPath = Tests::file( 0 ).parent_path()/"driveTests";
		rootPath.make_preferred();
		let root = rootPath;
		fs::remove_all( root );
		fs::create_directories( root );
		//two distinct instants, not one: with created==modified the test cannot tell the two apart, and a windows
		//GetTimes that returned the creation time in the modified slot would pass.
		let created = Chrono::ToTimePoint( 2024, 1, 2, 3, 4, 5 );
		let when = Chrono::ToTimePoint( 2024, 3, 4, 5, 6, 7 );
		IO::NativeDriveType drive;

		let dir = root/"folder";
		IO::IDirEntry entry{ IO::EFileFlags::Directory, dir, 0, created, when };
		let folder = drive.CreateFolder( dir, entry );
		ASSERT_TRUE( fs::is_directory(dir) );
		//to the second: the filesystem's timestamp granularity is not the TimePoint's, and utimensat/SetFileTime both
		//round-trip through a coarser representation.
		EXPECT_EQ( floor<std::chrono::seconds>(toSys(fs::last_write_time(dir))), floor<std::chrono::seconds>(when) ) << "the folder kept its creation-time mtime instead of the one asked for";
		ASSERT_TRUE( folder );
		EXPECT_EQ( floor<std::chrono::seconds>(folder->ModifiedTime), floor<std::chrono::seconds>(when) ) << "the entry handed back does not describe what was written";

		let file = root/"saved.bin";
		let bytes = vector<char>{ 'a', 'b', 'c' };
		IO::IDirEntry fileEntry{ IO::EFileFlags::None, file, bytes.size(), created, when };
		let saved = drive.Save( file, bytes, fileEntry );
		ASSERT_TRUE( fs::exists(file) );
		EXPECT_EQ( fs::file_size(file), bytes.size() );
		EXPECT_EQ( floor<std::chrono::seconds>(toSys(fs::last_write_time(file))), floor<std::chrono::seconds>(when) ) << "Save wrote the bytes but not the time";
		ASSERT_TRUE( saved );

		//and the drive's own reader has to see what its writer wrote - the conversion back is the other half of it.
		let read = drive.Get( file );
		ASSERT_TRUE( read );
		EXPECT_EQ( floor<std::chrono::seconds>(read->ModifiedTime), floor<std::chrono::seconds>(when) );
		EXPECT_EQ( read->Size, bytes.size() );
		EXPECT_EQ( drive.Load(*read), bytes );

		//a zero ModifiedTime means "leave it alone", not "set it to the epoch" - the whole block is gated on that.
		let untouched = root/"untouched";
		IO::IDirEntry noTime{ IO::EFileFlags::Directory, untouched, 0 };
		drive.CreateFolder( untouched, noTime );
		ASSERT_TRUE( fs::is_directory(untouched) );
		EXPECT_GT( toSys(fs::last_write_time(untouched)), when ) << "an unset time was applied as the epoch";
		fs::remove_all( root );
	}

	TEST_F( FileTests, WriteBytes ){
		let file = Tests::file( 901 );
		if( fs::exists(file) )
			fs::remove( file );
		vector<byte> data( IO::ChunkByteSize()*3+IO::ChunkByteSize()/2 );
		for( uint i=0; i<data.size(); ++i )
			data[i] = (byte)( i%251 );//251 is prime, so no value lines up with a chunk boundary.
		auto done = ms<std::atomic<bool>>(); auto error = ms<up<Exception>>();
		writeWith( file, data, {.Create=true, .Mode=IO::EWriteMode::Truncate}, done, error );
		let failure = waitError( done, error );
		ASSERT_TRUE( failure.empty() ) << "byte write: " << failure;

		let loaded = IO::LoadBinary( file );
		ASSERT_EQ( loaded.size(), data.size() );
		EXPECT_TRUE( std::equal(loaded.begin(), loaded.end(), (const char*)data.data()) ) << "the bytes on disk are not the bytes written";
	}

	// The IO failure paths had no coverage at all - readRaw/readRaw dropped every exception and no test asserted
	// an IOException, so a regression that resumed the coroutine without an error (or never resumed it) would
	// have shown up as a content mismatch or a hang.  ReadAwait::await_ready turns the open failure into
	// ExceptionPtr and await_resume rethrows it, so this never suspends.
	TEST_F( FileTests, ReadMissingFileThrows ){
		let file = Tests::file( 700 );
		if( fs::exists(file) )
			fs::remove( file );
		auto content = ms<string>( "sentinel" );
		auto error = ms<up<Exception>>();
		auto done = ms<std::atomic<bool>>();
		readRaw( file, content, done, error );
		ASSERT_TRUE( waitDone(*done) ) << "the failing read never completed";
		ASSERT_TRUE( *error ) << "reading a missing file did not throw";
		EXPECT_TRUE( dynamic_cast<IOException*>(error->get()) ) << (*error)->what();
		EXPECT_EQ( *content, "sentinel" ) << "a failed read must not assign";
	}

	TEST_F( FileTests, WriteNoCreateMissingFileThrows ){
		let file = Tests::file( 701 );
		if( fs::exists(file) )
			fs::remove( file );
		for( let mode : {IO::EWriteMode::Append, IO::EWriteMode::Truncate, IO::EWriteMode::At} ){
			auto error = ms<up<Exception>>();
			auto done = ms<std::atomic<bool>>();
			writeRaw( file, "x", done, error, mode, false );
			ASSERT_TRUE( waitDone(*done) ) << "the failing write never completed";
			ASSERT_TRUE( *error ) << "writing a missing file with create=false did not throw";
			let io = dynamic_cast<IOException*>( error->get() );
			ASSERT_TRUE( io ) << (*error)->what();
			EXPECT_EQ( io->Error, IO::EIOError::NotFound ) << io->what();
			EXPECT_FALSE( fs::exists(file) ) << "create=false created the file anyway";
		}
	}

	// A directory opens read-only fine on linux, so the failure arrives as -EISDIR on the io_uring completion -
	// the res<0 -> PostExp branch, which must also give back the request slot (fwk-max #5) or the poller falls
	// into its busy loop; windows fails at CreateFile instead.  Either way the coroutine resumes with an
	// IOException rather than hanging, and the io that follows still works.
	TEST_F( FileTests, ReadDirectoryThrows ){
		let dir = Tests::file( 0 ).parent_path();
		auto content = ms<string>( "sentinel" );
		auto error = ms<up<Exception>>();
		auto done = ms<std::atomic<bool>>();
		readRaw( dir, content, done, error );
		ASSERT_TRUE( waitDone(*done) ) << "the directory read never completed";
		ASSERT_TRUE( *error ) << "reading a directory did not throw";
		EXPECT_TRUE( dynamic_cast<IOException*>(error->get()) ) << (*error)->what();

		let file = Tests::file( 702 );
		if( fs::exists(file) )
			fs::remove( file );
		auto written = ms<std::atomic<bool>>(); auto writeError = ms<up<Exception>>();
		writeRaw( file, "after", written, writeError );
		let writeFailure = waitError( written, writeError );
		ASSERT_TRUE( writeFailure.empty() ) << "the write after the failed read: " << writeFailure;
		auto after = ms<string>();
		auto readDone = ms<std::atomic<bool>>(); auto readError = ms<up<Exception>>();
		readRaw( file, after, readDone, readError );
		let readFailure = waitError( readDone, readError );
		ASSERT_TRUE( readFailure.empty() ) << "the read after the failed read: " << readFailure;
		EXPECT_EQ( *after, "after" );
	}

	TEST_F( FileTests, ReadSurfacesItsOwnException ){
		let file = Tests::file( 800 );
		{
			std::ofstream os{ file, std::ios::binary };
			os << "not-a-guid\n";
		}
		auto& logger = Logging::GetLogger<Logging::MemoryLog>();
		Logging::ClearMemory();
		auto readValues = ms<Vector<uuid>>();
		read( file, readValues );
		let parseFailure = [&]{ return !logger.Find( function<bool(const Logging::Entry&)>{[](const Logging::Entry& e){
			return e.Message().contains( "Failed to parse GUID" );
		}} ).empty(); };
		ASSERT_TRUE( waitFor(parseFailure) ) << "the parse failure never reached the log";
		EXPECT_TRUE( logger.Find("unknown exception").empty() ) << "the exception was sliced on its way to the promise";
		EXPECT_TRUE( readValues->empty() );
	}

	TEST_F( FileTests, SaveBinaryRoundTrip ){
		let file = Tests::file( 600 );
		if( fs::exists(file) )
			fs::remove( file );
		let content = string( 16, 'r' );
		IO::SaveBinary<const char>( file, std::span{content} );
		let loaded = IO::LoadBinary( file );
		ASSERT_EQ( string(loaded.begin(), loaded.end()), content );
		EXPECT_EQ( IO::Load(file), content ) << "the string overload has to agree with the vector<char> one";

		//both sides are sized from fs::file_size and read binary, so an embedded NUL is content, not a terminator -
		//which is what makes these usable for the protobuf payloads (protobuf.h) and drive blobs that call them.
		string binary{ "a\0b", 3 };
		IO::SaveBinary<const char>( file, std::span{binary} );
		EXPECT_EQ( IO::Load(file).size(), 3u );
		EXPECT_EQ( IO::Load(file), binary );
		EXPECT_EQ( IO::LoadBinary(file).size(), 3u );
	}

	//the append flag is the whole of SaveBinary's mode selection and nothing chose the non-default before.  Getting it
	//backwards is silent in both directions: an append that truncates loses the earlier records, a default that
	//appends grows a file that every caller reads back whole.
	TEST_F( FileTests, SaveBinaryAppendsOnlyWhenAsked ){
		let file = Tests::file( 601 );
		if( fs::exists(file) )
			fs::remove( file );
		let first = string( 8, 'a' ), second = string( 8, 'b' );
		IO::SaveBinary<const char>( file, std::span{first} );
		IO::SaveBinary<const char>( file, std::span{second}, true );
		EXPECT_EQ( IO::Load(file), first+second ) << "append=true truncated";
		IO::SaveBinary<const char>( file, std::span{second} );
		EXPECT_EQ( IO::Load(file), second ) << "the default appended - it must truncate";
	}

	//Load/LoadBinary open CHECK_PATH-first, so a missing path is an IOException naming it rather than an empty string
	//handed back to a caller that cannot tell the difference.  A directory gets past CHECK_PATH (it exists) and fails
	//in fs::file_size instead, which arrives wrapped rather than as a raw fs::filesystem_error.
	TEST_F( FileTests, LoadThrowsRatherThanReturningNothing ){
		let missing = Tests::file( 602 );
		if( fs::exists(missing) )
			fs::remove( missing );
		EXPECT_THROW( IO::Load(missing), IOException );
		EXPECT_THROW( IO::LoadBinary(missing), IOException );
		let dir = missing.parent_path();
		EXPECT_THROW( IO::Load(dir), IOException ) << "a directory is not a file to read";
		EXPECT_THROW( IO::LoadBinary(dir), IOException );
	}

	//the platform-independent half of SaveBinaryThrowsOnWriteFailure above, which needs /dev/full and so is skipped on
	//windows: opening a directory for writing fails at the ofstream, before any of the buffering subtlety.
	TEST_F( FileTests, SaveBinaryToADirectoryThrows ){
		let dir = Tests::file( 0 ).parent_path();
		let content = string( 16, 'd' );
		ASSERT_TRUE( fs::is_directory(dir) );
		EXPECT_THROW( IO::SaveBinary<const char>(dir, std::span{content}), IOException );
	}

	//every caller discards the bool, so nothing said which way round it is - and "already there" reads equally well as
	//either answer.  It is fs::create_directories': true means this call created it, false means it was already a
	//directory; anything that is not a directory is an error, not a false.
	TEST_F( FileTests, CreateDirectoriesReportsWhetherItCreated ){
		let root = Tests::file( 0 ).parent_path()/"createDirectories";
		fs::remove_all( root );
		let nested = root/"a"/"b";
		EXPECT_TRUE( IO::CreateDirectories(nested) ) << "a new tree reports that it was created";
		EXPECT_TRUE( fs::is_directory(nested) );
		EXPECT_FALSE( IO::CreateDirectories(nested) ) << "an existing directory is not an error, and is not a creation either";
		EXPECT_FALSE( IO::CreateDirectories(root) ) << "...nor is an existing parent of it";

		let file = Tests::file( 603 );
		let content = string( 4, 'c' );
		IO::SaveBinary<const char>( file, std::span{content} );
		EXPECT_THROW( IO::CreateDirectories(file/"child"), IOException ) << "a file standing where a directory has to go is an error, not a false";
		fs::remove_all( root );
	}

	// Regression for fwk-max #29 (SaveBinary ignored the write result): the THROW_IFX it added ran while the
	// ofstream was still buffered, so for a payload under the streambuf size fail() was false and the ENOSPC
	// only surfaced when the destructor closed the stream - the failure the fix was supposed to report was
	// still lost.  16 bytes is that buffered case; the close() SaveBinary now does before the check is what
	// makes it throw.
	TEST_F( FileTests, SaveBinaryThrowsOnWriteFailure ){
		let full = fs::path{ "/dev/full" };//every write to it fails ENOSPC; no Windows equivalent.
		if( !fs::exists(full) )
			GTEST_SKIP() << full.string() << " not available on this platform";
		let content = string( 16, 'f' );
		EXPECT_THROW( IO::SaveBinary<const char>(full, std::span{content}), IOException );
	}

	//"" when the write finished cleanly, as waitError.
	Ω writeNow( const fs::path& file, string content, IO::WriteOptions options, SRCE )ι->string{
		auto done = ms<std::atomic<bool>>(); auto error = ms<up<Exception>>();
		writeWith( file, move(content), options, done, error, sl );
		return waitError( done, error );
	}
	//distinct at every offset a chunk boundary can fall on, so bytes that land in the wrong place compare unequal.
	Ω pattern( uint size )ι->string{
		string y( size, '\0' );
		for( uint i=0; i<size; ++i )
			y[i] = (char)( 'a'+i%23 );
		return y;
	}

	TEST_F( FileTests, WriteAtOverwritesInPlace ){
		let chunk = IO::ChunkByteSize();
		let file = Tests::file( 1000 );
		fs::remove( file );
		let original = pattern( chunk*3+chunk/2 );
		ASSERT_EQ( writeNow(file, original, {.Create=true, .Mode=IO::EWriteMode::Truncate}), "" );

		let patch = string( chunk+chunk/2, 'Z' );
		let offset = chunk/2+1;//starts mid-chunk and ends mid-chunk.
		ASSERT_EQ( writeNow(file, patch, {.Mode=IO::EWriteMode::At, .Offset=offset}), "" );
		auto expected = original;
		expected.replace( offset, patch.size(), patch );
		EXPECT_EQ( IO::Load(file), expected ) << "an At write moved or dropped bytes outside its range";
	}

	TEST_F( FileTests, WriteAtMissingFile ){
		let file = Tests::file( 1001 );
		fs::remove( file );
		auto error = ms<up<Exception>>();
		auto done = ms<std::atomic<bool>>();
		writeWith( file, "x", {.Mode=IO::EWriteMode::At, .Offset=4}, done, error );
		ASSERT_TRUE( waitDone(*done) ) << "the failing write never completed";
		ASSERT_TRUE( *error ) << "an At write to a missing file without Create did not throw";
		EXPECT_TRUE( dynamic_cast<IOException*>(error->get()) ) << (*error)->what();
		EXPECT_FALSE( fs::exists(file) ) << "Create=false created the file anyway";

		ASSERT_EQ( writeNow(file, "x", {.Create=true, .Mode=IO::EWriteMode::At, .Offset=4}), "" );
		EXPECT_EQ( IO::Load(file), string( 4, '\0' )+"x" ) << "a write past the end leaves zeros before it";
	}

	// Truncate with an Offset keeps that prefix and puts the data after it, so whatever followed the prefix is gone -
	// an append that first drops a torn tail.  Offset 0 is the whole-file replace TruncateReplacesFile pins.
	TEST_F( FileTests, TruncateKeepsPrefix ){
		let chunk = IO::ChunkByteSize();
		let file = Tests::file( 1002 );
		fs::remove( file );
		let original = pattern( chunk*4 );
		ASSERT_EQ( writeNow(file, original, {.Create=true, .Mode=IO::EWriteMode::Truncate}), "" );

		let kept = chunk+chunk/2;
		let data = string( chunk, 'Z' );
		ASSERT_EQ( writeNow(file, data, {.Mode=IO::EWriteMode::Truncate, .Offset=kept}), "" );
		let expected = original.substr( 0, kept )+data;
		ASSERT_EQ( IO::Load(file), expected ) << "the old tail outlived a write shorter than it";

		//a prefix that is the whole file is a plain append.
		ASSERT_EQ( writeNow(file, data, {.Mode=IO::EWriteMode::Truncate, .Offset=expected.size()}), "" );
		EXPECT_EQ( IO::Load(file), expected+data );
	}

	// An Offset on an Append is an At that lost its Mode.  Appending anyway would grow the file on every write, so the
	// write fails and leaves the file alone.  The bool overloads refuse a number outright, where it would have converted
	// to cache or create.
	TEST_F( FileTests, AppendWithOffsetThrows ){
		static_assert( std::is_constructible_v<IO::ReadAwait, fs::path, bool> && !std::is_constructible_v<IO::ReadAwait, fs::path, int> && !std::is_constructible_v<IO::ReadAwait, fs::path, uint> );
		static_assert( std::is_constructible_v<IO::WriteAwait, fs::path, string, bool> && !std::is_constructible_v<IO::WriteAwait, fs::path, string, int> && !std::is_constructible_v<IO::WriteAwait, fs::path, string, uint> );

		let file = Tests::file( 1011 );
		fs::remove( file );
		let content = string{ "0123456789" };
		ASSERT_EQ( writeNow(file, content, {.Create=true}), "" );

		auto error = ms<up<Exception>>(); auto done = ms<std::atomic<bool>>();
		writeWith( file, "x", {.Offset=4}, done, error );
		ASSERT_TRUE( waitDone(*done) ) << "the failing write never completed";
		let io = dynamic_cast<IOException*>( error->get() );
		ASSERT_TRUE( io ) << "an Append with an Offset did not throw";
		EXPECT_EQ( io->Error, IO::EIOError::Invalid ) << io->what();
		EXPECT_FALSE( io->Written );
		EXPECT_EQ( IO::Load(file), content ) << "the write that failed changed the file";
	}

#ifndef _MSC_VER
	// Only an open that may make the file makes the directories on its path.  A write without Create to a file that
	// isn't there fails at the open and leaves nothing behind:  no directories, and no log line claiming one.
	TEST_F( FileTests, NoCreateMakesNoDirectories ){
		let root = Tests::file( 1010 ).parent_path()/"noCreate";
		fs::remove_all( root );
		auto& logger = Logging::GetLogger<Logging::MemoryLog>();
		Logging::ClearMemory();
		let createdDirs = [&logger]()->uint{
			return logger.Find( function<bool(const Logging::Entry&)>{[](const Logging::Entry& e){ return e.Message().contains( "Created dir" ); }} ).size();
		};
		let failedAtOpen = []( str failure )->bool{ return failure.contains( "- open path=" ) && !failure.contains( "create_directories" ); };

		let orphan = root/"a"/"orphan.txt";
		for( let mode : {IO::EWriteMode::Append, IO::EWriteMode::Truncate, IO::EWriteMode::At} ){
			let failure = writeNow( orphan, "x", {.Mode=mode} );
			EXPECT_TRUE( failedAtOpen(failure) ) << "'" << failure << "'";
		}
		EXPECT_FALSE( fs::exists(root) ) << "a write without Create made directories";

		fs::create_directories( root );
		EXPECT_TRUE( failedAtOpen(writeNow(root/"missing.txt", "x", {})) );
		EXPECT_EQ( createdDirs(), 0u ) << "logged a directory it never made";

		let relative = writeNow( "noCreate-missing.txt", "x", {} );
		EXPECT_TRUE( failedAtOpen(relative) ) << "'" << relative << "'";

		ASSERT_EQ( writeNow(orphan, "x", {.Create=true}), "" );
		EXPECT_EQ( IO::Load(orphan), "x" );
		EXPECT_EQ( createdDirs(), 1u ) << "Create makes the directory, and says so";
	}
#endif

	// A Truncate that keeps a prefix needs the file to hold it.  Growing a shorter or missing file to Offset would pass
	// zeros off as the kept bytes, so the write fails and leaves the file as it found it.
	TEST_F( FileTests, TruncatePastEndThrows ){
		let file = Tests::file( 1007 );
		fs::remove( file );
		let content = string{ "0123456789" };
		ASSERT_EQ( writeNow(file, content, {.Create=true, .Mode=IO::EWriteMode::Truncate}), "" );

		auto error = ms<up<Exception>>(); auto done = ms<std::atomic<bool>>();
		writeWith( file, "x", {.Mode=IO::EWriteMode::Truncate, .Offset=content.size()+1}, done, error );
		ASSERT_TRUE( waitDone(*done) ) << "the failing write never completed";
		let io = dynamic_cast<IOException*>( error->get() );
		ASSERT_TRUE( io ) << "a Truncate past the end did not throw";
		EXPECT_TRUE( string{io->what()}.contains("past the end") ) << io->what();
		EXPECT_EQ( io->Error, IO::EIOError::Invalid );
		EXPECT_EQ( IO::Load(file), content ) << "the write that failed changed the file";

		fs::remove( file );
		EXPECT_NE( writeNow(file, "x", {.Create=true, .Mode=IO::EWriteMode::Truncate, .Offset=4}), "" ) << "no file, so no prefix to keep";
		EXPECT_FALSE( fs::exists(file) ) << "the write that failed left a file behind";
	}

#ifdef _MSC_VER
	// Windows share modes:  a reader can hold a file that is appended to, written in place or cut to a prefix, but not one
	// that is replaced - it would size its buffer from one version and read the bytes of another.
	TEST_F( FileTests, ReplaceExcludesReader ){
		let file = Tests::file( 1008 );
		fs::remove( file );
		let content = string{ "0123456789" };
		ASSERT_EQ( writeNow(file, content, {.Create=true, .Mode=IO::EWriteMode::Truncate}), "" );
		{
			const DWORD shareAll = FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE;//what ReadAwait's open shares.
			HandlePtr reader{ WinHandle{::CreateFileA(file.string().c_str(), GENERIC_READ, shareAll, nullptr, OPEN_EXISTING, 0, nullptr)} };
			ASSERT_TRUE( static_cast<bool>(reader.get()) ) << "the reader's open failed: " << ::GetLastError();
			EXPECT_NE( writeNow(file, "x", {.Mode=IO::EWriteMode::Truncate}), "" ) << "a replace went ahead under a reader";
			EXPECT_EQ( IO::Load(file), content ) << "the replace that failed changed the file";
			EXPECT_EQ( writeNow(file, "a", {}), "" ) << "an append under a reader";
			EXPECT_EQ( writeNow(file, "Z", {.Mode=IO::EWriteMode::At}), "" ) << "a write in place under a reader";
			EXPECT_EQ( writeNow(file, "b", {.Mode=IO::EWriteMode::Truncate, .Offset=content.size()+1}), "" ) << "a cut to a prefix under a reader";
			EXPECT_EQ( IO::Load(file), "Z123456789ab" );
		}
		EXPECT_EQ( writeNow(file, "x", {.Mode=IO::EWriteMode::Truncate}), "" ) << "a replace once the reader has closed";
		EXPECT_EQ( IO::Load(file), "x" );
	}
#endif

	// At and Truncate place every chunk by its offset, so they send a window of chunks at once, as a read does, and a
	// sync follows the last to finish.  Whatever order the chunks land in, each byte ends up where it belongs.
	TEST_F( FileTests, PositionedWritesSendManyChunks ){
		let chunk = IO::ChunkByteSize();
		let file = Tests::file( 1013 );
		fs::remove( file );
		let original = pattern( chunk*40+3 );
		ASSERT_EQ( writeNow(file, original, {.Create=true, .Mode=IO::EWriteMode::Truncate, .Sync=true}), "" );
		ASSERT_EQ( IO::Load(file), original );

		let patch = string( chunk*20+1, 'Z' );
		ASSERT_EQ( writeNow(file, patch, {.Mode=IO::EWriteMode::At, .Offset=chunk+1, .Sync=true}), "" );
		auto expected = original;
		expected.replace( chunk+1, patch.size(), patch );
		ASSERT_EQ( IO::Load(file), expected );

		let tail = pattern( chunk*30+7 );
		ASSERT_EQ( writeNow(file, tail, {.Mode=IO::EWriteMode::Truncate, .Offset=chunk*5, .Sync=true}), "" );
		EXPECT_EQ( IO::Load(file), expected.substr(0, chunk*5)+tail );
	}

	// Sync adds an fdatasync after the last chunk, and its completion is what resumes the awaiter.  Durability is not
	// observable here - this pins that each mode still completes, once, with the right bytes, and that an empty write,
	// which has no chunk to follow, still resumes.
	TEST_F( FileTests, SyncedWrites ){
		let chunk = IO::ChunkByteSize();
		let file = Tests::file( 1003 );
		fs::remove( file );
		let first = pattern( chunk*2+3 ), second = string( chunk+1, 'b' );
		ASSERT_EQ( writeNow(file, first, {.Create=true, .Mode=IO::EWriteMode::Truncate, .Sync=true}), "" );
		ASSERT_EQ( IO::Load(file), first );
		ASSERT_EQ( writeNow(file, second, {.Sync=true}), "" );
		ASSERT_EQ( IO::Load(file), first+second );
		ASSERT_EQ( writeNow(file, "ZZ", {.Mode=IO::EWriteMode::At, .Offset=1, .Sync=true}), "" );
		auto expected = first+second;
		expected.replace( 1, 2, "ZZ" );
		ASSERT_EQ( IO::Load(file), expected );
		ASSERT_EQ( writeNow(file, {}, {.Sync=true}), "" ) << "an empty write that syncs";
		EXPECT_EQ( IO::Load(file), expected );

		fs::remove( file );
		ASSERT_EQ( writeNow(file, {}, {.Create=true, .Sync=true}), "" ) << "an empty write that creates and syncs";
		EXPECT_TRUE( fs::exists(file) );
		EXPECT_EQ( fs::file_size(file), 0u );
	}

	// The proof that Sync really syncs, and that its failure reaches the awaiter:  /dev/null takes any write and has no
	// fsync, so the same write succeeds without Sync and fails with it.
	TEST_F( FileTests, SyncFailureSurfaces ){
		let null = fs::path{ "/dev/null" };//no Windows equivalent that refuses a flush.
		if( !fs::exists(null) )
			GTEST_SKIP() << null.string() << " not available on this platform";
		ASSERT_EQ( writeNow(null, "x", {}), "" );
		ASSERT_EQ( writeNow(null, "x", {.Mode=IO::EWriteMode::Truncate}), "" ) << "a device has no length to cut";

		auto error = ms<up<Exception>>();
		auto done = ms<std::atomic<bool>>();
		writeWith( null, "x", {.Sync=true}, done, error );
		ASSERT_TRUE( waitDone(*done) ) << "the failing sync never completed";
		ASSERT_TRUE( *error ) << "a sync the device refuses did not throw";
		let io = dynamic_cast<IOException*>( error->get() );
		ASSERT_TRUE( io ) << (*error)->what();
		EXPECT_TRUE( string{io->what()}.contains("fdatasync") ) << "it failed, but not at the sync:  " << io->what();
		EXPECT_TRUE( io->Written ) << "the write landed, and only the sync after it failed";
		EXPECT_EQ( io->Error, IO::EIOError::Invalid ) << io->what();

		ASSERT_EQ( writeNow(Tests::file(1005), "after", {.Create=true, .Mode=IO::EWriteMode::Truncate, .Sync=true}), "" ) << "the io after a failed sync";
	}

	// A synced write that adds a name syncs its directory as well - the file's, and on linux the parent of each directory
	// made for it - or a power loss could keep the data and lose the name.  The syncs aren't observable, so this counts
	// the ones logged, and pins that a file already there adds none.
	TEST_F( FileTests, SyncedCreateSyncsDirectories ){
		let root = Tests::file( 1006 ).parent_path()/"syncDirs";
		fs::remove_all( root );
		auto& logger = Logging::GetLogger<Logging::MemoryLog>();
		Logging::ClearMemory();
		let dirSyncs = [&logger]( const fs::path& file )->uint{
			return logger.Find( function<bool(const Logging::Entry&)>{[&file](const Logging::Entry& e){
				let message = e.Message();
				return message.contains( "Syncing directory" ) && message.contains( file.string() );
			}} ).size();
		};

		let nested = root/"a"/"b"/"nested.txt";
#ifdef _MSC_VER
		fs::create_directories( nested.parent_path() );//windows' Open makes no directories, so the file's is the only name it adds.
		let added = 1u;
#else
		let added = 4u;//the file's directory, and the parents of the three made for it.
#endif
		ASSERT_EQ( writeNow(nested, "x", {.Create=true, .Sync=true, .Tags=ELogTags::Test}), "" );
		EXPECT_EQ( IO::Load(nested), "x" );
		EXPECT_EQ( dirSyncs(nested), added ) << "a directory that gained a name was not synced";

		ASSERT_EQ( writeNow(nested, "y", {.Create=true, .Sync=true, .Tags=ELogTags::Test}), "" );
		ASSERT_EQ( writeNow(nested, "z", {.Create=true, .Mode=IO::EWriteMode::Truncate, .Sync=true, .Tags=ELogTags::Test}), "" );
		EXPECT_EQ( IO::Load(nested), "z" ) << "Create on a file already there still appends to it and truncates it";
		EXPECT_EQ( dirSyncs(nested), added ) << "a write to a file already there added no name";

		let sibling = root/"a"/"b"/"sibling.txt";
		ASSERT_EQ( writeNow(sibling, {}, {.Create=true, .Sync=true, .Tags=ELogTags::Test}), "" ) << "an empty write that creates and syncs";
		EXPECT_EQ( dirSyncs(sibling), 1u ) << "a new file in a directory already there";

		let unsynced = nested.parent_path()/"unsynced.txt";
		ASSERT_EQ( writeNow(unsynced, "x", {.Create=true, .Tags=ELogTags::Test}), "" );
		EXPECT_EQ( dirSyncs(unsynced), 0u ) << "a write that doesn't sync";
	}

	//Writes that make the same new directories at once, as the historian's groups do at a new day:  one whose
	//create_directories finds them made by another a moment ago opens again, rather than failing with ENOENT.
	TEST_F( FileTests, ConcurrentCreatesShareNewDirectories ){
#ifdef _WIN32
		GTEST_SKIP() << "windows' Open makes no directories.";
#endif
		let root = Tests::file( 1007 ).parent_path()/"concurrentDirs";
		fs::remove_all( root );
		constexpr uint threads{ 8 }, rounds{ 40 };
		uint failures{};
		string first;
		for( uint round=0; round<rounds; ++round ){
			let dir = root/std::to_string( round )/"a"/"b";
			vector<sp<std::atomic<bool>>> done;
			vector<sp<up<Exception>>> errors;
			for( uint i=0; i<threads; ++i ){
				done.push_back( ms<std::atomic<bool>>() );
				errors.push_back( ms<up<Exception>>() );
			}
			std::latch start{ threads };
			{
				vector<std::jthread> writers;
				for( uint i=0; i<threads; ++i ){
					writers.emplace_back( [&, i]{
						start.arrive_and_wait();
						writeWith( dir/Ƒ("{}.txt", i), string{"x"}, {.Create=true, .Sync=true, .Tags=ELogTags::Test}, done[i], errors[i] );
					} );
				}
			}
			for( uint i=0; i<threads; ++i ){
				if( let error = waitError(done[i], errors[i]); !error.empty() && !failures++ )
					first = error;
			}
		}
		fs::remove_all( root );
		EXPECT_EQ( failures, 0 ) << "of " << threads*rounds << ", the first:  " << first;
	}

	// IOException::Error names what went wrong without the native code, an errno here and a GetLastError value on
	// windows.  Written is set only by a failed sync:  a write that fails may have left none or part of its data.
	TEST_F( FileTests, FailuresAreClassified ){
		let fail = []( const fs::path& file, IO::WriteOptions options )->sp<up<Exception>>{
			auto error = ms<up<Exception>>(); auto done = ms<std::atomic<bool>>();
			writeWith( file, "x", options, done, error );
			waitDone( *done );
			return error;
		};
		let missing = Tests::file( 1009 );
		fs::remove( missing );
		auto error = fail( missing, {.Mode=IO::EWriteMode::At} );
		auto io = dynamic_cast<IOException*>( error->get() );
		ASSERT_TRUE( io ) << "a write to a missing file without Create did not throw an IOException";
		EXPECT_EQ( io->Error, IO::EIOError::NotFound ) << io->what();
		EXPECT_FALSE( io->Written );

		let full = fs::path{ "/dev/full" };//refuses every write with ENOSPC - no windows equivalent.
		if( fs::exists(full) ){
			error = fail( full, {} );
			io = dynamic_cast<IOException*>( error->get() );
			ASSERT_TRUE( io ) << "a write the device refuses did not throw an IOException";
			EXPECT_EQ( io->Error, IO::EIOError::NoSpace ) << io->what();
			EXPECT_FALSE( io->Written ) << "the write itself failed";
		}
	}

	// The options reach the op through its constructor, and a read has no write mode:  nothing that asks whether an op
	// appends or truncates can take a read for one.
	TEST( FileAwaitTests, OptionsReachTheOp ){
		IO::ReadAwait read{ "x", IO::ReadOptions{.Offset=4, .Size=2, .Tags=ELogTags::Test} };
		EXPECT_TRUE( read._arg->IsRead );
		EXPECT_FALSE( read._arg->Mode );
		EXPECT_EQ( read._arg->Offset, 4u );
		EXPECT_EQ( read._arg->Limit, 2u );
		EXPECT_EQ( read._arg->_tags, ELogTags::Test );

		IO::WriteAwait write{ "x", string{"data"}, IO::WriteOptions{.Mode=IO::EWriteMode::At, .Offset=8, .Sync=true, .Tags=ELogTags::Test} };
		EXPECT_FALSE( write._arg->IsRead );
		EXPECT_EQ( write._arg->Mode, IO::EWriteMode::At );
		EXPECT_EQ( write._arg->Offset, 8u );
		EXPECT_TRUE( write._arg->Sync );
		EXPECT_FALSE( write._arg->Limit );
		EXPECT_EQ( write._arg->_tags, ELogTags::Test );
		EXPECT_EQ( (IO::WriteAwait{"x", string{"data"}}._arg->Mode), IO::EWriteMode::Append ) << "the default";
	}

	TEST( IOExceptionTests, MapsNativeCodes ){
#ifdef _MSC_VER
		EXPECT_EQ( IO::ToIOError(ERROR_PATH_NOT_FOUND), IO::EIOError::NotFound );
		EXPECT_EQ( IO::ToIOError(ERROR_SHARING_VIOLATION), IO::EIOError::Busy );
		EXPECT_EQ( IO::ToIOError(ERROR_DISK_FULL), IO::EIOError::NoSpace );
		let notFound = ERROR_FILE_NOT_FOUND;
#else
		EXPECT_EQ( IO::ToIOError(EDQUOT), IO::EIOError::NoSpace );
		EXPECT_EQ( IO::ToIOError(EBUSY), IO::EIOError::Busy );
		EXPECT_EQ( IO::ToIOError(EIO), IO::EIOError::Device );
		let notFound = ENOENT;
#endif
		EXPECT_EQ( IO::ToIOError(0xFFFFFF), IO::EIOError::None );
		EXPECT_EQ( (IOException{ "x", (uint32)notFound, "open" }.Error), IO::EIOError::NotFound );
		EXPECT_EQ( (IOException{ fs::filesystem_error{"open", fs::path{"x"}, std::error_code{(int)notFound, std::system_category()}} }.Error), IO::EIOError::NotFound );
		EXPECT_EQ( (IOException{ "x", "no code" }.Error), IO::EIOError::None );
	}

	// A read logs under ReadOptions.Tags, as a write does under WriteOptions.Tags.
	TEST_F( FileTests, ReadLogsUnderItsTags ){
		let file = Tests::file( 1012 );
		fs::remove( file );
		ASSERT_EQ( writeNow(file, "content", {.Create=true}), "" );
		auto& logger = Logging::GetLogger<Logging::MemoryLog>();
		Logging::ClearMemory();
		auto content = ms<string>(); auto done = ms<std::atomic<bool>>(); auto error = ms<up<Exception>>();
		readRaw( file, content, done, error, {.Tags=ELogTags::Test} );
		ASSERT_EQ( waitError(done, error), "" );
		EXPECT_EQ( *content, "content" );
		let suspends = logger.Find( function<bool(const Logging::Entry&)>{[&file](const Logging::Entry& e){
			return e.Tags==ELogTags::Test && e.Message().contains( "ReadAwait::Suspend" ) && e.Message().contains( file.string() );
		}} );
		EXPECT_EQ( suspends.size(), 1u ) << "the read did not log under the tag it was given";
	}

	TEST_F( FileTests, ReadRange ){
		let chunk = IO::ChunkByteSize();
		let file = Tests::file( 1004 );
		fs::remove( file );
		let content = pattern( chunk*4+chunk/2 );
		ASSERT_EQ( writeNow(file, content, {.Create=true, .Mode=IO::EWriteMode::Truncate}), "" );

		let read = [&file]( IO::ReadOptions options )->string{
			auto y = ms<string>( "sentinel" );
			auto done = ms<std::atomic<bool>>(); auto error = ms<up<Exception>>();
			readRaw( file, y, done, error, options );
			let failure = waitError( done, error );
			return failure.empty() ? *y : "failed: "+failure;
		};
		EXPECT_EQ( read({.Offset=chunk/2+1, .Size=chunk*2+chunk/4}), content.substr(chunk/2+1, chunk*2+chunk/4) ) << "mid-chunk to mid-chunk";
		EXPECT_EQ( read({.Size=3}), content.substr(0, 3) );
		EXPECT_EQ( read({.Offset=chunk+1}), content.substr(chunk+1) ) << "no Size reads to the end";
		EXPECT_EQ( read({.Offset=chunk*4, .Size=chunk*3}), content.substr(chunk*4) ) << "a Size past the end is clamped";
		EXPECT_EQ( read({.Offset=content.size(), .Size=5}), "" ) << "a range that starts at the end";
		EXPECT_EQ( read({.Offset=content.size()+7}), "" ) << "a range that starts past the end";
		EXPECT_EQ( read({}), content ) << "no range is the whole file";
	}

	constexpr uint _fileSize{ 5 };
	TEST_F( FileTests, WriteRead ){
		ASSERT_TRUE( IO::ChunkByteSize()<74 ); //guid+\n*2
		ASSERT_TRUE( IO::ThreadSize()>1 ); //guid+\n
		vector<std::jthread> threads;
		for( uint i=0; i<_fileSize; ++i )
			threads.emplace_back( [i](){testFile(i);} );
		for( auto& thread : threads )
				thread.join();
	}
}