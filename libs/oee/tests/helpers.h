#pragma once
#include "../../historian/tests/ManualClock.h"

#define let const auto

namespace Jde::Calendar{
	Ξ PrintTo( const Span& span, std::ostream* os )->void{ *os << ToIsoString<seconds>( span.Start ) << " to " << ToIsoString<seconds>( span.End ); }
}
namespace Jde::Opc::Oee{
	Ξ PrintTo( const Interval& row, std::ostream* os )->void{
		*os << std::format( "{} {} run {} stop {} unknown {} total {} good {} reject {}", ToIsoString<seconds>(row.Start), Chrono::ToString(row.Width), Chrono::ToString(row.Run), Chrono::ToString(row.Stop), Chrono::ToString(row.Unknown), row.Total, row.Good, row.Reject );
	}
}
//The records a group's subscription delivers, scripted, and a sink that keeps what a host would write.  Times are on
//Monday 9 March 2026, UTC.
namespace Jde::Opc::Oee::Tests{
	using namespace std::chrono;
	using Hist::Tests::ManualClock;
	using Record = Hist::Proto::HistoryRecord;
	using DataValue = Hist::Proto::DataValue;

	constexpr uint32 StateNode{ 1 }, TotalNode{ 2 }, GoodNode{ 3 }, RejectNode{ 4 }, ReasonNode{ 5 }, RateNode{ 6 };
	Ξ At( int h, int m=0, int s=0 )ι->TimePoint{ return sys_days{2026y/March/9}+hours{h}+minutes{m}+seconds{s}; }
	Ξ Iso( TimePoint t )ι->string{ return ToIsoString<seconds>( t ); }
	Ξ SameInstant( TimePoint a, TimePoint b )ι->::testing::AssertionResult{
		return a==b ? ::testing::AssertionSuccess() : ::testing::AssertionFailure() << ToIsoString( a ) << " is not " << ToIsoString( b );
	}
	#define EXPECT_TP( a, b ) EXPECT_TRUE( SameInstant(a, b) )
	//What f's BadRequest says, or empty when it didn't throw.
	Ξ Refused( std::function<void()> f )->string{
		try{
			f();
		}
		catch( const Jde::Exception& e ){
			EXPECT_EQ( e.HttpStatus(), EHttpStatus::BadRequest ) << e.what();
			return e.what();
		}
		return {};
	}

	Ξ Stamp( uint32 node, TimePoint t )ι->DataValue{
		DataValue y;
		y.set_node_index( node );
		y.set_source_ts( UADateTime{t}.UA() );
		return y;
	}
	Ξ Flag( uint32 node, TimePoint t, bool on )ι->DataValue{
		auto y = Stamp( node, t );
		y.mutable_value()->set_boolean( on );
		return y;
	}
	Ξ Count( uint32 node, TimePoint t, uint32_t count )ι->DataValue{
		auto y = Stamp( node, t );
		y.mutable_value()->set_uint32( count );
		return y;
	}
	Ξ Number( uint32 node, TimePoint t, double v )ι->DataValue{
		auto y = Stamp( node, t );
		y.mutable_value()->set_double_value( v );
		return y;
	}
	Ξ Code( uint32 node, TimePoint t, int32_t code )ι->DataValue{
		auto y = Stamp( node, t );
		y.mutable_value()->set_int32( code );
		return y;
	}
	//A status alone:  a Bad record, or the historian's Bad_DataLost marker.
	Ξ Bad( uint32 node, TimePoint t, UA_StatusCode status=UA_STATUSCODE_BADDATALOST )ι->DataValue{
		auto y = Stamp( node, t );
		y.set_status( status );
		return y;
	}
	//v again at t, as the heartbeat re-stores it.
	Ξ Beat( DataValue v, TimePoint t )ι->DataValue{
		v.set_heartbeat( v.source_ts() );
		v.set_source_ts( UADateTime{t}.UA() );
		return v;
	}
	//A signal with no record before the opening.
	Ξ NoBound( uint32 node, TimePoint from )ι->DataValue{ return Bad( node, from, UA_STATUSCODE_BADBOUNDNOTFOUND ); }
	Ξ Stream( std::initializer_list<DataValue> values )ι->vector<Record>{
		vector<Record> y;
		for( let& v : values )
			*y.emplace_back().mutable_value() = v;
		return y;
	}
	Ξ Page( std::initializer_list<DataValue> values )ι->vector<Hist::ReadValue>{
		vector<Hist::ReadValue> y;
		for( let& v : values )
			y.push_back( {v} );
		return y;
	}
	//An edit's record, as the group pushes it:  what it put at the time it targets, nothing for a delete.
	Ξ Edit( uint32 node, TimePoint target, optional<DataValue> value, Hist::Proto::UpdateType type=Hist::Proto::UPDATE_TYPE_REPLACE )ι->vector<Record>{
		vector<Record> y;
		auto& m = *y.emplace_back().mutable_modification();
		m.set_node_index( node );
		m.set_target_source_ts( UADateTime{target}.UA() );
		m.set_update_type( type );
		if( value ){
			*m.mutable_new_value() = *value;
			m.mutable_new_value()->clear_node_index();
		}
		return y;
	}

	//What a host would hold:  the interval rows as written, the stop rows as its table would read after each call, and
	//each call by name.  A row its unique index would refuse fails the test.
	struct Recorder final : ISink{
		α Closed( const Interval& row )ι->void override{
			EXPECT_EQ( row.Run+row.Stop+row.Unknown, row.Width ) << Iso( row.Start );
			if( !Rows.empty() )
				EXPECT_TP( row.Start, Rows.back().End() );
			Rows.push_back( row );
		}
		α Opened( const Stop& stop )ι->void override{
			Calls.push_back( "opened "+Text(stop) );
			EXPECT_TRUE( Stops.emplace(stop.Start, stop).second ) << "a second row at " << Iso( stop.Start );
		}
		α Closed( const Stop& stop )ι->void override{
			Calls.push_back( "closed "+Text(stop) );
			let p = Stops.find( stop.Start );
			if( p==Stops.end() || p->second.End || !stop.End )
				ADD_FAILURE() << "closed a row that was not open:  " << Text( stop );
			else
				p->second = stop;
		}
		α Rewritten( TimePoint from, const vector<Stop>& stops )ι->void override{
			auto call = "rewritten "+Iso( from );
			Stops.erase( Stops.lower_bound(from), Stops.end() );
			for( let& stop : stops ){
				call += " "+Text( stop );
				EXPECT_GE( stop.Start, from );
				Stops.emplace( stop.Start, stop );
			}
			Calls.push_back( move(call) );
		}
		α Rebuild( uint32 node, Span span )ι->void override{ Rebuilds.emplace_back( node, span ); }
		α Live( const LiveState& state )ι->void override{ States.push_back( state ); }

		//`06:02:15-06:04:45`, with `?` for unknown time and `#n` for a code.
		Ω Text( const Stop& stop )ι->string{
			auto y = std::format( "{:%T}-", floor<seconds>(stop.Start) );
			if( stop.End )
				y += std::format( "{:%T}", floor<seconds>(*stop.End) );
			if( stop.Unknown )
				y += "?";
			if( stop.Code )
				y += Ƒ( "#{}", *stop.Code );
			return y;
		}
		α Table()Ι->vector<string>{
			vector<string> y;
			for( let& [_, stop] : Stops )
				y.push_back( Text(stop) );
			return y;
		}
		α Row( TimePoint start )Ι->Interval{
			let p = std::ranges::find( Rows, start, &Interval::Start );
			if( p!=Rows.end() )
				return *p;
			ADD_FAILURE() << "no row at " << Iso( start );
			return {};
		}
		vector<Interval> Rows;
		std::map<TimePoint,Stop> Stops;
		vector<string> Calls;
		vector<std::pair<uint32,Span>> Rebuilds;
		vector<LiveState> States;
	};

	Ξ Signals( std::initializer_list<std::pair<ERole,uint32>> roles )ι->vector<Signal>{
		vector<Signal> y;
		for( let& [role, node] : roles )
			y.push_back( {role, node} );
		return y;
	}
	Ξ Sec( const Duration& d )ι->double{ return duration<double>( d ).count(); }
	Ξ Min( const Duration& d )ι->double{ return duration<double,std::ratio<60>>( d ).count(); }

	struct EngineTest : ::testing::Test{
		//An engine that opens at `from`, the clock there too.
		α Open( EngineConfig config, Opening opening )ε->Engine&{
			Time = ms<ManualClock>( opening.From );
			Machine = mu<Engine>( move(config), move(opening), Sink, Time );
			return *Machine;
		}
		//The stream as it arrives:  the clock moved to the record's time and `lag`, then the record handed over.
		α Arrive( DataValue v, Duration lag={} )ε->void{
			Time->AdvanceTo( UADateTime{UA_DateTime{v.source_ts()}}.Time()+lag );
			Machine->Records( Stream({move(v)}) );
		}
		//A page of the snapshot, read back after its records were stored:  the clock no earlier than the last of them.
		α Replay( std::initializer_list<DataValue> values )ε->void{
			for( let& v : values )
				Time->AdvanceTo( std::max(Time->Now(), UADateTime{UA_DateTime{v.source_ts()}}.Time()) );
			Machine->Replay( Page(values) );
		}
		sp<ManualClock> Time;
		sp<Recorder> Sink{ ms<Recorder>() };
		up<Engine> Machine;
	};
}