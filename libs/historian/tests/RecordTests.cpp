//Records (#202):  the HistoryRecord oneof with delta times over the framework's record layer - whose framing, first-open
//scan and truncation Jde.Fwk.Tests covers (io/RecordsTests.cpp) - and what a group buffers becomes on disk.
#include "hosts.h"
#include "files.h"
#include <fstream>
#include <jde/fwk/exceptions/IOException.h>
#include <jde/fwk/io/crc.h>
#include <jde/fwk/log/MemoryLog.h>
#include <jde/opc/proto/opc.Common.h>
#include "../src/io/Records.h"

#define let const auto

namespace Jde::Opc::Hist::Tests{
	using namespace std::chrono;
	using Proto::HistoryRecord;

	namespace{
		const Ticks Day = UADateTime{ sys_days{2026y/March/7} }.UA();
		Ξ at( Duration d )ι->Ticks{ return Day+ticks( d ); }

		Ω fileStart( uint32_t generation=0 )ι->HistoryRecord{
			HistoryRecord y;
			y.mutable_file_start()->set_ts( Day );
			y.mutable_file_start()->set_generation( generation );
			y.mutable_file_start()->set_crc( IO::Records::StartCrc(y.file_start()) );
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
		Ω readAll( sv bytes )ι->vector<HistoryRecord>{ return read( bytes, 0, bytes.size(), 0 ); }
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
		EXPECT_EQ( ProtoUtils::ToExNodeId(joined.node()).ToString(), Node("Pump1.Speed").ToString() );
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

	//A value whose record no frame holds is stored without it, as BadEncodingLimitsExceeded:  one byte past MaxMemberBytes.
	//One at it is kept, and its record fits a frame even with times that grow the most as deltas.
	TEST( RecordTests, TooLargeValue ){
		let text = []( uint size, Ticks source ){
			UA_DataValue dv{};
			const string s( size, 'x' );
			const UA_String us{ s.size(), (UA_Byte*)s.data() };
			UA_Variant_setScalarCopy( &dv.value, &us, &UA_TYPES[UA_TYPES_STRING] );
			dv.hasValue = true;
			dv.sourceTimestamp = source;
			dv.hasSourceTimestamp = true;
			return Value{ move(dv) };
		};
		let probe = MaxMemberBytes-1024;
		let overhead = ToProto( text(probe, 1), 3 ).ByteSizeLong()-probe;
		let fits = ToProto( text(MaxMemberBytes-overhead, 1), 3 );
		ASSERT_EQ( fits.ByteSizeLong(), MaxMemberBytes );
		EXPECT_TRUE( fits.has_value() );
		HistoryRecord r;
		*r.mutable_value() = fits;
		Ticks last{ std::numeric_limits<Ticks>::max() };//a 1-byte source time becomes a 10-byte delta.
		ToDisk( r, last );
		EXPECT_LE( r.ByteSizeLong(), IO::Records::MaxFrameBytes );

		let refused = ToProto( text(MaxMemberBytes-overhead+1, at(1s)), 3 );
		EXPECT_FALSE( refused.has_value() );
		EXPECT_EQ( refused.status(), UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED );
		EXPECT_EQ( refused.source_ts(), at(1s) );
		EXPECT_EQ( refused.node_index(), 3 );
	}

	//A value ToValue can't encode, a decoded ExtensionObject with nothing decoded, is stored without it either way, but
	//only the flush's conversion warns:  the subscribers' copy is taken under the group's lock, where Enqueue never logs.
	TEST( RecordTests, OnlyTheFlushWarns ){
		if( !Logging::FindLogger<Logging::MemoryLog>() )
			Logging::AddLogger( mu<Logging::MemoryLog>() );
		let warnings = []{
			return Logging::Find( []( const Logging::Entry& e ){ return e.Level==ELogLevel::Warning && e.Message().contains("node_index 3's 'ExtensionObject' value is stored without it"); } ).size();
		};
		auto value = []{
			UA_DataValue dv{};
			auto eo = UA_ExtensionObject_new();
			eo->encoding = UA_EXTENSIONOBJECT_DECODED_NODELETE;
			UA_Variant_setScalar( &dv.value, eo, &UA_TYPES[UA_TYPES_EXTENSIONOBJECT] );
			dv.hasValue = true;
			dv.sourceTimestamp = at( 1s );
			dv.hasSourceTimestamp = true;
			return Value{ move(dv) };
		};
		Logging::ClearMemory();
		let copy = ToProto( DataValue{3, value()} );
		EXPECT_EQ( copy.status(), UA_STATUSCODE_BADENCODINGERROR );
		EXPECT_FALSE( copy.has_value() );
		EXPECT_EQ( warnings(), 0u );
		let flushed = ToProto( Record{DataValue{3, value()}} );
		EXPECT_EQ( flushed.value().status(), UA_STATUSCODE_BADENCODINGERROR );
		EXPECT_EQ( warnings(), 1u ) << "the flush's";
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
}