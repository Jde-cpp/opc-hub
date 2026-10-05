//What a node's thresholds store of what it delivers:  the deviation band, MinTimeInterval's pending value, the
//heartbeat, and the comparison that judges a node's first value after a break.
#include "dayFiles.h"

#define let const auto

namespace Jde::Opc::Hist::Tests{
	namespace{
		constexpr auto PercentOfValue = UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFVALUE;
		Ω isMarker( const DataValue& v )ι->bool{ return !v.Data.hasValue && v.Data.status==UA_STATUSCODE_BADDATALOST; }
		Ω integer( int64_t v, TimePoint source )ι->Value{
			UA_DataValue dv{};
			UA_Variant_setScalarCopy( &dv.value, &v, &UA_TYPES[UA_TYPES_INT64] );
			dv.hasValue = true;
			dv.sourceTimestamp = ticks( source );
			dv.hasSourceTimestamp = true;
			return Value{ move(dv) };
		}
		//What a group buffered of a node:  the values it stored, its markers and its heartbeats.
		struct Buffered{
			Buffered( const Group& group, NodeIndex index )ι{
				for( auto& r : group.Buffer() ){
					auto p = std::get_if<DataValue>( &r );
					if( !p || p->Index!=index )
						continue;
					if( p->Heartbeat )
						Heartbeats.push_back( move(*p) );
					else if( isMarker(*p) )
						Markers.push_back( move(*p) );
					else{
						Values.push_back( p->Data.Get<double>(0) );
						Sources.push_back( p->Data.sourceTimestamp );
					}
				}
			}
			vector<double> Values;
			vector<UA_DateTime> Sources;//each value's SourceTimestamp.
			vector<DataValue> Markers;
			vector<DataValue> Heartbeats;
		};
	}

	//One gateway group, whose collector publishes every 500 ms.
	struct Compression : GatewayHost{
		Compression()ε{ Pump = AddGroup(); }
		//A change the server sampled lag ago, arriving now.
		α Change( NodeIndex index, double v, Duration lag={} )ι->bool{ return DataChange( *Pump, index, v, Time->Now()-lag ); }
		α Of( NodeIndex index )Ι->Buffered{ return Buffered{ *Pump, index }; }
		sp<Group> Pump;
	};

	TEST_F( Compression, AbsoluteBand ){
		let speed = Join( *Pump, "Pump1.Speed", {.ExceptionDeviation=1} );
		for( let v : {10., 10.5, 10.99, 11., 10.5, 9.9, 9.9} )//a change of the deviation itself has left the band.
			Change( speed, v );
		EXPECT_EQ( Of(speed).Values, (vector<double>{10, 11, 9.9}) );

		let every = Join( *Pump, "Pump1.Flow" );
		let zero = Join( *Pump, "Pump1.Level", {.ExceptionDeviation=0} );
		for( let index : {every, zero} ){
			for( let v : {5., 5., 5.1} )
				Change( index, v );
			EXPECT_EQ( Of(index).Values, (vector<double>{5, 5, 5.1}) );
		}
	}

	//A percentage of the last stored value's magnitude, so the band moves with it, and at zero every change is stored.
	TEST_F( Compression, PercentOfValueBand ){
		let speed = Join( *Pump, "Pump1.Speed", {.ExceptionDeviation=10, .DeviationFormat=PercentOfValue} );
		for( let v : {100., 109., 110., 120., 121., -121., -110., -108.} )
			Change( speed, v );
		EXPECT_EQ( Of(speed).Values, (vector<double>{100, 110, 121, -121, -108}) );

		let level = Join( *Pump, "Tank1.Level", {.ExceptionDeviation=10, .DeviationFormat=PercentOfValue} );
		for( let v : {0., 0.001, 0.00105} )
			Change( level, v );
		EXPECT_EQ( Of(level).Values, (vector<double>{0, 0.001}) );
	}

	//A percentage of the node's InstrumentRange or EURange, whichever its format names.
	TEST_F( Compression, PercentOfRangeBand ){
		for( let format : {UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFRANGE, UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFEURANGE} ){
			let speed = Join( *Pump, Ƒ("Pump{}.Speed", underlying(format)), {.ExceptionDeviation=1, .DeviationFormat=format, .Range=Range{-100, 100}} );
			for( let v : {50., 51.9, 52., 53.9, 50.} )
				Change( speed, v );
			EXPECT_EQ( Of(speed).Values, (vector<double>{50, 52, 50}) );
		}
	}

	TEST_F( Compression, StatusChangeIsStored ){
		let speed = Join( *Pump, "Pump1.Speed", {.ExceptionDeviation=100} );
		let enqueue = [&]( double v, StatusCode status ){
			auto reading = Reading( v, Time->Now() );
			reading.status = status;
			reading.hasStatus = status!=UA_STATUSCODE_GOOD;
			Pump->Enqueue( speed, reading );
		};
		enqueue( 10, UA_STATUSCODE_GOOD );
		enqueue( 11, UA_STATUSCODE_GOOD );
		enqueue( 12, UA_STATUSCODE_UNCERTAINLASTUSABLEVALUE );
		enqueue( 13, UA_STATUSCODE_UNCERTAINLASTUSABLEVALUE );
		enqueue( 14, UA_STATUSCODE_GOOD );
		EXPECT_EQ( Of(speed).Values, (vector<double>{10, 12, 14}) );
	}

	//A state change is never noise, and an integer's band is exact however large the value.
	TEST_F( Compression, BandIsForNumbers ){
		let label = Join( *Pump, "Pump1.Label", {.ExceptionDeviation=100} );
		for( let text : {"on", "off", "off"} )
			Pump->Enqueue( label, Text(text, Time->Now()) );
		let running = Join( *Pump, "Pump1.Running", {.ExceptionDeviation=100} );
		for( let on : {true, false, true} ){
			UA_DataValue dv{};
			UA_Variant_setScalarCopy( &dv.value, &on, &UA_TYPES[UA_TYPES_BOOLEAN] );
			dv.hasValue = true;
			Pump->Enqueue( running, Value{move(dv)} );
		}
		let count = Join( *Pump, "Pump1.Count", {.ExceptionDeviation=2} );
		constexpr int64_t large{ (1LL<<62)+1 };//past what a double tells apart.
		for( let v : {large, large+1, large+2, large+3, large} )
			Pump->Enqueue( count, integer(v, Time->Now()) );
		let nan = Join( *Pump, "Pump1.Temp", {.ExceptionDeviation=100} );
		for( let v : {1., std::numeric_limits<double>::quiet_NaN(), 2.} )
			Change( nan, v );

		uint labels{}, states{};
		vector<int64_t> counts;
		for( let& v : Records<DataValue>() ){
			labels += v.Index==label;
			states += v.Index==running;
			if( v.Index==count )
				counts.push_back( v.Data.Get<UA_Int64>(0) );
		}
		EXPECT_EQ( labels, 3 );
		EXPECT_EQ( states, 3 );
		EXPECT_EQ( counts, (vector<int64_t>{large, large+2, large}) );
		EXPECT_EQ( Of(nan).Values.size(), 3 );//a NaN is at no distance from anything.
	}

	//A change inside MinTimeInterval of the last stored value is held, each later one replacing it, and stored with its
	//own timestamps when the interval ends.
	TEST_F( Compression, MinTimeIntervalHoldsWhereItSettles ){
		let speed = Join( *Pump, "Pump1.Speed", {.MinTimeInterval=1s} );
		Change( speed, 1 );
		Time->Advance( 200ms );
		Change( speed, 2 );
		Time->Advance( 200ms );
		let settled = Time->Now();
		Change( speed, 3 );
		EXPECT_EQ( Of(speed).Values, (vector<double>{1}) );
		Time->Advance( 599ms );
		EXPECT_EQ( Of(speed).Values.size(), 1 );
		Time->Advance( 1ms );//a second after the stored value.
		EXPECT_EQ( Of(speed).Values, (vector<double>{1, 3}) );
		EXPECT_EQ( Of(speed).Sources[1], ticks(settled) );

		Time->Advance( 500ms );//1.1 s after the value now stored.
		Change( speed, 4 );
		EXPECT_EQ( Of(speed).Values, (vector<double>{1, 3, 4}) );
	}

	//The pending value is whatever came last, passing or not, so a value that went back inside the band is not stored as
	//the spike it left.
	TEST_F( Compression, PendingMustStillPass ){
		let speed = Join( *Pump, "Pump1.Speed", {.ExceptionDeviation=1, .MinTimeInterval=1s} );
		Change( speed, 10 );
		Time->Advance( 200ms );
		Change( speed, 12 );
		Time->Advance( 200ms );
		Change( speed, 10.2 );
		Time->Advance( 1s );
		EXPECT_EQ( Of(speed).Values, (vector<double>{10}) );
		Change( speed, 12 );
		EXPECT_EQ( Of(speed).Values, (vector<double>{10, 12}) );
	}

	//The interval is the source's:  samples a publish delivers together are as far apart as their SourceTimestamps say.
	TEST_F( Compression, MinTimeIntervalBySourceTime ){
		let speed = Join( *Pump, "Pump1.Speed", {.MinTimeInterval=1s} );
		let base = Time->Now();
		Time->Advance( 5s );
		for( uint i=0; i<5; ++i )
			DataChange( *Pump, speed, i, base+i*1s );
		EXPECT_EQ( Of(speed).Values, (vector<double>{0, 1, 2, 3, 4}) );

		//One sampled early is held, and stored when a later one shows its interval ended.
		let jittered = Join( *Pump, "Pump1.Flow", {.MinTimeInterval=1s} );
		DataChange( *Pump, jittered, 0, base );
		DataChange( *Pump, jittered, 1, base+900ms );
		EXPECT_EQ( Of(jittered).Values.size(), 1 );
		DataChange( *Pump, jittered, 2, base+2s );
		EXPECT_EQ( Of(jittered).Values, (vector<double>{0, 1, 2}) );
		Time->Advance( 1s );//the held one's timer finds nothing.
		EXPECT_EQ( Of(jittered).Values.size(), 3 );
	}

	//A break, or the node's leaving, settles the pending value at once:  it was delivered before.
	TEST_F( Compression, BreakSettlesPending ){
		let speed = Join( *Pump, "Pump1.Speed", {.MinTimeInterval=1s} );
		let flow = Join( *Pump, "Pump1.Flow", {.MinTimeInterval=1s} );
		for( let index : {speed, flow} ){
			Change( index, 1 );
			Time->Advance( 200ms );
			Change( index, 2 );
		}
		EXPECT_EQ( Of(speed).Values.size(), 1 );
		Pump->Disconnected( Time->Now() );
		EXPECT_EQ( Of(speed).Values, (vector<double>{1, 2}) );
		EXPECT_EQ( Of(flow).Values, (vector<double>{1, 2}) );
		Time->Advance( 1s );
		EXPECT_EQ( Of(speed).Values.size(), 2 );

		let level = Join( *Pump, "Tank1.Level", {.MinTimeInterval=1s} );
		Change( level, 1 );
		Time->Advance( 200ms );
		Change( level, 2 );
		Pump->Remove( level );
		EXPECT_EQ( Of(level).Values, (vector<double>{1, 2}) );
		EXPECT_TRUE( std::holds_alternative<NodeRemoved>(Pump->Buffer().back()) );//after its last value.
	}

	//OpcServer's shape:  values arrive as they are written, so a heartbeat's time is the timer's.
	struct Heartbeat : ServerHost{
		α Of( NodeIndex index )Ι->Buffered{ return Buffered{ *Server, index }; }
	};

	TEST_F( Heartbeat, RepeatsTheLastValue ){
		let speed = Historize( "Pump1.Speed", {.MaxTimeInterval=10s} );
		let quiet = Historize( "Pump1.Flow" );
		let first = Time->Now();
		SetValue( speed, 1 );
		SetValue( quiet, 9 );
		Time->Advance( 9s );
		EXPECT_TRUE( Of(speed).Heartbeats.empty() );
		Time->Advance( 1s );
		auto beats = Of( speed ).Heartbeats;
		ASSERT_EQ( beats.size(), 1 );
		EXPECT_EQ( beats[0].Data.Get<double>(0), 1 );
		EXPECT_EQ( beats[0].Heartbeat, ticks(first) );//the value it repeats.
		EXPECT_EQ( beats[0].Data.sourceTimestamp, ticks(first+10s) );
		EXPECT_EQ( beats[0].Data.serverTimestamp, ticks(first+10s) );
		Time->Advance( 10s );
		beats = Of( speed ).Heartbeats;
		ASSERT_EQ( beats.size(), 2 );
		EXPECT_EQ( beats[1].Heartbeat, ticks(first) );
		EXPECT_EQ( beats[1].Data.sourceTimestamp, ticks(first+20s) );

		Time->Advance( 5s );
		let second = Time->Now();
		SetValue( speed, 2 );//MaxTimeInterval counts from it.
		Time->Advance( 9s );
		EXPECT_EQ( Of(speed).Heartbeats.size(), 2 );
		Time->Advance( 1s );
		beats = Of( speed ).Heartbeats;
		ASSERT_EQ( beats.size(), 3 );
		EXPECT_EQ( beats[2].Data.Get<double>(0), 2 );
		EXPECT_EQ( beats[2].Heartbeat, ticks(second) );
		EXPECT_TRUE( Of(quiet).Heartbeats.empty() );//MaxTimeInterval 0 is off.
		EXPECT_EQ( Of(speed).Values, (vector<double>{1, 2}) );
	}

	//A node that gains a MaxTimeInterval, or a shorter one, is found by the timer.
	TEST_F( Heartbeat, ThresholdsArmIt ){
		let speed = Historize( "Pump1.Speed", {.MaxTimeInterval=1h} );
		let flow = Historize( "Pump1.Flow" );
		SetValue( speed, 1 );
		SetValue( flow, 2 );
		Time->Advance( 1min );
		Server->SetThresholds( speed, {.MaxTimeInterval=2min} );
		Server->SetThresholds( flow, {.MaxTimeInterval=30s} );
		Time->Advance( 0s );
		EXPECT_EQ( Of(flow).Heartbeats.size(), 1 );//it stored nothing for a minute.
		EXPECT_TRUE( Of(speed).Heartbeats.empty() );
		Time->Advance( 1min );
		EXPECT_EQ( Of(speed).Heartbeats.size(), 1 );
		EXPECT_EQ( Of(flow).Heartbeats.size(), 3 );
	}

	//A heartbeat only repeats, so it never counts as a stored value for MinTimeInterval.
	TEST_F( Heartbeat, NeverCountsForMinTimeInterval ){
		let speed = Historize( "Pump1.Speed", {.MinTimeInterval=30s, .MaxTimeInterval=10s} );
		SetValue( speed, 1 );
		Time->Advance( 31s );
		EXPECT_EQ( Of(speed).Heartbeats.size(), 3 );
		SetValue( speed, 2 );//a second after a heartbeat, and 31 after the value.
		EXPECT_EQ( Of(speed).Values, (vector<double>{1, 2}) );
	}

	//A heartbeat's SourceTimestamp is the timer's time in the source's clock:  less the node's offset, the arrival time
	//less the SourceTimestamp of the last change it delivered.
	TEST_F( Compression, HeartbeatInTheSourcesClock ){
		let speed = Join( *Pump, "Pump1.Speed", {.MaxTimeInterval=10s} );
		Change( speed, 1, 5s );//its first value's time is when it last changed, not when it was sent.
		Time->Advance( 10s );
		auto beats = Of( speed ).Heartbeats;
		ASSERT_EQ( beats.size(), 1 );
		EXPECT_EQ( beats[0].Data.sourceTimestamp, ticks(Time->Now()) );

		Time->Advance( 1s );
		Change( speed, 2, 300ms );
		Time->Advance( 10s );
		beats = Of( speed ).Heartbeats;
		ASSERT_EQ( beats.size(), 2 );
		EXPECT_EQ( beats[1].Data.sourceTimestamp, ticks(Time->Now()-300ms) );
		EXPECT_EQ( beats[1].Data.serverTimestamp, ticks(Time->Now()) );
		EXPECT_EQ( beats[1].Heartbeat, ticks(Time->Now()-10s-300ms) );
	}

	//Never earlier than MaxTimeInterval after the node's last stored record, whatever the offset says.
	TEST_F( Compression, HeartbeatNoEarlierThanItsInterval ){
		let speed = Join( *Pump, "Pump1.Speed", {.ExceptionDeviation=100, .MaxTimeInterval=10s} );
		let first = Time->Now();
		Change( speed, 1 );
		Time->Advance( 2s );
		Change( speed, 1.5, 5s );//dropped by the band, and it sets the offset.
		Time->Advance( 8s );
		let beats = Of( speed ).Heartbeats;
		ASSERT_EQ( beats.size(), 1 );
		EXPECT_EQ( beats[0].Data.sourceTimestamp, ticks(first+10s) );
	}

	//The timer writes only while the connection is up, and not for a node whose first value after the break is still to
	//say what was lost.
	TEST_F( Compression, NoHeartbeatAcrossABreak ){
		let speed = Join( *Pump, "Pump1.Speed", {.MaxTimeInterval=10s} );
		let first = Time->Now();
		Change( speed, 1 );
		Time->Advance( 5s );
		Pump->Disconnected( Time->Now() );
		Time->Advance( 1min );
		EXPECT_TRUE( Of(speed).Heartbeats.empty() );
		Pump->Connected();
		Time->Advance( 1min );
		EXPECT_TRUE( Of(speed).Heartbeats.empty() );

		DataChange( *Pump, speed, 1, first );//nothing was lost.
		EXPECT_EQ( Of(speed).Values.size(), 1 );
		Time->Advance( 0s );
		let beats = Of( speed ).Heartbeats;
		ASSERT_EQ( beats.size(), 1 );
		EXPECT_EQ( beats[0].Data.sourceTimestamp, ticks(Time->Now()) );
		EXPECT_EQ( beats[0].Heartbeat, ticks(first) );
	}

	//A value in flight when the connection broke takes the node's break, so the reconnect itself arms its heartbeat.
	TEST_F( Compression, HeartbeatResumesWithTheConnection ){
		let speed = Join( *Pump, "Pump1.Speed", {.ExceptionDeviation=100, .MaxTimeInterval=10s} );
		Change( speed, 1 );
		Time->Advance( 1s );
		Pump->Disconnected( Time->Now() );
		Change( speed, 1.5 );
		EXPECT_FALSE( Pump->FindBreak(speed) );
		Time->Advance( 1min );
		EXPECT_TRUE( Of(speed).Heartbeats.empty() );
		Pump->Connected();
		Change( speed, 1.6 );//the new subscription's first value, inside the band.
		Time->Advance( 0s );
		EXPECT_EQ( Of(speed).Heartbeats.size(), 1 );
		Time->Advance( 10s );
		EXPECT_EQ( Of(speed).Heartbeats.size(), 2 );
	}

	//A change sampled at or before a heartbeat still buffered drops it:  the heartbeat would replay the old value over
	//the new one.
	TEST_F( Compression, EarlierChangeDropsTheHeartbeat ){
		let speed = Join( *Pump, "Pump1.Speed", {.MaxTimeInterval=10s} );
		Change( speed, 1 );
		Time->Advance( 1s );
		Change( speed, 2, 400ms );
		Time->Advance( 10s );
		ASSERT_EQ( Of(speed).Heartbeats.size(), 1 );
		let held = Library->Buffered();
		Time->Advance( 50ms );
		Change( speed, 3, 500ms );//sampled 50 ms before the heartbeat's time.
		EXPECT_TRUE( Of(speed).Heartbeats.empty() );
		EXPECT_EQ( Of(speed).Values, (vector<double>{1, 2, 3}) );
		EXPECT_EQ( Library->Buffered(), held );//one record for another of its size.

		Time->Advance( 10s );
		ASSERT_EQ( Of(speed).Heartbeats.size(), 1 );
		Time->Advance( 50ms );
		Change( speed, 4 );//sampled after it.
		EXPECT_EQ( Of(speed).Heartbeats.size(), 1 );
	}

	//So too when the change is held pending.
	TEST_F( Compression, PendingChangeDropsTheHeartbeat ){
		let speed = Join( *Pump, "Pump1.Speed", {.MinTimeInterval=1min, .MaxTimeInterval=10s} );
		Change( speed, 1 );
		Time->Advance( 10s );
		ASSERT_EQ( Of(speed).Heartbeats.size(), 1 );
		Time->Advance( 50ms );
		Change( speed, 2, 100ms );
		EXPECT_TRUE( Of(speed).Heartbeats.empty() );
		EXPECT_EQ( Of(speed).Values.size(), 1 );
		Time->Advance( 49s );//no heartbeat while a pending value waits to say what follows.
		EXPECT_TRUE( Of(speed).Heartbeats.empty() );
		Time->Advance( 2s );//a minute after the stored value, by the source's clock.
		EXPECT_EQ( Of(speed).Values, (vector<double>{1, 2}) );
	}

	//A flush leaves a heartbeat made less than a publishing interval ago for the next, so a change still on its way
	//finds it.
	TEST_F( GatewayFiles, FlushHoldsBackAFreshHeartbeat ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed", {.MaxTimeInterval=10s} );
		let first = Time->Now();
		DataChange( *group, speed, 1, first );
		Time->Advance( 10s );
		Time->Advance( 100ms );
		EXPECT_TRUE( Flush(*group) );
		let file = File( *group, March7 );
		EXPECT_FALSE( std::ranges::any_of(readFile(file), []( let& r ){ return r.value().has_heartbeat(); }) );
		ASSERT_EQ( Records<DataValue>().size(), 1 );
		EXPECT_TRUE( Records<DataValue>()[0].Heartbeat );

		Time->Advance( 400ms );
		EXPECT_TRUE( Flush(*group) );
		let records = readFile( file );
		let& beat = records.back().value();
		ASSERT_TRUE( beat.has_heartbeat() );
		EXPECT_EQ( beat.heartbeat(), ticks(first) );
		EXPECT_EQ( beat.source_ts(), ticks(first+10s) );
		EXPECT_EQ( beat.value().double_value(), 1 );
		EXPECT_TRUE( group->Buffer().empty() );

		Time->Advance( 9600ms );//the next, 100 ms ago:  the group's end is followed by no flush, so it holds none back.
		group.reset();
		Restart();
		EXPECT_EQ( std::ranges::count_if(readFile(file), []( let& r ){ return r.value().has_heartbeat(); }), 2 );
	}

	//OpcServer receives values as they are written, so its flush holds none back.
	TEST_F( ServerFiles, FlushHoldsBackNoHeartbeat ){
		let speed = Historize( "Pump1.Speed", {.MaxTimeInterval=10s} );
		SetValue( speed, 1 );
		Time->Advance( 10s );
		EXPECT_TRUE( Flush(*Server) );
		EXPECT_TRUE( readFile(File(March7)).back().value().has_heartbeat() );
		EXPECT_TRUE( Server->Buffer().empty() );
	}

	//An equal SourceTimestamp after a break means nothing was lost, so nothing is stored.
	TEST_F( Compression, GapWithNothingLost ){
		let speed = Join( *Pump, "Pump1.Speed" );
		let first = Time->Now();
		Change( speed, 1 );
		Time->Advance( 1min );
		Pump->Disconnected( Time->Now() );
		Time->Advance( 1min );
		Pump->Connected();
		DataChange( *Pump, speed, 1, first );
		EXPECT_FALSE( Pump->FindBreak(speed) );
		EXPECT_EQ( Records<DataValue>().size(), 1 );
	}

	//A later one means the historian cannot know what it missed:  a Bad_DataLost at the break, then the value.
	TEST_F( Compression, GapMarkedAtTheBreak ){
		let speed = Join( *Pump, "Pump1.Speed" );
		Change( speed, 1 );
		Time->Advance( 1min );
		let broke = Time->Now();
		Pump->Disconnected( broke );
		Time->Advance( 1min );
		Pump->Connected();
		Change( speed, 2 );
		let values = Records<DataValue>();
		ASSERT_EQ( values.size(), 3 );
		EXPECT_TRUE( isMarker(values[1]) );
		EXPECT_EQ( values[1].Data.sourceTimestamp, ticks(broke) );
		EXPECT_EQ( values[2].Data.Get<double>(0), 2 );
		EXPECT_EQ( values[2].Data.sourceTimestamp, ticks(Time->Now()) );
	}

	//A value that changed just before the break, its notification lost with the connection:  the marker goes at the
	//value's time, first, so the node never reads Bad after the value that ends the gap.
	TEST_F( Compression, GapMarkedNoLaterThanItsEnd ){
		let speed = Join( *Pump, "Pump1.Speed" );
		Change( speed, 1 );
		Time->Advance( 1min );
		let broke = Time->Now();
		Pump->Disconnected( broke );
		Time->Advance( 1min );
		DataChange( *Pump, speed, 2, broke-1s );
		let values = Records<DataValue>();
		ASSERT_EQ( values.size(), 3 );
		EXPECT_TRUE( isMarker(values[1]) );
		EXPECT_EQ( values[1].Data.sourceTimestamp, ticks(broke-1s) );
		EXPECT_EQ( values[2].Data.sourceTimestamp, ticks(broke-1s) );
	}

	//An earlier one means the source's clock or state went backwards:  the marker alone, and the node reads Bad until
	//its next change, which is stored whatever the band says.
	TEST_F( Compression, GapWithAnEarlierValue ){
		let speed = Join( *Pump, "Pump1.Speed", {.ExceptionDeviation=100} );
		let first = Time->Now();
		Change( speed, 1 );
		Time->Advance( 1min );
		let broke = Time->Now();
		Pump->Disconnected( broke );
		Time->Advance( 1min );
		DataChange( *Pump, speed, 2, first-1s );
		auto values = Records<DataValue>();
		ASSERT_EQ( values.size(), 2 );
		EXPECT_TRUE( isMarker(values[1]) );
		EXPECT_EQ( values[1].Data.sourceTimestamp, ticks(broke) );
		Change( speed, 3 );
		EXPECT_EQ( Of(speed).Values, (vector<double>{1, 3}) );
	}

	//The comparison is with the last value delivered, whether the band dropped it, MinTimeInterval held it, or a
	//heartbeat was stored since.
	TEST_F( Compression, GapComparesTheLastDelivered ){
		let dropped = Join( *Pump, "Pump1.Speed", {.ExceptionDeviation=100} );
		let pending = Join( *Pump, "Pump1.Flow", {.MinTimeInterval=1min} );
		let beating = Join( *Pump, "Pump1.Level", {.MaxTimeInterval=10s} );
		Change( dropped, 1 );
		Change( pending, 1 );
		let first = Time->Now();
		Change( beating, 1 );
		Time->Advance( 30s );
		let second = Time->Now();
		Change( dropped, 2 );
		Change( pending, 2 );
		ASSERT_EQ( Of(beating).Heartbeats.size(), 3 );
		Pump->Disconnected( Time->Now() );
		Time->Advance( 1min );
		Pump->Connected();
		DataChange( *Pump, dropped, 2, second );
		DataChange( *Pump, pending, 2, second );
		DataChange( *Pump, beating, 1, first );
		for( let index : {dropped, pending, beating} ){
			SCOPED_TRACE( index );
			EXPECT_TRUE( Of(index).Markers.empty() );
			EXPECT_FALSE( Pump->FindBreak(index) );
		}
		EXPECT_EQ( Of(dropped).Values, (vector<double>{1}) );
		EXPECT_EQ( Of(pending).Values, (vector<double>{1, 2}) );//settled by the break.
		EXPECT_EQ( Of(beating).Values, (vector<double>{1}) );
	}

	//With no SourceTimestamp the comparison means nothing, so the first value is taken as later.
	TEST_F( Compression, GapWithoutASourceTimestamp ){
		let speed = Join( *Pump, "Pump1.Speed" );
		Pump->Enqueue( speed, Reading(1) );
		let broke = Time->Now();
		Pump->Disconnected( broke );
		Time->Advance( 1min );
		Pump->Enqueue( speed, Reading(1) );
		let values = Records<DataValue>();
		ASSERT_EQ( values.size(), 3 );
		EXPECT_TRUE( isMarker(values[1]) );
		EXPECT_EQ( values[1].Data.sourceTimestamp, ticks(broke) );
	}

	//After a stop the comparison takes the node's newest record in its files:  a value the band dropped before the stop
	//then compares as later, and the marker goes before it.
	TEST_F( GatewayFiles, RestartComparesTheNewestRecord ){
		auto group = AddGroup();
		let name = group->Name();
		const Thresholds band{ .ExceptionDeviation=100 };
		let speed = Join( *group, "Pump1.Speed", band );
		let flow = Join( *group, "Pump1.Flow", band );
		let first = Time->Now();
		DataChange( *group, speed, 1, first );
		DataChange( *group, flow, 1, first );
		Time->Advance( 1s );
		let second = Time->Now();
		DataChange( *group, flow, 2, second );//dropped.
		group.reset();
		Restart();
		group = Rejoin( name, {{Node("Pump1.Speed"), band, speed}, {Node("Pump1.Flow"), band, flow}} );
		EXPECT_EQ( group->FindBreak(speed), second );
		Time->Advance( 30s );

		DataChange( *group, speed, 1, first );
		EXPECT_TRUE( group->Buffer().empty() );//nothing was lost.
		EXPECT_FALSE( group->FindBreak(speed) );
		DataChange( *group, speed, 1.5, Time->Now() );
		EXPECT_TRUE( group->Buffer().empty() );//inside the band of the value the files hold.

		DataChange( *group, flow, 2, second );
		let values = Records<DataValue>();
		ASSERT_EQ( values.size(), 2 );
		EXPECT_TRUE( isMarker(values[0]) );
		EXPECT_EQ( values[0].Data.sourceTimestamp, ticks(second) );//the stop, which its last flush gives.
		EXPECT_EQ( values[1].Data.Get<double>(0), 2 );
		EXPECT_TRUE( Flush(*group) );
		let records = readFile( File(*group, March7) );
		ASSERT_GE( records.size(), 2 );
		EXPECT_EQ( records[records.size()-2].value().status(), UA_STATUSCODE_BADDATALOST );
		EXPECT_TRUE( isValue(records.back(), flow, 2, second) );
	}

	//A heartbeat in the files compares by the SourceTimestamp of the value it repeats, and the heartbeat goes on from it.
	TEST_F( ServerFiles, RestartComparesAHeartbeatByItsValue ){
		const Thresholds beat{ .MaxTimeInterval=10s };
		let speed = Historize( "Pump1.Speed", beat );
		let first = Time->Now();
		SetValue( speed, 1 );
		Time->Advance( 10s );
		Start( {{Node("Pump1.Speed"), beat}} );
		ASSERT_TRUE( readFile(File(March7)).back().value().has_heartbeat() );
		Server->Enqueue( speed, Reading(1, first) );//OpcServer's current value at start.
		EXPECT_TRUE( Server->Buffer().empty() );
		Time->Advance( 10s );
		let values = Records<DataValue>();
		ASSERT_EQ( values.size(), 1 );
		EXPECT_EQ( values[0].Heartbeat, ticks(first) );
		EXPECT_EQ( values[0].Data.Get<double>(0), 1 );
		EXPECT_EQ( values[0].Data.sourceTimestamp, ticks(first+20s) );
	}

	//When the newest record is a marker the comparison means nothing, so the first value is taken as later.
	TEST_F( ServerFiles, RestartAfterAMarker ){
		let speed = Historize( "Pump1.Speed" );
		let first = Time->Now();
		SetValue( speed, 1 );
		Start( {{Node("Pump1.Speed")}} );
		Time->Advance( 30s );
		Server->Enqueue( speed, Reading(2, first-1s) );//earlier:  the marker alone.
		ASSERT_EQ( Records<DataValue>().size(), 1 );
		Start( {{Node("Pump1.Speed")}} );
		Time->Advance( 30s );
		Server->Enqueue( speed, Reading(2, first-1s) );
		let values = Records<DataValue>();
		ASSERT_EQ( values.size(), 2 );
		EXPECT_TRUE( isMarker(values[0]) );
		EXPECT_EQ( values[0].Data.sourceTimestamp, ticks(first-1s) );//no later than the value.
		EXPECT_EQ( values[1].Data.Get<double>(0), 2 );
	}
}
