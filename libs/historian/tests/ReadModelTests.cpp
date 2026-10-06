//The model-based test (#205):  a naive oracle keeps every record the group buffers, in memory, and random sequences of
//values, late records, breaks and restarts go to the library, every read then compared with what the oracle answers -
//forward, reverse and open-ended, with and without bounds, paged at every size.  The oracle takes each record as the
//group buffers it, so it holds what the library decided to store, and the comparison is of the read path alone:
//compression and gaps have their own suites.  #206 and #207 extend it with edits and at-time reads.
#include <random>
#include <absl/container/flat_hash_set.h>
#include "reads.h"

#define let const auto

namespace Jde::Opc::Hist::Tests{
	using namespace std::chrono;
	using Proto::HistoryRecord;

	namespace{
		Ω join( const auto& values )->string{
			string y;
			for( let& v : values )
				y += Ƒ( "{}{}", y.empty() ? "" : ",", v );
			return y;
		}
		struct Oracle final{
			struct Entry{ HistoryRecord Record; uint Sequence; Ticks Time; };
			α Add( HistoryRecord&& r )ι->void{
				let t = *PrimaryTime( r );
				Entries.push_back( {move(r), (uint)Entries.size(), t} );
			}
			//The node's newest value, a marker included.
			α Newest( NodeIndex index )Ι->optional<Ticks>{
				optional<Ticks> y;
				for( let& e : Entries ){
					if( e.Record.has_value() && e.Record.value().node_index()==index )
						y = std::max( y.value_or(e.Time), e.Time );
				}
				return y;
			}
			//What a read of request returns, every page together.
			α Expected( const ReadRequest& r )Ι->vector<ReadValue>{
				let reverse = ( r.Start && r.End && *r.Start>*r.End ) || !r.Start;
				let earlier = r.Start && r.End ? ( reverse ? r.End : r.Start ) : r.Start;
				let later = r.Start && r.End ? ( reverse ? r.Start : r.End ) : r.End;
				let wants = [&]( NodeIndex index ){ return std::ranges::contains( r.Nodes, index ); };
				//Each record of the nodes in order:  by time, then as buffered.
				vector<const Entry*> values;
				for( let& e : Entries ){
					if( e.Record.has_value() && wants(e.Record.value().node_index()) )
						values.push_back( &e );
				}
				std::ranges::stable_sort( values, {}, &Entry::Time );
				vector<std::pair<const Entry*,bool>> data;
				for( let e : values ){
					if( (!earlier || e->Time>=*earlier) && (!later || e->Time<=*later) )
						data.emplace_back( e, false );
				}
				//A record at an end serves as its node's bound there:  the first at the earlier end, the last at the later.
				absl::flat_hash_set<NodeIndex> atEarlier, atLater;
				if( r.Bounds ){
					for( auto& [e,bound] : data ){
						if( earlier && e->Time==*earlier && atEarlier.insert(e->Record.value().node_index()).second )
							bound = true;
					}
					for( auto& [e,bound] : data | std::views::reverse ){
						if( later && e->Time==*later && atLater.insert(e->Record.value().node_index()).second )
							bound = true;
					}
				}
				if( reverse )
					std::ranges::reverse( data );
				vector<ReadValue> y;
				let separate = [&]( Ticks at, bool before, const absl::flat_hash_set<NodeIndex>& served ){
					vector<std::pair<Ticks,ReadValue>> found;
					for( let index : r.Nodes ){
						if( served.contains(index) )
							continue;
						optional<Proto::DataValue> bound;
						Ticks t{ at };
						if( before ){//the latest before at, unless a NodeRemoved is later still.
							const Entry* latest{};
							bool removed{};
							for( let& e : Entries ){
								if( e.Time>=at )
									continue;
								if( e.Record.has_value() && e.Record.value().node_index()==index ){
									if( !latest || e.Time>=latest->Time ){
										latest = &e;
										removed = false;
									}
								}
								else if( e.Record.has_node_removed() && e.Record.node_removed().node_index()==index && (!latest || e.Time>=latest->Time) )
									removed = true;
							}
							if( latest && !removed ){
								bound = latest->Record.value();
								t = latest->Time;
							}
						}
						else{//the earliest after at.
							const Entry* earliest{};
							for( let& e : Entries ){
								if( e.Time>at && e.Record.has_value() && e.Record.value().node_index()==index && (!earliest || e.Time<earliest->Time) )
									earliest = &e;
							}
							if( earliest ){
								bound = earliest->Record.value();
								t = earliest->Time;
							}
						}
						if( !bound ){
							bound.emplace();
							bound->set_node_index( index );
							bound->set_status( UA_STATUSCODE_BADBOUNDNOTFOUND );
							bound->set_source_ts( at );
						}
						found.emplace_back( t, ReadValue{move(*bound), true} );
					}
					std::ranges::stable_sort( found, [reverse]( let& a, let& b ){ return reverse ? a.first>b.first : a.first<b.first; } );
					for( auto& [_,v] : found )
						y.push_back( move(v) );
				};
				if( r.Bounds ){
					if( reverse && later )
						separate( *later, false, atLater );
					else if( !reverse && earlier )
						separate( *earlier, true, atEarlier );
				}
				for( let& [e,bound] : data )
					y.push_back( {e->Record.value(), bound} );
				if( r.Bounds ){
					if( reverse && earlier )
						separate( *earlier, true, atEarlier );
					else if( !reverse && later )
						separate( *later, false, atLater );
				}
				return y;
			}
			vector<Entry> Entries;
		};
		Ω describe( const ReadValue& v )->string{ return Ƒ( "{}{}", v.Bound ? "bound " : "", v.Value.ShortDebugString() ); }
		Ω describe( const ReadRequest& r )->string{
			let t = []( const optional<UA_DateTime>& t ){ return t ? ToIsoString( UADateTime{*t}.Time() ) : "none"; };
			return Ƒ( "nodes {} start {} end {} bounds {} limit {}", join(r.Nodes), t(r.Start), t(r.End), r.Bounds, r.Limit );
		}
	}

	struct ReadModel : GatewayFiles{
		//Every record the group has buffered since the last look.
		α Capture()ε->void{
			auto buffer = Pump->Buffer();
			ASSERT_GE( buffer.size(), _baseline );
			for( uint i=_baseline; i<buffer.size(); ++i )
				Model.Add( ToProto(buffer[i]) );
			_baseline = buffer.size();
		}
		//Around the clock running, which may flush the buffer:  everything buffered is captured first, and after, with
		//the flushes it started settled, so none takes the buffer between a step and its capture, the buffer is as it was
		//or taken.
		α Advance( TimePoint to )ε->void{
			Capture();
			Time->AdvanceTo( to );
			Settle( *Pump );
			let size = Pump->Buffer().size();
			ASSERT_LE( size, _baseline );
			_baseline = size;
		}
		α Seconds( uint most )ι->seconds{ return seconds{ std::uniform_int_distribution<uint>{0, most}(Rng) }; }
		α Chance( uint percent )ι->bool{ return std::uniform_int_distribution<uint>{1, 100}(Rng)<=percent; }
		α AnyNode()ι->NodeIndex{ return Indexes[std::uniform_int_distribution<uint>{0, (uint)Indexes.size()-1}(Rng)]; }
		α Value()ι->double{ return (double)++_value; }
		//A value sampled up to a few seconds ago, in whole seconds so times repeat, and never before the day began.
		α Sampled()ι->TimePoint{ return std::max<TimePoint>( floor<seconds>(Time->Now())-Seconds(3), DayStart(DayOf(Time->Now(), utc()), utc()) ); }

		α Step()ε->void{
			let roll = std::uniform_int_distribution<uint>{ 1, 100 }( Rng );
			let node = AnyNode();
			if( roll<=45 )
				DataChange( *Pump, node, Value(), Sampled() );
			else if( roll<=60 ){//a late record:  inside the day of the node's newest, so no later file's start value goes stale.
				if( let newest = Model.Newest(node); newest && !Pump->FindBreak(node) ){
					let dayStart = DayStart( DayOf(UADateTime{*newest}.Time(), utc()), utc() );
					let span = duration_cast<seconds>( UADateTime{*newest}.Time()-dayStart ).count();
					DataChange( *Pump, node, Value(), dayStart+seconds{ std::uniform_int_distribution<int64_t>{0, std::max<int64_t>(span, 0)}(Rng) } );
				}
			}
			else if( roll<=80 ){//the clock, now and then past midnight and its rewrite.
				if( Chance(10) )
					Advance( NextDayStart(Time->Now(), utc())+1min+Seconds(30) );
				else
					Advance( Time->Now()+Seconds(600)+1s );
			}
			else if( roll<=92 ){//a break, with a value in flight now and then.
				Pump->Disconnected( Time->Now() );
				Advance( Time->Now()+Seconds(20) );
				if( Chance(30) )
					DataChange( *Pump, node, Value(), Sampled() );
				Pump->Connected();
				Reconnected();
			}
			else{//the process stopping and starting again.
				Capture();
				Restart();
				Pump = Rejoin( Name, Members );
				_baseline = 0;
				Advance( Time->Now()+Seconds(120) );
				Reconnected();
			}
			Capture();
		}
		//The host reads each node once its connection is back, and at a start:  so each node's break is answered, and its
		//marker lands, before a later day's file takes the node's start value.  A node left quiet across a midnight would
		//put its marker, at the break, behind a start value already written, which step 5's corrections mend.
		α Reconnected()ι->void{
			for( let index : Indexes )
				DataChange( *Pump, index, Value(), Sampled() );
		}
		α Times()Ι->vector<Ticks>{
			vector<Ticks> y{ ticks(Time->Now()), ticks(Time->Now()-1h) };
			for( let& e : Model.Entries )
				y.push_back( e.Time );
			return y;
		}
		α Request()ι->ReadRequest{
			let times = Times();
			let pick = [&]{
				let t = times[std::uniform_int_distribution<uint>{0, (uint)times.size()-1}(Rng)];
				let nudge = std::uniform_int_distribution<int>{ -2, 2 }( Rng );
				return nudge==2 ? t+ticks( seconds{1} ) : nudge==-2 ? t-ticks( seconds{1} ) : t+nudge;
			};
			ReadRequest r;
			for( let index : Indexes ){
				if( Chance(60) )
					r.Nodes.push_back( index );
			}
			if( r.Nodes.empty() )
				r.Nodes.push_back( AnyNode() );
			std::ranges::shuffle( r.Nodes, Rng );
			let shape = std::uniform_int_distribution<uint>{ 1, 10 }( Rng );
			if( shape<=6 ){
				r.Start = pick();
				r.End = pick();
				if( shape==6 )
					r.End = r.Start;
			}
			else if( shape<=8 )
				r.Start = pick();
			else
				r.End = pick();
			r.Bounds = Chance( 50 );
			constexpr uint limits[]{ 1, 2, 3, 5, 8, 1000 };
			r.Limit = limits[std::uniform_int_distribution<uint>{0, 5}(Rng)];
			return r;
		}
		α Compare( const ReadRequest& r )ε->void{
			let expected = Model.Expected( r );
			vector<uint> pages;
			let actual = readAll( *Pump, r, &pages );
			SCOPED_TRACE( describe(r) );
			let dump = [&]{
				string y{ Ƒ("pages {}; now {}\nexpected:\n", join(pages), ToIsoString(Time->Now())) };
				for( let& v : expected )
					y += describe( v )+"\n";
				y += "actual:\n";
				for( let& v : actual )
					y += describe( v )+"\n";
				y += Ƒ( "buffered {}:\n", Pump->Buffer().size() );
				for( let& e : Model.Entries )
					y += Ƒ( "{} {}\n", e.Sequence, e.Record.ShortDebugString() );
				return y;
			};
			ASSERT_EQ( actual.size(), expected.size() ) << dump();
			for( uint i=0; i<actual.size(); ++i )
				EXPECT_EQ( describe(actual[i]), describe(expected[i]) ) << i;
			let most = std::max<uint>( r.Limit, r.Nodes.size() );
			for( uint i=0; i<pages.size(); ++i ){
				EXPECT_LE( pages[i], i+1==pages.size() ? most+r.Nodes.size() : most ) << join( pages );
				EXPECT_GE( pages[i], i+1==pages.size() ? 0u : 1u ) << join( pages );
			}
		}
		α Run( uint64_t seed, uint steps )ε->void{
			Rng.seed( seed );
			Model = {};
			Members.clear();
			Indexes.clear();
			_baseline = 0;
			Pump = AddGroup();
			Name = Pump->Name();
			for( let id : {"Pump1.Speed", "Pump1.Temp", "Pump1.Flow"} ){
				let index = Join( *Pump, id );
				Indexes.push_back( index );
				Members.push_back( {Node(id), {}, index} );
			}
			Capture();
			for( uint step=0; step<steps && !HasFatalFailure(); ++step ){
				Step();
				if( step%12==11 ){
					for( uint i=0; i<6 && !HasFailure(); ++i )
						Compare( Request() );
				}
			}
			ASSERT_FALSE( HasFailure() ) << "seed " << seed;
		}

		std::mt19937_64 Rng;
		Oracle Model;
		sp<Group> Pump;
		string Name;
		vector<Member> Members;
		vector<NodeIndex> Indexes;
	private:
		uint _baseline{};
		uint _value{};
	};

	TEST_F( ReadModel, MatchesTheOracle ){
		for( uint64_t seed=1; seed<=6 && !HasFailure(); ++seed ){
			SCOPED_TRACE( Ƒ("seed {}", seed) );
			if( seed>1 )
				Restart();
			Run( seed, 240 );
		}
	}
}