//Processed reads (#207):  Part 13's aggregates over the example historians its Annex A publishes, every expected value
//checked; then the read's own paging, reversed intervals, one interval over the range, Median, several nodes, and a
//range over day files and the buffer.
#include <cstdio>
#include <jde/opc/UAException.h>
#include "reads.h"
#include "Part13Examples.h"

#define let const auto

namespace Jde::Opc::Hist::Tests{
	using namespace std::chrono;
	constexpr StatusCode Info{ 0x400 }, Calculated{ 0x1 }, Interpolated{ 0x2 }, Partial{ 0x4 }, Multi{ 0x10 };
	constexpr StatusCode Uncertain{ 0x40000000 }, Bad{ 0x80000000 }, SubNormal{ UA_STATUSCODE_UNCERTAINDATASUBNORMAL }, NoData{ UA_STATUSCODE_BADNODATA };
	Ω bits( StatusCode status, StatusCode b )ι->StatusCode{ return status | Info | b; }

	namespace Part13{
		//A CSV line's cells, a quoted one with its commas.
		Ω cells( sv line )->vector<string>{
			vector<string> y{ "" };
			bool quoted{};
			for( let c : line ){
				if( c=='"' )
					quoted = !quoted;
				else if( c==',' && !quoted )
					y.emplace_back();
				else
					y.back() += c;
			}
			for( auto& cell : y ){
				while( !cell.empty() && cell.back()==' ' )
					cell.pop_back();
			}
			return y;
		}
		//hh:mm:ss[.fff] on the example's day.
		Ω timeOf( sv cell, TimePoint day )->TimePoint{
			unsigned h{}, m{}, s{}, ms{};
			std::sscanf( string{cell}.c_str(), "%u:%u:%u.%u", &h, &m, &s, &ms );
			return day+hours{h}+minutes{m}+seconds{s}+milliseconds{ms};
		}
		//The CSV's status cell:  the severity, then the info bits it names.
		Ω statusOf( sv cell )->StatusCode{
			StatusCode y{};
			bool info{};
			for( let& part : cells(cell) ){
				let word = part.starts_with(' ') ? sv{part}.substr(1) : sv{part};
				if( word=="Good" ) y |= UA_STATUSCODE_GOOD;
				else if( word=="Uncertain" ) y |= Uncertain;
				else if( word=="Bad" ) y |= Bad;
				else if( word=="BadNoData" || word=="Bad_NoData" ) y |= NoData;
				else if( word=="UncertainDataSubNormal" ) y |= SubNormal;
				else{
					info = true;
					if( word=="Calculated" ) y |= Calculated;
					else if( word=="Interpolated" ) y |= Interpolated;
					else if( word=="Partial" ) y |= Partial;
					else if( word=="MultipleValues" ) y |= Multi;
					else throw std::runtime_error{ Ƒ("unknown status '{}'", word) };
				}
			}
			return info ? y | Info : y;
		}
		struct Row{ TimePoint Time; optional<double> Value; optional<bool> Flag; StatusCode Status; };
		struct Table{ string Aggregate; uint Historian{}; Duration Interval{}; bool Stepped{}; AggregateConfiguration Config; vector<Row> Rows; };
		//The raw tables, one per historian, or the processed ones, one per aggregate and historian.
		Ω parse( sv text, TimePoint day )->vector<Table>{
			vector<Table> y;
			string aggregate;
			for( let line : std::views::split(text, '\n') ){
				let fields = cells( sv{line.begin(), line.end()} );
				let& key = fields[0];
				if( key=="Aggregate" )
					aggregate = fields[1];
				else if( key.starts_with("Historian") ){
					y.push_back( {aggregate} );
					y.back().Historian = std::stoul( key.substr(9) );
				}
				else if( y.empty() || fields.size()<2 )
					continue;
				else if( key=="Processing Interval" ) y.back().Interval = milliseconds{ std::stol(fields[1]) };
				else if( key=="Stepped" ) y.back().Stepped = fields[1]=="true";
				else if( key=="Treat Uncertain as Bad" ) y.back().Config.TreatUncertainAsBad = fields[1]=="true";
				else if( key=="Percent Bad" ) y.back().Config.PercentDataBad = (uint8_t)std::stoul( fields[1] );
				else if( key=="Percent Good" ) y.back().Config.PercentDataGood = (uint8_t)std::stoul( fields[1] );
				else if( key=="Use Sloped Extrapolation" ) y.back().Config.UseSlopedExtrapolation = fields[1]=="true";
				else if( key.starts_with("12:") && fields[2]!="Bad_NoData" ){//the archive's creation entry is no record.
					Row row{ timeOf(key, day), {}, {}, statusOf(fields[2]) };
					if( fields[1]=="true" || fields[1]=="false" )
						row.Flag = fields[1]=="true";
					else if( !fields[1].empty() && fields[1]!="undefined" )
						row.Value = std::stod( fields[1] );
					y.back().Rows.push_back( row );
				}
			}
			return y;
		}
		Ω aggregateOf( sv name )->EAggregate{
			static const flat_map<string,EAggregate,std::less<>> names{ {"Interpolative", EAggregate::Interpolative}, {"Average", EAggregate::Average}, {"TimeAverage", EAggregate::TimeAverage},
				{"Count", EAggregate::Count}, {"Minimum", EAggregate::Minimum}, {"Maximum", EAggregate::Maximum}, {"Start", EAggregate::Start}, {"End", EAggregate::End}, {"StandardDeviationSample", EAggregate::StandardDeviationSample} };
			return names.at( string{name} );
		}
	}

	//The five example historians as five nodes of one group, each with its Stepped, their records in the 7th's file.
	struct Processed : GatewayFiles{
		α Load()ε->void{
			Pump = AddGroup();
			for( let& t : Part13::parse(Part13::RawData, Midnight) ){
				let index = Join( *Pump, Ƒ("Historian{}", t.Historian), Thresholds{.Stepped=t.Stepped} );
				Nodes[t.Historian] = index;
				for( let& r : t.Rows ){
					let value = r.Flag ? Flag( *r.Flag, r.Time, r.Status ) : r.Value ? Graded( *r.Value, r.Status, r.Time ) : Status( r.Status, r.Time );
					ASSERT_TRUE( Pump->Enqueue(index, value) );
				}
			}
			ASSERT_TRUE( Flush(*Pump) );
		}
		α Read( ProcessedRequest request )ε->ReadResult{ return Pump->ReadProcessed( move(request) ); }
		α All( ProcessedRequest request, vector<uint>* pages=nullptr )ε->vector<ReadValue>{ return readAllProcessed( *Pump, move(request), pages ); }
		α Request( uint historian, EAggregate aggregate, Duration interval, AggregateConfiguration config={} )Ι->ProcessedRequest{
			return { .Nodes={Nodes.at(historian)}, .Start=ticks(Noon), .End=ticks(Noon+100s), .Interval=interval, .Aggregate=aggregate, .Configuration=config };
		}
		//Historian1's configuration:  Uncertain values count, and the percentages at their defaults.
		static AggregateConfiguration One()ι{ return { .TreatUncertainAsBad=false }; }
		const TimePoint Midnight{ sys_days{March7} }, Noon{ Midnight+12h };//the examples' 12:00:00.
		sp<Group> Pump;
		flat_map<uint,NodeIndex> Nodes;
	};

	//Every value the official example results give for the nine aggregates, over the five historians.
	TEST_F( Processed, Part13Examples ){
		Load();
		uint checked{};
		for( let& t : Part13::parse(Part13::Processed, Midnight) ){
			SCOPED_TRACE( Ƒ("{} historian {}", t.Aggregate, t.Historian) );
			let actual = All( Request(t.Historian, Part13::aggregateOf(t.Aggregate), t.Interval, t.Config) );
			ASSERT_EQ( actual.size(), t.Rows.size() );
			for( uint i=0; i<actual.size(); ++i ){
				SCOPED_TRACE( Ƒ("row {} at {}", i, ToIsoString(t.Rows[i].Time)) );
				let& v = actual[i].Value;
				let& e = t.Rows[i];
				EXPECT_EQ( v.node_index(), Nodes[t.Historian] );
				EXPECT_EQ( v.source_ts(), ticks(e.Time) );
				EXPECT_EQ( v.status(), e.Status ) << Ƒ( "status {:x}, expected {:x}", v.status(), e.Status );
				if( e.Value ){
					ASSERT_TRUE( numberOf(v) );
					EXPECT_NEAR( *numberOf(v), *e.Value, 0.0015 );
				}
				else
					EXPECT_FALSE( v.has_value() );
				++checked;
			}
		}
		EXPECT_EQ( checked, 407 );
	}

	//Each page holds Limit values, the intervals in order and an interval's values in the nodes' order, and resumes
	//where the last stopped, inside an interval included.  A continuation passed with other arguments is refused.
	TEST_F( Processed, PagesWithAContinuation ){
		Load();
		ProcessedRequest r{ .Nodes={Nodes[1], Nodes[5]}, .Start=ticks(Noon), .End=ticks(Noon+100s), .Interval=5s, .Aggregate=EAggregate::Average, .Configuration=One() };
		let whole = All( r );
		ASSERT_EQ( whole.size(), 40 );
		for( uint i=0; i<whole.size(); ++i ){
			EXPECT_EQ( whole[i].Value.node_index(), i%2 ? Nodes[5] : Nodes[1] ) << i;
			EXPECT_EQ( whole[i].Value.source_ts(), ticks(Noon+seconds{5*(i/2)}) ) << i;
		}
		for( let limit : {1u, 3u, 7u, 40u, 100u} ){
			vector<uint> pages;
			r.Limit = limit;
			let paged = All( r, &pages );
			ASSERT_EQ( paged.size(), whole.size() ) << limit;
			for( uint i=0; i<whole.size(); ++i )
				EXPECT_EQ( paged[i].Value.ShortDebugString(), whole[i].Value.ShortDebugString() ) << limit << " " << i;
			for( uint i=0; i+1<pages.size(); ++i )
				EXPECT_EQ( pages[i], std::min(limit, 40u) ) << limit;
		}
		r.Limit = 3;
		auto page = Read( r );
		ASSERT_EQ( page.Values.size(), 3 );
		ASSERT_FALSE( page.Continuation.empty() );
		let c = continuation( page );
		EXPECT_EQ( c.next(), 1 );
		ASSERT_EQ( c.counts_size(), 2 );
		EXPECT_EQ( c.counts(0), 1 );//the second interval's first node is on the page.
		EXPECT_EQ( c.counts(1), 0 );
		for( uint i=0; i<6; ++i ){
			auto other = r;
			switch( i ){
			case 0: other.Nodes = {Nodes[5], Nodes[1]}; break;
			case 1: other.End = ticks( Noon+95s ); break;
			case 2: other.Interval = 10s; break;
			case 3: other.Aggregate = EAggregate::Count; break;
			case 4: other.Configuration.TreatUncertainAsBad = true; break;
			case 5: other.Start = ticks( Noon+1s ); break;
			}
			other.Continuation = page.Continuation;
			EXPECT_THROW( Read(other), UAException ) << i;
		}
		r.Continuation = "not a continuation";
		EXPECT_THROW( Read(r), UAException );
		r.Continuation = page.Continuation;
		r.Limit = 100;
		let rest = Read( r );
		EXPECT_EQ( rest.Values.size(), 37 );
		EXPECT_TRUE( rest.Continuation.empty() );
	}

	//With Start after End the intervals run back from Start, each (Lo, Hi] stamped Hi, so the later time is in and the
	//earlier out, and the last holds the rest of the range.
	TEST_F( Processed, ReversedIntervals ){
		Load();
		let v = All( {.Nodes={Nodes[1]}, .Start=ticks(Noon+100s), .End=ticks(Noon), .Interval=16s, .Aggregate=EAggregate::Count, .Configuration=One()} );
		ASSERT_EQ( v.size(), 7 );
		EXPECT_EQ( sources(v), (vector<Ticks>{ticks(Noon+100s), ticks(Noon+84s), ticks(Noon+68s), ticks(Noon+52s), ticks(Noon+36s), ticks(Noon+20s), ticks(Noon+4s)}) );
		EXPECT_EQ( statuses(v), (vector<StatusCode>{bits(0, Calculated|Partial), bits(SubNormal, Calculated), bits(0, Calculated), bits(SubNormal, Calculated), bits(0, Calculated), bits(0, Calculated|Partial), NoData}) );
		vector<double> counts;
		for( uint i=0; i<6; ++i )
			counts.push_back( *numberOf(v[i].Value) );
		EXPECT_EQ( counts, (vector<double>{1, 1, 1, 1, 1, 2}) );
		EXPECT_FALSE( v[6].Value.has_value() );

		//The value at the beginning of a reversed interval is at its later end:  a raw value there, else the one
		//interpolated from the records either side, extrapolated past the last.
		let at = All( {.Nodes={Nodes[1]}, .Start=ticks(Noon+100s), .End=ticks(Noon), .Interval=5s, .Aggregate=EAggregate::Interpolative, .Configuration=One()} );
		ASSERT_EQ( at.size(), 20 );
		EXPECT_EQ( at[0].Value.source_ts(), ticks(Noon+100s) );
		EXPECT_EQ( at[0].Value.status(), bits(SubNormal, Interpolated) );
		EXPECT_EQ( *numberOf(at[0].Value), 90 );
		EXPECT_EQ( at[2].Value.status(), 0 );//12:01:30's raw value.
		EXPECT_EQ( *numberOf(at[3].Value), 85 );
		EXPECT_EQ( at[3].Value.status(), bits(0, Interpolated) );
		EXPECT_EQ( at[19].Value.status(), NoData );//12:00:05.
	}

	//An interval of 0, or one the range doesn't hold, is one interval over the whole range.  A start equal to the end,
	//a negative interval or percentages that can't be met are refused.
	TEST_F( Processed, OneIntervalOverTheRange ){
		Load();
		for( let interval : {Duration::zero(), Duration{100s}, Duration{1h}} ){
			let v = All( Request(1, EAggregate::Average, interval, One()) );
			ASSERT_EQ( v.size(), 1 ) << interval;
			EXPECT_EQ( v[0].Value.source_ts(), ticks(Noon) );
			EXPECT_NEAR( *numberOf(v[0].Value), 340.0/7, 1e-9 );
			EXPECT_EQ( v[0].Value.status(), bits(SubNormal, Calculated) );
		}
		EXPECT_THROW( Read({.Nodes={Nodes[1]}, .Start=ticks(Noon), .End=ticks(Noon), .Interval=5s}), UAException );
		EXPECT_THROW( Read({.Nodes={Nodes[1]}, .Start=ticks(Noon), .End=ticks(Noon+100s), .Interval=-5s}), Exception );
		EXPECT_THROW( Read({.Nodes={}, .Start=ticks(Noon), .End=ticks(Noon+100s), .Interval=5s}), Exception );
		EXPECT_THROW( Read({.Nodes={Nodes[1]}, .Start=ticks(Noon), .End=ticks(Noon+100s), .Interval=5s, .Configuration={.PercentDataBad=80, .PercentDataGood=0}}), UAException );
		EXPECT_THROW( Read({.Nodes={Nodes[1]}, .Start=ticks(Noon), .End=ticks(Noon+100s), .Interval=5s, .Configuration={.PercentDataBad=101}}), UAException );
	}

	//Median takes the Good values as Average does, the middle one, or the mean of the two middle ones, and is Uncertain
	//where any value was left out.
	TEST_F( Processed, Median ){
		Load();
		let whole = All( Request(1, EAggregate::Median, 0s, One()) );
		ASSERT_EQ( whole.size(), 1 );
		EXPECT_EQ( *numberOf(whole[0].Value), 50 );
		EXPECT_EQ( whole[0].Value.status(), bits(SubNormal, Calculated|Partial) );
		let v = All( Request(1, EAggregate::Median, 40s, One()) );
		ASSERT_EQ( v.size(), 3 );
		EXPECT_EQ( doubles(v), (vector<double>{20, 55, 85}) );
		EXPECT_EQ( statuses(v), (vector<StatusCode>{bits(0, Calculated|Partial), bits(SubNormal, Calculated), bits(0, Calculated|Partial)}) );
		let booleans = All( Request(4, EAggregate::Median, 0s) );
		ASSERT_EQ( booleans.size(), 1 );
		EXPECT_EQ( booleans[0].Value.status(), UA_STATUSCODE_BADAGGREGATEINVALIDINPUTS );
	}

	//Several nodes:  each interval's values come in the nodes' order, each node under its own Stepped.
	TEST_F( Processed, SeveralNodes ){
		Load();
		let v = All( {.Nodes={Nodes[3], Nodes[1]}, .Start=ticks(Noon), .End=ticks(Noon+100s), .Interval=16s, .Aggregate=EAggregate::Count, .Configuration=One()} );
		ASSERT_EQ( v.size(), 14 );
		for( uint i=0; i<v.size(); ++i ){
			EXPECT_EQ( v[i].Value.node_index(), i%2 ? Nodes[1] : Nodes[3] ) << i;
			EXPECT_EQ( v[i].Value.source_ts(), ticks(Noon+seconds{16*(i/2)}) ) << i;
		}
		let at = All( {.Nodes={Nodes[3], Nodes[1]}, .Start=ticks(Noon+45s), .End=ticks(Noon+50s), .Interval=5s, .Aggregate=EAggregate::Interpolative, .Configuration=One()} );
		ASSERT_EQ( at.size(), 2 );
		EXPECT_EQ( *numberOf(at[0].Value), 30 );//stepped from 12:00:39, past the Bad 12:00:42.
		EXPECT_EQ( at[0].Value.status(), bits(SubNormal, Interpolated) );
		EXPECT_EQ( *numberOf(at[1].Value), 45 );//sloped between 12:00:30 and 12:00:50, past the Bad 12:00:40.
		EXPECT_EQ( at[1].Value.status(), bits(SubNormal, Interpolated) );
	}

	//A range over an archive, a live file and the buffer:  each record is counted once, the hours with none are
	//Bad_NoData, and the last interval with data is partial, as is the uneven last one.  The bounds before and after the
	//range come from the files either side.
	TEST_F( Processed, AcrossDaysAndTheBuffer ){
		Pump = AddGroup();
		let speed = Join( *Pump, "Pump1.Speed" );
		let t0 = Time->Now();
		let eighth = sys_days{March8};
		DataChange( *Pump, speed, 1, t0+1s );
		DataChange( *Pump, speed, 2, t0+2s );
		EXPECT_TRUE( Flush(*Pump) );
		Time->AdvanceTo( eighth+1min );
		Settle( *Pump );
		DataChange( *Pump, speed, 3, eighth+2min );
		EXPECT_TRUE( Flush(*Pump) );
		Time->AdvanceTo( eighth+3min );
		DataChange( *Pump, speed, 4, eighth+3min );
		let v = All( {.Nodes={speed}, .Start=ticks(t0), .End=ticks(eighth+4min), .Interval=1h, .Aggregate=EAggregate::Count} );
		ASSERT_EQ( v.size(), 8 );
		EXPECT_EQ( *numberOf(v[0].Value), 2 );
		EXPECT_EQ( v[0].Value.status(), bits(0, Calculated|Partial) );
		for( uint i=1; i<7; ++i )
			EXPECT_EQ( v[i].Value.status(), NoData ) << i;
		EXPECT_EQ( *numberOf(v[7].Value), 2 );
		EXPECT_EQ( v[7].Value.status(), bits(0, Calculated|Partial) );
		EXPECT_EQ( v[7].Value.source_ts(), ticks(eighth) );

		let at = All( {.Nodes={speed}, .Start=ticks(t0), .End=ticks(eighth+4min), .Interval=1h, .Aggregate=EAggregate::Interpolative} );
		ASSERT_EQ( at.size(), 8 );
		EXPECT_EQ( at[0].Value.status(), NoData );
		for( uint i=1; i<8; ++i ){
			EXPECT_EQ( *numberOf(at[i].Value), 2 ) << i;
			EXPECT_EQ( at[i].Value.status(), bits(0, Interpolated) ) << i;
		}
		let average = All( {.Nodes={speed}, .Start=ticks(eighth+2min), .End=ticks(eighth+4min), .Interval=1min, .Aggregate=EAggregate::TimeAverage} );
		ASSERT_EQ( average.size(), 2 );
		EXPECT_EQ( *numberOf(average[0].Value), 3.5 );//3 at 2min, 4 at 3min, sloped.
		EXPECT_EQ( average[0].Value.status(), bits(0, Calculated) );
		EXPECT_EQ( *numberOf(average[1].Value), 4 );//extrapolated past the buffer's value.
		EXPECT_EQ( average[1].Value.status(), bits(SubNormal, Calculated|Partial) );
	}
}