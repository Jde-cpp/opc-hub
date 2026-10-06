#include <jde/historian/Group.h>
#include <deque>
#include <functional>
#include <map>
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <jde/fwk/io/crc.h>
#include "Store.h"
#include "io/DayFiles.h"
#include "io/Records.h"
DISABLE_WARNINGS
#include <jde/historian/proto/Hist.Read.pb.h>
#include <google/protobuf/io/coded_stream.h>
ENABLE_WARNINGS

#define let const auto

namespace Jde::Opc::Hist{
	using namespace std::chrono;
	using Proto::HistoryRecord;
	using google::protobuf::io::CodedOutputStream;

	namespace{
		constexpr Ticks Earliest{ std::numeric_limits<Ticks>::min() }, Latest{ std::numeric_limits<Ticks>::max() };
		Ω bare( const Proto::NodeAdded& added )ι->bool{ return !added.has_identity_id() && added.user_name().empty(); }
		Ω time( const HistoryRecord& r )ι->Ticks{ return *PrimaryTime( r ); }

		//The CRC-32C of a read's arguments but Limit, which a continuation carries:  the same read, paged at any size.
		Ω crc( const ReadRequest& r )ι->uint32_t{
			string bytes;
			{
				google::protobuf::io::StringOutputStream out{ &bytes };
				CodedOutputStream coded{ &out };
				coded.WriteVarint64( r.Nodes.size() );
				for( let node : r.Nodes )
					coded.WriteVarint32( node );
				for( let& t : {r.Start, r.End} ){
					coded.WriteVarint32( t ? 1 : 0 );
					coded.WriteLittleEndian64( (uint64_t)t.value_or(0) );
				}
				coded.WriteVarint32( r.Bounds ? 1 : 0 );
			}
			return IO::Crc::Calc32c( bytes );
		}

		//The read as its request and continuation set it out.
		struct Plan final{
			Plan( const ReadRequest& r, uint readLimit, SL sl )ε:Nodes{ r.Nodes }, Bounds{ r.Bounds }, Crc{ crc(r) }{
				THROW_IFSL( Nodes.empty(), "A read names no nodes." );
				THROW_IFSL( !r.Start && !r.End, "A read needs a start or an end." );
				if( r.Start && r.End ){
					Reverse = *r.Start>*r.End;
					Earlier = Reverse ? r.End : r.Start;
					Later = Reverse ? r.Start : r.End;
				}
				else if( r.Start )
					Earlier = r.Start;
				else{
					Reverse = true;
					Later = r.End;
				}
				Limit = r.Limit && r.Limit<readLimit ? r.Limit : readLimit;
				for( uint i=0; i<Nodes.size(); ++i )
					Slots.try_emplace( Nodes[i], i );
				if( r.Continuation.empty() )
					return;
				Proto::Continuation from;
				THROW_IFSL( !from.ParseFromString(r.Continuation) || from.counts_size()!=(int)Nodes.size(), "The continuation isn't one of the historian's." );
				THROW_IFSL( from.crc()!=Crc, "The continuation is for a read with other arguments." );
				From = move( from );
			}
			α Slot( NodeIndex index )Ι->optional<uint>{
				auto p = Slots.find( index );
				return p==Slots.end() ? optional<uint>{} : p->second;
			}
			α Resume()Ι->optional<Ticks>{ return From ? optional<Ticks>{ From->time() } : nullopt; }
			//How many of the node's records at time the pages before returned:  those at the resume time.
			α Had( uint slot, Ticks time )Ι->uint32_t{ return From && From->time()==time ? From->counts(slot) : 0; }
			α HadOf( NodeIndex index, Ticks time )Ι->uint32_t{ return Had( *Slot(index), time ); }

			vector<NodeIndex> Nodes;
			absl::flat_hash_map<NodeIndex,uint> Slots;//each node's place in Nodes, which the continuation counts by.
			optional<Ticks> Earlier, Later;//the ends of the range, whichever way it flows:  Earlier is set in a forward read, Later in a reverse one.
			bool Reverse{};
			bool Bounds;
			uint Limit;
			uint32_t Crc;
			optional<Proto::Continuation> From;
		};

		//A day's records in primary-time order, one ahead:  the file's, through the runs that matter, and the buffer's for
		//the day, merged.
		struct Stream final{
			Stream( fs::path path, vector<Run> runs, vector<HistoryRecord> late, SL sl )ε:_merge{ move(path), move(runs), move(late), sl }{ Advance(); }
			α Peek()Ι->const HistoryRecord*{ return _has ? &_next : nullptr; }
			α Time()Ι->optional<Ticks>{ return _has ? PrimaryTime( _next ) : nullopt; }
			//Whether the next record is a preamble record of day:  a bare NodeAdded at its start.
			α AtPreamble( Ticks start )Ι->bool{ return _has && _next.has_node_added() && bare(_next.node_added()) && PrimaryTime(_next)==start; }
			α Take( HistoryRecord& r, optional<Merge::Position>& where )ε->bool{
				if( !_has )
					return false;
				r = move( _next );
				where = _where;
				Advance();
				return true;
			}
		private:
			α Advance()ε->void{
				_has = _merge.Next( _next );
				_where = _merge.Where();
			}
			Merge _merge;
			HistoryRecord _next;
			optional<Merge::Position> _where;
			bool _has{};
		};

		//Each node's value at the earlier end of the range, as the records before it say:  its latest, a preamble's start
		//value among them, and none past a NodeRemoved.  A later preamble record of a day replaces that day's.
		struct Before final{
			struct Found{ Proto::DataValue Value; Ticks Time; optional<Day> Preamble; };
			Before( const Plan& plan, const time_zone& tz )ι:_plan{ plan }, _tz{ tz }{}
			α Take( const HistoryRecord& r, Ticks t )ι->void{
				let earlier = *_plan.Earlier;
				switch( r.record_case() ){
				case HistoryRecord::kValue:
					if( t<earlier )
						Offer( r.value().node_index(), r.value(), t, nullopt );
					break;
				case HistoryRecord::kNodeAdded:{
					let& added = r.node_added();
					let day = DayOf( t, _tz );
					if( !bare(added) || t!=StartOf(day, _tz) )
						break;
					if( added.has_start() ){
						if( let start = PrimaryTime(added.start()); start && *start<earlier ){
							auto value = added.start();
							value.set_node_index( added.node_index() );
							Offer( added.node_index(), move(value), *start, day );
						}
					}
					else if( auto p = Values.find(added.node_index()); p!=Values.end() && p->second.Preamble==day )
						Values.erase( p );
					break;}
				case HistoryRecord::kNodeRemoved:{
					if( t>=earlier )
						break;
					let index = r.node_removed().node_index();
					auto [p, _] = _removed.try_emplace( index, t );
					p->second = std::max( p->second, t );
					if( auto v = Values.find(index); v!=Values.end() && v->second.Time<=t )
						Values.erase( v );
					break;}
				default:
					break;
				}
			}
			α Find( NodeIndex index )Ι->optional<Proto::DataValue>{
				auto p = Values.find( index );
				return p==Values.end() ? optional<Proto::DataValue>{} : p->second.Value;
			}
			absl::flat_hash_map<NodeIndex,Found> Values;
		private:
			α Offer( NodeIndex index, Proto::DataValue value, Ticks time, optional<Day> preamble )ι->void{
				if( !_plan.Slot(index) )
					return;
				if( auto r = _removed.find(index); r!=_removed.end() && time<=r->second )
					return;
				auto p = Values.find( index );
				if( p==Values.end() || time>=p->second.Time || (preamble && p->second.Preamble==preamble) )
					Values.insert_or_assign( index, Found{move(value), time, preamble} );
			}
			const Plan& _plan;
			const time_zone& _tz;
			absl::flat_hash_map<NodeIndex,Ticks> _removed;
		};

		Ω notFound( NodeIndex index, Ticks at )ι->Proto::DataValue{
			Proto::DataValue y;
			y.set_node_index( index );
			y.set_status( UA_STATUSCODE_BADBOUNDNOTFOUND );
			y.set_source_ts( at );
			return y;
		}
		//A separate bound per node not served by a record at the time itself, in the order the page lists them:  by time,
		//forward or back, then node.
		Ω bounds( const Plan& plan, const absl::flat_hash_set<NodeIndex>& served, const std::function<optional<Proto::DataValue>( NodeIndex )>& find, Ticks at, bool reverse )->vector<ReadValue>{
			vector<std::pair<Ticks,ReadValue>> y;
			for( let index : plan.Nodes ){
				if( served.contains(index) )
					continue;
				auto value = find( index ).value_or( notFound(index, at) );
				let t = PrimaryTime( value ).value_or( at );
				y.emplace_back( t, ReadValue{move(value), true} );
			}
			std::ranges::stable_sort( y, [reverse]( let& a, let& b ){ return reverse ? a.first>b.first : a.first<b.first; } );
			vector<ReadValue> values;
			for( auto& [_,v] : y )
				values.push_back( move(v) );
			return values;
		}

		//What a read takes of the group's files under its lock:  which days hold a file, and what each day the read looks
		//at serves, taken with the buffer so no record is in both or neither.  A day past those, which the bounds alone
		//look at, is served as it is asked for.
		struct Files final{
			vector<Day> OnDisk;
			std::map<Day,optional<GroupFiles::Served>> Taken;
			std::function<optional<GroupFiles::Served>( Day )> Serve;
			α Of( Day day )->optional<GroupFiles::Served>{
				auto p = Taken.find( day );
				return p==Taken.end() ? Serve( day ) : p->second;
			}
		};

		//One page.
		struct Reading final{
			Reading( const Plan& plan, const time_zone& tz, Files&& files, vector<HistoryRecord>&& buffered, SL sl )ε:
				_plan{ plan }, _tz{ tz }, _files{ move(files) }, _sl{ sl }{
				for( auto& r : buffered )
					_buffered[DayOf(time(r), tz)].push_back( move(r) );
				flat_set<Day> days{ _files.OnDisk.begin(), _files.OnDisk.end() };
				for( let& [day,_] : _buffered )
					days.insert( day );
				_days.assign( days.begin(), days.end() );
				_counts.assign( _plan.Nodes.size(), 0 );
			}
			α Page()ε->ReadResult{
				if( _plan.Reverse )
					Backward();
				else
					Forward();
				return move( _result );
			}
		private:
			//A record on its way out:  where it lies in its file, when the file is an archive.
			struct Picked{ Proto::DataValue Value; Ticks Time; optional<Merge::Position> Where; bool Archive; uint32_t Generation; };
			α Pick( HistoryRecord& r, Ticks t, optional<Merge::Position> where )Ι->Picked{
				return { move(*r.mutable_value()), t, where, _generation!=0, _generation };
			}
			α Day0( Ticks t )Ι->Day{ return DayOf( t, _tz ); }
			α Wants( const HistoryRecord& r )Ι->bool{ return r.has_value() && _plan.Slot( r.value().node_index() ); }

			//The day's stream.  A resume's archive position narrows the run, and the buffer's records with it; a live file's
			//runs are those that overlap [from, to], or all of them for a day the bounds look through.
			α Open( Day day, Ticks from, Ticks to, bool all, const Proto::Continuation* at )ε->up<Stream>{
				auto late = _buffered.find( day );
				vector<HistoryRecord> buffered = late==_buffered.end() ? vector<HistoryRecord>{} : late->second;
				auto served = _files.Of( day );
				if( !served && buffered.empty() )
					return nullptr;
				vector<Run> runs;
				_generation = served ? served->Generation : 0;
				_atOffset = false;
				if( served && _generation && at && at->has_generation() && at->generation()==_generation && at->offset()<=served->Size ){
					_atOffset = true;
					if( _plan.Reverse ){//the record at offset was the page's earliest:  the buffer's at its time went before it.
						runs.push_back( {.Offset=0, .End=at->offset(), .Chain=0, .First=Earliest, .Last=Latest} );
						std::erase_if( buffered, [&]( let& r ){ return time(r)>=at->time(); } );
					}
					else{//the page's last:  the buffer's at its time follow it.
						runs.push_back( {.Offset=at->offset(), .End=served->Size, .Chain=at->time(), .First=Earliest, .Last=Latest} );
						std::erase_if( buffered, [&]( let& r ){ return time(r)<at->time(); } );
					}
				}
				else if( served ){
					for( let& run : served->Runs ){
						if( all || _generation || (run.Last>=from && run.First<=to) )
							runs.push_back( run );
					}
				}
				return mu<Stream>( served ? served->Path : fs::path{}, move(runs), move(buffered), _sl );
			}
			α Emit( Picked&& picked, bool bound )ι->void{
				if( !_hasLast || picked.Time!=_last ){
					_last = picked.Time;
					_hasLast = true;
					std::ranges::fill( _counts, 0 );
				}
				++_counts[*_plan.Slot(picked.Value.node_index())];
				_where = picked.Where;
				_lastArchive = picked.Archive;
				_lastGeneration = picked.Generation;
				++_emitted;
				_result.Values.push_back( {move(picked.Value), bound} );
			}
			α Continue()ι->void{
				Proto::Continuation next;
				next.set_crc( _plan.Crc );
				//A first page the opening bounds fill resumes where the values start.
				let time = _hasLast ? _last : _plan.Reverse ? *_plan.Later : *_plan.Earlier;
				next.set_time( time );
				for( uint i=0; i<_counts.size(); ++i )
					next.add_counts( _counts[i]+_plan.Had(i, time) );
				if( _lastArchive && _where ){
					next.set_generation( _lastGeneration );
					next.set_offset( _plan.Reverse ? _where->Start : _where->End );
				}
				_result.Continuation = next.SerializeAsString();
			}
			//Opening bounds go before the page's values, which they leave room for.
			α Opening( vector<ReadValue>&& opening )ι->void{
				_opened = opening.size();
				_dataLimit = _plan.Limit>_opened ? _plan.Limit-_opened : 0;
				std::ranges::move( opening, std::back_inserter(_result.Values) );
			}
			//What lies before the first day the read looks at, for the bounds at the earlier end:  the buffer's records for
			//older days, since a late record not yet in its file is the value before the range when it is the newest, and,
			//when that day has no file to carry start values, the file before it, read through.
			α Seed( Before& before, Day firstDay )ε->void{
				for( let& [day,records] : _buffered ){
					if( day>=firstDay )
						break;
					for( let& r : records )
						before.Take( r, time(r) );
				}
				if( _files.Of(firstDay) )
					return;
				auto previous = std::ranges::lower_bound( _files.OnDisk, firstDay );
				if( previous==_files.OnDisk.begin() )
					return;
				auto stream = Open( *std::prev(previous), Earliest, Latest, true, nullptr );
				HistoryRecord r;
				optional<Merge::Position> where;
				while( stream && stream->Take(r, where) )
					before.Take( r, time(r) );
			}
			//The first record past `later` of each node in need, the earliest of:  the rest of the range's last day; the
			//later days' preambles, each read until its first record isn't at its start, where a start value past `later`
			//puts the record in the day before, which is then read through, and a day read through when a node in need
			//isn't in its preamble, a rejoin perhaps, or it is the newest, which no later preamble tells of; and the
			//buffer's.
			α After( Stream* rest, Day lastDay, absl::flat_hash_set<NodeIndex> need )ε->absl::flat_hash_map<NodeIndex,Proto::DataValue>{
				absl::flat_hash_map<NodeIndex,Proto::DataValue> y;
				let later = *_plan.Later;
				let offer = [&]( const Proto::DataValue& v, Ticks t ){
					if( t<=later || !need.contains(v.node_index()) )
						return;
					auto p = y.find( v.node_index() );
					if( p==y.end() || t<*PrimaryTime(p->second) )
						y.insert_or_assign( v.node_index(), v );
				};
				let scan = [&]( Stream& stream ){//through the stream, until each node in need has a record past `later`.
					HistoryRecord r;
					optional<Merge::Position> where;
					absl::flat_hash_set<NodeIndex> found;
					while( !need.empty() && stream.Take(r, where) ){
						let t = time( r );
						if( r.has_value() ){
							offer( r.value(), t );
							if( t>later && need.erase(r.value().node_index()) )
								found.insert( r.value().node_index() );
						}
						else if( r.has_node_added() && bare(r.node_added()) && r.node_added().has_start() ){
							auto value = r.node_added().start();
							value.set_node_index( r.node_added().node_index() );
							if( let start = PrimaryTime(value); start )
								offer( value, *start );
						}
					}
					for( let index : found )//found in order:  the day holds none earlier.
						need.erase( index );
				};
				if( rest )
					scan( *rest );
				for( auto day = std::ranges::upper_bound(_files.OnDisk, lastDay); !need.empty() && day!=_files.OnDisk.end(); ++day ){
					auto stream = Open( *day, Earliest, Latest, true, nullptr );
					if( !stream )
						continue;
					absl::flat_hash_set<NodeIndex> listed;
					vector<NodeIndex> changed;//each node whose start value is past `later`.
					HistoryRecord r;
					optional<Merge::Position> where;
					for( let start = StartOf(*day, _tz); stream->AtPreamble(start); ){
						stream->Take( r, where );
						let& added = r.node_added();
						if( !need.contains(added.node_index()) )
							continue;
						listed.insert( added.node_index() );
						if( let t = added.has_start() ? PrimaryTime(added.start()) : nullopt; t && *t>later ){
							auto value = added.start();
							value.set_node_index( added.node_index() );
							offer( value, *t );
							changed.push_back( added.node_index() );
						}
					}
					if( !changed.empty() ){//in the day before, whose first past `later` the start value needn't be.
						if( let before = day-1; *before>lastDay ){
							if( auto earlier = Open(*before, later, Latest, true, nullptr) )
								scan( *earlier );
						}
						for( let index : changed )
							need.erase( index );
					}
					//A node no preamble lists may have rejoined, and the newest day's records are in no later preamble.
					if( day+1==_files.OnDisk.end() || std::ranges::any_of(need, [&]( NodeIndex index ){ return !listed.contains(index); }) )
						scan( *stream );
				}
				for( let& [_,records] : _buffered ){
					for( let& r : records ){
						if( r.has_value() )
							offer( r.value(), time(r) );
					}
				}
				return y;
			}
			//The bounds at `later`, for the nodes no record at `later` serves:  each one's first record past it.
			α LaterBounds( Stream* rest, Day lastDay, const absl::flat_hash_set<NodeIndex>& served )ε->vector<ReadValue>{
				absl::flat_hash_set<NodeIndex> need;
				for( let index : _plan.Nodes ){
					if( !served.contains(index) )
						need.insert( index );
				}
				auto found = After( rest, lastDay, std::move(need) );
				return bounds( _plan, served, [&]( NodeIndex index )->optional<Proto::DataValue>{
					auto p = found.find( index );
					return p==found.end() ? optional<Proto::DataValue>{} : optional<Proto::DataValue>{ p->second };
				}, *_plan.Later, _plan.Reverse );
			}
			α EarlierBounds( const Before& before, const absl::flat_hash_set<NodeIndex>& served )ε->vector<ReadValue>{
				return bounds( _plan, served, [&]( NodeIndex index ){ return before.Find( index ); }, *_plan.Earlier, _plan.Reverse );
			}
			//Each node's last record at `later` among the page's values is its bound there, unless more of its follow:
			//flagged, and counted as served.  A page before can't have had the node's last, since this one holds more.
			α FlagLastAtLater( absl::flat_hash_set<NodeIndex>& served, const absl::flat_hash_set<NodeIndex>& following )ι->void{
				absl::flat_hash_set<NodeIndex> flagged;
				for( uint i=_result.Values.size(); i-->_opened; ){
					auto& v = _result.Values[i];
					if( PrimaryTime(v.Value)!=_plan.Later )
						break;
					let index = v.Value.node_index();
					if( !following.contains(index) && flagged.insert(index).second ){
						v.Bound = true;
						served.insert( index );
					}
				}
			}

			α Forward()ε->void{
				let earlier = *_plan.Earlier;
				let resume = _plan.Resume();
				let from = resume.value_or( earlier );
				let firstDay = Day0( from );
				let lastDay = _plan.Later ? optional<Day>{ Day0(*_plan.Later) } : nullopt;
				let first = !_plan.From;
				let opening = first && _plan.Bounds;//the opening bounds are this page's to decide, from the records at `earlier`.
				Before before{ _plan, _tz };
				if( opening )
					Seed( before, firstDay );
				bool opened = !opening;//decided, and values may follow.
				absl::flat_hash_set<NodeIndex> atEarlier, atLater;//each node with a record at the end itself, which serves as its bound.
				vector<Picked> held;//the records at `earlier`, until the opening bounds are decided.
				vector<uint32_t> skip( _plan.Nodes.size() );//at the resume time, what the pages before returned.
				for( uint i=0; i<skip.size(); ++i )
					skip[i] = _plan.Had( i, from );
				bool more{};
				absl::flat_hash_set<NodeIndex> following;//each node with a record at `later` left for the next page.
				up<Stream> stream;
				auto day = std::ranges::lower_bound( _days, firstDay );
				let open = [&]{
					for( ; day!=_days.end() && (!lastDay || *day<=*lastDay); ++day ){
						let last = lastDay && *day==*lastDay;
						stream = Open( *day, from, _plan.Later.value_or(Latest), _plan.Bounds && (*day==firstDay || last), *day==firstDay && _plan.From ? &*_plan.From : nullptr );
						if( stream ){
							++day;
							return true;
						}
					}
					return false;
				};
				let emit = [&]( Picked&& picked ){
					let bound = _plan.Bounds && picked.Time==earlier && !_plan.HadOf(picked.Value.node_index(), earlier) && atEarlier.insert( picked.Value.node_index() ).second;
					Emit( move(picked), bound );
				};
				let decide = [&]{//the opening bounds, then the records at `earlier`.
					opened = true;
					absl::flat_hash_set<NodeIndex> served;
					for( let& picked : held )
						served.insert( picked.Value.node_index() );
					Opening( EarlierBounds(before, served) );
					for( auto& picked : held ){
						if( _emitted>=_dataLimit ){
							more = true;
							if( _plan.Later && earlier==*_plan.Later )
								following.insert( picked.Value.node_index() );
							continue;
						}
						emit( move(picked) );
					}
					held.clear();
				};
				HistoryRecord r;
				optional<Merge::Position> where;
				for( bool have = open(); have && !more; ){
					let t = stream->Time();
					if( !t ){
						have = open();
						continue;
					}
					if( _plan.Later && *t>*_plan.Later )
						break;//the range's last day, in order:  nothing after is in range.
					stream->Take( r, where );
					if( !opened )
						before.Take( r, *t );
					if( !Wants(r) || *t<from )
						continue;
					if( resume && *t==*resume && !_atOffset ){
						if( auto& left = skip[*_plan.Slot(r.value().node_index())]; left ){
							--left;
							continue;
						}
					}
					if( !opened ){
						if( *t==earlier ){
							held.push_back( Pick(r, *t, where) );
							continue;
						}
						decide();
						if( more )
							break;
					}
					if( _emitted>=_dataLimit ){
						more = true;
						break;
					}
					emit( Pick(r, *t, where) );
				}
				if( !opened )
					decide();
				if( more ){
					//A record at `later` is its node's closing bound when it is the node's last there:  known once the rest at
					//`later` are seen, which the next page returns again.
					if( _plan.Bounds && _plan.Later && _hasLast && _last==*_plan.Later ){
						if( Wants(r) && time(r)==*_plan.Later )
							following.insert( r.value().node_index() );
						while( stream && stream->Time()==*_plan.Later ){
							stream->Take( r, where );
							if( Wants(r) )
								following.insert( r.value().node_index() );
						}
						FlagLastAtLater( atLater, following );
					}
					Continue();
					return;
				}
				if( !_plan.Bounds || !_plan.Later )
					return;
				//The last page:  each node's last record at `later` is its closing bound, on this page or one before.
				for( let index : _plan.Nodes ){
					if( _plan.HadOf(index, *_plan.Later) )
						atLater.insert( index );
				}
				if( _hasLast && _last==*_plan.Later )
					FlagLastAtLater( atLater, {} );
				auto closing = LaterBounds( stream && stream->Peek() ? stream.get() : nullptr, *lastDay, atLater );
				std::ranges::move( closing, std::back_inserter(_result.Values) );
			}

			α Backward()ε->void{
				let later = *_plan.Later;
				let resume = _plan.Resume();
				let to = resume.value_or( later );
				let lastDay = Day0( to );
				let firstDay = _plan.Earlier ? optional<Day>{ Day0(*_plan.Earlier) } : nullopt;
				let first = !_plan.From;
				let closing = _plan.Bounds && _plan.Earlier;//the bounds at `earlier`, which the last page ends with.
				vector<uint32_t> skip( _plan.Nodes.size() );//at the resume time, what the pages before returned:  the node's last there.
				for( uint i=0; i<skip.size(); ++i )
					skip[i] = _plan.Had( i, to );
				absl::flat_hash_set<NodeIndex> atLater, atEarlier;
				Before before{ _plan, _tz };
				if( closing )
					Seed( before, *firstDay );
				bool opened = !( first && _plan.Bounds );//the opening bounds, from the first day read.
				bool more{};
				//Each day read forward, the latest kept, and returned reversed.
				struct Kept{ Picked Item; bool FirstAtEarlier; bool LastAtLater; };
				auto day = std::ranges::upper_bound( _days, lastDay );//one past the day to read.
				optional<Day> jump;//a day that held nothing jumps to the day of the latest start value.
				while( day!=_days.begin() && !more ){
					--day;
					if( jump && *day>*jump )
						continue;
					jump.reset();
					if( firstDay && *day<*firstDay )
						break;
					let isLast = *day==lastDay;
					auto stream = Open( *day, _plan.Earlier.value_or(Earliest), to, _plan.Bounds && (day==_days.begin() || (firstDay && *day==*firstDay) || isLast), isLast && _plan.From ? &*_plan.From : nullptr );
					if( !stream )
						continue;
					std::deque<Kept> kept;
					let room = [&]{ return _dataLimit-_emitted+1; };//one past what the page can take, which says there is more.
					vector<Kept> atTo;//the records at the resume time, of which each node's first (all - skip) stay.
					absl::flat_hash_map<NodeIndex,uint32_t> countAtTo;
					absl::flat_hash_set<NodeIndex> seenAtEarlier, listed;
					absl::flat_hash_set<NodeIndex> servedAtLater;//each node with a record at `later`, kept or not.
					absl::flat_hash_map<NodeIndex,Ticks> starts;
					HistoryRecord r;
					optional<Merge::Position> where;
					while( let t = stream->Time() ){
						if( *t>to )
							break;
						stream->Take( r, where );
						if( r.has_node_added() && bare(r.node_added()) && *t==StartOf(*day, _tz) && _plan.Slot(r.node_added().node_index()) ){
							listed.insert( r.node_added().node_index() );
							if( r.node_added().has_start() )
								starts.insert_or_assign( r.node_added().node_index(), *PrimaryTime(r.node_added().start()) );
						}
						if( closing )
							before.Take( r, *t );
						if( !Wants(r) || (_plan.Earlier && *t<*_plan.Earlier) )
							continue;
						let index = r.value().node_index();
						if( isLast && *t==later )
							servedAtLater.insert( index );
						Kept k{ Pick(r, *t, where), _plan.Earlier && *t==*_plan.Earlier && seenAtEarlier.insert(index).second, false };
						if( resume && *t==*resume && !_atOffset ){
							atTo.push_back( move(k) );
							++countAtTo[index];
							continue;
						}
						kept.push_back( move(k) );
						if( kept.size()>room() )
							kept.pop_front();
					}
					for( auto& k : atTo ){
						let index = k.Item.Value.node_index();
						if( auto& left = countAtTo[index]; left>skip[*_plan.Slot(index)] ){
							--left;
							kept.push_back( move(k) );
							if( kept.size()>room() )
								kept.pop_front();
						}
					}
					if( !opened ){//each node's last record at `later` opens the page as its bound.
						opened = true;
						absl::flat_hash_set<NodeIndex> flagged;
						for( auto& k : kept | std::views::reverse ){
							if( k.Item.Time!=later )
								break;
							k.LastAtLater = flagged.insert( k.Item.Value.node_index() ).second;
						}
						atLater = servedAtLater;
						Opening( LaterBounds(isLast && stream->Peek() ? stream.get() : nullptr, lastDay, atLater) );
						while( kept.size()>room() )
							kept.pop_front();
					}
					else if( isLast ){
						absl::flat_hash_set<NodeIndex> flagged;
						for( auto& k : kept | std::views::reverse ){
							if( k.Item.Time!=later )
								break;
							k.LastAtLater = !_plan.HadOf( k.Item.Value.node_index(), later ) && flagged.insert( k.Item.Value.node_index() ).second;
						}
					}
					if( kept.empty() ){
						optional<Day> target;
						if( auto late = _buffered.lower_bound(*day); late!=_buffered.begin() )
							target = std::prev( late )->first;
						if( std::ranges::all_of(_plan.Nodes, [&]( NodeIndex index ){ return listed.contains( index ); }) ){
							optional<Ticks> latest;
							for( let& [_,t] : starts )
								latest = std::max( latest.value_or(t), t );
							if( latest )
								target = std::max( target.value_or(Day0(*latest)), Day0(*latest) );
							if( !target )
								break;//every node joined with nothing before:  nothing earlier.
							jump = target;
						}
						continue;
					}
					for( auto& k : kept | std::views::reverse ){
						if( _emitted>=_dataLimit ){
							more = true;
							break;
						}
						if( k.FirstAtEarlier )
							atEarlier.insert( k.Item.Value.node_index() );
						Emit( move(k.Item), _plan.Bounds && (k.LastAtLater || k.FirstAtEarlier) );
					}
				}
				if( !opened )//no day held a record at `later`:  the opening bounds alone serve it.
					Opening( LaterBounds(nullptr, lastDay, {}) );
				if( more ){
					Continue();
					return;
				}
				if( !closing )
					return;
				for( let index : _plan.Nodes ){
					if( _plan.HadOf(index, *_plan.Earlier) )
						atEarlier.insert( index );
				}
				auto ending = EarlierBounds( before, atEarlier );
				std::ranges::move( ending, std::back_inserter(_result.Values) );
			}

			const Plan& _plan;
			const time_zone& _tz;
			Files _files;
			SL _sl;
			std::map<Day,vector<HistoryRecord>> _buffered;
			vector<Day> _days;//each with a file or buffered records.
			ReadResult _result;
			uint _opened{};//the opening bounds, which lead _result.Values.
			uint _dataLimit{ _plan.Limit };//what the page's values may come to, the opening bounds taken out.
			uint _emitted{};
			bool _hasLast{};
			Ticks _last{};//the last value's time, and each node's count at it.
			vector<uint32_t> _counts;
			optional<Merge::Position> _where;//the last value's place in its file.
			bool _lastArchive{};
			uint32_t _lastGeneration{};
			uint32_t _generation{};//the open day's.
			bool _atOffset{};//the open day resumed at the continuation's offset.
		};
	}

	α Group::Read( const ReadRequest& request, SL sl )ε->ReadResult{
		let& tz = *_store->Config.TimeZone;
		const Plan plan{ request, _store->Config.ReadLimit, sl };
		//The days the values come from, which the look at the buffer under the same hold leaves in one place or the other.
		let from = plan.Reverse ? plan.Earlier : optional<Ticks>{ plan.Resume().value_or(*plan.Earlier) };
		let to = plan.Reverse ? optional<Ticks>{ plan.Resume().value_or(*plan.Later) } : plan.Later;
		vector<Buffered> snapshot;
		Files files;
		{
			ul _{ _filesMutex };
			{
				ul _{ _mutex };
				snapshot = Snapshot();
			}
			files.OnDisk = _files->Days( sl );
			for( let day : files.OnDisk ){
				if( (!from || day>=DayOf(*from, tz)) && (!to || day<=DayOf(*to, tz)) )
					files.Taken.emplace( day, _files->Serve(day, sl) );
			}
		}
		files.Serve = [this, sl]( Day day ){
			ul _{ _filesMutex };
			return _files->Serve( day, sl );
		};
		vector<Proto::HistoryRecord> buffered;
		buffered.reserve( snapshot.size() );
		for( auto& b : snapshot ){
			if( auto value = get_if<DataValue>(&b.Item) )
				value->Unsupported = false;//warned of by the flush.
			try{
				buffered.push_back( ToProto(b.Item) );
			}
			catch( const Exception& )//no file form:  the flush says so.
			{}
		}
		std::ranges::stable_sort( buffered, {}, []( let& r ){ return *PrimaryTime(r); } );
		return Reading{ plan, tz, move(files), move(buffered), sl }.Page();
	}
}