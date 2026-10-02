//Records and I/O (#202):  the HistoryRecord oneof with delta times, appends written delimited and ended by a checkpoint,
//the reader over an offset and a limit, and the first-open scan that drops a torn append whole.
#include "hosts.h"
#include <fstream>
#include <jde/fwk/io/crc.h>
#include <jde/opc/proto/opc.Common.h>
#include "../src/io/Reader.h"

#define let const auto

namespace Jde::Opc::Hist::Tests{
	using namespace std::chrono;
	using Proto::HistoryRecord;
	using google::protobuf::io::ArrayInputStream;

	namespace{
		const Ticks Day = UADateTime{ sys_days{2026y/March/7} }.UA();
		Ξ ticks( Duration d )ι->Ticks{ return duration_cast<duration<Ticks,std::ratio<1,10'000'000>>>( d ).count(); }
		Ξ at( Duration d )ι->Ticks{ return Day+ticks( d ); }

		Ω fileStart( uint32_t generation=0 )ι->HistoryRecord{
			HistoryRecord y;
			y.mutable_file_start()->set_ts( Day );
			y.mutable_file_start()->set_generation( generation );
			return y;
		}
		Ω dataValue( double v, optional<Ticks> source, optional<Ticks> server={}, NodeIndex index={} )ι->Proto::DataValue{
			Proto::DataValue y;
			y.set_node_index( index );
			if( source )
				y.set_source_ts( *source );
			if( server )
				y.set_server_ts( *server );
			y.mutable_value()->set_double_value( v );
			return y;
		}
		Ω stored( NodeIndex index, double v, optional<Ticks> source, optional<Ticks> server={} )ι->HistoryRecord{
			HistoryRecord y;
			*y.mutable_value() = dataValue( v, source, server, index );
			return y;
		}
		Ω added( NodeIndex index, sv id, Ticks ts, optional<Proto::DataValue> start={} )ι->HistoryRecord{
			HistoryRecord y;
			auto& record = *y.mutable_node_added();
			record.set_node_index( index );
			*record.mutable_node() = ProtoUtils::ToExNodeId( HostFixture::Node(id) );
			record.set_ts( ts );
			if( start )
				*record.mutable_start() = move( *start );
			return y;
		}
		Ω removed( NodeIndex index, Ticks ts )ι->HistoryRecord{
			HistoryRecord y;
			auto& record = *y.mutable_node_removed();
			record.set_node_index( index );
			record.set_ts( ts );
			record.set_identity_id( 7 );
			record.set_user_name( "admin" );
			return y;
		}

		α expectEqual( const HistoryRecord& actual, const HistoryRecord& expected )ι->void{
			EXPECT_EQ( actual.SerializeAsString(), expected.SerializeAsString() ) << actual.ShortDebugString() << "\n  expected: " << expected.ShortDebugString();
		}
		//Every record but the checkpoints, read from start to end.
		Ω read( google::protobuf::io::ZeroCopyInputStream& in, uint start, uint end, Ticks chain )ι->vector<HistoryRecord>{
			Reader reader{ in, start, end, chain };
			vector<HistoryRecord> y;
			HistoryRecord r;
			while( reader.Next(r) ){
				if( !r.has_checkpoint() )
					y.push_back( r );
			}
			EXPECT_EQ( reader.Stop(), EStop::End );
			EXPECT_EQ( reader.Offset(), end );
			return y;
		}
		Ω readAll( sv bytes )ι->vector<HistoryRecord>{
			ArrayInputStream in{ bytes.data(), (int)bytes.size() };
			return read( in, 0, bytes.size(), 0 );
		}
	}

	//On disk, each record's primary time is a delta from the previous record's, and its other times from its own primary.
	TEST( RecordTests, Deltas ){
		HistoryRecord heartbeat = stored( 1, 11, at(60s), at(60s)+ticks(5ms) );
		heartbeat.mutable_value()->set_heartbeat( at(1s) );
		HistoryRecord edit;
		auto& m = *edit.mutable_modification();
		m.set_node_index( 1 );
		m.set_target_source_ts( at(1s) );
		m.set_update_type( Proto::UPDATE_TYPE_REPLACE );
		*m.mutable_original() = dataValue( 11, at(1s), at(1s)+ticks(5ms) );
		*m.mutable_new_value() = dataValue( 11.5, at(1s), at(2h) );
		m.set_ts( at(2h) );
		const vector<HistoryRecord> records{
			fileStart(),
			added( 1, "Pump1.Speed", Day, dataValue(10, Day-ticks(1h), Day-ticks(1h)+ticks(5ms)) ),
			stored( 1, 11, at(1s), at(1s)+ticks(5ms) ),
			stored( 1, 9, at(500ms) ),//late:  before the record above it.
			stored( 1, 12, {}, at(2s) ),//no source timestamp:  filed by its server timestamp.
			heartbeat,
			edit,
			removed( 1, at(90s) )
		};
		Ticks chain{};
		vector<HistoryRecord> disk;
		for( auto r : records ){
			ToDisk( r, chain );
			disk.push_back( move(r) );
		}
		EXPECT_EQ( disk[0].file_start().ts(), Day );//the only absolute time.
		EXPECT_EQ( disk[1].node_added().ts(), 0 );//the day's start, which the FileStart set.
		EXPECT_EQ( disk[1].node_added().start().source_ts(), -ticks(1h) );
		EXPECT_EQ( disk[1].node_added().start().server_ts(), -ticks(1h)+ticks(5ms) );
		EXPECT_EQ( disk[2].value().source_ts(), ticks(1s) );
		EXPECT_EQ( disk[2].value().server_ts(), ticks(5ms) );
		EXPECT_EQ( disk[3].value().source_ts(), -ticks(500ms) );//simply negative.
		EXPECT_FALSE( disk[3].value().has_server_ts() );
		EXPECT_FALSE( disk[4].value().has_source_ts() );
		EXPECT_EQ( disk[4].value().server_ts(), ticks(1500ms) );
		EXPECT_EQ( disk[5].value().source_ts(), ticks(58s) );
		EXPECT_EQ( disk[5].value().heartbeat(), -ticks(59s) );
		EXPECT_EQ( disk[6].modification().target_source_ts(), -ticks(59s) );
		EXPECT_EQ( disk[6].modification().ts(), ticks(2h)-ticks(1s) );
		EXPECT_EQ( disk[6].modification().original().source_ts(), 0 );
		EXPECT_TRUE( disk[6].modification().original().has_source_ts() );//0, not unset.
		EXPECT_EQ( disk[6].modification().new_value().server_ts(), ticks(2h)-ticks(1s) );
		EXPECT_EQ( disk[7].node_removed().ts(), ticks(89s) );
		EXPECT_EQ( chain, at(90s) );
		EXPECT_LE( disk[2].ByteSizeLong(), 24u );//a double with both timestamps; 35 with them absolute.

		chain = 0;
		for( uint i=0; i<disk.size(); ++i ){
			ToMemory( disk[i], chain );
			expectEqual( disk[i], records[i] );
		}

		string bytes;
		Appender appender{ bytes, 0 };
		for( auto r : records )
			appender.Add( move(r) );
		EXPECT_EQ( appender.Seal(), at(90s) );
		let read = readAll( bytes );
		ASSERT_EQ( read.size(), records.size() );
		for( uint i=0; i<read.size(); ++i )
			expectEqual( read[i], records[i] );
	}

	//The run's last record is a checkpoint over the bytes before it.
	TEST( RecordTests, Checkpoint ){
		string bytes;
		Appender appender{ bytes, 0 };
		appender.Add( fileStart() );
		appender.Add( stored(1, 11, at(1s)) );
		let checkpointAt = bytes.size();
		appender.Seal();
		ArrayInputStream in{ bytes.data(), (int)bytes.size() };
		Reader reader{ in, 0, bytes.size(), 0 };
		HistoryRecord r;
		ASSERT_TRUE( reader.Next(r) && reader.Next(r) && reader.Next(r) );
		ASSERT_TRUE( r.has_checkpoint() );
		EXPECT_EQ( r.checkpoint().crc(), IO::Crc::Calc32c(sv{bytes}.substr(0, checkpointAt)) );
		EXPECT_EQ( reader.Offset(), bytes.size() );
		EXPECT_FALSE( reader.Next(r) );
		EXPECT_EQ( reader.Stop(), EStop::End );
	}

	//What a group buffers becomes the file's records:  membership with its writer and the NodeId's namespace URI, and the
	//value with both timestamps.
	TEST_F( GatewayHost, RecordsFromTheBuffer ){
		auto& group = *AddGroup();
		let speed = Join( group, "Pump1.Speed" );
		let now = Time->Now();
		ASSERT_TRUE( DataChange(group, speed, 1450.5, now-50ms) );
		group.Remove( speed, Admin );
		let buffer = group.Buffer();
		ASSERT_EQ( buffer.size(), 3 );

		string bytes;
		Appender appender{ bytes, 0 };
		appender.Add( fileStart() );
		for( let& record : buffer )
			appender.Add( ToProto(record) );
		appender.Seal();
		let read = readAll( bytes );
		ASSERT_EQ( read.size(), 4 );

		ASSERT_TRUE( read[1].has_node_added() );
		let& joined = read[1].node_added();
		EXPECT_EQ( joined.node_index(), speed );
		EXPECT_EQ( ProtoUtils::ToExNodeId(joined.node()).to_string(), Node("Pump1.Speed").to_string() );
		EXPECT_EQ( joined.node().namespace_uri(), "urn:jde:pumps" );
		EXPECT_EQ( joined.ts(), Ua(now) );
		EXPECT_EQ( joined.identity_id(), Admin.IdentityId.Value );
		EXPECT_EQ( joined.user_name(), "admin" );
		EXPECT_FALSE( joined.has_start() );

		ASSERT_TRUE( read[2].has_value() );
		EXPECT_EQ( read[2].value().node_index(), speed );
		auto value = ToUA( read[2].value() );//AsNumber is not const.
		let& enqueued = get<DataValue>( buffer[1] ).Data;
		EXPECT_TRUE( UA_equal(&value, &enqueued, &UA_TYPES[UA_TYPES_DATAVALUE]) );
		EXPECT_EQ( value.AsNumber<double>(), 1450.5 );

		ASSERT_TRUE( read[3].has_node_removed() );
		EXPECT_EQ( read[3].node_removed().node_index(), speed );
		EXPECT_EQ( read[3].node_removed().identity_id(), Admin.IdentityId.Value );
	}

	//A DataValue or DiagnosticInfo, from a node typed BaseDataType, has no file form:  stored under the status saying so.
	TEST( RecordTests, UnsupportedValue ){
		UA_DataValue dv{};
		UA_DiagnosticInfo info{};
		UA_Variant_setScalarCopy( &dv.value, &info, &UA_TYPES[UA_TYPES_DIAGNOSTICINFO] );
		dv.hasValue = true;
		dv.sourceTimestamp = at( 1s );
		dv.hasSourceTimestamp = true;
		let y = ToProto( Value{move(dv)}, 3 );
		EXPECT_FALSE( y.has_value() );
		EXPECT_EQ( y.status(), UA_STATUSCODE_BADNOTSUPPORTED );
		EXPECT_EQ( y.source_ts(), at(1s) );
		EXPECT_EQ( y.node_index(), 3 );
	}

	//A day's live file as its first flushes leave it:  the preamble, a flush, and a flush with a late record, a marker, a
	//value filed by its server timestamp and a removal.
	struct ScanTests : ::testing::Test{
		ScanTests()ι{
			Append( {fileStart(), added(1, "Pump1.Speed", Day, dataValue(10, Day-ticks(1h))), added(2, "Pump1.Flow", Day, dataValue(3, Day-ticks(2h)))} );
			HistoryRecord heartbeat = stored( 2, 4, at(60s), at(60s) );
			heartbeat.mutable_value()->set_heartbeat( at(2s) );
			Append( {stored(1, 11, at(1s), at(1s)+ticks(5ms)), stored(2, 4, at(2s), at(2s)+ticks(5ms)), stored(1, 12, at(3s)), heartbeat} );
			HistoryRecord lost;
			auto& marker = *lost.mutable_value();
			marker.set_node_index( 1 );
			marker.set_source_ts( at(70s) );
			marker.set_status( UA_STATUSCODE_BADDATALOST );
			Append( {stored(1, 9, at(500ms), at(65s)), lost, stored(2, 5, {}, at(80s)), removed(2, at(90s))} );
		}
		α SetUp()->void override{ fs::create_directories( File.parent_path() ); }
		α TearDown()->void override{
			fs::remove_all( File.parent_path() );
			std::error_code ec;
			fs::remove( File.parent_path().parent_path(), ec );//once the last test's is gone.
		}

		struct Placed{ uint Start; uint End; uint Run; };
		α Append( vector<HistoryRecord> run )ι->void{
			Appender appender{ Bytes, Chains.empty() ? 0 : Chains.back() };
			for( auto& r : run ){
				let start = Bytes.size();
				appender.Add( HistoryRecord{r} );
				Records.push_back( {start, Bytes.size(), (uint)Runs.size()} );
			}
			let start = Bytes.size();
			Chains.push_back( appender.Seal() );
			Records.push_back( {start, Bytes.size(), (uint)Runs.size()} );
			Ends.push_back( Bytes.size() );
			Runs.push_back( move(run) );
		}
		//How many runs end within size bytes, all of which the scan keeps.
		α Kept( uint size )Ι->uint{ return std::ranges::upper_bound( Ends, size )-Ends.begin(); }
		α End( uint runs )Ι->uint{ return runs ? Ends[runs-1] : 0; }
		α Expected( uint runs )Ι->vector<HistoryRecord>{
			vector<HistoryRecord> y;
			for( uint i=0; i<runs; ++i )
				y.insert( y.end(), Runs[i].begin(), Runs[i].end() );
			return y;
		}
		α Write( sv bytes )Ι->void{
			std::ofstream f{ File, std::ios::binary | std::ios::trunc };
			f.write( bytes.data(), bytes.size() );
		}
		α OnDisk()Ι->string{
			std::ifstream f{ File, std::ios::binary };
			return string{ std::istreambuf_iterator<char>{f}, {} };
		}
		//bytes as the file, scanned, and what the scan left of it.
		α ScanFile( sv bytes )Ι->tuple<Scanned,string>{
			Write( bytes );
			auto scanned = Scan( File );
			return { move(scanned), OnDisk() };
		}

		string Bytes;
		vector<vector<HistoryRecord>> Runs;
		vector<Placed> Records;//checkpoints included.
		vector<uint> Ends;//each run's.
		vector<Ticks> Chains;//where the chain stands after each run.
		const fs::path File{ fs::current_path()/"hist-tests"/::testing::UnitTest::GetInstance()->current_test_info()->name()/"group.binpb" };//beside the logs.
	};

	//A torn flush is dropped whole, wherever the file ends, and the chain resumes where the kept runs leave it.
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
				let [first, last] = std::ranges::minmax( Runs[i] | std::views::filter([]( let& r ){ return PrimaryTime(r).has_value(); })
					| std::views::transform([]( let& r ){ return *PrimaryTime(r); }) );
				EXPECT_EQ( run.First, first );
				EXPECT_EQ( run.Last, last );
			}
			if( !runs )
				continue;
			EXPECT_EQ( scanned.Chain, Chains[runs-1] );
			string resumed{ onDisk };
			Appender next{ resumed, scanned.Chain };
			next.Add( stored(1, 13, at(100s)) );
			next.Seal();
			auto expected = Expected( runs );
			expected.push_back( stored(1, 13, at(100s)) );
			let read = readAll( resumed );
			ASSERT_EQ( read.size(), expected.size() );
			for( uint i=0; i<read.size(); ++i )
				expectEqual( read[i], expected[i] );
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

		std::tie( scanned, onDisk ) = ScanFile( zeros );//a new file whose preamble never landed.
		EXPECT_EQ( scanned.Size, 0 );
		EXPECT_FALSE( scanned.Start );
		EXPECT_EQ( scanned.Stop, EStop::Empty );
		EXPECT_TRUE( onDisk.empty() );
	}

	//A body garbled with its length intact, checkpoints' included:  its run is dropped whole, whether the body no longer
	//parses or parses and only the CRC catches it.
	TEST_F( ScanTests, GarbledBody ){
		for( let& record : Records ){
			google::protobuf::io::CodedInputStream length{ reinterpret_cast<const uint8_t*>(Bytes.data()+record.Start), (int)(record.End-record.Start) };
			uint32_t size;
			ASSERT_TRUE( length.ReadVarint32(&size) );
			string unparsed{ Bytes }, parsed{ Bytes };
			for( auto i=record.Start+length.CurrentPosition(); i<record.End; ++i )
				unparsed[i] ^= 0xFF;
			parsed[record.End-1] ^= 1;//every record here ends in a double, a varint's last byte, a string or a CRC.
			for( let& [garbled, crc] : {tuple{unparsed, false}, tuple{parsed, true}} ){
				SCOPED_TRACE( Ƒ("{} record at bytes {}-{} of run {}", crc ? "parsing" : "unparsable", record.Start, record.End, record.Run) );
				let [scanned, onDisk] = ScanFile( garbled );
				EXPECT_EQ( scanned.Size, End(record.Run) );
				EXPECT_EQ( onDisk, Bytes.substr(0, End(record.Run)) );
				EXPECT_EQ( scanned.Runs.size(), record.Run );
				if( crc )
					EXPECT_EQ( scanned.Stop, EStop::Crc );
				else
					EXPECT_NE( scanned.Stop, EStop::End );
			}
		}
	}

	//Each run reads alone, from its offset to its end, through the file:  what a read of a live file's runs does.
	TEST_F( ScanTests, ReadsARun ){
		let [scanned, _] = ScanFile( Bytes );
		ASSERT_EQ( scanned.Runs.size(), Runs.size() );
		for( uint i=0; i<Runs.size(); ++i ){
			let& run = scanned.Runs[i];
			std::ifstream file{ File, std::ios::binary };
			file.seekg( run.Offset );
			google::protobuf::io::IstreamInputStream in{ &file };
			let read = Tests::read( in, run.Offset, run.End, run.Chain );
			ASSERT_EQ( read.size(), Runs[i].size() );
			for( uint j=0; j<read.size(); ++j )
				expectEqual( read[j], Runs[i][j] );
		}
	}

	//An archive is replaced whole, never torn, and has no checkpoints:  the scan leaves it as it is.
	TEST_F( ScanTests, Archive ){
		string archive;
		Appender appender{ archive, 0 };
		appender.Add( fileStart(1) );
		appender.Add( added(1, "Pump1.Speed", Day, dataValue(10, Day-ticks(1h))) );
		appender.Add( stored(1, 11, at(1s)) );
		let [scanned, onDisk] = ScanFile( archive );
		ASSERT_TRUE( scanned.Start );
		EXPECT_EQ( scanned.Start->generation(), 1 );
		EXPECT_EQ( scanned.Size, archive.size() );
		EXPECT_EQ( onDisk, archive );
		EXPECT_TRUE( scanned.Runs.empty() );
	}

	TEST_F( ScanTests, NoFileStart ){
		string bytes;
		Appender appender{ bytes, Day };
		appender.Add( stored(1, 11, at(1s)) );
		appender.Seal();
		let [scanned, onDisk] = ScanFile( bytes );
		EXPECT_EQ( scanned.Size, 0 );
		EXPECT_EQ( scanned.Stop, EStop::NoFileStart );
		EXPECT_TRUE( onDisk.empty() );
	}
}