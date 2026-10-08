//The model-based test (#205, #206):  a naive oracle keeps every record the group buffers, in memory, and random
//sequences of values, late records, breaks, restarts and edits go to the library, every read then compared with what
//the oracle answers - forward, reverse and open-ended, with and without bounds, raw and modified, paged at every size.
//The oracle takes each record as the group buffers it, so it holds what the library decided to store, and applies each
//edit literally to the series as it stands, predicting what the library should answer it; so the comparison is of the
//read and edit paths alone:  compression and gaps have their own suites.  #207 extends it with at-time reads.
#include <random>
#include <absl/container/flat_hash_set.h>
#include "reads.h"

#define let const auto

namespace Jde::Opc::Hist::Tests{
	using namespace std::chrono;
	using Proto::HistoryRecord;

	Ω join( const auto& values )->string{
		string y;
		for( let& v : values )
			y += Ƒ( "{}{}", y.empty() ? "" : ",", v );
		return y;
	}
	Ω userName()->string{ return "admin"; }
	struct Oracle final{
		struct Entry{ HistoryRecord Record; uint Sequence; Ticks Time; };
		//An edit as made:  the record at (Node, Time) it matched, the value it put, and when.
		struct Edit{ NodeIndex Node; Ticks Time; Proto::UpdateType Type; optional<Proto::DataValue> Original; optional<Proto::DataValue> New; Ticks Ts; };
		using Series = std::map<Ticks,vector<Proto::DataValue>>;
		α Add( HistoryRecord&& r )ι->void{
			let t = *PrimaryTime( r );
			Entries.push_back( {move(r), (uint)Entries.size(), t} );
		}
		//The nodes' records as the edits leave them:  the raw records by time, then as buffered, and at each time the
		//edits there applied in the order they were made - an INSERT appends its value, a REPLACE or UPDATE puts its value
		//in place of the last record of its node there that is its original, or appends it when it has none, and a DELETE
		//removes that record.
		α Replayed( const std::function<bool( NodeIndex )>& wants )Ι->Series{
			vector<const Entry*> values;
			for( let& e : Entries ){
				if( e.Record.has_value() && wants(e.Record.value().node_index()) )
					values.push_back( &e );
			}
			std::ranges::stable_sort( values, {}, &Entry::Time );
			Series y;
			for( let e : values )
				y[e->Time].push_back( e->Record.value() );
			for( let& edit : Edits ){
				if( wants(edit.Node) )
					Apply( y[edit.Time], edit );
			}
			std::erase_if( y, []( let& at ){ return at.second.empty(); } );
			return y;
		}
		Ω Apply( vector<Proto::DataValue>& at, const Edit& edit )ι->void{
			auto target = at.end();
			if( edit.Original ){
				for( auto p = at.rbegin(); p!=at.rend(); ++p ){
					if( p->node_index()==edit.Node && Same(*p, *edit.Original) ){
						target = std::prev( p.base() );
						break;
					}
				}
			}
			auto value = edit.New.value_or( Proto::DataValue{} );
			value.set_node_index( edit.Node );
			if( edit.Type==Proto::UPDATE_TYPE_DELETE ){
				if( target!=at.end() )
					at.erase( target );
			}
			else if( target!=at.end() && edit.Type!=Proto::UPDATE_TYPE_INSERT )
				*target = value;
			else
				at.push_back( value );
		}
		//The node's newest value, a marker included, as the edits leave it.
		α Newest( NodeIndex index )Ι->optional<Ticks>{
			let series = Replayed( [index]( NodeIndex i ){ return i==index; } );
			return series.empty() ? optional<Ticks>{} : series.rbegin()->first;
		}
		//An edit applied literally to the series as it stands:  the results the library should answer with, and the
		//edits it should have written.
		α Edited( const vector<EditDetails>& details, Ticks ts )ι->vector<EditResult>{
			vector<EditResult> y( details.size() );
			for( uint e=0; e<details.size(); ++e ){
				let node = std::visit( []( let& d ){ return d.Node; }, details[e] );
				auto series = Replayed( [node]( NodeIndex i ){ return i==node; } );
				let made = [&]( Ticks t, Proto::UpdateType type, const Proto::DataValue* original, const Proto::DataValue* value ){
					Edit edit{ node, t, type, {}, {}, ts };
					if( original ){
						edit.Original = *original;
						edit.Original->clear_node_index();
					}
					if( value ){
						edit.New = *value;
						edit.New->clear_node_index();
					}
					Edits.push_back( move(edit) );
				};
				if( let update = get_if<UpdateData>(&details[e]) ){
					for( let& v : update->Values ){
						let t = v.sourceTimestamp;
						auto stored = ToProto( v, node );
						auto& at = series[t];
						let insert = update->Type==UA_PERFORMUPDATETYPE_INSERT, replace = update->Type==UA_PERFORMUPDATETYPE_REPLACE;
						if( insert && !at.empty() ){
							y[e].Results.push_back( UA_STATUSCODE_BADENTRYEXISTS );
							continue;
						}
						if( replace && at.empty() ){
							y[e].Results.push_back( UA_STATUSCODE_BADNOENTRYEXISTS );
							continue;
						}
						let replacing = !at.empty() && !insert;
						made( t, insert ? Proto::UPDATE_TYPE_INSERT : replace ? Proto::UPDATE_TYPE_REPLACE : Proto::UPDATE_TYPE_UPDATE, replacing ? &at.back() : nullptr, &stored );
						if( replacing )
							at.back() = stored;
						else
							at.push_back( stored );
						y[e].Results.push_back( replacing ? UA_STATUSCODE_GOODENTRYREPLACED : UA_STATUSCODE_GOODENTRYINSERTED );
					}
				}
				else if( let erase = get_if<DeleteRaw>(&details[e]) ){
					uint count{};
					for( auto& [t,at] : series ){
						if( t<erase->Start || t>erase->End )
							continue;
						for( let& original : at )
							made( t, Proto::UPDATE_TYPE_DELETE, &original, nullptr );
						count += at.size();
						at.clear();
					}
					y[e].Status = count ? UA_STATUSCODE_GOOD : UA_STATUSCODE_BADNODATA;
				}
				else{
					for( let t : get<DeleteAtTime>(details[e]).Times ){
						auto& at = series[t];
						if( at.empty() ){
							y[e].Results.push_back( UA_STATUSCODE_BADNOENTRYEXISTS );
							continue;
						}
						for( let& original : at )
							made( t, Proto::UPDATE_TYPE_DELETE, &original, nullptr );
						at.clear();
						y[e].Results.push_back( UA_STATUSCODE_GOOD );
					}
				}
			}
			return y;
		}
		//What a modified read of request returns:  each edit of the nodes in its range, by the time it targets, then as
		//made, with the value it inserted or replaced.
		α ExpectedModified( const ReadRequest& r )Ι->vector<ReadValue>{
			let reverse = ( r.Start && r.End && *r.Start>*r.End ) || !r.Start;
			let earlier = r.Start && r.End ? ( reverse ? r.End : r.Start ) : r.Start;
			let later = r.Start && r.End ? ( reverse ? r.Start : r.End ) : r.End;
			vector<const Edit*> edits;
			for( let& edit : Edits ){
				if( std::ranges::contains(r.Nodes, edit.Node) && (!earlier || edit.Time>=*earlier) && (!later || edit.Time<=*later) )
					edits.push_back( &edit );
			}
			std::ranges::stable_sort( edits, {}, &Edit::Time );
			if( reverse )
				std::ranges::reverse( edits );
			vector<ReadValue> y;
			for( let edit : edits ){
				let inserted = edit->Type==Proto::UPDATE_TYPE_INSERT || !edit->Original;
				auto value = inserted ? *edit->New : *edit->Original;
				value.set_node_index( edit->Node );
				y.push_back( {move(value), false, ModificationInfo{edit->Ts, edit->Type, userName(), 7}} );
			}
			return y;
		}
		//What a read of request returns, every page together.
		α Expected( const ReadRequest& r )Ι->vector<ReadValue>{
			if( r.Modified )
				return ExpectedModified( r );
			let reverse = ( r.Start && r.End && *r.Start>*r.End ) || !r.Start;
			let earlier = r.Start && r.End ? ( reverse ? r.End : r.Start ) : r.Start;
			let later = r.Start && r.End ? ( reverse ? r.Start : r.End ) : r.End;
			let wants = [&]( NodeIndex index ){ return std::ranges::contains( r.Nodes, index ); };
			//Each record of the nodes in order:  by time, then as buffered, the edits applied.
			let series = Replayed( wants );
			vector<std::pair<const Proto::DataValue*,bool>> data;
			vector<std::pair<Ticks,const Proto::DataValue*>> all;
			for( let& [t,at] : series ){
				for( let& v : at ){
					all.emplace_back( t, &v );
					if( (!earlier || t>=*earlier) && (!later || t<=*later) )
						data.emplace_back( &v, false );
				}
			}
			let timeOf = [&]( const Proto::DataValue* v ){ return *PrimaryTime( *v ); };
			//A record at an end serves as its node's bound there:  the first at the earlier end, the last at the later.
			absl::flat_hash_set<NodeIndex> atEarlier, atLater;
			if( r.Bounds ){
				for( auto& [v,bound] : data ){
					if( earlier && timeOf(v)==*earlier && atEarlier.insert(v->node_index()).second )
						bound = true;
				}
				for( auto& [v,bound] : data | std::views::reverse ){
					if( later && timeOf(v)==*later && atLater.insert(v->node_index()).second )
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
						const Proto::DataValue* latest{};
						Ticks latestTime{};
						for( let& [time,v] : all ){
							if( time<at && v->node_index()==index && (!latest || time>=latestTime) ){
								latest = v;
								latestTime = time;
							}
						}
						bool removed{};
						for( let& e : Entries ){
							if( e.Time<at && e.Record.has_node_removed() && e.Record.node_removed().node_index()==index && (!latest || e.Time>=latestTime) )
								removed = true;
						}
						if( latest && !removed ){
							bound = *latest;
							t = latestTime;
						}
					}
					else{//the earliest after at.
						const Proto::DataValue* earliest{};
						Ticks earliestTime{};
						for( let& [time,v] : all ){
							if( time>at && v->node_index()==index && (!earliest || time<earliestTime) ){
								earliest = v;
								earliestTime = time;
							}
						}
						if( earliest ){
							bound = *earliest;
							t = earliestTime;
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
			for( let& [v,bound] : data )
				y.push_back( {*v, bound} );
			if( r.Bounds ){
				if( reverse && earlier )
					separate( *earlier, true, atEarlier );
				else if( !reverse && later )
					separate( *later, false, atLater );
			}
			return y;
		}
		vector<Entry> Entries;
		vector<Edit> Edits;
	};
	Ω describe( const ReadValue& v )->string{
		let m = v.Modification ? Ƒ( "{} {} {} {} ", v.Modification->Time, (int)v.Modification->Type, v.Modification->UserName, v.Modification->IdentityId.value_or(0) ) : string{};
		return Ƒ( "{}{}{}", v.Bound ? "bound " : "", m, v.Value.ShortDebugString() );
	}
	Ω describe( const ReadRequest& r )->string{
		let t = []( const optional<UA_DateTime>& t ){ return t ? ToIsoString( UADateTime{*t}.Time() ) : "none"; };
		return Ƒ( "nodes {} start {} end {} bounds {} modified {} limit {}", join(r.Nodes), t(r.Start), t(r.End), r.Bounds, r.Modified, r.Limit );
	}
	Ω describe( const EditResult& r )->string{
		string y{ Ƒ("{:x}:", r.Status) };
		for( let s : r.Results )
			y += Ƒ( " {:x}", s );
		return y;
	}
	Ω describe( const EditDetails& d )->string{
		let t = []( UA_DateTime t ){ return ToIsoString( UADateTime{t}.Time() ); };
		if( let update = get_if<UpdateData>(&d) ){
			string times;
			for( let& v : update->Values )
				times += Ƒ( " {}", t(v.sourceTimestamp) );
			return Ƒ( "node {} {}{}", update->Node, update->Type==UA_PERFORMUPDATETYPE_INSERT ? "insert" : update->Type==UA_PERFORMUPDATETYPE_REPLACE ? "replace" : "update", times );
		}
		if( let erase = get_if<DeleteRaw>(&d) )
			return Ƒ( "node {} delete {} - {}", erase->Node, t(erase->Start), t(erase->End) );
		string times;
		for( let time : get<DeleteAtTime>(d).Times )
			times += Ƒ( " {}", t(time) );
		return Ƒ( "node {} delete at{}", get<DeleteAtTime>(d).Node, times );
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
			if( Trace )
				std::cout << "step roll " << roll << " node " << node << " now " << ToIsoString(Time->Now()) << "\n";
			if( roll<=40 )
				DataChange( *Pump, node, Value(), Sampled() );
			else if( roll<=52 ){//a late record:  in the day of the node's newest, or up to two days before it, which the later files' start values follow.
				if( let newest = Model.Newest(node); newest && !Pump->FindBreak(node) ){
					let newestDay = DayStart( DayOf(UADateTime{*newest}.Time(), utc()), utc() );
					let back = days{ std::uniform_int_distribution<int>{0, 2}(Rng) };
					let span = back.count() ? 86'399 : duration_cast<seconds>( UADateTime{*newest}.Time()-newestDay ).count();
					DataChange( *Pump, node, Value(), newestDay-back+seconds{ std::uniform_int_distribution<int64_t>{0, std::max<int64_t>(span, 0)}(Rng) } );
				}
			}
			else if( roll<=68 ){//the clock, now and then past midnight and its rewrite.
				if( Chance(10) )
					Advance( NextDayStart(Time->Now(), utc())+1min+Seconds(30) );
				else
					Advance( Time->Now()+Seconds(600)+1s );
			}
			else if( roll<=78 ){//a break, with a value in flight now and then.  A node left quiet has its marker land later, behind a start value already written, which the corrections mend.
				Pump->Disconnected( Time->Now() );
				Advance( Time->Now()+Seconds(20) );
				if( Chance(30) )
					DataChange( *Pump, node, Value(), Sampled() );
				Pump->Connected();
			}
			else if( roll<=84 ){//the process stopping and starting again.
				Capture();
				Restart();
				Pump = Rejoin( Name, Members );
				_baseline = 0;
				Advance( Time->Now()+Seconds(120) );
			}
			else
				EditStep();
			Capture();
		}
		//An edit of one or two entries, each of any kind, at times records have or near them:  the oracle predicts its
		//results and applies it literally, and the library's results must match.  The edit flushes the buffer, so it is
		//captured first.
		α EditStep()ε->void{
			Capture();
			let times = Times();
			let pick = [&]{
				let t = times[std::uniform_int_distribution<uint>{0, (uint)times.size()-1}(Rng)];
				let nudge = std::uniform_int_distribution<int>{ -3, 3 }( Rng );
				return nudge==3 ? t+ticks( seconds{1} ) : nudge==-3 ? t-ticks( seconds{1} ) : std::abs( nudge )==2 ? t+nudge/2 : t;
			};
			vector<EditDetails> details;
			for( uint i=0, entries=1+Chance(30); i<entries; ++i ){
				let node = AnyNode();
				let kind = std::uniform_int_distribution<uint>{ 1, 5 }( Rng );
				if( kind<=3 ){
					vector<Opc::Value> values;
					for( uint j=0, count=1+Chance(40); j<count; ++j )
						values.push_back( Reading(Value(), UADateTime{pick()}.Time()) );
					details.push_back( UpdateData{node, kind==1 ? UA_PERFORMUPDATETYPE_INSERT : kind==2 ? UA_PERFORMUPDATETYPE_REPLACE : UA_PERFORMUPDATETYPE_UPDATE, move(values)} );
				}
				else if( kind==4 ){
					auto a = pick(), b = pick();
					if( a>b )
						std::swap( a, b );
					details.push_back( DeleteRaw{node, a, b} );
				}
				else{
					vector<UA_DateTime> at;
					for( uint j=0, count=1+Chance(40); j<count; ++j )
						at.push_back( pick() );
					details.push_back( DeleteAtTime{node, move(at)} );
				}
			}
			let expected = Model.Edited( details, ticks(Time->Now()) );
			if( Trace ){
				for( let& d : details )
					std::cout << "edit " << describe( d ) << "\n";
			}
			let actual = BlockAny( Pump->Edit(details, Admin) );
			Settle( *Pump );
			_baseline = Pump->Buffer().size();
			string described;
			for( let& d : details )
				described += describe( d )+"; ";
			SCOPED_TRACE( described );
			ASSERT_EQ( actual.size(), expected.size() );
			for( uint e=0; e<actual.size(); ++e )
				EXPECT_EQ( describe(actual[e]), describe(expected[e]) ) << e;
		}
		α Times()Ι->vector<Ticks>{
			vector<Ticks> y{ ticks(Time->Now()), ticks(Time->Now()-1h) };
			for( let& e : Model.Entries )
				y.push_back( e.Time );
			for( let& e : Model.Edits )
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
			r.Modified = Chance( 30 );
			r.Bounds = !r.Modified && Chance( 50 );
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
				y += "edits:\n";
				for( let& e : Model.Edits )
					y += Ƒ( "node {} at {} type {} ts {} original {} new {}\n", e.Node, e.Time, (int)e.Type, e.Ts, e.Original ? e.Original->ShortDebugString() : "-", e.New ? e.New->ShortDebugString() : "-" );
				y += "preambles:\n";
				for( let day : Days(Path(), Name) ){
					let file = File( *Pump, day );
					let start = ReadStart( file );
					y += Ƒ( "{} generation {}:", DayDirectory(day).string(), start ? start->generation() : 999 );
					for( let& record : readFile(file) ){
						if( record.has_node_added() && isPreamble(record, record.node_added().node_index(), day) && std::ranges::contains(r.Nodes, record.node_added().node_index()) )
							y += Ƒ( " [{} {}]", record.node_added().node_index(), record.node_added().has_start() ? record.node_added().start().ShortDebugString() : "none" );
					}
					y += "\n";
				}
				return y;
			};
			ASSERT_EQ( actual.size(), expected.size() ) << dump();
			for( uint i=0; i<actual.size() && !HasFailure(); ++i )
				EXPECT_EQ( describe(actual[i]), describe(expected[i]) ) << i << "\n" << dump();
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
		bool Trace{ std::getenv("HIST_MODEL_TRACE")!=nullptr };
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