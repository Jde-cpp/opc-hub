//The record layer (include/jde/fwk/io/records):  frames with CRC-32C checkpoints, the first-open scan and the truncation
//that drops a torn append whole, the run merge, the rewrite in parts, the .flushed slots, the day walk and the OS
//primitives, over a test record with a delta-encoded time, as the historian's.
#include <fstream>
#include <ranges>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/fwk/io/records/Scan.h>
#include <jde/fwk/io/records/Merge.h>
#include <jde/fwk/io/records/Rewrite.h>
#include <jde/fwk/io/records/FileState.h>
#include <jde/fwk/io/records/Flushed.h>
#include <jde/fwk/io/durable.h>
#include <jde/fwk/io/crc.h>
#include <jde/fwk/log/MemoryLog.h>
#include <jde/fwk/process/execution.h>
DISABLE_WARNINGS
#include <jde/fwk/tests/proto/Tests.Records.pb.h>
#include <google/protobuf/io/coded_stream.h>
ENABLE_WARNINGS

#define let const auto

namespace Jde::IO::Records{
	//The test record:  a FileStart at 1, a Sample at 2 and a Checkpoint at 3; a sample's time is absolute in memory and
	//a delta from the record before on disk.
	template<> struct Traits<RecordsTests::Proto::TestRecord>{
		using R = RecordsTests::Proto::TestRecord;
		static constexpr uint32_t StartField{ 1 };
		static constexpr uint32_t CheckpointField{ 3 };
		Ω Known( uint32_t field )ι->bool{ return field>=1 && field<=3; }
		Ω Time( const R& r )ι->optional<Records::Time>{ return r.has_sample() ? optional<Records::Time>{ r.sample().ts() } : nullopt; }
		Ω ToDisk( R& r, State& chain )ι->void{
			if( r.has_file_start() )
				chain = r.file_start().ts();
			else if( r.has_sample() ){
				let t = r.sample().ts();
				r.mutable_sample()->set_ts( t-chain );
				chain = t;
			}
		}
		Ω ToMemory( R& r, State& chain )ι->void{
			if( r.has_file_start() )
				chain = r.file_start().ts();
			else if( r.has_sample() ){
				let t = chain+r.sample().ts();
				r.mutable_sample()->set_ts( t );
				chain = t;
			}
		}
		Ω SetStart( R& r, const Proto::FileStart& f )ι->void{ *r.mutable_file_start() = f; }
	};
}
namespace Jde::IO::Records::Tests{
	using namespace std::chrono;
	using RecordsTests::Proto::TestRecord;
	using TestTraits = Traits<TestRecord>;
	constexpr uint32_t StartField{ TestTraits::StartField };
	constexpr uint32_t CheckpointField{ TestTraits::CheckpointField };
	constexpr Time DayStart{ 1'000'000'000 };//the file's day start, in the owner's units.
	constexpr Day March6{ 2026y/March/6 };
	constexpr Day March7{ 2026y/March/7 };

	namespace{
		Ξ at( int64_t d )ι->Time{ return DayStart+d; }
		Ω fileStart( uint32_t generation=0 )ι->TestRecord{
			TestRecord y;
			auto& start = *y.mutable_file_start();
			start.set_ts( DayStart );
			start.set_generation( generation );
			start.set_crc( StartCrc(start) );
			return y;
		}
		Ω sample( Time ts, string text )ι->TestRecord{
			TestRecord y;
			y.mutable_sample()->set_ts( ts );
			y.mutable_sample()->set_text( move(text) );
			return y;
		}
		//A record of a field this build doesn't have.
		Ω unknown()ι->TestRecord{
			TestRecord y;
			y.GetReflection()->MutableUnknownFields( &y )->AddLengthDelimited( 9, "newer" );
			return y;
		}
		//r's frame as its own serialization writes it:  a FileStart's crc as set, generation left out when 0.
		Ω raw( const TestRecord& r )ι->string{ return r.SerializeAsString(); }
		Ω contents( const fs::path& file )ι->string{
			std::ifstream f{ file, std::ios::binary };
			return string{ std::istreambuf_iterator<char>{f}, {} };
		}
		Ω save( const fs::path& file, sv bytes )ι->void{
			std::ofstream f{ file, std::ios::binary | std::ios::trunc };
			f.write( bytes.data(), bytes.size() );
		}
		Ω expectEqual( const TestRecord& a, const TestRecord& b )ι->void{ EXPECT_EQ( a.SerializeAsString(), b.SerializeAsString() ); }
		//Every record of run but the checkpoints, through windows of windowBytes cut from bytes, with their times absolute.
		Ω readRun( sv bytes, Records::Run run, uint windowBytes=MergeWindowBytes )ι->vector<TestRecord>{
			Reader<TestRecord> reader{ run, windowBytes };
			vector<TestRecord> y;
			TestRecord r;
			for( ;; ){
				let next = reader.Next( r );
				if( next==ENext::More ){
					let w = *reader.Wants();
					reader.Feed( string{bytes.substr(w.Offset, w.Size)} );
					continue;
				}
				if( next==ENext::End )
					break;
				if( reader.Field()!=CheckpointField )
					y.push_back( r );
			}
			EXPECT_EQ( reader.Stop(), EStop::End );
			EXPECT_EQ( reader.Offset(), run.End );
			return y;
		}
		Ω readWhole( sv bytes )ι->vector<TestRecord>{ return readRun( bytes, Records::Run{.Offset=0, .End=(uint)bytes.size(), .Chain=0, .First=0, .Last=0} ); }
		Ω scan( const fs::path& file, std::function<void( TestRecord& )> sealed={} )ε->Scanned{
			Decoder<TestRecord> decoder{ move(sealed) };
			return ScanNow( ms<OpenFile>(file), StartField, CheckpointField, decoder );
		}
		Ω testDir()ι->fs::path{ return fs::current_path()/"records-tests"/::testing::UnitTest::GetInstance()->current_test_info()->name(); }
	}

	struct LayerTests : ::testing::Test{
		Ω SetUpTestCase()ι->void{ Execution::Run(); }
		α SetUp()->void override{ fs::create_directories( Dir ); }
		α TearDown()->void override{
			fs::remove_all( Dir );
			std::error_code ec;
			fs::remove( Dir.parent_path(), ec );//once the last test's is gone.
		}
		const fs::path Dir{ testDir() };
	};

	//A frame is its field's tag, length and body:  a FileStart's every field written, a checkpoint 7 bytes whatever its crc.
	TEST_F( LayerTests, Frames ){
		string widest;
		Proto::FileStart start;
		start.set_ts( std::numeric_limits<int64_t>::min() );
		start.set_generation( std::numeric_limits<uint32_t>::max() );
		EncodeStart( widest, StartField, start );
		EXPECT_EQ( widest.size(), MaxStartFrame );
		Cursor cursor{ widest, 0 };
		auto raw = cursor.Next();
		ASSERT_TRUE( raw );
		EXPECT_EQ( raw->Field, StartField );
		let decoded = DecodeStart( raw->Body );
		ASSERT_TRUE( decoded );
		EXPECT_EQ( decoded->ts(), start.ts() );
		EXPECT_EQ( decoded->generation(), start.generation() );
		string narrow;
		EncodeStart( narrow, StartField, Proto::FileStart{} );//ts 0, generation 0:  written all the same.
		EXPECT_EQ( narrow.size(), 2u+1+1+1+1+1+4 );
		TestRecord parsed;
		ASSERT_TRUE( parsed.ParseFromString(narrow) );
		EXPECT_TRUE( parsed.has_file_start() );

		for( let crc : {0u, 1u, std::numeric_limits<uint32_t>::max()} ){
			string checkpoint;
			EncodeCheckpoint( checkpoint, CheckpointField, crc );
			ASSERT_EQ( checkpoint.size(), CheckpointBytes );
			let mark = CheckpointMark( CheckpointField );
			EXPECT_EQ( sv{checkpoint}.substr(0, 3), (sv{mark.data(), 3}) );
			Cursor c{ checkpoint, 0 };
			auto frame = c.Next();
			ASSERT_TRUE( frame );
			EXPECT_EQ( DecodeCheckpoint(frame->Body), crc );
			TestRecord r;
			ASSERT_TRUE( r.ParseFromString(checkpoint) );
			EXPECT_EQ( r.checkpoint().crc(), crc );
		}
	}

	//The run's last record is a checkpoint over the bytes before it.
	TEST_F( LayerTests, Checkpoint ){
		string bytes;
		Appender<TestRecord> appender{ bytes, 0 };
		appender.Start( fileStart().file_start() );
		appender.Add( sample(at(1), "a") );
		let checkpointAt = bytes.size();
		appender.Seal();
		Reader<TestRecord> reader{ Records::Run{.Offset=0, .End=(uint)bytes.size(), .Chain=0, .First=0, .Last=0} };
		reader.Feed( string{bytes} );
		TestRecord r;
		ASSERT_EQ( reader.Next(r), ENext::Record );
		EXPECT_TRUE( r.has_file_start() );
		ASSERT_EQ( reader.Next(r), ENext::Record );
		EXPECT_EQ( r.sample().ts(), at(1) );//absolute again.
		ASSERT_EQ( reader.Next(r), ENext::Record );
		ASSERT_TRUE( r.has_checkpoint() );
		EXPECT_EQ( reader.Field(), CheckpointField );
		EXPECT_EQ( r.checkpoint().crc(), Crc::Calc32c(sv{bytes}.substr(0, checkpointAt)) );
		EXPECT_EQ( reader.Offset(), bytes.size() );
		EXPECT_EQ( reader.Next(r), ENext::End );
		EXPECT_EQ( reader.Stop(), EStop::End );
	}

	//A day's live file as its first flushes leave it:  the FileStart and two records at the day's start, a flush, and a
	//flush with a late record.
	struct ScanTests : LayerTests{
		ScanTests()ε{
			Append( fileStart(), {sample(DayStart, "Pump1.Speed"), sample(DayStart, "Pump1.Flow")} );
			Append( {}, {sample(at(1000), "a"), sample(at(2000), "b"), sample(at(3000), "c"), sample(at(60000), "heartbeat")} );
			Append( {}, {sample(at(500), "late"), sample(at(70000), "lost"), sample(at(80000), "server"), sample(at(90000), "removed")} );
		}
		struct Placed{ uint Start; uint End; uint Run; };
		α Append( optional<TestRecord> start, vector<TestRecord> run )ε->void{
			Appender<TestRecord> appender{ Bytes, Chains.empty() ? 0 : Chains.back() };
			if( start ){
				let s = Bytes.size();
				appender.Start( start->file_start() );
				Records.push_back( {s, Bytes.size(), (uint)Runs.size()} );
				run.insert( run.begin(), *start );
			}
			for( uint i=start ? 1 : 0; i<run.size(); ++i ){
				let s = Bytes.size();
				appender.Add( TestRecord{run[i]} );
				Records.push_back( {s, Bytes.size(), (uint)Runs.size()} );
			}
			let s = Bytes.size();
			Chains.push_back( appender.Seal() );
			Records.push_back( {s, Bytes.size(), (uint)Runs.size()} );
			Ends.push_back( Bytes.size() );
			Runs.push_back( move(run) );
		}
		//How many runs end within size bytes, all of which the scan keeps.
		α Kept( uint size )Ι->uint{ return std::ranges::upper_bound( Ends, size )-Ends.begin(); }
		α End( uint runs )Ι->uint{ return runs ? Ends[runs-1] : 0; }
		α Expected( uint runs )Ι->vector<TestRecord>{
			vector<TestRecord> y;
			for( uint i=0; i<runs; ++i )
				y.insert( y.end(), Runs[i].begin(), Runs[i].end() );
			return y;
		}
		α Write( sv bytes )Ι->void{ save( File, bytes ); }
		α OnDisk()Ι->string{ return contents( File ); }
		//bytes as the file, scanned, which leaves it as it is, then truncated for an append, and what that left of it.
		α ScanFile( sv bytes )Ε->tuple<Scanned,string>{
			Write( bytes );
			auto scanned = scan( File );
			EXPECT_EQ( scanned.FileSize, bytes.size() );
			EXPECT_EQ( OnDisk(), bytes );
			EXPECT_FALSE( scanned.Keep() );
			Truncate( File, scanned );
			EXPECT_EQ( scanned.FileSize, scanned.Size );
			return { move(scanned), OnDisk() };
		}
		//Truncate's refusal, which an operator must see:  at Error.
		α RefusedTruncate( Scanned& scanned )Ε->void{
			try{
				Truncate( File, scanned );
				ADD_FAILURE() << "Truncate didn't refuse.";
			}
			catch( const IOException& e ){
				EXPECT_EQ( e.Level(), ELogLevel::Error ) << e.what();
			}
		}
		//bytes as a file that isn't a live one's:  the scan keeps nothing of it, and the truncation refuses it.
		α Refused( sv bytes )Ε->Scanned{
			Write( bytes );
			auto scanned = scan( File );
			EXPECT_EQ( scanned.Size, 0 );
			EXPECT_EQ( scanned.StopOffset, 0 );
			EXPECT_TRUE( scanned.Keep() );
			RefusedTruncate( scanned );
			EXPECT_EQ( scanned.FileSize, bytes.size() );
			EXPECT_EQ( OnDisk(), bytes );
			return scanned;
		}
		//bytes as a live file damaged before a run its owner sealed:  the scan keeps the runs before the damage, and the
		//truncation refuses it.
		α Damaged( sv bytes )Ε->Scanned{
			Write( bytes );
			auto scanned = scan( File );
			EXPECT_TRUE( scanned.SealedAfter );
			EXPECT_TRUE( scanned.Keep() );
			RefusedTruncate( scanned );
			EXPECT_EQ( scanned.FileSize, bytes.size() );
			EXPECT_EQ( OnDisk(), bytes );
			return scanned;
		}
		//Where record's body starts in Bytes.
		α BodyStart( const Placed& record )Ι->uint{
			Cursor cursor{ sv{Bytes}.substr(record.Start, record.End-record.Start), record.Start };
			let raw = cursor.Next();
			return (uint)( raw->Body.data()-Bytes.data() );
		}

		string Bytes;
		vector<vector<TestRecord>> Runs;
		vector<Placed> Records;//checkpoints included.
		vector<uint> Ends;//each run's.
		vector<State> Chains;//where the chain stands after each run.
		const fs::path File{ Dir/"group.binpb" };
	};

	//A torn flush is dropped whole, wherever the file ends, and the chain resumes where the kept runs leave it.
	//The scan hands each record of every append it keeps to the decoder, as a read through what it kept would see it, and
	//none of a torn one:  so a first open maps and restores in the one read.
	TEST_F( ScanTests, SealedTakesWhatIsKept ){
		for( uint size=0; size<=Bytes.size(); ++size ){
			SCOPED_TRACE( Ƒ("file cut at byte {} of {}", size, Bytes.size()) );
			Write( sv{Bytes}.substr(0, size) );
			vector<string> sealed;
			(void)scan( File, [&]( TestRecord& r ){ sealed.push_back( r.SerializeAsString() ); } );
			vector<string> expected;
			for( let& r : Expected(Kept(size)) )
				expected.push_back( r.SerializeAsString() );
			ASSERT_EQ( sealed, expected );
		}
	}

	TEST_F( ScanTests, TruncatesAtEveryOffset ){
		for( uint size=0; size<=Bytes.size(); ++size ){
			SCOPED_TRACE( Ƒ("file cut at byte {} of {}", size, Bytes.size()) );
			let runs = Kept( size );
			let [scanned, onDisk] = ScanFile( sv{Bytes}.substr(0, size) );
			ASSERT_EQ( scanned.Size, End(runs) );
			ASSERT_EQ( onDisk, Bytes.substr(0, End(runs)) );
			EXPECT_EQ( scanned.Start.has_value(), runs>0 );
			if( size==End(runs) )
				EXPECT_EQ( scanned.Stop, EStop::End );
			ASSERT_EQ( scanned.Runs.size(), runs );
			for( uint i=0; i<runs; ++i ){
				let& run = scanned.Runs[i];
				EXPECT_EQ( run.Offset, End(i) );
				EXPECT_EQ( run.End, Ends[i] );
				EXPECT_EQ( run.Chain, i ? Chains[i-1] : 0 );
				let [first, last] = std::ranges::minmax( Runs[i] | std::views::filter([]( let& r ){ return TestTraits::Time(r).has_value(); })
					| std::views::transform([]( let& r ){ return *TestTraits::Time(r); }) );
				EXPECT_EQ( run.First, first );
				EXPECT_EQ( run.Last, last );
			}
			if( !runs )
				continue;
			EXPECT_EQ( scanned.Chain, Chains[runs-1] );
			string resumed{ onDisk };
			Appender<TestRecord> next{ resumed, scanned.Chain };
			next.Add( sample(at(100000), "resumed") );
			next.Seal();
			auto expected = Expected( runs );
			expected.push_back( sample(at(100000), "resumed") );
			let got = readWhole( resumed );
			ASSERT_EQ( got.size(), expected.size() );
			for( uint i=0; i<got.size(); ++i )
				expectEqual( got[i], expected[i] );
		}
	}

	//What some filesystems leave after a power loss:  zeros past the last write.  A zero byte reads as an empty record.
	TEST_F( ScanTests, ZeroFilledTail ){
		const string zeros( 4096, '\0' );
		auto [scanned, onDisk] = ScanFile( Bytes+zeros );
		EXPECT_EQ( scanned.Size, Bytes.size() );
		EXPECT_EQ( scanned.Stop, EStop::Empty );
		EXPECT_EQ( onDisk, Bytes );
		EXPECT_EQ( scanned.Runs.size(), Runs.size() );

		std::tie( scanned, onDisk ) = ScanFile( Bytes.substr(0, Ends[1]+5)+zeros );//the third flush torn after 5 bytes.
		EXPECT_EQ( scanned.Size, Ends[1] );
		EXPECT_EQ( onDisk, Bytes.substr(0, Ends[1]) );

		std::tie( scanned, onDisk ) = ScanFile( zeros );//a new file whose FileStart never landed.
		EXPECT_EQ( scanned.Size, 0 );
		EXPECT_FALSE( scanned.Start );
		EXPECT_EQ( scanned.Stop, EStop::Empty );
		EXPECT_TRUE( onDisk.empty() );
	}

	//Zeros where a run's bytes were, which no torn flush leaves before a later run:  damage, so the file is left as it is,
	//with the scan keeping the runs before it.  At the file's start too, where a zero byte alone would be a torn FileStart.
	TEST_F( ScanTests, ZeroedSector ){
		for( let& [at, kept] : {std::pair<uint,uint>{0, 0}, std::pair<uint,uint>{Ends[0]+3, 1}} ){
			SCOPED_TRACE( Ƒ("16 zeros at byte {}", at) );
			string zeroed{ Bytes };
			std::fill_n( zeroed.begin()+at, 16, '\0' );
			let scanned = Damaged( zeroed );
			EXPECT_EQ( scanned.Size, End(kept) );
			EXPECT_EQ( scanned.Runs.size(), kept );
			EXPECT_EQ( scanned.SealedAfter, Ends[kept+1] );
		}
	}

	//A body garbled with its length intact, checkpoints' included, whether it no longer parses or parses and only the CRC
	//catches it.  The scan keeps the runs before it.  Before a run its owner sealed, it is damage and the file is left as
	//it is; in the last run it is a torn flush, and its run is dropped whole.  A sealed run is found by its checkpoint's
	//bytes and the ones before them, so the run after an unparsable checkpoint can't be proved.
	TEST_F( ScanTests, GarbledBody ){
		for( let& record : Records ){
			let bodyStart = BodyStart( record );
			string unparsed{ Bytes }, parsed{ Bytes };
			for( auto i=bodyStart; i<record.End; ++i )
				unparsed[i] ^= 0xFF;
			parsed[record.End-1] ^= 1;//every record here ends in a string's last byte, a varint's last byte or a CRC.
			for( let& [garbled, crc] : {tuple{unparsed, false}, tuple{parsed, true}} ){
				SCOPED_TRACE( Ƒ("{} record at bytes {}-{} of run {}", crc ? "parsing" : "unparsable", record.Start, record.End, record.Run) );
				if( !record.Start ){//a whole first record that is no good FileStart:  not a live file's.
					let stop = Refused( garbled ).Stop;
					if( crc )
						EXPECT_EQ( stop, EStop::NoFileStart );
					else
						EXPECT_NE( stop, EStop::End );
					continue;
				}
				let checkpoint = record.End==Ends[record.Run];
				let provable = record.Run+2<Runs.size() || (record.Run+2==Runs.size() && (crc || !checkpoint));
				Scanned scanned;
				if( provable )
					scanned = Damaged( garbled );
				else{
					string onDisk;
					std::tie( scanned, onDisk ) = ScanFile( garbled );
					EXPECT_EQ( onDisk, Bytes.substr(0, End(record.Run)) );
				}
				EXPECT_EQ( scanned.Size, End(record.Run) );
				EXPECT_EQ( scanned.Runs.size(), record.Run );
				if( crc )
					EXPECT_EQ( scanned.Stop, EStop::Crc );
				else
					EXPECT_NE( scanned.Stop, EStop::End );
			}
		}
	}

	//A length garbled to claim the rest of the file, whose bytes are all there:  the frame parses as a record with its fields
	//repeated, which its length gives away.  The runs after it are sealed, so the file is left as it is.
	TEST_F( ScanTests, GarbledLength ){
		let& record = Records[Runs[0].size()+1];//the second run's first record.
		let start = record.Start;
		ASSERT_EQ( start, Ends[0] );
		let rest = Bytes.substr( BodyStart(record) );
		uint8_t prefix[5];
		let prefixEnd = google::protobuf::io::CodedOutputStream::WriteVarint32ToArray( (uint32_t)rest.size(), prefix );
		let garbled = Bytes.substr( 0, start+1 )+string{ reinterpret_cast<char*>(prefix), (uint)(prefixEnd-prefix) }+rest;

		Reader<TestRecord> reader{ Records::Run{.Offset=0, .End=(uint)garbled.size(), .Chain=0, .First=0, .Last=0} };
		TestRecord r;
		for( auto n = reader.Next(r); n!=ENext::End; n = reader.Next(r) ){
			if( n==ENext::More ){
				let w = *reader.Wants();
				reader.Feed( string{sv{garbled}.substr(w.Offset, w.Size)} );
			}
		}
		EXPECT_EQ( reader.Stop(), EStop::Body );
		EXPECT_EQ( reader.Offset(), start );

		let scanned = Damaged( garbled );
		EXPECT_EQ( scanned.Stop, EStop::Body );
		EXPECT_EQ( scanned.StopOffset, start );
		EXPECT_EQ( scanned.Size, start );
	}
	//A length garbled past MaxFrameBytes is damage where it stands, not a frame the file cuts short:  the reader and the
	//scan stop at it without reading on for what it claims, and the runs after it are kept.
	TEST_F( ScanTests, GarbledPastMaxFrame ){
		let& record = Records[Runs[0].size()+1];//the second run's first record.
		let start = record.Start;
		let rest = Bytes.substr( BodyStart(record) );
		uint8_t prefix[5];
		let prefixEnd = google::protobuf::io::CodedOutputStream::WriteVarint32ToArray( (uint32_t)MaxFrameBytes, prefix );
		let garbled = Bytes.substr( 0, start+1 )+string{ reinterpret_cast<char*>(prefix), (uint)(prefixEnd-prefix) }+rest;

		Reader<TestRecord> reader{ Records::Run{.Offset=0, .End=(uint)garbled.size(), .Chain=0, .First=0, .Last=0} };
		TestRecord r;
		for( auto n = reader.Next(r); n!=ENext::End; n = reader.Next(r) ){
			if( n==ENext::More ){
				let w = *reader.Wants();
				reader.Feed( string{sv{garbled}.substr(w.Offset, w.Size)} );
			}
		}
		EXPECT_EQ( reader.Stop(), EStop::BadLength );
		EXPECT_EQ( reader.Offset(), start );

		let scanned = Damaged( garbled );
		EXPECT_EQ( scanned.Stop, EStop::BadLength );
		EXPECT_EQ( scanned.StopOffset, start );
		EXPECT_EQ( scanned.Size, start );
	}

	//A frame that straddles two windows is read whole, the window grown to it:  every window size reads the same records.
	TEST_F( ScanTests, WindowBoundaries ){
		let expected = Expected( Runs.size() );
		for( let window : vector<uint>{1, 2, 3, 5, 8, 13, 64, Bytes.size()} ){
			SCOPED_TRACE( Ƒ("{} byte windows", window) );
			let records = readRun( Bytes, Records::Run{.Offset=0, .End=(uint)Bytes.size(), .Chain=0, .First=0, .Last=0}, window );
			ASSERT_EQ( records.size(), expected.size() );
			for( uint i=0; i<records.size(); ++i )
				expectEqual( records[i], expected[i] );
		}
	}

	//Each run reads alone, from its offset to its end, through the open file:  what a read of a live file's runs does.
	TEST_F( ScanTests, ReadsARun ){
		let [scanned, _] = ScanFile( Bytes );
		ASSERT_EQ( scanned.Runs.size(), Runs.size() );
		auto file = ms<OpenFile>( File );
		for( uint i=0; i<Runs.size(); ++i ){
			Reader<TestRecord> reader{ scanned.Runs[i] };
			vector<TestRecord> got;
			TestRecord r;
			while( NextNow(reader, file, r) ){
				if( reader.Field()!=CheckpointField )
					got.push_back( r );
			}
			EXPECT_EQ( reader.Stop(), EStop::End );
			ASSERT_EQ( got.size(), Runs[i].size() );
			for( uint j=0; j<got.size(); ++j )
				expectEqual( got[j], Runs[i][j] );
		}
	}

	//An archive is replaced whole, never torn, and has no checkpoints:  the scan leaves it as it is, and reads it through
	//only for a decoder that wants its records.
	TEST_F( ScanTests, Archive ){
		string archive;
		Appender<TestRecord> appender{ archive, 0 };
		appender.Start( fileStart(1).file_start() );
		appender.Add( sample(DayStart, "Pump1.Speed") );
		appender.Add( sample(at(1000), "a") );
		let [scanned, onDisk] = ScanFile( archive );
		ASSERT_TRUE( scanned.Start );
		EXPECT_EQ( scanned.Start->generation(), 1 );
		EXPECT_EQ( scanned.Size, archive.size() );
		EXPECT_EQ( onDisk, archive );
		EXPECT_TRUE( scanned.Runs.empty() );
		vector<TestRecord> sealed;
		(void)scan( File, [&]( TestRecord& r ){ sealed.push_back( r ); } );
		ASSERT_EQ( sealed.size(), 3 );
		EXPECT_EQ( sealed[2].sample().ts(), at(1000) );
	}

	//A scan of a file that has grown or shrunk since:  the truncation refuses to cut by it, which would drop bytes it never
	//read, or lengthen the file.
	TEST_F( ScanTests, StaleScan ){
		let torn = Bytes.substr( 0, Ends[1]+5 );
		Write( torn );
		auto scanned = scan( File );
		EXPECT_EQ( scanned.FileSize, torn.size() );
		EXPECT_FALSE( scanned.Keep() );
		for( let& changed : {torn+"later", torn.substr(0, Ends[1]+2)} ){
			Write( changed );
			RefusedTruncate( scanned );
			EXPECT_EQ( OnDisk(), changed );
			EXPECT_EQ( scanned.FileSize, torn.size() );
		}
	}

	//A scan that keeps less than the whole file logs it once, read or not:  at Warning for a torn flush, which the first
	//append drops, and at Error for what must be kept.  A whole file logs nothing.
	TEST_F( ScanTests, LogsWhatItLeavesOut ){
		if( !Logging::FindLogger<Logging::MemoryLog>() )
			Logging::AddLogger( mu<Logging::MemoryLog>() );
		let logged = [this]( sv bytes ){
			Write( bytes );
			Logging::ClearMemory();
			(void)scan( File );
			return Logging::Find( [this]( const Logging::Entry& e ){ return e.Message().contains(File.string()); } );
		};
		EXPECT_TRUE( logged(Bytes).empty() );
		let torn = logged( Bytes.substr(0, Ends[1]+5) );
		ASSERT_EQ( torn.size(), 1 );
		EXPECT_EQ( torn[0].Level, ELogLevel::Warning ) << torn[0].Message();
		string zeroed{ Bytes };
		std::fill_n( zeroed.begin()+Ends[0]+3, 16, '\0' );
		let damaged = logged( zeroed );
		ASSERT_EQ( damaged.size(), 1 );
		EXPECT_EQ( damaged[0].Level, ELogLevel::Error ) << damaged[0].Message();
	}

	//A whole first frame that isn't a FileStart is a damaged archive or another program's file, never a torn append:  the
	//truncation leaves it as it is.  So is a first byte that isn't the FileStart's tag, a varint the layer never writes,
	//or a FileStart frame cut short that no FileStart fills.
	TEST_F( ScanTests, NoFileStart ){
		string bytes;
		Appender<TestRecord> appender{ bytes, DayStart };
		appender.Add( sample(at(1000), "a") );
		appender.Seal();
		EXPECT_EQ( Refused(bytes).Stop, EStop::NoFileStart );

		EXPECT_EQ( Refused("\xC2\x3E\x01\x01"sv).Stop, EStop::NoFileStart );//a whole frame of field 1000, which no build knows.
		EXPECT_EQ( Refused("\xFF\xD8\xFF\xE0\x00\x10JFIF"sv).Stop, EStop::BadLength );//a JPEG, whose first bytes read as a padded varint.
		EXPECT_EQ( Refused("\x80\x00"sv).Stop, EStop::BadLength );//0, padded.
		EXPECT_EQ( Refused("\x80"sv).Stop, EStop::NoFileStart );//a tag past 15, cut short.
		EXPECT_EQ( Refused("\x0A\x40 claims 64 bytes"sv).Stop, EStop::BadLength );//cut short, but longer than any FileStart.
		string claims{ "\x0A" };//a FileStart's tag claiming two windows, all there:  judged before the second is read.
		uint8_t prefix[5];
		claims.append( reinterpret_cast<char*>(prefix), (uint)(google::protobuf::io::CodedOutputStream::WriteVarint32ToArray((uint32_t)WindowBytes*2, prefix)-prefix) );
		claims.resize( claims.size()+WindowBytes*2, 'x' );
		EXPECT_EQ( Refused(claims).Stop, EStop::BadLength );
		EXPECT_EQ( Refused("\x08\x01"sv).Stop, EStop::Tag );//field 1 as a varint.

		string archive;
		Appender<TestRecord> archived{ archive, 0 };
		archived.Start( fileStart(1).file_start() );
		let start = archive.size();
		archived.Add( sample(at(1000), "a") );
		for( uint i=1; i<start; ++i )
			archive[i] ^= 0xFF;
		EXPECT_NE( Refused(archive).Stop, EStop::End );
	}

	//A read's scan of a file with a torn append serves what is good and leaves the rest for the first append to drop.
	TEST_F( ScanTests, OnlyTruncateChanges ){
		let torn = Bytes.substr( 0, Ends[1]+5 );
		Write( torn );
		auto scanned = scan( File );
		EXPECT_EQ( scanned.Size, Ends[1] );
		EXPECT_EQ( scanned.FileSize, torn.size() );
		EXPECT_EQ( scanned.StopOffset, Ends[1] );
		EXPECT_EQ( scanned.Runs.size(), 2 );
		EXPECT_EQ( OnDisk(), torn );

		Truncate( File, scanned );
		EXPECT_EQ( scanned.FileSize, Ends[1] );
		EXPECT_EQ( OnDisk(), Bytes.substr(0, Ends[1]) );
		Write( Bytes );//the append that follows.
		Truncate( File, scanned );
		EXPECT_EQ( OnDisk(), Bytes );
	}

	//A FileStart is trusted only with its own CRC:  a generation or a time that damage changed is refused, not followed.
	TEST_F( ScanTests, FileStartCrc ){
		let rest = Bytes.substr( Records[0].End );
		auto archived = fileStart();
		archived.mutable_file_start()->set_generation( 1 );//a live file that would read as an archive.
		EXPECT_EQ( Refused(raw(archived)+rest).Stop, EStop::NoFileStart );
		auto none = fileStart();
		none.mutable_file_start()->clear_crc();
		EXPECT_EQ( Refused(raw(none)+rest).Stop, EStop::NoFileStart );

		string archive;
		Appender<TestRecord> appender{ archive, 0 };
		appender.Start( fileStart(1).file_start() );
		let start = archive.size();
		appender.Add( sample(at(1000), "a") );
		auto live = fileStart( 1 );
		live.mutable_file_start()->set_generation( 0 );//an archive that would read as a live file with no checkpoint.
		EXPECT_EQ( Refused(raw(live)+archive.substr(start)).Stop, EStop::NoFileStart );
		auto moved = fileStart( 1 );
		moved.mutable_file_start()->set_ts( DayStart+1 );//an archive whose every time would move.
		EXPECT_EQ( Refused(raw(moved)+archive.substr(start)).Stop, EStop::NoFileStart );
		//A FileStart its owner's serialization wrote, generation left out, reads all the same.
		string owners;
		Appender<TestRecord> written{ owners, 0 };
		written.Add( fileStart() );
		written.Add( sample(at(1000), "a") );
		written.Seal();
		let [scanned, onDisk] = ScanFile( owners );
		EXPECT_EQ( scanned.Runs.size(), 1 );
		EXPECT_EQ( scanned.Size, owners.size() );
	}

	//The owner writes one FileStart.  An append it sealed with another is left as it is; cut short, it is dropped.
	TEST_F( ScanTests, SecondFileStart ){
		string bytes{ Bytes };
		Appender<TestRecord> again{ bytes, Chains.back() };
		again.Add( sample(at(100000), "d") );
		let secondAt = bytes.size();
		again.Add( fileStart() );
		again.Add( sample(at(101000), "e") );
		again.Seal();

		Write( bytes );
		auto scanned = scan( File );
		EXPECT_EQ( scanned.Stop, EStop::SecondStart );
		EXPECT_EQ( scanned.StopOffset, secondAt );
		EXPECT_EQ( scanned.Size, Bytes.size() );
		EXPECT_EQ( scanned.Runs.size(), Runs.size() );
		EXPECT_TRUE( scanned.Keep() );
		RefusedTruncate( scanned );
		EXPECT_EQ( OnDisk(), bytes );

		let [torn, onDisk] = ScanFile( sv{bytes}.substr(0, bytes.size()-1) );
		EXPECT_EQ( torn.Stop, EStop::SecondStart );
		EXPECT_EQ( torn.StopOffset, secondAt );
		EXPECT_FALSE( torn.SealedAfter );
		EXPECT_EQ( onDisk, Bytes );
	}

	//After a downgrade:  an append holding records this build doesn't know, and ending in a good checkpoint, is a newer
	//build's.  The scan keeps the file up to it and the truncation leaves it.  Cut short of its checkpoint, it is torn.
	TEST_F( ScanTests, NewerBuild ){
		string bytes{ Bytes };
		Appender<TestRecord> newer{ bytes, Chains.back() };
		newer.Add( sample(at(100000), "d") );
		let unknownAt = bytes.size();
		newer.Add( unknown() );
		newer.Add( unknown() );
		newer.Add( sample(at(101000), "e") );
		newer.Seal();
		Appender<TestRecord> later{ bytes, 0 };
		later.Add( sample(at(102000), "f") );
		later.Seal();

		Write( bytes );
		auto scanned = scan( File );
		EXPECT_EQ( scanned.Stop, EStop::Unknown );
		EXPECT_EQ( scanned.StopOffset, unknownAt );
		EXPECT_EQ( scanned.Size, Bytes.size() );
		EXPECT_EQ( scanned.Chain, Chains.back() );
		EXPECT_EQ( scanned.Runs.size(), Runs.size() );
		EXPECT_TRUE( scanned.Keep() );
		RefusedTruncate( scanned );
		EXPECT_EQ( OnDisk(), bytes );

		let [torn, onDisk] = ScanFile( sv{bytes}.substr(0, unknownAt+12) );//through both, and short of the checkpoint.
		EXPECT_EQ( torn.Stop, EStop::Unknown );
		EXPECT_EQ( torn.StopOffset, unknownAt );
		EXPECT_EQ( onDisk, Bytes );
	}

	//A checkpoint's three-byte mark inside a body is no checkpoint:  the search for a sealed append takes it as data too,
	//so the real checkpoint after it still proves its append.  Here the second run is damaged and the third holds the
	//false mark:  the third's checkpoint, over its bytes from the second's, is the proof.
	TEST_F( ScanTests, FalseMarkInsideABody ){
		let mark = CheckpointMark( CheckpointField );
		string bytes;
		Appender<TestRecord> first{ bytes, 0 };
		first.Start( fileStart().file_start() );
		first.Add( sample(DayStart, "p") );
		auto chain = first.Seal();
		let end0 = bytes.size();
		Appender<TestRecord> second{ bytes, chain };
		let damagedAt = bytes.size()+2;//the sample's time, past the frame's tag and length.
		second.Add( sample(at(1000), "q") );
		chain = second.Seal();
		Appender<TestRecord> third{ bytes, chain };
		third.Add( sample(at(2000), string{mark.data(), mark.size()}+"abcd") );
		third.Seal();
		let end2 = bytes.size();
		bytes[damagedAt] ^= 0x7F;
		let scanned = Damaged( bytes );
		EXPECT_EQ( scanned.Size, end0 );
		EXPECT_EQ( scanned.SealedAfter, end2 );
	}

	//The merge:  the runs' records in time order with late's, ties to the earlier run and to the file's before late's, each
	//file record placed, and the file let go at the end.  FileStart and checkpoints are left out.
	TEST_F( ScanTests, MergeOrders ){
		let [scanned, _] = ScanFile( Bytes );
		auto file = ms<OpenFile>( File );
		Merge<TestRecord> merge{ file, scanned.Runs, {sample(at(2000), "late2"), sample(at(85000), "late85")} };
		vector<std::pair<Time,string>> order;
		vector<optional<Merge<TestRecord>::Position>> where;
		TestRecord r;
		while( NextNow(merge, r) ){
			ASSERT_TRUE( r.has_sample() );
			order.emplace_back( r.sample().ts(), r.sample().text() );
			where.push_back( merge.Where() );
		}
		const vector<std::pair<Time,string>> expected{ {DayStart, "Pump1.Speed"}, {DayStart, "Pump1.Flow"}, {at(500), "late"}, {at(1000), "a"}, {at(2000), "b"}, {at(2000), "late2"}, {at(3000), "c"},
			{at(60000), "heartbeat"}, {at(70000), "lost"}, {at(80000), "server"}, {at(85000), "late85"}, {at(90000), "removed"} };
		EXPECT_EQ( order, expected );
		ASSERT_EQ( where.size(), expected.size() );
		for( uint i=0; i<where.size(); ++i ){
			let late = expected[i].second.starts_with( "late" ) && expected[i].second.size()>4;
			EXPECT_EQ( where[i].has_value(), !late ) << i;
			if( where[i] )
				EXPECT_LT( where[i]->Start, where[i]->End );
		}
		EXPECT_FALSE( merge.File() );
		EXPECT_FALSE( merge.Unreadable() );
	}

	//A run that no longer reads through, the file having changed since its scan:  the merge throws at Error.
	TEST_F( ScanTests, MergeThrowsOnAChangedRun ){
		let [scanned, _] = ScanFile( Bytes );
		Write( Bytes.substr(0, Ends[1]+5) );
		Merge<TestRecord> merge{ ms<OpenFile>(File), scanned.Runs, {} };
		TestRecord r;
		try{
			while( NextNow(merge, r) ){}
			ADD_FAILURE() << "the merge read a run the file no longer holds";
		}
		catch( const IOException& e ){
			EXPECT_EQ( e.Level(), ELogLevel::Error );
			EXPECT_TRUE( merge.Unreadable() );
		}
	}

	//A rewrite:  the FileStart one generation on, the prologue, the head the owner folds, then the runs merged with late,
	//in parts, with no checkpoint; the temp file renamed over the day's reads as an archive.
	TEST_F( ScanTests, RewriteInParts ){
		let [scanned, _] = ScanFile( Bytes );
		Proto::FileStart start;
		start.set_ts( DayStart );
		start.set_generation( scanned.Start->generation()+1 );
		Rewrite<TestRecord>::Hooks hooks{
			.IsHead=[]( const TestRecord& r ){ return r.sample().ts()==DayStart; },
			.FoldHead=[]( vector<TestRecord>& head ){ std::erase_if( head, []( let& r ){ return r.sample().text()=="Pump1.Flow"; } ); }
		};
		let temp = Dir/"group.binpb.tmp";
		Rewrite<TestRecord> rewrite{ File, temp, ms<OpenFile>(File), scanned.Runs, {sample(at(2500), "late")}, start, {sample(DayStart, "issued")}, move(hooks), 40 };
		uint parts{};
		for( auto p = rewrite.Next(); p!=EPart::Done; p = rewrite.Next() ){
			if( p==EPart::More )
				FeedNow( rewrite );
			else{
				++parts;
				BlockAwait( rewrite.Write() );
			}
		}
		EXPECT_GT( parts, 1u );
		EXPECT_FALSE( rewrite.File() );
		EXPECT_EQ( rewrite.Size(), fs::file_size(temp) );
		let records = readWhole( contents(temp) );
		vector<string> texts;
		for( let& r : records | std::views::drop(1) )
			texts.push_back( r.sample().text() );
		EXPECT_TRUE( records[0].has_file_start() );
		EXPECT_EQ( records[0].file_start().generation(), 1 );
		const vector<string> expected{ "issued", "Pump1.Speed", "late", "a", "b", "late", "c", "heartbeat", "lost", "server", "removed" };
		EXPECT_EQ( texts, expected );
		let mark = CheckpointMark( CheckpointField );
		EXPECT_EQ( (contents(temp).find(sv{mark.data(), mark.size()})), string::npos );
		Replace( temp, File );
		let archive = scan( File );
		EXPECT_EQ( archive.Start->generation(), 1 );
		EXPECT_TRUE( archive.Runs.empty() );
		EXPECT_EQ( archive.Size, archive.FileSize );
	}

	//The note of the head hears only of what the rewrite writes:  a head record too large for a frame is left out, and
	//never noted, so an owner's map kept from the note lists only what the archive holds.
	TEST_F( ScanTests, RewriteNotesWhatItWrites ){
		Proto::FileStart start;
		start.set_ts( DayStart );
		start.set_generation( 1 );
		vector<TestRecord> noted;
		Rewrite<TestRecord>::Hooks hooks{
			.IsHead=[]( const TestRecord& r ){ return r.sample().ts()==DayStart; },
			.NoteHead=[&]( const TestRecord& r ){ noted.push_back( r ); }
		};
		let temp = Dir/"group.binpb.tmp";
		vector<TestRecord> late{ sample(DayStart, "Pump1.Speed"), sample(DayStart, string(MaxFrameBytes, 'x')), sample(at(1000), "a") };
		Rewrite<TestRecord> rewrite{ File, temp, nullptr, {}, move(late), start, {}, move(hooks) };
		for( auto p = rewrite.Next(); p!=EPart::Done; p = rewrite.Next() ){
			ASSERT_EQ( p, EPart::Ready );
			BlockAwait( rewrite.Write() );
		}
		ASSERT_EQ( noted.size(), 1 );
		expectEqual( noted[0], sample(DayStart, "Pump1.Speed") );
		let records = readWhole( contents(temp) );
		ASSERT_EQ( records.size(), 3 );
		expectEqual( records[1], sample(DayStart, "Pump1.Speed") );
		expectEqual( records[2], sample(at(1000), "a") );
	}

	//An append built for a file:  with an opening, the file's first run, sealed on its own, then the run; each run that holds
	//a record with a time is reported, and the bytes scan as those runs.
	TEST_F( ScanTests, BuildsAnAppend ){
		uint noted{};
		Opening<TestRecord> opening{ fileStart().file_start(), {sample(DayStart, "issued")}, {sample(DayStart, "Pump1.Speed")} };
		auto y = BuildAppend<TestRecord>( 0, 0, move(opening), {sample(at(1000), "a"), sample(at(3000), "c")}, [&]( const TestRecord& ){ ++noted; } );
		EXPECT_EQ( noted, 4u );
		ASSERT_EQ( y.Runs.size(), 2 );
		EXPECT_EQ( y.Runs[0].Offset, 0 );
		EXPECT_EQ( y.Runs[1].Offset, y.Runs[0].End );
		EXPECT_EQ( y.Runs[1].End, y.End );
		EXPECT_EQ( y.Runs[1].First, at(1000) );
		EXPECT_EQ( y.Runs[1].Last, at(3000) );
		EXPECT_EQ( y.End, y.Bytes.size() );
		Write( y.Bytes );
		let scanned = scan( File );
		EXPECT_EQ( scanned.Size, y.End );
		EXPECT_EQ( scanned.Chain, y.Chain );
		ASSERT_EQ( scanned.Runs.size(), 2 );
		EXPECT_EQ( scanned.Runs[1].Chain, y.Runs[1].Chain );
		//A second append, with no opening, onto what the first left.
		auto more = BuildAppend<TestRecord>( y.End, y.Chain, nullopt, {sample(at(4000), "d")}, {} );
		Write( y.Bytes+more.Bytes );
		EXPECT_EQ( scan(File).Runs.size(), 3 );
	}

	//The note hears only of what an append writes, as it was given, times absolute:  a record too large for a frame is
	//left out, the opening's and the run's alike, and never noted.
	TEST_F( ScanTests, AppendNotesWhatItWrites ){
		let tooLarge = string( MaxFrameBytes, 'x' );
		vector<TestRecord> noted;
		Opening<TestRecord> opening{ fileStart().file_start(), {}, {sample(DayStart, "Pump1.Speed"), sample(DayStart, tooLarge)} };
		auto y = BuildAppend<TestRecord>( 0, 0, move(opening), {sample(at(1000), "a"), sample(at(2000), tooLarge), sample(at(3000), "c")}, [&]( const TestRecord& r ){ noted.push_back( r ); } );
		const vector<TestRecord> expected{ sample(DayStart, "Pump1.Speed"), sample(at(1000), "a"), sample(at(3000), "c") };
		ASSERT_EQ( noted.size(), expected.size() );
		let written = readWhole( y.Bytes );
		ASSERT_EQ( written.size(), expected.size()+1 );//after its FileStart.
		for( uint i=0; i<expected.size(); ++i ){
			expectEqual( noted[i], expected[i] );
			expectEqual( written[i+1], expected[i] );
		}
	}

	//The file's state:  an append handed out and committed, and the check that the file on disk is the one known.
	TEST_F( LayerTests, FileStateVerifies ){
		FileState file{ .Path=Dir/"g.binpb" };
		auto verified = Verify( file, false, false );
		EXPECT_FALSE( verified.Existed );
		EXPECT_FALSE( verified.Restart );
		save( file.Path, string(10, 'x') );
		EXPECT_THROW( Verify(file, false, false), IOException );//bytes nothing wrote:  a file no append has gone to is only ever scanned.
		file.Committed( 10, 7, {Records::Run{.Offset=0, .End=10, .Chain=0, .First=1, .Last=2}} );
		EXPECT_FALSE( file.Outstanding );
		EXPECT_TRUE( file.Named );
		EXPECT_EQ( file.Size, 10 );
		EXPECT_EQ( file.Chain, 7 );
		EXPECT_EQ( file.Runs.size(), 1 );
		EXPECT_TRUE( Verify(file, false, false).Existed );
		save( file.Path, string(14, 'x') );
		EXPECT_THROW( Verify(file, false, false), IOException );//bytes past what was written, with no append out.
		file.Hand( 10, 14 );
		EXPECT_FALSE( Verify(file, false, false).Restart );//an outstanding append's own.
		file.Hand( 10, 12 );
		EXPECT_EQ( file.Outstanding->End, 14 );//the farther of the two at one offset.
		save( file.Path, string(4, 'x') );
		EXPECT_THROW( Verify(file, false, false), IOException );//shorter than written.
		fs::remove( file.Path );
		EXPECT_TRUE( Verify(file, false, false).Restart );//purged since.
		EXPECT_TRUE( StampOf(file.Path)==nullopt );
	}

	struct FlushedTests : LayerTests{
		Ω Slot( int64_t time, uint32_t sequence, Day recover=March6 )ι->string{
			string y( Flushed::SlotSize, '\0' );
			for( uint i=0; i<8; ++i )
				y[i] = (char)( (uint64_t)time>>(8*i) );
			for( uint i=0; i<4; ++i )
				y[8+i] = (char)( sequence>>(8*i) );
			let day = (uint32_t)sys_days{ recover }.time_since_epoch().count();
			for( uint i=0; i<4; ++i )
				y[12+i] = (char)( day>>(8*i) );
			let crc = Crc::Calc32c( sv{y}.substr(0, 16) );
			for( uint i=0; i<4; ++i )
				y[16+i] = (char)( crc>>(8*i) );
			return y;
		}
		//As a flush writes it.
		Ω Write( Flushed& flushed, int64_t time, Day recover=March6 )ε->void{
			let slot = flushed.Next( time, recover );
			BlockAwait( slot.Write() );
			flushed.Wrote( slot );
		}
		static constexpr int64_t Time{ 1'700'000'000'000 };
		static constexpr int64_t Minute{ 60'000 };
		const fs::path File{ Dir/"group.flushed" };
	};

	//Two 20-byte slots, written alternately in place:  the time, a sequence, the day a start looks for live files from, in
	//days since 1970-01-01, and a CRC-32C, little-endian.
	TEST_F( FlushedTests, AlternatesSlots ){
		{
			auto flushed = Flushed::Load( File );
			EXPECT_FALSE( flushed.Time() );//no file:  never flushed.
			EXPECT_FALSE( flushed.Recover() );
			EXPECT_FALSE( fs::exists(File) );
			Write( flushed, Time );
			EXPECT_EQ( flushed.Time(), Time );
			EXPECT_EQ( contents(File), Slot(Time, 1) );
			Write( flushed, Time+Minute );
			EXPECT_EQ( contents(File), Slot(Time, 1)+Slot(Time+Minute, 2) );
			Write( flushed, Time+2*Minute, March7 );
			EXPECT_EQ( contents(File), Slot(Time+2*Minute, 3, March7)+Slot(Time+Minute, 2) );
			EXPECT_EQ( flushed.Time(), Time+2*Minute );
			EXPECT_EQ( flushed.Recover(), March7 );
		}
		auto loaded = Flushed::Load( File );
		EXPECT_EQ( loaded.Time(), Time+2*Minute );
		EXPECT_EQ( loaded.Recover(), March7 );
		constexpr Day first{ 1601y/January/1 };//before 1970, so negative.
		Write( loaded, Time+3*Minute, first );//over the older slot.
		EXPECT_EQ( contents(File), Slot(Time+2*Minute, 3, March7)+Slot(Time+3*Minute, 4, first) );
		EXPECT_EQ( Flushed::Load(File).Recover(), first );
	}

	//A write torn at any byte loses that flush's time, not the file:  the reader falls back to the other slot, and the
	//next write goes over the torn one.
	TEST_F( FlushedTests, TornSlotFallsBack ){
		let before = Slot( Time, 1 )+Slot( Time+Minute, 2 );
		let written = Slot( Time+2*Minute, 3 );
		for( uint torn=0; torn<Flushed::SlotSize; ++torn ){
			SCOPED_TRACE( Ƒ("{} bytes of the slot written", torn) );
			save( File, written.substr(0, torn)+before.substr(torn) );
			auto flushed = Flushed::Load( File );
			EXPECT_EQ( flushed.Time(), Time+Minute );
			Write( flushed, Time+3*Minute );
			EXPECT_EQ( contents(File), Slot(Time+3*Minute, 3)+Slot(Time+Minute, 2) );
		}
		save( File, written+before.substr(Flushed::SlotSize) );
		EXPECT_EQ( Flushed::Load(File).Time(), Time+2*Minute );//whole.

		for( uint torn=0; torn<Flushed::SlotSize; ++torn ){//the second slot's first write, which grows the file.
			save( File, Slot(Time, 1)+Slot(Time+Minute, 2).substr(0, torn) );
			EXPECT_EQ( Flushed::Load(File).Time(), Time ) << torn;
		}
	}

	TEST_F( FlushedTests, NoValidSlot ){
		if( !Logging::FindLogger<Logging::MemoryLog>() )
			Logging::AddLogger( mu<Logging::MemoryLog>() );
		for( let& bytes : {string{}, string(32, '\0'), string(32, 'x'), Slot(Time, 1).substr(0, 9)} ){
			save( File, bytes );
			Logging::ClearMemory();
			auto flushed = Flushed::Load( File );
			EXPECT_FALSE( flushed.Time() );
			let warned = Logging::Find( [this]( const Logging::Entry& e ){ return e.Message().contains(File.string()); } );
			ASSERT_EQ( warned.size(), 1 );
			EXPECT_EQ( warned[0].Level, ELogLevel::Warning );
			Write( flushed, Time );
			EXPECT_EQ( Flushed::Load(File).Time(), Time );
		}
	}

	//A file there that can't be read is an error, at Error, not an owner that never flushed:  a directory, and on windows a
	//file that opens but whose slots another handle has locked, which fails the read.
	TEST_F( FlushedTests, UnreadableThrows ){
		let level = [this]()->optional<ELogLevel>{
			try{
				Flushed::Load( File );
			}
			catch( const IOException& e ){
				return e.Level();
			}
			return nullopt;
		};
		fs::create_directories( File );
		EXPECT_EQ( level(), ELogLevel::Error ) << "a directory";
		fs::remove( File );
#ifdef _MSC_VER
		save( File, Slot(Time, 1) );
		HandlePtr locker{ WinHandle{::CreateFileW(File.c_str(), GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr)} };
		ASSERT_TRUE( static_cast<bool>(locker.get()) ) << ::GetLastError();
		OVERLAPPED at{};
		ASSERT_TRUE( ::LockFileEx(locker.get(), LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY, 0, 2*Flushed::SlotSize, 0, &at) ) << ::GetLastError();
		EXPECT_EQ( level(), ELogLevel::Error ) << "a read that fails";
#endif
	}

	//The sequence wraps, so the later slot is the one ahead by less than half its range.
	TEST_F( FlushedTests, SequenceWraps ){
		let last = std::numeric_limits<uint32_t>::max();
		save( File, Slot(Time+Minute, 0)+Slot(Time, last) );
		auto flushed = Flushed::Load( File );
		EXPECT_EQ( flushed.Time(), Time+Minute );
		Write( flushed, Time+2*Minute );
		EXPECT_EQ( contents(File), Slot(Time+Minute, 0)+Slot(Time+2*Minute, 1) );
		save( File, Slot(Time, last-1)+Slot(Time+Minute, last) );
		auto wrapped = Flushed::Load( File );
		Write( wrapped, Time+2*Minute );
		EXPECT_EQ( contents(File), Slot(Time+2*Minute, 0)+Slot(Time+Minute, last) );
	}

	//The day directories:  <yyyy>/<m>/<d> with no padding, walked newest first, and the days that hold a file oldest first;
	//a directory that isn't a day's is passed over.
	TEST_F( LayerTests, DaysWalk ){
		EXPECT_EQ( DayDirectory(March6), fs::path{"2026"}/"3"/"6" );
		constexpr Day eve{ 2025y/December/31 };
		for( let& day : {March6, March7, eve} )
			fs::create_directories( Dir/DayDirectory(day) );
		fs::create_directories( Dir/"2026"/"13"/"1" );
		fs::create_directories( Dir/"stray" );
		fs::create_directories( Dir/"2026"/"03"/"5" );//padded:  not a day's.
		save( Dir/DayDirectory(March6)/"g.binpb", "x" );
		save( Dir/DayDirectory(eve)/"g.binpb", "x" );
		vector<Day> walked;
		Walk( Dir, SRCE_CUR, [&]( Day day ){ walked.push_back( day ); return false; } );
		EXPECT_EQ( walked, (vector<Day>{March7, March6, eve}) );
		EXPECT_EQ( Days(Dir, "g.binpb"), (vector<Day>{eve, March6}) );
		vector<Day> first;
		Walk( Dir, SRCE_CUR, [&]( Day day ){ first.push_back( day ); return true; } );
		EXPECT_EQ( first, (vector<Day>{March7}) );
	}

	//The OS primitives:  a lock one holder has, a rename over a file and the directory syncs, and a temp file's removal.
	TEST_F( LayerTests, LockReplaceSync ){
		auto lock = PathLock::TryLock( Dir, "test.lock" );
		ASSERT_TRUE( lock );
		EXPECT_FALSE( PathLock::TryLock(Dir, "test.lock") );
		lock.reset();
		EXPECT_TRUE( PathLock::TryLock(Dir, "test.lock") );

		let day = Dir/DayDirectory( March6 );
		MakeDirectories( day );
		fs::create_directories( day );
		save( day/"g.binpb", "old" );
		save( day/"g.binpb.tmp", "new" );
		Replace( day/"g.binpb.tmp", day/"g.binpb" );
		EXPECT_EQ( contents(day/"g.binpb"), "new" );
		EXPECT_FALSE( fs::exists(day/"g.binpb.tmp") );
		SyncDirectories( Dir, DayDirectory(March6) );
		SyncDirectories( Dir );
		EXPECT_THROW( Replace(day/"missing", day/"g.binpb"), IOException );
		fs::create_directories( day/"dir.tmp" );
		EXPECT_FALSE( RemoveTemp(day/"dir.tmp") );//a directory isn't a temp file.
		EXPECT_TRUE( fs::exists(day/"dir.tmp") );
		save( day/"file.tmp", "x" );
		EXPECT_TRUE( RemoveTemp(day/"file.tmp") );
		EXPECT_FALSE( RemoveTemp(day/"file.tmp") );
	}
}