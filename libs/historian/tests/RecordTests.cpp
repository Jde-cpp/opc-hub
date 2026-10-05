//Records and I/O (#202):  the HistoryRecord oneof with delta times, appends written delimited and ended by a checkpoint,
//the reader over an offset and a limit, the first-open scan, and the truncation that drops a torn append whole.
#include "hosts.h"
#include "files.h"
#include <fstream>
#include <jde/fwk/exceptions/IOException.h>
#include <jde/fwk/io/crc.h>
#include <jde/fwk/log/MemoryLog.h>
#include <jde/opc/proto/opc.Common.h>
#include "../src/io/Reader.h"
DISABLE_WARNINGS
#include <google/protobuf/util/delimited_message_util.h>
ENABLE_WARNINGS

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
			y.mutable_file_start()->set_crc( StartCrc(y.file_start()) );
			return y;
		}
		//r as a file holds it, written as it is:  Appender::Add would set a FileStart's crc.
		Ω raw( const HistoryRecord& r )ι->string{
			string y;
			google::protobuf::io::StringOutputStream out{ &y };
			(void)google::protobuf::util::SerializeDelimitedToZeroCopyStream( r, &out );
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
		//What a later build's record reads as:  a whole record with no member this build knows.
		Ω unknown()ι->HistoryRecord{
			HistoryRecord y;
			y.GetReflection()->MutableUnknownFields( &y )->AddVarint( 1000, 1 );
			return y;
		}

		α expectEqual( const HistoryRecord& actual, const HistoryRecord& expected )ι->void{
			EXPECT_EQ( actual.SerializeAsString(), expected.SerializeAsString() ) << actual.ShortDebugString() << "\n  expected: " << expected.ShortDebugString();
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
		EXPECT_EQ( joined.ts(), ticks(now) );
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

	//node_index and identity_id are 32 bits in a record, where NodeIndex and UserPK are 64 on Linux:  a wider one is
	//refused before it is buffered, and System is stored as UINT32_MAX, its value on Windows.
	TEST_F( GatewayHost, ThirtyTwoBits ){
		if( std::numeric_limits<NodeIndex>::max()==std::numeric_limits<uint32_t>::max() )
			GTEST_SKIP() << "NodeIndex and UserPK are 32 bits here.";
		auto& group = *AddGroup();
		let speed = Join( group, "Pump1.Speed" );
		group.Remove( speed, Writer{{UserPK::System}, "system"} );
		EXPECT_EQ( ToProto(group.Buffer().back()).node_removed().identity_id(), std::numeric_limits<uint32_t>::max() );

		let wide = (NodeIndex)std::numeric_limits<uint32_t>::max()+6;
		EXPECT_THROW( group.Add({Node("Pump1.Flow"), {}, wide}, Admin), Exception );
		EXPECT_FALSE( group.Find(Node("Pump1.Flow")) );
		EXPECT_THROW( (Writer{{wide}, "wide"}), Exception );
		EXPECT_THROW( (Writer{{std::numeric_limits<uint32_t>::max()}, "max"}), Exception );//it would read back as System.
	}

	//No torn preamble claims more than the widest FileStart.
	TEST( RecordTests, MaxFileStartBody ){
		HistoryRecord widest;
		auto& start = *widest.mutable_file_start();
		start.set_ts( std::numeric_limits<int64_t>::min() );
		start.set_generation( std::numeric_limits<uint32_t>::max() );
		start.set_next_node_index( std::numeric_limits<uint32_t>::max() );
		start.set_crc( std::numeric_limits<uint32_t>::max() );
		EXPECT_EQ( widest.ByteSizeLong(), MaxFileStartBody );
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

	//A node typed BaseDataType sending DiagnosticInfo values:  each is stored without it, and only the node's first is
	//flagged for the flush to warn of, so a steady stream of them doesn't flood the log.
	TEST_F( GatewayHost, UnsupportedOncePerNode ){
		auto& group = *AddGroup();
		let status = Join( group, "Pump1.Status" );
		let speed = Join( group, "Pump1.Speed" );
		let diagnostic = []{
			UA_DataValue dv{};
			UA_DiagnosticInfo info{};
			UA_Variant_setScalarCopy( &dv.value, &info, &UA_TYPES[UA_TYPES_DIAGNOSTICINFO] );
			dv.hasValue = true;
			return Value{ move(dv) };
		};
		ASSERT_TRUE( group.Enqueue(status, diagnostic()) );
		ASSERT_TRUE( group.Enqueue(status, Reading(1)) );
		ASSERT_TRUE( group.Enqueue(status, diagnostic()) );
		ASSERT_TRUE( group.Enqueue(speed, diagnostic()) );
		let values = Records<DataValue>();
		ASSERT_EQ( values.size(), 4 );
		EXPECT_TRUE( values[0].Unsupported );
		EXPECT_FALSE( values[1].Unsupported );
		EXPECT_FALSE( values[2].Unsupported );
		EXPECT_TRUE( values[3].Unsupported );
		for( let i : {0, 2, 3} ){
			let stored = ToProto( Record{values[i]} ).value();
			EXPECT_FALSE( stored.has_value() );
			EXPECT_EQ( stored.status(), UA_STATUSCODE_BADNOTSUPPORTED );
		}
	}

	//A String that isn't UTF-8, Latin-1 from an older device say, which protobuf would write and then refuse to read:
	//stored without it, as BadEncodingError.  The node's first such value is flagged, apart from its first with no file form.
	TEST_F( GatewayHost, NotUtf8OncePerNode ){
		auto& group = *AddGroup();
		let label = Join( group, "Pump1.Label" );
		let speed = Join( group, "Pump1.Speed" );
		UA_DataValue diagnostic{};
		const UA_DiagnosticInfo info{};
		UA_Variant_setScalarCopy( &diagnostic.value, &info, &UA_TYPES[UA_TYPES_DIAGNOSTICINFO] );
		diagnostic.hasValue = true;
		ASSERT_TRUE( group.Enqueue(label, Text("25\xB0" "C")) );
		ASSERT_TRUE( group.Enqueue(label, Text("25\xC2\xB0" "C")) );
		ASSERT_TRUE( group.Enqueue(label, Text("26\xB0" "C")) );
		ASSERT_TRUE( group.Enqueue(speed, Text("\xFF")) );
		ASSERT_TRUE( group.Enqueue(label, Value{move(diagnostic)}) );
		let values = Records<DataValue>();
		ASSERT_EQ( values.size(), 5 );
		EXPECT_TRUE( values[0].Unsupported );
		EXPECT_FALSE( values[1].Unsupported );
		EXPECT_FALSE( values[2].Unsupported );
		EXPECT_TRUE( values[3].Unsupported );
		EXPECT_TRUE( values[4].Unsupported );
		for( let i : {0, 2, 3} ){
			let stored = ToProto( Record{values[i]} ).value();
			EXPECT_FALSE( stored.has_value() );
			EXPECT_EQ( stored.status(), UA_STATUSCODE_BADENCODINGERROR );
			Proto::DataValue parsed;
			EXPECT_TRUE( parsed.ParseFromString(stored.SerializeAsString()) );
		}
		EXPECT_EQ( ToProto(Record{values[1]}).value().value().string_value(), "25\xC2\xB0" "C" );
		EXPECT_EQ( ToProto(Record{values[4]}).value().status(), UA_STATUSCODE_BADNOTSUPPORTED );
	}

	//What a file holds as a `string` - a node's string identifier, its namespace URI, a writer's name - must be UTF-8:
	//one that isn't is refused before anything is buffered.
	TEST_F( GatewayHost, NotUtf8Refused ){
		auto& group = *AddGroup();
		EXPECT_THROW( Join(group, "Pump1.\xB0" "C"), Exception );
		EXPECT_THROW( group.Add({Node("Pump1.Speed", "urn:jde:\xE9"), {}, 200}, Admin), Exception );
		EXPECT_TRUE( Records<NodeAdded>().empty() );
		EXPECT_THROW( (Writer{{{7}}, "ad\xE9" "min"}), Exception );
	}

	//A DataValue comes back as it was collected, except that a field at its default, whose flag would cost bytes to store,
	//comes back with its flag clear.
	TEST( RecordTests, DataValueRoundTrip ){
		let roundTrip = []( const UA_DataValue& v ){
			Proto::DataValue stored;
			EXPECT_TRUE( stored.ParseFromString(ToProto(v, 3).SerializeAsString()) );
			return ToUA( stored );
		};
		UA_DataValue full{};
		const double reading{ 1450.5 };
		UA_Variant_setScalarCopy( &full.value, &reading, &UA_TYPES[UA_TYPES_DOUBLE] );
		full.hasValue = true;
		full.status = UA_STATUSCODE_UNCERTAININITIALVALUE;
		full.hasStatus = true;
		full.sourceTimestamp = at( 1s );
		full.hasSourceTimestamp = true;
		full.sourcePicoseconds = 1234;
		full.hasSourcePicoseconds = true;
		full.serverTimestamp = at( 2s );
		full.hasServerTimestamp = true;
		full.serverPicoseconds = 9999;
		full.hasServerPicoseconds = true;
		const Value collected{ move(full) };
		let read = roundTrip( collected );
		EXPECT_TRUE( UA_equal(&read, &collected, &UA_TYPES[UA_TYPES_DATAVALUE]) );

		UA_DataValue defaults{};//an explicit Good status, a null value and zero picoseconds.
		defaults.hasStatus = defaults.hasValue = defaults.hasSourcePicoseconds = defaults.hasServerPicoseconds = true;
		const UA_DataValue none{};
		EXPECT_EQ( ToProto(defaults, 3).ByteSizeLong(), ToProto(none, 3).ByteSizeLong() );
		let cleared = roundTrip( defaults );
		EXPECT_TRUE( UA_equal(&cleared, &none, &UA_TYPES[UA_TYPES_DATAVALUE]) );
	}

	//A day's live file as its first flushes leave it:  the preamble, a flush, and a flush with a late record, a marker, a
	//value filed by its server timestamp and a removal.
	struct ScanTests : ::testing::Test{
		ScanTests()ε{
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
		α Append( vector<HistoryRecord> run )ε->void{
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
		α Write( sv bytes )Ι->void{ save( File, bytes ); }
		α OnDisk()Ι->string{ return contents( File ); }
		//bytes as the file, scanned, which leaves it as it is, then truncated for an append, and what that left of it.
		α ScanFile( sv bytes )Ε->tuple<Scanned,string>{
			Write( bytes );
			auto scanned = Scan( File );
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
			catch( const IO::IOException& e ){
				EXPECT_EQ( e.Level(), ELogLevel::Error ) << e.what();
			}
		}
		//bytes as a file that isn't a live one's:  the scan keeps nothing of it, and the truncation refuses it.
		α Refused( sv bytes )Ε->Scanned{
			Write( bytes );
			auto scanned = Scan( File );
			EXPECT_EQ( scanned.Size, 0 );
			EXPECT_EQ( scanned.StopOffset, 0 );
			EXPECT_TRUE( scanned.Keep() );
			RefusedTruncate( scanned );
			EXPECT_EQ( scanned.FileSize, bytes.size() );
			EXPECT_EQ( OnDisk(), bytes );
			return scanned;
		}
		//bytes as a live file damaged before a run a historian sealed:  the scan keeps the runs before the damage, and the
		//truncation refuses it.
		α Damaged( sv bytes )Ε->Scanned{
			Write( bytes );
			auto scanned = Scan( File );
			EXPECT_TRUE( scanned.SealedAfter );
			EXPECT_TRUE( scanned.Keep() );
			RefusedTruncate( scanned );
			EXPECT_EQ( scanned.FileSize, bytes.size() );
			EXPECT_EQ( OnDisk(), bytes );
			return scanned;
		}

		string Bytes;
		vector<vector<HistoryRecord>> Runs;
		vector<Placed> Records;//checkpoints included.
		vector<uint> Ends;//each run's.
		vector<Ticks> Chains;//where the chain stands after each run.
		const fs::path File{ fs::current_path()/"hist-tests"/::testing::UnitTest::GetInstance()->current_test_info()->name()/"group.binpb" };//beside the logs.
	};

	//A torn flush is dropped whole, wherever the file ends, and the chain resumes where the kept runs leave it.
	//The scan hands each record of every append it keeps to sealed, as a read through what it kept would see it, and none
	//of a torn one:  so a first open maps and restores in the one read.
	TEST_F( ScanTests, SealedTakesWhatIsKept ){
		for( uint size=0; size<=Bytes.size(); ++size ){
			SCOPED_TRACE( Ƒ("file cut at byte {} of {}", size, Bytes.size()) );
			Write( sv{Bytes}.substr(0, size) );
			vector<string> sealed;
			(void)Scan( File, [&]( HistoryRecord& r ){ sealed.push_back( r.SerializeAsString() ); } );
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

	//Zeros where a run's bytes were, which no torn flush leaves before a later run:  damage, so the file is left as it is,
	//with the scan keeping the runs before it.  At the file's start too, where a zero byte alone would be a torn preamble.
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
	//catches it.  The scan keeps the runs before it.  Before a run a historian sealed, it is damage and the file is left
	//as it is; in the last run it is a torn flush, and its run is dropped whole.  A sealed run is found by its checkpoint's
	//bytes and the ones before them, so the run after an unparsable checkpoint can't be proved.
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

	//A length garbled to claim the rest of the file, whose bytes are all there:  the reader stops at it from the body's
	//first bytes, without buffering what it claims.  The runs after it are sealed, so the file is left as it is.
	TEST_F( ScanTests, GarbledLength ){
		let start = Ends[0];//the second run's first record.
		google::protobuf::io::CodedInputStream length{ reinterpret_cast<const uint8_t*>(Bytes.data()+start), 5 };
		uint32_t size;
		ASSERT_TRUE( length.ReadVarint32(&size) );
		let rest = Bytes.substr( start+length.CurrentPosition() );
		let claimed = (uint32_t)rest.size();
		uint8_t prefix[5];
		let prefixEnd = google::protobuf::io::CodedOutputStream::WriteVarint32ToArray( claimed, prefix );
		let garbled = Bytes.substr( 0, start )+string{ reinterpret_cast<char*>(prefix), (uint)(prefixEnd-prefix) }+rest;

		for( let keepBytes : {false, true} ){
			ArrayInputStream in{ garbled.data(), (int)garbled.size() };
			Reader reader{ in, 0, garbled.size(), 0, keepBytes };
			HistoryRecord r;
			while( reader.Next(r) ){}
			EXPECT_EQ( reader.Stop(), EStop::Body );
			EXPECT_EQ( reader.Offset(), start );
			EXPECT_LT( reader.Bytes().size(), claimed );
		}

		let scanned = Damaged( garbled );
		EXPECT_EQ( scanned.Stop, EStop::Body );
		EXPECT_EQ( scanned.StopOffset, start );
		EXPECT_EQ( scanned.Size, start );
	}

	//A read parses each body straight from the stream, copying none, except one whose first bytes straddle two of the
	//stream's blocks.  Every block size reads the same records.
	TEST_F( ScanTests, ParsesInPlace ){
		let expected = Expected( Runs.size() );
		for( let block : {1, 2, 3, 5, 8, 13, 64, (int)Bytes.size()} ){
			SCOPED_TRACE( Ƒ("{} byte blocks", block) );
			ArrayInputStream in{ Bytes.data(), (int)Bytes.size(), block };
			let records = read( in, 0, Bytes.size(), 0 );
			ASSERT_EQ( records.size(), expected.size() );
			for( uint i=0; i<records.size(); ++i )
				expectEqual( records[i], expected[i] );
		}
		ArrayInputStream whole{ Bytes.data(), (int)Bytes.size() };
		Reader reader{ whole, 0, Bytes.size(), 0 };
		HistoryRecord r;
		while( reader.Next(r) )
			EXPECT_TRUE( reader.Bytes().empty() );
		EXPECT_EQ( reader.Stop(), EStop::End );
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

	//A scan of a file that has grown or shrunk since:  the truncation refuses to cut by it, which would drop bytes it never
	//read, or lengthen the file.
	TEST_F( ScanTests, StaleScan ){
		let torn = Bytes.substr( 0, Ends[1]+5 );
		Write( torn );
		auto scanned = Scan( File );
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
			(void)Scan( File );
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

	//A whole first record that isn't a FileStart is a damaged archive or another program's file, never a torn append:  the
	//truncation leaves it as it is.  So is a first length the historian never writes, or one no FileStart fills.
	TEST_F( ScanTests, NoFileStart ){
		string bytes;
		Appender appender{ bytes, Day };
		appender.Add( stored(1, 11, at(1s)) );
		appender.Seal();
		EXPECT_EQ( Refused(bytes).Stop, EStop::NoFileStart );

		EXPECT_EQ( Refused("\x03\xC0\x3E\x01"sv).Stop, EStop::Unknown );//a record holding only field 1000, which no build knows.
		EXPECT_EQ( Refused("\xFF\xD8\xFF\xE0\x00\x10JFIF"sv).Stop, EStop::BadLength );//a JPEG, whose first bytes read as a padded varint.
		EXPECT_EQ( Refused("\x80\x00"sv).Stop, EStop::BadLength );//0, padded.
		EXPECT_EQ( Refused("\x80"sv).Stop, EStop::BadLength );//a first length past 127, cut short.
		EXPECT_EQ( Refused("\x40 claims 64 bytes"sv).Stop, EStop::BadLength );//cut short, but longer than any FileStart.

		string archive;
		Appender archived{ archive, 0 };
		archived.Add( fileStart(1) );
		let start = archive.size();
		archived.Add( stored(1, 11, at(1s)) );
		for( uint i=1; i<start; ++i )
			archive[i] ^= 0xFF;
		EXPECT_EQ( Refused(archive).Stop, EStop::Body );
	}

	//A read's scan of a file with a torn append serves what is good and leaves the rest for the first append to drop.
	TEST_F( ScanTests, OnlyTruncateChanges ){
		let torn = Bytes.substr( 0, Ends[1]+5 );
		Write( torn );
		auto scanned = Scan( File );
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
		Appender appender{ archive, 0 };
		appender.Add( fileStart(1) );
		let start = archive.size();
		appender.Add( stored(1, 11, at(1s)) );
		auto live = fileStart( 1 );
		live.mutable_file_start()->set_generation( 0 );//an archive that would read as a live file with no checkpoint.
		EXPECT_EQ( Refused(raw(live)+archive.substr(start)).Stop, EStop::NoFileStart );
		auto moved = fileStart( 1 );
		moved.mutable_file_start()->set_ts( Day+1 );//an archive whose every time would move.
		EXPECT_EQ( Refused(raw(moved)+archive.substr(start)).Stop, EStop::NoFileStart );
	}

	//The historian writes one FileStart.  An append it sealed with another is left as it is; cut short, it is dropped.
	TEST_F( ScanTests, SecondFileStart ){
		string bytes{ Bytes };
		Appender again{ bytes, Chains.back() };
		again.Add( stored(1, 13, at(100s)) );
		let secondAt = bytes.size();
		again.Add( fileStart() );
		again.Add( stored(1, 14, at(101s)) );
		again.Seal();

		Write( bytes );
		auto scanned = Scan( File );
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
		Appender newer{ bytes, Chains.back() };
		newer.Add( stored(1, 13, at(100s)) );
		let unknownAt = bytes.size();
		newer.Add( unknown() );
		newer.Add( unknown() );
		newer.Add( stored(1, 14, at(101s)) );
		newer.Seal();
		Appender later{ bytes, 0 };
		later.Add( stored(1, 15, at(102s)) );
		later.Seal();

		Write( bytes );
		auto scanned = Scan( File );
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
}