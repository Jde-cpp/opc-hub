//The model-based test (#205, #206):  a naive oracle keeps every record the group buffers, in memory, and random
//sequences of values, late records, breaks, restarts and edits go to the library, every read then compared with what
//the oracle answers - forward, reverse and open-ended, with and without bounds, raw and modified, paged at every size.
//The oracle takes each record as the group buffers it, so it holds what the library decided to store, and applies each
//edit literally to the series as it stands, predicting what the library should answer it; so the comparison is of the
//read and edit paths alone:  compression and gaps have their own suites.  #207 extends it with at-time and processed
//reads:  the oracle computes each value and aggregate by brute force over the node's series, Part 13's rules applied
//to records it holds whole, where the library streams them once and keeps nothing between pages.
#include <cmath>
#include <numeric>
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
		//A node's records as the edits leave them, in time order.
		struct Rec{ Ticks Time; Proto::DataValue Value; };
		α SeriesOf( NodeIndex node )Ι->vector<Rec>{
			vector<Rec> y;
			for( let& [t,at] : Replayed([node]( NodeIndex i ){ return i==node; }) ){
				for( let& v : at )
					y.push_back( {t, v} );
			}
			return y;
		}
		//Part 13's classes:  Bad, Uncertain under TreatUncertainAsBad included, is skipped; Uncertain otherwise is used
		//but counts as no Good value.
		enum class EClass : uint8{ Good, Uncertain, Bad };
		Ω ClassOf( const Proto::DataValue& v, bool treatUncertainAsBad )->EClass{
			let status = v.status();
			if( status & 0x80000000 )
				return EClass::Bad;
			if( (status & 0xC0000000)==0x40000000 )
				return treatUncertainAsBad ? EClass::Bad : EClass::Uncertain;
			return EClass::Good;
		}
		//A bounding value:  the raw record, or a computed number, with its status; NoData when there is none.
		struct Bound{ optional<Proto::DataValue> Raw; optional<double> Value; StatusCode Status{}; bool Extrapolated{}; };
		static constexpr StatusCode SubNormal{ UA_STATUSCODE_UNCERTAINDATASUBNORMAL }, NoData{ UA_STATUSCODE_BADNODATA };
		Ω Unsure( const Bound& b )->bool{ return b.Raw ? (b.Raw->status() & 0xC0000000)==0x40000000 : b.Status!=UA_STATUSCODE_GOOD; }
		//Part 13 §3.1.8 by brute force:  the nearest non-Bad records either side of t, every Bad one between noted.
		Ω InterpolatedBound( const vector<Rec>& s, Ticks t, bool stepped, const AggregateConfiguration& c )->Bound{
			let cls = [&]( const Rec& r ){ return ClassOf( r.Value, c.TreatUncertainAsBad ); };
			const Rec* at{};
			bool badAt{};
			for( let& r : s ){
				if( r.Time!=t )
					continue;
				if( cls(r)==EClass::Bad )
					badAt = true;
				else
					at = &r;
			}
			if( at )
				return { at->Value };
			const Rec *before{}, *before2{};
			uint beforeAt{};
			for( uint i=0; i<s.size(); ++i ){
				if( s[i].Time<t && cls(s[i])!=EClass::Bad ){
					before2 = before;
					before = &s[i];
					beforeAt = i;
				}
			}
			if( !before )
				return { {}, {}, NoData };
			bool unsure = badAt || cls(*before)==EClass::Uncertain;
			for( uint i=beforeAt+1; i<s.size(); ++i ){//in the series' order, which at one time is the records'.
				if( s[i].Time<t && cls(s[i])==EClass::Bad )
					unsure = true;
			}
			let v0 = *numberOf( before->Value );
			let anyAfter = std::ranges::any_of( s, [t]( let& r ){ return r.Time>t; } );
			if( stepped )
				return { {}, v0, anyAfter && !unsure ? UA_STATUSCODE_GOOD : SubNormal, !anyAfter };
			const Rec* after{};
			bool badBetweenAfter{};
			for( let& r : s ){
				if( r.Time<=t )
					continue;
				if( cls(r)!=EClass::Bad ){
					after = &r;
					break;
				}
				badBetweenAfter = true;
			}
			if( !after ){
				if( c.UseSlopedExtrapolation && before2 && before2->Time<before->Time ){
					let v1 = *numberOf( before2->Value );
					return { {}, v0+(v0-v1)*(double)(t-before->Time)/(double)(before->Time-before2->Time), SubNormal, true };
				}
				return { {}, v0, SubNormal, true };
			}
			unsure = unsure || badBetweenAfter || cls(*after)==EClass::Uncertain;
			let v1 = *numberOf( after->Value );
			return { {}, v0+(v1-v0)*(double)(t-before->Time)/(double)(after->Time-before->Time), unsure ? SubNormal : UA_STATUSCODE_GOOD };
		}
		//Part 13 §3.1.9 by brute force:  the nearest records either side of t, whatever their status.
		Ω SimpleBound( const vector<Rec>& s, Ticks t, bool stepped, const AggregateConfiguration& c )->Bound{
			let cls = [&]( const Rec& r ){ return ClassOf( r.Value, c.TreatUncertainAsBad ); };
			const Rec *atAny{}, *beforeAny{}, *afterAny{};
			for( let& r : s ){
				if( r.Time==t )
					atAny = &r;
				else if( r.Time<t )
					beforeAny = &r;
				else if( !afterAny )
					afterAny = &r;
			}
			if( atAny )
				return cls(*atAny)==EClass::Bad ? Bound{ {}, {}, NoData } : Bound{ atAny->Value };
			if( !beforeAny || cls(*beforeAny)==EClass::Bad )
				return { {}, {}, NoData };
			bool unsure = cls(*beforeAny)==EClass::Uncertain;
			let v0 = *numberOf( beforeAny->Value );
			if( stepped )
				return { {}, v0, afterAny && !unsure ? UA_STATUSCODE_GOOD : SubNormal, !afterAny };
			if( !afterAny )
				return { {}, v0, SubNormal, true };
			if( cls(*afterAny)==EClass::Bad )
				return { {}, v0, SubNormal };
			unsure = unsure || cls(*afterAny)==EClass::Uncertain;
			let v1 = *numberOf( afterAny->Value );
			return { {}, v0+(v1-v0)*(double)(t-beforeAny->Time)/(double)(afterAny->Time-beforeAny->Time), unsure ? SubNormal : UA_STATUSCODE_GOOD };
		}
		//A result as the library shapes it:  the raw record, its heartbeat cleared, or a value computed at ts.
		Ω Shape( NodeIndex node, Ticks ts, const Bound& b, StatusCode bits )->Proto::DataValue{
			Proto::DataValue y;
			if( b.Raw ){
				y = *b.Raw;
				y.clear_heartbeat();
				y.clear_heartbeat_unsourced();
				if( bits & 0x4 )
					y.set_status( y.status() | 0x400 | 0x4 );
			}
			else{
				y.set_source_ts( ts );
				y.set_status( b.Status==NoData || b.Status==UA_STATUSCODE_BAD || !bits ? b.Status : b.Status | 0x400 | bits );
				if( b.Value )
					y.mutable_value()->set_double_value( *b.Value );
			}
			y.set_node_index( node );
			return y;
		}
		//What an at-time read returns:  each time's values in the nodes' order, the times in the request's.
		α ExpectedAtTime( const AtTimeRequest& r, const std::function<bool( NodeIndex )>& stepped )Ι->vector<ReadValue>{
			vector<ReadValue> y;
			flat_map<NodeIndex,vector<Rec>> series;
			for( let node : r.Nodes )
				series.emplace( node, SeriesOf(node) );
			for( let t : r.Times ){
				for( let node : r.Nodes ){
					let bound = r.SimpleBounds ? SimpleBound( series[node], t, stepped(node), r.Configuration ) : InterpolatedBound( series[node], t, stepped(node), r.Configuration );
					y.push_back( {Shape(node, t, bound, 0x2), false, {}} );
				}
			}
			return y;
		}
		//What a processed read returns, each interval's aggregate from the records it holds, Part 13 §5.4.3.
		α ExpectedProcessed( const ProcessedRequest& r, const std::function<bool( NodeIndex )>& stepped )Ι->vector<ReadValue>{
			let& c = r.Configuration;
			let reverse = r.Start>r.End;
			let range = reverse ? r.Start-r.End : r.End-r.Start;
			let interval = ticks( r.Interval );
			let width = interval<=0 || interval>=range ? range : interval;
			let count = (uint64_t)( (range+width-1)/width );
			flat_map<NodeIndex,vector<Rec>> series;
			for( let node : r.Nodes )
				series.emplace( node, SeriesOf(node) );
			vector<ReadValue> y;
			for( uint64_t k=0; k<count; ++k ){
				let hi = reverse ? r.Start-(Ticks)k*width : std::min( r.Start+(Ticks)(k+1)*width, r.End );
				let lo = reverse ? std::max( hi-width, r.End ) : r.Start+(Ticks)k*width;
				let ts = reverse ? hi : lo;
				let uneven = k+1==count && range%width!=0;
				for( let node : r.Nodes ){
					let& s = series[node];
					let cls = [&]( const Rec& x ){ return ClassOf( x.Value, c.TreatUncertainAsBad ); };
					vector<const Rec*> inside;
					for( let& x : s ){
						if( reverse ? x.Time>lo && x.Time<=hi : x.Time>=lo && x.Time<hi )
							inside.push_back( &x );
					}
					uint good{}, bad{}, uncertain{};
					vector<double> goods;
					for( let x : inside ){
						switch( cls(*x) ){
						case EClass::Bad: ++bad; break;
						case EClass::Uncertain: ++uncertain; break;
						default: ++good; goods.push_back( *numberOf(x->Value) ); break;
						}
					}
					let dataBefore = std::ranges::any_of( s, [&]( let& x ){ return x.Time<lo || (reverse && x.Time==lo); } );
					let dataAfter = std::ranges::any_of( s, [&]( let& x ){ return x.Time>hi || (!reverse && x.Time==hi); } );
					let partial = !inside.empty() && ( !dataBefore || !dataAfter || uneven ) ? 0x4u : 0u;
					let byCounts = [&]{
						let total = (uint)inside.size();
						let goodEnough = good*100>=c.PercentDataGood*total, badEnough = bad*100>=c.PercentDataBad*total;
						if( c.PercentDataGood==100-c.PercentDataBad )
							return goodEnough ? UA_STATUSCODE_GOOD : badEnough ? UA_STATUSCODE_BAD : SubNormal;
						return badEnough ? UA_STATUSCODE_BAD : goodEnough ? UA_STATUSCODE_GOOD : SubNormal;
					};
					Bound b;
					StatusCode bits{ 0x1 | partial };
					switch( r.Aggregate ){
					case EAggregate::Interpolative:
						b = InterpolatedBound( s, ts, stepped(node), c );
						bits = 0x2;
						break;
					case EAggregate::Count:
						if( inside.empty() )
							b = { {}, {}, NoData };
						else if( let status = byCounts(); status==UA_STATUSCODE_BAD )
							b = { {}, {}, UA_STATUSCODE_BAD };
						else
							b = { {}, (double)good, status };
						break;
					case EAggregate::Average:
						bits = 0x1;
						if( !good )
							b = { {}, {}, NoData };
						else if( let status = byCounts(); status==UA_STATUSCODE_BAD )
							b = { {}, {}, UA_STATUSCODE_BAD };
						else
							b = { {}, std::accumulate(goods.begin(), goods.end(), 0.0)/good, status };
						break;
					case EAggregate::Minimum:
					case EAggregate::Maximum:{
						if( !good ){
							b = { {}, {}, NoData };
							break;
						}
						let minimum = r.Aggregate==EAggregate::Minimum;
						let extreme = minimum ? *std::ranges::min_element( goods ) : *std::ranges::max_element( goods );
						let matches = std::ranges::count( goods, extreme );
						Ticks when{};
						for( let x : inside ){
							if( cls(*x)==EClass::Good && *numberOf(x->Value)==extreme ){
								when = x->Time;
								break;
							}
						}
						b = { {}, extreme, bad ? SubNormal : UA_STATUSCODE_GOOD };
						bits = ( b.Status==UA_STATUSCODE_GOOD && when==ts ? 0 : 0x1 ) | ( matches>1 ? 0x10 : 0 ) | partial;
						break;}
					case EAggregate::Start:
					case EAggregate::End:
						if( inside.empty() )
							b = { {}, {}, NoData };
						else
							b = { (r.Aggregate==EAggregate::Start ? inside.front() : inside.back())->Value };
						bits = partial;
						break;
					case EAggregate::StandardDeviationSample:
					case EAggregate::Median:{
						if( !good ){
							b = { {}, {}, NoData };
							break;
						}
						double value{};
						if( r.Aggregate==EAggregate::Median ){
							std::ranges::sort( goods );
							value = good%2 ? goods[good/2] : ( goods[good/2-1]+goods[good/2] )/2;
						}
						else if( good>1 ){
							let mean = std::accumulate( goods.begin(), goods.end(), 0.0 )/good;
							double m2{};
							for( let g : goods )
								m2 += ( g-mean )*( g-mean );
							value = std::sqrt( m2/(good-1) );
						}
						b = { {}, value, inside.size()>good ? SubNormal : UA_STATUSCODE_GOOD };
						break;}
					case EAggregate::TimeAverage:{
						auto start = InterpolatedBound( s, lo, false, c ), end = InterpolatedBound( s, hi, false, c );
						vector<std::pair<Ticks,double>> line;
						for( let x : inside ){
							if( cls(*x)!=EClass::Bad )
								line.emplace_back( x->Time, *numberOf(x->Value) );
						}
						let missing = start.Status==NoData;
						if( (missing && line.empty()) || (line.empty() && end.Extrapolated) ){
							b = { {}, {}, NoData };
							break;
						}
						let number = []( const Bound& x ){ return x.Raw ? *numberOf(*x.Raw) : *x.Value; };
						if( !missing )
							line.insert( line.begin(), {lo, number(start)} );
						line.emplace_back( hi, number(end) );
						double area{};
						for( uint i=1; i<line.size(); ++i )
							area += ( line[i-1].second+line[i].second )/2*(double)( line[i].first-line[i-1].first );
						let from = line.front().first;
						let unsure = missing || Unsure(start) || Unsure(end) || bad || uncertain;
						b = { {}, hi>from ? area/(double)(hi-from) : line.front().second, unsure ? SubNormal : UA_STATUSCODE_GOOD };
						bits = 0x1 | ( missing || end.Extrapolated || uneven ? 0x4 : 0 );
						break;}
					}
					auto value = Shape( node, ts, b, bits );
					if( r.Aggregate==EAggregate::Count && value.has_value() ){
						let n = value.value().double_value();
						value.mutable_value()->set_int32( (int32_t)n );
					}
					y.push_back( {move(value), false, {}} );
				}
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
	Ω describe( const AtTimeRequest& r )->string{
		string times;
		for( let t : r.Times )
			times += Ƒ( " {}", ToIsoString(UADateTime{t}.Time()) );
		return Ƒ( "at times: nodes {} times{} simple {} uncertainAsBad {} sloped {} limit {}", join(r.Nodes), times, r.SimpleBounds, r.Configuration.TreatUncertainAsBad, r.Configuration.UseSlopedExtrapolation, r.Limit );
	}
	Ω describe( const ProcessedRequest& r )->string{
		let t = []( UA_DateTime t ){ return ToIsoString( UADateTime{t}.Time() ); };
		return Ƒ( "processed: nodes {} start {} end {} interval {}ms aggregate {} uncertainAsBad {} bad {} good {} sloped {} limit {}", join(r.Nodes), t(r.Start), t(r.End),
			duration_cast<milliseconds>(r.Interval).count(), (int)r.Aggregate, r.Configuration.TreatUncertainAsBad, r.Configuration.PercentDataBad, r.Configuration.PercentDataGood, r.Configuration.UseSlopedExtrapolation, r.Limit );
	}
	//Whether two values are one, a number within rounding.
	Ω same( const ReadValue& a, const ReadValue& b )->bool{
		let& x = a.Value;
		let& y = b.Value;
		if( a.Bound!=b.Bound || a.Modification.has_value()!=b.Modification.has_value() || x.node_index()!=y.node_index() || x.status()!=y.status() )
			return false;
		if( x.has_source_ts()!=y.has_source_ts() || x.source_ts()!=y.source_ts() || x.has_server_ts()!=y.has_server_ts() || x.server_ts()!=y.server_ts() || x.has_value()!=y.has_value() )
			return false;
		if( !x.has_value() )
			return true;
		let m = numberOf( x ), n = numberOf( y );
		if( m && n )
			return x.value().of_case()==y.value().of_case() && std::abs( *m-*n )<=1e-9*std::max( 1.0, std::abs(*m) );
		return x.value().ShortDebugString()==y.value().ShortDebugString();
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
			if( roll<=40 ){//a value, now and then Bad with nothing to show, or Uncertain.
				let grade = std::uniform_int_distribution<uint>{ 1, 10 }( Rng );
				if( grade==1 )
					Pump->Enqueue( node, Status(UA_STATUSCODE_BAD, Sampled()) );
				else if( grade==2 )
					Pump->Enqueue( node, Graded(Value(), UA_STATUSCODE_UNCERTAIN, Sampled()) );
				else
					DataChange( *Pump, node, Value(), Sampled() );
			}
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
		α Pick()ι->Ticks{
			let times = Times();
			let t = times[std::uniform_int_distribution<uint>{0, (uint)times.size()-1}(Rng)];
			let nudge = std::uniform_int_distribution<int>{ -2, 2 }( Rng );
			return nudge==2 ? t+ticks( seconds{1} ) : nudge==-2 ? t-ticks( seconds{1} ) : t+nudge;
		}
		α SomeNodes()ι->vector<NodeIndex>{
			vector<NodeIndex> y;
			for( let index : Indexes ){
				if( Chance(60) )
					y.push_back( index );
			}
			if( y.empty() )
				y.push_back( AnyNode() );
			std::ranges::shuffle( y, Rng );
			return y;
		}
		α SomeConfiguration()ι->AggregateConfiguration{
			constexpr std::pair<uint8_t,uint8_t> percents[]{ {100, 100}, {50, 50}, {80, 80}, {30, 90} };
			let [bad, good] = percents[std::uniform_int_distribution<uint>{0, 3}(Rng)];
			return { .TreatUncertainAsBad=Chance(50), .PercentDataBad=bad, .PercentDataGood=good, .UseSlopedExtrapolation=Chance(30) };
		}
		static constexpr uint Limits[]{ 1, 2, 3, 5, 8, 1000 };
		α SomeLimit()ι->uint{ return Limits[std::uniform_int_distribution<uint>{0, 5}(Rng)]; }
		α AtTimeRequestOf()ι->AtTimeRequest{
			AtTimeRequest r{ .Nodes=SomeNodes(), .SimpleBounds=Chance(30), .Configuration=SomeConfiguration(), .Limit=SomeLimit() };
			for( uint i=0, count=1+std::uniform_int_distribution<uint>{0, 4}(Rng); i<count; ++i )
				r.Times.push_back( Pick() );
			return r;
		}
		α ProcessedRequestOf()ι->ProcessedRequest{
			ProcessedRequest r{ .Nodes=SomeNodes(), .Start=Pick(), .End=Pick(), .Aggregate=(EAggregate)std::uniform_int_distribution<uint>{0, 9}(Rng), .Configuration=SomeConfiguration(), .Limit=SomeLimit() };
			if( r.End==r.Start )
				r.End += ticks( seconds{1} );
			let range = std::abs( r.End-r.Start );
			constexpr uint parts[]{ 1, 2, 3, 7, 50 };
			let n = parts[std::uniform_int_distribution<uint>{0, 4}(Rng)];
			if( !Chance(20) )
				r.Interval = duration_cast<Duration>( UATick{std::max<Ticks>(range/n, 1)} );
			return r;
		}
		α Stepped()Ι->std::function<bool( NodeIndex )>{
			return [this]( NodeIndex index ){ return Pump->FindThresholds( index ).value_or( Thresholds{} ).Stepped; };
		}
		//The read's values against the oracle's, every page together, and each page within the limit.
		α CompareValues( const string& described, const vector<ReadValue>& expected, const vector<ReadValue>& actual, const vector<uint>& pages, uint limit )ε->void{
			SCOPED_TRACE( described );
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
				return y;
			};
			ASSERT_EQ( actual.size(), expected.size() ) << dump();
			for( uint i=0; i<actual.size() && !HasFailure(); ++i )
				EXPECT_TRUE( same(actual[i], expected[i]) ) << i << "\n" << describe( actual[i] ) << "\n" << describe( expected[i] ) << "\n" << dump();
			for( uint i=0; i<pages.size(); ++i ){
				EXPECT_LE( pages[i], limit ) << join( pages );
				EXPECT_GE( pages[i], 1u ) << join( pages );
			}
		}
		α CompareAtTime( const AtTimeRequest& r )ε->void{
			vector<uint> pages;
			let actual = readAllAtTime( *Pump, r, &pages );
			CompareValues( describe(r), Model.ExpectedAtTime(r, Stepped()), actual, pages, r.Limit );
		}
		α CompareProcessed( const ProcessedRequest& r )ε->void{
			vector<uint> pages;
			let actual = readAllProcessed( *Pump, r, &pages );
			CompareValues( describe(r), Model.ExpectedProcessed(r, Stepped()), actual, pages, r.Limit );
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
				const Thresholds config{ .Stepped=sv{id}!="Pump1.Temp" };//one sloped node.
				let index = Join( *Pump, id, config );
				Indexes.push_back( index );
				Members.push_back( {Node(id), config, index} );
			}
			Capture();
			for( uint step=0; step<steps && !HasFatalFailure(); ++step ){
				Step();
				if( step%12==11 ){
					for( uint i=0; i<6 && !HasFailure(); ++i )
						Compare( Request() );
					for( uint i=0; i<3 && !HasFailure(); ++i )
						CompareAtTime( AtTimeRequestOf() );
					for( uint i=0; i<3 && !HasFailure(); ++i )
						CompareProcessed( ProcessedRequestOf() );
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