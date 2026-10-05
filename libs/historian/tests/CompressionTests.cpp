//What a node's thresholds store of what it delivers:  the deviation band, MinTimeInterval's pending value, the
//heartbeat, and the comparison that judges a node's first value after a break.
#include "dayFiles.h"
#include <jde/fwk/log/MemoryLog.h>

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

	//A change of MinTimeInterval moves the end of the interval a pending value waits out:  one that has already ended
	//stores it at once, and its heartbeat is held back no longer.
	TEST_F( Compression, ThresholdsMoveAPendingValuesInterval ){
		let speed = Join( *Pump, "Pump1.Speed", {.MinTimeInterval=1h} );
		let flow = Join( *Pump, "Pump1.Flow", {.MinTimeInterval=1h} );
		let level = Join( *Pump, "Tank1.Level", {.MinTimeInterval=1min} );
		for( let index : {speed, flow, level} )
			Change( index, 1 );
		Time->Advance( 10s );
		for( let index : {speed, flow, level} )
			Change( index, 2 );
		EXPECT_EQ( Of(speed).Values.size(), 1 );

		Pump->SetThresholds( speed, {.MaxTimeInterval=10s} );//no interval.
		EXPECT_EQ( Of(speed).Values, (vector<double>{1, 2}) );
		Pump->SetThresholds( flow, {.MinTimeInterval=30s} );//20 s of it are left.
		Pump->SetThresholds( level, {.MinTimeInterval=2min} );
		EXPECT_EQ( Of(flow).Values.size(), 1 );
		Time->Advance( 19s );
		EXPECT_EQ( Of(flow).Values.size(), 1 );
		Time->Advance( 1s );
		EXPECT_EQ( Of(flow).Values, (vector<double>{1, 2}) );
		EXPECT_EQ( Of(speed).Heartbeats.size(), 2 );

		Time->Advance( 30s );//the interval it was held for at first.
		EXPECT_EQ( Of(level).Values.size(), 1 );
		Time->Advance( 1min );
		EXPECT_EQ( Of(level).Values, (vector<double>{1, 2}) );
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

	//So do the group's removal and the historian's stop, whose last flush writes it.
	TEST_F( GatewayFiles, CloseAndStopSettlePending ){
		auto closing = AddGroup();
		auto stopping = AddGroup();
		let speed = Join( *closing, "Pump1.Speed", {.MinTimeInterval=1min} );
		let flow = Join( *stopping, "Pump1.Flow", {.MinTimeInterval=1min} );
		let first = Time->Now();
		DataChange( *closing, speed, 1, first );
		DataChange( *stopping, flow, 1, first );
		Time->Advance( 200ms );
		let second = Time->Now();
		DataChange( *closing, speed, 2, second );
		DataChange( *stopping, flow, 2, second );
		EXPECT_EQ( Buffered(*closing, speed).Values.size(), 1 );
		EXPECT_EQ( Buffered(*stopping, flow).Values.size(), 1 );

		closing->Close( Admin );
		EXPECT_EQ( Buffered(*closing, speed).Values, (vector<double>{1, 2}) );
		EXPECT_TRUE( std::holds_alternative<NodeRemoved>(closing->Buffer().back()) );//after its last value.
		EXPECT_EQ( Time->Advance(0s), 1 );//the flush Close asks for.
		EXPECT_TRUE( Settle(*closing) );
		let closed = readFile( File(*closing, March7) );
		ASSERT_GE( closed.size(), 2 );
		EXPECT_TRUE( closed.back().has_node_removed() );
		EXPECT_TRUE( isValue(closed[closed.size()-2], speed, 2, second) );

		let file = File( *stopping, March7 );
		closing.reset();
		stopping.reset();
		Restart();//the stop is a break too.
		let stopped = readFile( file );
		ASSERT_GE( stopped.size(), 2 );
		EXPECT_TRUE( isValue(stopped.back(), flow, 2, second) );
		EXPECT_TRUE( isValue(stopped[stopped.size()-2], flow, 1, first) );
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

	//What a node keeps to repeat is its last stored value whatever its type:  a number over the last one, anything else
	//as a copy of its own.
	TEST_F( Heartbeat, RepeatsTheLastOfAnyType ){
		let state = Historize( "Pump1.State", {.MaxTimeInterval=10s} );
		let text = []( const DataValue& v ){
			let& s = *(const UA_String*)v.Data.value.data;
			return string{ (const char*)s.data, s.length };
		};
		Server->Enqueue( state, Text("idle", Time->Now()) );
		Server->Enqueue( state, Text("running", Time->Now()) );
		Time->Advance( 10s );
		auto values = Records<DataValue>();
		ASSERT_EQ( values.size(), 3 );
		ASSERT_TRUE( values[2].Heartbeat );
		EXPECT_EQ( text(values[2]), "running" );
		EXPECT_EQ( text(values[0]), "idle" );

		Time->Advance( 1s );
		SetValue( state, 7 );
		SetValue( state, 8 );
		Time->Advance( 10s );
		values = Records<DataValue>();
		ASSERT_EQ( values.size(), 6 );
		ASSERT_TRUE( values[5].Heartbeat );
		EXPECT_EQ( values[5].Data.Get<double>(0), 8 );
		EXPECT_EQ( values[3].Data.Get<double>(0), 7 );//each record is its own copy.
		EXPECT_EQ( values[4].Data.Get<double>(0), 8 );

		Time->Advance( 1s );
		Server->Enqueue( state, Text("stopped", Time->Now()) );
		Time->Advance( 10s );
		values = Records<DataValue>();
		ASSERT_EQ( values.size(), 8 );
		ASSERT_TRUE( values[7].Heartbeat );
		EXPECT_EQ( text(values[7]), "stopped" );
	}

	//Each node is looked at on its own deadline:  one that stores meanwhile waits again, a shorter interval is found at
	//once, a longer one when the old one is up, and a node that left not at all.
	TEST_F( Heartbeat, EachNodeOnItsOwnDeadline ){
		let fast = Historize( "Pump1.Speed", {.MaxTimeInterval=10s} );
		let slow = Historize( "Pump1.Flow", {.MaxTimeInterval=25s} );
		let busy = Historize( "Tank1.Level", {.MaxTimeInterval=10s} );
		let first = Time->Now();
		for( let index : {fast, slow, busy} )
			SetValue( index, 0 );
		for( uint i=1; i<=50; ++i ){
			Time->Advance( 1s );
			SetValue( busy, i );
		}
		let made = [&]( NodeIndex index ){
			vector<Duration> y;
			for( let& beat : Of(index).Heartbeats )
				y.push_back( UADateTime{beat.Data.serverTimestamp}.Time()-first );
			return y;
		};
		EXPECT_EQ( made(fast), (vector<Duration>{10s, 20s, 30s, 40s, 50s}) );
		EXPECT_EQ( made(slow), (vector<Duration>{25s, 50s}) );
		EXPECT_TRUE( made(busy).empty() );

		Server->SetThresholds( slow, {.MaxTimeInterval=5s} );
		Server->SetThresholds( fast, {.MaxTimeInterval=1min} );
		Time->Advance( 10s );
		EXPECT_EQ( made(slow), (vector<Duration>{25s, 50s, 55s, 60s}) );
		EXPECT_EQ( made(busy), (vector<Duration>{60s}) );
		Server->Remove( slow );
		Time->Advance( 50s );
		EXPECT_EQ( made(fast), (vector<Duration>{10s, 20s, 30s, 40s, 50s, 110s}) );
		EXPECT_EQ( made(slow).size(), 4 );
		EXPECT_EQ( made(busy), (vector<Duration>{60s, 70s, 80s, 90s, 100s, 110s}) );
	}

	//The wall clock set back leaves every deadline in the time before it.  The timer then has each node wait from now,
	//so none waits longer than its own interval.
	TEST_F( Heartbeat, ClockSetBack ){
		let speed = Historize( "Pump1.Speed", {.MaxTimeInterval=10s} );
		let flow = Historize( "Pump1.Flow", {.MaxTimeInterval=10s} );
		SetValue( speed, 1 );
		Time->Advance( 4s );
		SetValue( flow, 1 );
		Time->Advance( 3s );
		Time->Step( -1h );
		Time->Advance( 3s );//the timer's interval is up.
		Time->Advance( 9s );
		EXPECT_TRUE( Of(speed).Heartbeats.empty() );
		EXPECT_TRUE( Of(flow).Heartbeats.empty() );
		Time->Advance( 1s );
		for( let index : {speed, flow} ){
			SCOPED_TRACE( index );
			let beats = Of( index ).Heartbeats;
			ASSERT_EQ( beats.size(), 1 );
			EXPECT_EQ( beats[0].Data.serverTimestamp, ticks(Time->Now()) );
		}
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

	//A value in flight when the connection broke was sent before the break:  it takes the ordinary test, and the break
	//stands for the first value the connection brings back, which arms the heartbeat again.
	TEST_F( Compression, InFlightValueLeavesTheBreak ){
		let speed = Join( *Pump, "Pump1.Speed", {.ExceptionDeviation=100, .MaxTimeInterval=10s} );
		let flow = Join( *Pump, "Pump1.Flow" );
		Change( speed, 1 );
		Change( flow, 1 );
		Time->Advance( 1s );
		let broke = Time->Now();
		Pump->Disconnected( broke );
		Change( speed, 1.5 );//inside the band.
		Change( flow, 2, 200ms );
		for( let index : {speed, flow} ){
			SCOPED_TRACE( index );
			EXPECT_EQ( Pump->FindBreak(index), broke );
			EXPECT_TRUE( Of(index).Markers.empty() );
		}
		EXPECT_EQ( Of(speed).Values, (vector<double>{1}) );
		EXPECT_EQ( Of(flow).Values, (vector<double>{1, 2}) );
		Time->Advance( 1min );
		Pump->Connected();
		Time->Advance( 1min );
		EXPECT_TRUE( Of(speed).Heartbeats.empty() );

		Change( speed, 1.6 );//the new subscription's first value:  inside the band, and later than the last delivered.
		let later = Of( speed );
		ASSERT_EQ( later.Markers.size(), 1 );
		EXPECT_EQ( later.Markers[0].Data.sourceTimestamp, ticks(broke) );
		EXPECT_EQ( later.Values, (vector<double>{1, 1.6}) );
		EXPECT_FALSE( Pump->FindBreak(speed) );
		Time->Advance( 10s );
		EXPECT_EQ( Of(speed).Heartbeats.size(), 1 );

		DataChange( *Pump, flow, 2, broke-200ms );//what was in flight is the last delivered, so nothing was lost.
		EXPECT_TRUE( Of(flow).Markers.empty() );
		EXPECT_EQ( Of(flow).Values, (vector<double>{1, 2}) );
		EXPECT_FALSE( Pump->FindBreak(flow) );
	}

	//One that MinTimeInterval holds is settled by the first value after the break, as the break settles those before it.
	TEST_F( Compression, InFlightPendingIsSettledFirst ){
		let speed = Join( *Pump, "Pump1.Speed", {.MinTimeInterval=1min} );
		Change( speed, 1 );
		Time->Advance( 1s );
		let broke = Time->Now();
		Pump->Disconnected( broke );
		Change( speed, 2 );
		EXPECT_EQ( Of(speed).Values.size(), 1 );
		Time->Advance( 10s );
		Pump->Connected();
		Change( speed, 3 );
		let values = Records<DataValue>();
		ASSERT_EQ( values.size(), 4 );
		EXPECT_EQ( values[1].Data.Get<double>(0), 2 );
		EXPECT_TRUE( isMarker(values[2]) );
		EXPECT_EQ( values[2].Data.sourceTimestamp, ticks(broke) );
		EXPECT_EQ( values[3].Data.Get<double>(0), 3 );
		Time->Advance( 1min );
		EXPECT_EQ( Of(speed).Values, (vector<double>{1, 2, 3}) );
	}

	//A connection that breaks again before a node has answered its first break:  the value in flight is the node's
	//first since that one, so it is judged, and the second break stands in its place.
	TEST_F( Compression, InFlightValueAnswersAnEarlierBreak ){
		let speed = Join( *Pump, "Pump1.Speed", {.ExceptionDeviation=100} );
		Change( speed, 1 );
		Time->Advance( 1min );
		let broke = Time->Now();
		Pump->Disconnected( broke );
		Time->Advance( 1min );
		Pump->Connected();
		Time->Advance( 1s );
		let again = Time->Now();
		Pump->Disconnected( again );
		Change( speed, 1.5, 500ms );
		EXPECT_EQ( Pump->FindBreak(speed), again );
		auto of = Of( speed );
		ASSERT_EQ( of.Markers.size(), 1 );
		EXPECT_EQ( of.Markers[0].Data.sourceTimestamp, ticks(broke) );
		EXPECT_EQ( of.Values, (vector<double>{1, 1.5}) );

		Time->Advance( 1min );
		Pump->Connected();
		Change( speed, 1.6 );
		of = Of( speed );
		ASSERT_EQ( of.Markers.size(), 2 );
		EXPECT_EQ( of.Markers[1].Data.sourceTimestamp, ticks(again) );
		EXPECT_EQ( of.Values, (vector<double>{1, 1.5, 1.6}) );
		EXPECT_FALSE( Pump->FindBreak(speed) );
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
		EXPECT_EQ( Of(speed).Heartbeats[0].Data.sourceTimestamp, ticks(Time->Now()-500ms) );//MaxTimeInterval after the change, not after the heartbeat it dropped.
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

	//A full buffer drops a node's heartbeat with its oldest values, and holds the newest of what a node lost to write
	//back.  A change sampled before a heartbeat held so drops it too:  nothing then ends the node's gap but the change.
	TEST_F( GatewayFiles, EarlierChangeDropsALostHeartbeat ){
		auto config = Config( 1min );
		config.MaxBuffer = 4'000;
		Restart( move(config) );
		auto group = AddGroup();
		const Thresholds beat{ .MaxTimeInterval=10s };
		let speed = Join( *group, "Pump1.Speed", beat );
		let flow = Join( *group, "Pump1.Flow", beat );
		let level = Join( *group, "Tank1.Level" );
		let first = Time->Now();
		DataChange( *group, speed, 1, first );
		EXPECT_TRUE( Flush(*group) );//its value is in the file, so its heartbeat is all it loses.
		let file = File( *group, March7 ), aside = file.parent_path()/"aside";
		fs::rename( file, aside );
		fs::create_directory( file );//nothing appends to a directory.
		DataChange( *group, flow, 1, first );
		EXPECT_FALSE( Flush(*group) );//so it trims rather than flushes.
		Time->Advance( 10s );
		ASSERT_EQ( Buffered(*group, speed).Heartbeats.size(), 1 );
		ASSERT_EQ( Buffered(*group, flow).Heartbeats.size(), 1 );
		for( uint i=1; i<200 && !Buffered(*group, flow).Heartbeats.empty(); ++i ){
			DataChange( *group, level, i, Time->Now()+i*1ms );
			Time->Advance( 0s );//the trim's hop.
		}
		ASSERT_TRUE( Buffered(*group, speed).Heartbeats.empty() );
		ASSERT_TRUE( Buffered(*group, flow).Heartbeats.empty() );
		ASSERT_TRUE( Buffered(*group, flow).Values.empty() );

		DataChange( *group, speed, 2, first+5s );
		DataChange( *group, flow, 2, first+5s );
		fs::remove( file );
		fs::rename( aside, file );
		EXPECT_TRUE( Flush(*group) );
		let stored = [&]( NodeIndex index ){
			vector<Proto::HistoryRecord> y;
			for( let& r : readFile(file) ){
				if( r.has_value() && r.value().node_index()==index )
					y.push_back( r );
			}
			return y;
		};
		let speeds = stored( speed );
		ASSERT_EQ( speeds.size(), 2 );
		EXPECT_TRUE( isValue(speeds[0], speed, 1, first) );
		EXPECT_TRUE( isValue(speeds[1], speed, 2, first+5s) );
		let flows = stored( flow );
		ASSERT_EQ( flows.size(), 2 );
		EXPECT_EQ( flows[0].value().status(), UA_STATUSCODE_BADDATALOST );//the value it lost.
		EXPECT_EQ( flows[0].value().source_ts(), ticks(first) );
		EXPECT_TRUE( isValue(flows[1], flow, 2, first+5s) );
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

	//But only when the value agrees.  A source whose timestamps are coarse can change it inside one, in its value or its
	//status code, and that is taken as later.  A change the node's band would drop is none.
	TEST_F( Compression, GapWithAnotherValueAtTheSameTimestamp ){
		let speed = Join( *Pump, "Pump1.Speed" );
		let flow = Join( *Pump, "Pump1.Flow" );
		let level = Join( *Pump, "Tank1.Level", {.ExceptionDeviation=100} );
		let first = Time->Now();
		for( let index : {speed, flow, level} )
			Change( index, 1 );
		Time->Advance( 1min );
		Pump->Disconnected( Time->Now() );
		Time->Advance( 1min );
		Pump->Connected();

		DataChange( *Pump, speed, 2, first );
		let changed = Of( speed );
		ASSERT_EQ( changed.Markers.size(), 1 );
		EXPECT_EQ( changed.Markers[0].Data.sourceTimestamp, ticks(first) );//no later than the value.
		EXPECT_EQ( changed.Values, (vector<double>{1, 2}) );
		EXPECT_EQ( changed.Sources.back(), ticks(first) );
		EXPECT_FALSE( Pump->FindBreak(speed) );

		auto uncertain = Reading( 1, first, first+5ms );
		uncertain.status = UA_STATUSCODE_UNCERTAINLASTUSABLEVALUE;
		uncertain.hasStatus = true;
		Pump->Enqueue( flow, uncertain );
		EXPECT_EQ( Of(flow).Markers.size(), 1 );
		EXPECT_EQ( Of(flow).Values, (vector<double>{1, 1}) );

		DataChange( *Pump, level, 1.5, first );
		EXPECT_TRUE( Of(level).Markers.empty() );
		EXPECT_EQ( Of(level).Values, (vector<double>{1}) );
		EXPECT_FALSE( Pump->FindBreak(level) );
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
		Pump->Connected();
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
		if( !Logging::FindLogger<Logging::MemoryLog>() )
			Logging::AddLogger( mu<Logging::MemoryLog>() );
		Logging::ClearMemory();
		let speed = Join( *Pump, "Pump1.Speed", {.ExceptionDeviation=100} );
		let first = Time->Now();
		Change( speed, 1 );
		Time->Advance( 1min );
		let broke = Time->Now();
		Pump->Disconnected( broke );
		Time->Advance( 1min );
		Pump->Connected();
		DataChange( *Pump, speed, 2, first-1s );
		auto values = Records<DataValue>();
		ASSERT_EQ( values.size(), 2 );
		EXPECT_TRUE( isMarker(values[1]) );
		EXPECT_EQ( values[1].Data.sourceTimestamp, ticks(broke) );
		Time->Advance( 0s );//the warning's hop, off the collection path.
		let warned = Logging::Find( []( const Logging::Entry& e ){ return e.Message().contains("came back from the break"); } );
		ASSERT_EQ( warned.size(), 1 );
		EXPECT_EQ( warned[0].Level, ELogLevel::Warning );
		EXPECT_EQ( warned[0].Tags, ELogTags::IO );//with the historian's other warnings about its records.
		Change( speed, 3 );
		EXPECT_EQ( Of(speed).Values, (vector<double>{1, 3}) );
	}

	//The break is in the historian's clock and a node's records are in the source's.  With the source's ahead, the
	//marker goes at the node's newest record, never before it, whether the value that ends the gap is later or earlier.
	TEST_F( Compression, GapMarkedNoEarlierThanTheNewestRecord ){
		let speed = Join( *Pump, "Pump1.Speed" );
		let flow = Join( *Pump, "Pump1.Flow" );
		let stored = Time->Now()+30s;
		DataChange( *Pump, speed, 1, stored );
		DataChange( *Pump, flow, 1, stored );
		Time->Advance( 10s );
		let broke = Time->Now();
		Pump->Disconnected( broke );
		Time->Advance( 10min );
		Pump->Connected();

		DataChange( *Pump, speed, 2, Time->Now()+30s );
		let later = Of( speed );
		ASSERT_EQ( later.Markers.size(), 1 );
		EXPECT_EQ( later.Markers[0].Data.sourceTimestamp, ticks(stored) );
		EXPECT_EQ( later.Values, (vector<double>{1, 2}) );
		let values = Records<DataValue>();
		ASSERT_EQ( values.size(), 4 );
		EXPECT_TRUE( isMarker(values[2]) );//after the record it shares a time with, as every sort by source time keeps it.

		DataChange( *Pump, flow, 2, stored-5s );//its clock was set back meanwhile.
		let earlier = Of( flow );
		ASSERT_EQ( earlier.Markers.size(), 1 );
		EXPECT_EQ( earlier.Markers[0].Data.sourceTimestamp, ticks(stored) );
		EXPECT_EQ( earlier.Values, (vector<double>{1}) );
	}

	//A heartbeat is a record too, and its time is in the source's clock.
	TEST_F( Compression, GapMarkedNoEarlierThanAHeartbeat ){
		let speed = Join( *Pump, "Pump1.Speed", {.MaxTimeInterval=10s} );
		Change( speed, 1 );
		Time->Advance( 1s );
		Change( speed, 2, -30s );//the source's clock is 30 s ahead.
		Time->Advance( 10s );
		let beats = Of( speed ).Heartbeats;
		ASSERT_EQ( beats.size(), 1 );
		Time->Advance( 1s );
		let broke = Time->Now();
		ASSERT_GT( beats[0].Data.sourceTimestamp, ticks(broke) );
		Pump->Disconnected( broke );
		Time->Advance( 10min );
		Pump->Connected();
		Change( speed, 3, -30s );
		let after = Of( speed );
		ASSERT_EQ( after.Markers.size(), 1 );
		EXPECT_EQ( after.Markers[0].Data.sourceTimestamp, beats[0].Data.sourceTimestamp );
		EXPECT_EQ( after.Heartbeats.size(), 1 );
		EXPECT_EQ( after.Values, (vector<double>{1, 2, 3}) );
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

	//But one the value that ends the gap drops is no floor for the marker.
	TEST_F( Compression, GapFloorIsWhatTheValueLeaves ){
		let speed = Join( *Pump, "Pump1.Speed", {.MaxTimeInterval=10s} );
		Change( speed, 1 );
		Time->Advance( 1s );
		let second = Time->Now()+30s;
		Change( speed, 2, -30s );//the source's clock is 30 s ahead.
		Time->Advance( 10s );
		ASSERT_EQ( Of(speed).Heartbeats.size(), 1 );
		Time->Advance( 1s );
		Pump->Disconnected( Time->Now() );
		Time->Advance( 10s );
		Pump->Connected();
		DataChange( *Pump, speed, 3, second+4s );//sourced before the heartbeat's time.
		let after = Of( speed );
		EXPECT_TRUE( after.Heartbeats.empty() );
		ASSERT_EQ( after.Markers.size(), 1 );
		EXPECT_EQ( after.Markers[0].Data.sourceTimestamp, ticks(second) );
		EXPECT_EQ( after.Sources.back(), ticks(second+4s) );
	}

	//A break the host reports late:  the timer went on repeating each value over a dead feed.  The heartbeats made
	//since the break go, and its marker goes at the break, whichever way the value that ends it compares.
	TEST_F( Compression, LateBreakDropsItsHeartbeats ){
		const Thresholds beat{ .ExceptionDeviation=100, .MaxTimeInterval=10s };
		let speed = Join( *Pump, "Pump1.Speed", beat );
		let flow = Join( *Pump, "Pump1.Flow", beat );
		let first = Time->Now();
		Change( speed, 1 );
		Change( flow, 1 );
		Time->Advance( 10s );
		let held = Library->Buffered();
		Time->Advance( 15s );
		ASSERT_EQ( Of(speed).Heartbeats.size(), 2 );
		let broke = first+15s;
		Pump->Disconnected( broke );
		EXPECT_EQ( Library->Buffered(), held );
		for( let index : {speed, flow} ){
			SCOPED_TRACE( index );
			let beats = Of( index ).Heartbeats;
			ASSERT_EQ( beats.size(), 1 );
			EXPECT_EQ( beats[0].Data.serverTimestamp, ticks(first+10s) );//made while the connection was up.
		}
		Time->Advance( 1min );
		Pump->Connected();

		Change( speed, 2 );
		let later = Of( speed );
		ASSERT_EQ( later.Markers.size(), 1 );
		EXPECT_EQ( later.Markers[0].Data.sourceTimestamp, ticks(broke) );
		EXPECT_EQ( later.Values, (vector<double>{1, 2}) );

		DataChange( *Pump, flow, 2, first-1s );
		let earlier = Of( flow );
		ASSERT_EQ( earlier.Markers.size(), 1 );
		EXPECT_EQ( earlier.Markers[0].Data.sourceTimestamp, ticks(broke) );
		EXPECT_EQ( earlier.Heartbeats.size(), 1 );
		EXPECT_EQ( earlier.Values, (vector<double>{1}) );
	}

	//One a flush already wrote is out of reach, so the marker goes after it.
	TEST_F( GatewayFiles, LateBreakKeepsAFlushedHeartbeat ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed", {.MaxTimeInterval=10s} );
		let first = Time->Now();
		DataChange( *group, speed, 1, first );
		Time->Advance( 10s );
		Time->Advance( 1s );//past the publishing interval, so the flush takes the heartbeat.
		EXPECT_TRUE( Flush(*group) );
		EXPECT_TRUE( group->Buffer().empty() );
		Time->Advance( 9s );
		ASSERT_EQ( Records<DataValue>().size(), 1 );
		group->Disconnected( first+5s );
		EXPECT_TRUE( group->Buffer().empty() );
		Time->Advance( 30s );
		group->Connected();
		DataChange( *group, speed, 2, Time->Now() );
		let values = Records<DataValue>();
		ASSERT_EQ( values.size(), 2 );
		EXPECT_TRUE( isMarker(values[0]) );
		EXPECT_EQ( values[0].Data.sourceTimestamp, ticks(first+10s) );
	}

	//With no SourceTimestamp the comparison means nothing, so the first value is taken as later.
	TEST_F( Compression, GapWithoutASourceTimestamp ){
		let speed = Join( *Pump, "Pump1.Speed" );
		Pump->Enqueue( speed, Reading(1) );
		let broke = Time->Now();
		Pump->Disconnected( broke );
		Time->Advance( 1min );
		Pump->Connected();
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

	//The stop's time is the historian's too, so after it the marker is no earlier than the node's newest record in its
	//files.
	TEST_F( GatewayFiles, RestartGapNoEarlierThanTheNewestRecord ){
		auto group = AddGroup();
		let name = group->Name();
		let speed = Join( *group, "Pump1.Speed" );
		let stored = Time->Now()+30s;
		DataChange( *group, speed, 1, stored );
		group.reset();
		Restart();
		group = Rejoin( name, {{Node("Pump1.Speed"), {}, speed}} );
		ASSERT_LT( group->FindBreak(speed), stored );
		Time->Advance( 10min );

		let ended = Time->Now()+30s;
		DataChange( *group, speed, 2, ended );
		let values = Records<DataValue>();
		ASSERT_EQ( values.size(), 2 );
		EXPECT_TRUE( isMarker(values[0]) );
		EXPECT_EQ( values[0].Data.sourceTimestamp, ticks(stored) );
		EXPECT_TRUE( Flush(*group) );
		let records = readFile( File(*group, March7) );
		ASSERT_GE( records.size(), 3 );
		EXPECT_TRUE( isValue(records[records.size()-3], speed, 1, stored) );
		EXPECT_EQ( records[records.size()-2].value().status(), UA_STATUSCODE_BADDATALOST );
		EXPECT_EQ( records[records.size()-2].value().source_ts(), ticks(stored) );
		EXPECT_TRUE( isValue(records.back(), speed, 2, ended) );
	}

	//And the value with that record's, as it reads back:  an alias as its built-in type, which is the same on the wire.
	TEST_F( GatewayFiles, RestartComparesTheNewestRecordsValue ){
		auto group = AddGroup();
		let name = group->Name();
		let speed = Join( *group, "Pump1.Speed" );
		let started = Join( *group, "Pump1.Started" );
		let first = Time->Now();
		DataChange( *group, speed, 1, first );
		UA_DataValue dv{};
		const UA_UtcTime at{ ticks(first-1h) };
		UA_Variant_setScalarCopy( &dv.value, &at, &UA_TYPES[UA_TYPES_UTCTIME] );
		dv.hasValue = dv.hasSourceTimestamp = true;
		dv.sourceTimestamp = ticks( first );
		const Value utc{ move(dv) };//read back as a DateTime.
		group->Enqueue( started, utc );
		group.reset();
		Restart();
		group = Rejoin( name, {{Node("Pump1.Speed"), {}, speed}, {Node("Pump1.Started"), {}, started}} );
		Time->Advance( 30s );

		group->Enqueue( started, utc );
		EXPECT_TRUE( group->Buffer().empty() );//nothing was lost.
		EXPECT_FALSE( group->FindBreak(started) );

		DataChange( *group, speed, 2, first );
		let values = Records<DataValue>();
		ASSERT_EQ( values.size(), 2 );
		EXPECT_TRUE( isMarker(values[0]) );
		EXPECT_EQ( values[1].Data.Get<double>(0), 2 );
		EXPECT_EQ( values[1].Data.sourceTimestamp, ticks(first) );
	}

	//MaxTimeInterval counts in the historian's clock, and the newest record's time is in the source's.  With that one
	//ahead, the heartbeat still comes MaxTimeInterval after the start at the latest.
	TEST_F( ServerFiles, RestartHeartbeatInTheHistoriansClock ){
		let speed = Historize( "Pump1.Speed" );
		let first = Time->Now();
		Server->Enqueue( speed, Reading(1, first+1h) );
		Time->Advance( 5min );
		Start( {{Node("Pump1.Speed"), {.MaxTimeInterval=10s}}} );
		Server->Enqueue( speed, Reading(1, first+1h) );//nothing was lost.
		EXPECT_TRUE( Server->Buffer().empty() );
		Time->Advance( 9s );
		EXPECT_TRUE( Server->Buffer().empty() );
		Time->Advance( 1s );
		let values = Records<DataValue>();
		ASSERT_EQ( values.size(), 1 );
		EXPECT_EQ( values[0].Heartbeat, ticks(first+1h) );
		EXPECT_EQ( values[0].Data.serverTimestamp, ticks(Time->Now()) );
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
		EXPECT_FALSE( readFile(File(March7)).back().value().heartbeat_unsourced() );
		Server->Enqueue( speed, Reading(1, first) );//OpcServer's current value at start.
		EXPECT_TRUE( Server->Buffer().empty() );
		Time->Advance( 10s );
		let values = Records<DataValue>();
		ASSERT_EQ( values.size(), 1 );
		EXPECT_EQ( values[0].Heartbeat, ticks(first) );
		EXPECT_EQ( values[0].Data.Get<double>(0), 1 );
		EXPECT_EQ( values[0].Data.sourceTimestamp, ticks(first+20s) );
	}

	//A heartbeat's record says when the value it repeats came with no SourceTimestamp.  After a restart there is then
	//nothing to compare, as there was none in memory, so the first value is taken as later.
	TEST_F( ServerFiles, RestartComparesNoUnsourcedHeartbeat ){
		const Thresholds beat{ .MaxTimeInterval=10s };
		let speed = Historize( "Pump1.Speed", beat );
		let first = Time->Now();
		Server->Enqueue( speed, Reading(1) );//filed by the time it arrived.
		Time->Advance( 10s );
		ASSERT_EQ( Records<DataValue>().size(), 2 );
		EXPECT_TRUE( Records<DataValue>()[1].Unsourced );
		Start( {{Node("Pump1.Speed"), beat}} );
		let stored = readFile( File(March7) ).back().value();
		ASSERT_TRUE( stored.has_heartbeat() );
		EXPECT_TRUE( stored.heartbeat_unsourced() );
		EXPECT_EQ( stored.heartbeat(), ticks(first) );

		Time->Advance( 30s );
		Server->Enqueue( speed, Reading(2, first-1s) );//earlier than that server time, were the two compared.
		let values = Records<DataValue>();
		ASSERT_EQ( values.size(), 2 );
		EXPECT_TRUE( isMarker(values[0]) );
		EXPECT_EQ( values[1].Data.Get<double>(0), 2 );
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
