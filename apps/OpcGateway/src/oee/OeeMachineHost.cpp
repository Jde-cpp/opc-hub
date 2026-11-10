#include "OeeMachineHost.h"
#include <absl/cleanup/cleanup.h>
#include <boost/asio/post.hpp>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/oee/rules.h>
#include "../async/ExecutorAwait.h"
#include "../hist/HistGroupHost.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };
	using std::chrono::milliseconds;

	//What the engine tells its sink, queued under the engine's lock and written here, on the executor:  one batch at a time,
	//each what was queued when the last ended, so a settle tick's rows go out together.  Consecutive interval rows share an
	//insert, IntervalsPerInsert at most, and `processed` moves after each, so a backfill a crash interrupts resumes where its
	//last insert ended.  The rebuilds it is asked for queue here too, and OnRebuild starts the host's loop over them.  It holds
	//no host:  the engine holds it, and a host that held the engine would be held by it.
	struct OeeMachineHost::Writer final : Oee::ISink, std::enable_shared_from_this<Writer>{
		struct StopOpened final{ Oee::Stop Row; };
		struct StopClosed final{ Oee::Stop Row; };
		struct StopsRewritten final{ vector<Oee::Stop> Rows; vector<TimePoint> Held; };
		using Op = std::variant<Oee::Interval,StopOpened,StopClosed,StopsRewritten>;
		struct Ask final{ uint32 Node; Oee::Span Span; };//Node 0:  recomputeOee's, which no record narrows.
		Writer( OeeMachinePK machine, string name )ι:_machine{ machine }, _name{ move(name) }{}

		α Closed( const Oee::Interval& row )ι->void override{ Queue( row ); }
		α Opened( const Oee::Stop& stop )ι->void override{ Queue( StopOpened{stop} ); }
		α Closed( const Oee::Stop& stop )ι->void override{ Queue( StopClosed{stop} ); }
		α Rewritten( TimePoint, const vector<Oee::Stop>& stops, const vector<TimePoint>& held )ι->void override{ Queue( StopsRewritten{stops, held} ); }
		α Rebuild( uint32 node, Oee::Span span )ι->void override{ Asked( {node, span} ); }
		α Live( const Oee::LiveState& )ι->void override{}//the live feed's (#403).

		α Asked( Ask&& ask )ι->void{
			{
				lg _{ _mutex };
				if( _ended )
					return;
				++_rebuilds;
				DBG( "[{}]A rebuild is wanted for member {}, {} to {}.", _name, ask.Node, ToIsoString(ask.Span.Start), ToIsoString(ask.Span.End) );
				_asks.push_back( move(ask) );
				if( std::exchange(_rebuilding, true) )
					return;
			}
			if( OnRebuild )
				OnRebuild();
		}
		//The asks queued:  none, with Rebuilding dropped and its waiters resumed, once the queue is empty or this has ended.
		α Asks()ι->vector<Ask>{
			vector<Ask> y;
			vector<QuietAwait*> waiters;
			{
				lg _{ _mutex };
				if( !_ended )
					y = std::exchange( _asks, {} );
				if( y.empty() ){
					_rebuilding = false;
					waiters = Released();
				}
			}
			for( auto waiter : waiters )
				waiter->Resume();
			return y;
		}
		α RebuildEnded()ι->void{
			vector<QuietAwait*> waiters;
			{
				lg _{ _mutex };
				_rebuilding = false;
				_asks.clear();
				waiters = Released();
			}
			for( auto waiter : waiters )
				waiter->Resume();
		}
		α End()ι->void{ lg _{ _mutex }; _ended = true; _ops.clear(); _asks.clear(); }
		α Idle()Ι->bool{ lg _{ _mutex }; return !_writing && !_rebuilding && _ops.empty(); }
		α Busy( uint8 of )Ι->bool{ lg _{ _mutex }; return (Active() & of)!=0; }
		α Wait( QuietAwait* waiter )ι->bool{//false when nothing it waits for is running.
			lg _{ _mutex };
			let busy = (Active() & waiter->For)!=0;
			if( busy )
				_waiters.push_back( waiter );
			return busy;
		}
		α StartBegan()ι->void{ lg _{ _mutex }; _starting = true; }
		α StartEnded()ι->void{
			vector<QuietAwait*> waiters;
			{
				lg _{ _mutex };
				_starting = false;
				waiters = Released();
			}
			for( auto waiter : waiters )
				waiter->Resume();
		}
		α Rebuilds()Ι->uint{ return _rebuilds; }
		function<void( string why )> OnFailure;//set before the engine is made, so before anything is queued.
		function<void()> OnRebuild;//the first ask after a quiet spell:  the host's loop takes the rest.
	private:
		α Active()Ι->uint8{ return (_writing ? ForWrites : 0) | (_starting ? ForStart : 0) | (_rebuilding ? ForRebuild : 0); }//under _mutex.
		α Released()ι->vector<QuietAwait*>{//under _mutex:  the waiters nothing holds now.
			vector<QuietAwait*> y;
			let active = Active();
			std::erase_if( _waiters, [&]( QuietAwait* waiter ){
				let released = (active & waiter->For)==0;
				if( released )
					y.push_back( waiter );
				return released;
			} );
			return y;
		}
		α Queue( Op&& op )ι->void{
			{
				lg _{ _mutex };
				if( _ended )
					return;
				_ops.push_back( move(op) );
				if( std::exchange(_writing, true) )
					return;
			}
			Post( [self=shared_from_this()]{ self->Write(); } );
		}
		α Statements( vector<Op>&& batch )Ε->vector<DB::Sql>{
			vector<DB::Sql> y;
			vector<Oee::Interval> rows;
			let flush = [&]{
				for( uint i=0; i<rows.size(); i+=OeeSql::IntervalsPerInsert ){
					let count = std::min<uint>( OeeSql::IntervalsPerInsert, rows.size()-i );
					y.push_back( OeeSql::InsertIntervals(_machine, std::span{rows}.subspan(i, count)) );
					y.push_back( OeeSql::SetProcessed(_machine, rows[i+count-1].End()) );
				}
				rows.clear();
			};
			for( auto& op : batch ){
				if( let row = std::get_if<Oee::Interval>(&op); row ){
					rows.push_back( *row );
					continue;
				}
				flush();
				if( let opened = std::get_if<StopOpened>(&op); opened )
					y.push_back( OeeSql::InsertStop(_machine, opened->Row) );
				else if( let closed = std::get_if<StopClosed>(&op); closed )
					y.push_back( OeeSql::UpdateStop(_machine, closed->Row) );
				else{
					let& rewritten = std::get<StopsRewritten>( op );
					for( let start : rewritten.Held ){
						if( !std::ranges::contains(rewritten.Rows, start, &Oee::Stop::Start) )
							y.push_back( OeeSql::DeleteStop(_machine, start) );
					}
					for( let& stop : rewritten.Rows )
						y.push_back( std::ranges::contains(rewritten.Held, stop.Start) ? OeeSql::UpdateStop(_machine, stop) : OeeSql::InsertStop(_machine, stop) );
				}
			}
			flush();
			return y;
		}
		α Write()ι->VoidTask{
			let self = shared_from_this();
			string failure;
			for( ;; ){
				vector<Op> batch;
				vector<QuietAwait*> waiters;
				{
					lg _{ _mutex };
					if( failure.size() ){
						_ended = true;
						_ops.clear();
					}
					if( _ended || _ops.empty() ){
						_writing = false;
						waiters = Released();
					}
					else
						batch = std::exchange( _ops, {} );
				}
				if( batch.empty() ){
					for( auto waiter : waiters )
						waiter->Resume();
					break;
				}
				try{
					for( auto& sql : Statements(move(batch)) ){
						if( Ended() )
							break;
						co_await Any( DS()->Execute(move(sql)) );
					}
				}
				catch( const std::exception& e ){
					failure = e.what();
				}
			}
			if( failure.size() && OnFailure )
				OnFailure( move(failure) );
		}
		α Ended()Ι->bool{ lg _{ _mutex }; return _ended; }

		const OeeMachinePK _machine;
		const string _name;
		mutable std::mutex _mutex;
		vector<Op> _ops;
		vector<Ask> _asks;
		vector<QuietAwait*> _waiters;
		bool _writing{}, _ended{}, _starting{}, _rebuilding{};
		std::atomic<uint> _rebuilds{};
	};
	α OeeMachineHost::QuietAwait::await_ready()ι->bool{ return !_writer->Busy( For ); }
	α OeeMachineHost::QuietAwait::Suspend()ι->void{
		if( !_writer->Wait(this) )//what it waited for ended since await_ready.
			Resume();
	}
	//A rebuild's engine tells this, and the host writes what it holds at the end:  the rows made, and the stop rows as the
	//engine left them, by start, the loaded one among them while it kept it.
	struct OeeMachineHost::Rebuilt final : Oee::ISink{
		α Closed( const Oee::Interval& row )ι->void override{ Rows.push_back( row ); }
		α Opened( const Oee::Stop& stop )ι->void override{ Stops.insert_or_assign( stop.Start, stop ); }
		α Closed( const Oee::Stop& stop )ι->void override{ Stops.insert_or_assign( stop.Start, stop ); }
		α Rewritten( TimePoint from, const vector<Oee::Stop>& stops, const vector<TimePoint>& )ι->void override{
			Stops.erase( Stops.lower_bound(from), Stops.end() );
			for( let& stop : stops )
				Stops.insert_or_assign( stop.Start, stop );
		}
		α Rebuild( uint32 node, Oee::Span span )ι->void override{ Asked.emplace_back( node, span ); }//over stored records in order, none is expected.
		α Live( const Oee::LiveState& )ι->void override{}
		vector<Oee::Interval> Rows;
		std::map<TimePoint,Oee::Stop> Stops;
		vector<std::pair<uint32,Oee::Span>> Asked;
	};

	Ω good( const Hist::Proto::DataValue& v )ι->bool{ return v.has_value() && Oee::IsGood( v.status() ); }
	Ω when( const Hist::Proto::DataValue& v )ι->optional<TimePoint>{
		if( v.has_source_ts() )
			return UADateTime{ UA_DateTime{v.source_ts()} }.Time();
		return v.has_server_ts() ? optional<TimePoint>{ UADateTime{UA_DateTime{v.server_ts()}}.Time() } : nullopt;
	}
	//Each counter whose bound is there and isn't Good:  its last Good value before it, read back from the group, later first
	//(spec *The engine*), which is what its first Good value after the gap counts against.
	Ω lastGood( Hist::Group& group, const vector<Oee::Signal>& signals, const vector<Hist::Proto::DataValue>& bounds, UA_DateTime before, uint limit )ε->vector<Hist::Proto::DataValue>{
		vector<Hist::Proto::DataValue> y;
		for( let& bound : bounds ){
			using enum Oee::ERole;
			let counter = std::ranges::any_of( signals, [&]( const Oee::Signal& s ){ return s.Node==bound.node_index() && (s.Role==Total || s.Role==Good || s.Role==Reject); } );
			if( !counter || bound.status()==UA_STATUSCODE_BADBOUNDNOTFOUND || good(bound) )
				continue;
			Hist::ReadRequest request{ .Nodes={bound.node_index()}, .End=before, .Limit=limit };
			for( bool found{}; !found; ){
				auto page = group.Read( request );
				for( let& v : page.Values ){
					if( (found = good(v.Value)) ){
						y.push_back( v.Value );
						break;
					}
				}
				if( page.Continuation.empty() )
					break;
				request.Continuation = move( page.Continuation );
			}
		}
		return y;
	}
	//The node's next stored record after `after`, a heartbeat passed over, no further than `until`:  where a late record's
	//effect ends, a stepped value holding until it and a counter's next delta being against it.
	Ω nextRecord( Hist::Group& group, uint32 node, TimePoint after, TimePoint until, uint limit )ε->optional<TimePoint>{
		let from = UADateTime{ after }.UA()+1, to = UADateTime{ until }.UA();
		if( from>to )
			return nullopt;
		Hist::ReadRequest request{ .Nodes={node}, .Start=from, .End=to, .Limit=limit };
		for( ;; ){
			auto page = group.Read( request );
			for( let& v : page.Values ){
				if( let t = v.Value.has_heartbeat() ? nullopt : when( v.Value ); t )
					return t;
			}
			if( page.Continuation.empty() )
				return nullopt;
			request.Continuation = move( page.Continuation );
		}
	}
	//`span` into `spans`, merged with those it overlaps or touches, kept in time order.
	Ω merge( vector<Oee::Span>& spans, Oee::Span span )ι->void{
		for( auto p = spans.begin(); p!=spans.end(); ){
			if( p->End<span.Start || p->Start>span.End ){
				++p;
				continue;
			}
			span = { std::min(span.Start, p->Start), std::max(span.End, p->End) };
			p = spans.erase( p );
		}
		spans.insert( std::ranges::upper_bound(spans, span.Start, {}, &Oee::Span::Start), span );
	}
	Ω rowWidth( const DB::Row& r )ι->optional<Duration>{//ms as a width of the list;  none for the ragged first row.
		let ms = r.Get<uint32>( 1 );
		return ms%60000==0 && std::ranges::contains(Oee::Widths, ms/60000) ? optional<Duration>{ milliseconds{ms} } : nullopt;
	}

	OeeMachineHost::OeeMachineHost( OeeMachineRow row, Inputs inputs, sp<HistGroupHost> group, sp<Hist::IClock> clock, Settings settings, Duration offset )ι:
		_row{ move(row) }, _inputs{ move(inputs) }, _group{ move(group) }, _clock{ move(clock) }, _settings{ settings }, _offset{ offset },
		_writer{ ms<Writer>(_row.Id, _row.Name) }, _strand{ boost::asio::make_strand(*Executor()) }, _width{ _row.Width }
	{}
	OeeMachineHost::~OeeMachineHost(){ End(); }

	α OeeMachineHost::Start( Failed failed, Takeover takeover )ι->void{
		_failed = move( failed );
		_takeover = move( takeover );
		_writer->OnFailure = [weak=weak_from_this()]( string why ){
			if( auto self = weak.lock(); self ){
				self->End();
				self->_failed( self->Id(), self.get(), move(why) );
			}
		};
		_writer->OnRebuild = [weak=weak_from_this()]{//under the engine's lock:  the loop runs on the executor, where it can take that lock.
			if( auto self = weak.lock(); self )
				Post( [self]{ self->Rebuilding(); } );
		};
		_writer->StartBegan();
		Post( [self=shared_from_this()]{ self->Starting(); } );
	}
	α OeeMachineHost::Starting()ι->VoidTask{
		let self = shared_from_this();
		absl::Cleanup started = [this]{ _writer->StartEnded(); };
		string failure;
		try{
			THROW_IF( !_row.Processed, "it has no `processed`:  its create's live side didn't run." );
			let from = *_row.Processed;
			//What a run before this one wrote past `processed` is about to be replayed.
			if( _ended )
				co_return;
			co_await Any( DS()->Execute(OeeSql::DeleteIntervalsFrom(Id(), from)) );
			if( _ended )
				co_return;
			co_await Any( DS()->Execute(OeeSql::DeleteStopsFrom(Id(), from)) );
			if( _ended )
				co_return;
			Oee::Opening opening{ .From=from };
			if( auto over = co_await Any( DS()->SelectAsync(OeeSql::SelectStopOver(Id(), from)) ); over.size() ){
				opening.Open = OeeSql::ToStop( over.front() );
				if( opening.Open->End && !_ended )//the replay ends it again.
					co_await Any( DS()->Execute(OeeSql::ClearStopFinish(Id(), opening.Open->Start)) );
			}
			//One tick before:  a read flags a record at its start as the bound, and one stamped `processed` has to replay.
			let start = UADateTime{ from }.UA()-1;
			auto page = co_await ExecutorAwait<Hist::ReadResult>{ [&]{ return Subscribe( start ); } };
			for( let& v : page.Values ){
				if( v.Bound )
					opening.Bounds.push_back( v.Value );
			}
			opening.LastGood = co_await ExecutorAwait<vector<Hist::Proto::DataValue>>{ [&]{ return lastGood( *_group->Group(), _inputs.Signals, opening.Bounds, start, _settings.ReadLimit ); } };
			auto engine = ms<Oee::Engine>( Oee::EngineConfig{_inputs.Signals, _inputs.SmallStop, _inputs.IdleTimeout, _row.Width, _settings.Settle}, move(opening), _writer, _clock );
			vector<Hist::Proto::HistoryRecord> early;
			Duration width;
			{
				lg _{ _queueMutex };
				if( _ended )
					co_return;
				_engine = engine;
				early = move( _early );
				width = _width;
			}
			if( width!=_row.Width )
				engine->SetWidth( width );
			engine->Records( move(early) );//held until Live, as what OnRecords hands it from here on is.
			for( ;; ){
				co_await ExecutorAwait<void>{ [&]{ engine->Replay( page.Values ); } };
				co_await Written();//a page's rows are written before the next is read:  a year's backfill queues a page's worth.
				if( page.Continuation.empty() || _ended )
					break;
				page = co_await ExecutorAwait<Hist::ReadResult>{ [&]{ return Next(); } };
			}
			if( _ended )
				co_return;
			engine->Live( _offset );
			_live = true;
			INFO( "[{}]OEE is live from {}{}.", _row.Name, ToIsoString(from), _offset==Duration::zero() ? string{} : Ƒ(", with a source offset of {}", Chrono::ToString(_offset)) );
		}
		catch( const std::exception& e ){
			failure = e.what();
		}
		if( failure.size() ){
			End();
			_failed( Id(), this, move(failure) );
		}
	}
	α OeeMachineHost::Subscribe( UA_DateTime from )ε->Hist::ReadResult{
		vector<Hist::NodeIndex> nodes;
		for( let& signal : _inputs.Signals ){
			if( std::ranges::find(nodes, signal.Node)==nodes.end() )
				nodes.push_back( signal.Node );
		}
		auto subscription = _group->Group()->Subscribe( shared_from_this(), move(nodes), from );
		lg _{ _pageMutex };
		if( _ended ){//End found nothing to end.
			subscription->End();
			return {};
		}
		_subscription = move( subscription );
		return _subscription->Next( _settings.ReadLimit );
	}
	α OeeMachineHost::Next()ε->Hist::ReadResult{
		lg _{ _pageMutex };
		return _ended || !_subscription ? Hist::ReadResult{} : _subscription->Next( _settings.ReadLimit );
	}
	//One span at a time (spec *The engine*, *Intervals*):  its rows read back and their span widened to whole rows, then
	//through any stop that spans their end, so the fresh engine ends on running time;  a span that reaches `processed`, a
	//machine whose count is its state, or a stop in progress at the end, is a takeover instead, as is a failure once the rows
	//are deleted, which starts the machine again from the span's start and heals it.
	α OeeMachineHost::Rebuilding()ι->VoidTask{
		let self = shared_from_this();
		absl::Cleanup done = [this]{ _writer->RebuildEnded(); };
		optional<TimePoint> deleted;//a rebuild's start once its rows are deleted:  a failure after that is a takeover from it.
		string failure;
		try{
			for( ;; ){
				co_await QuietAwait{ _writer, ForWrites|ForStart, SRCE_CUR };
				auto asks = _writer->Asks();
				if( asks.empty() || _ended )
					co_return;
				let group = _group->Group();
				vector<Oee::Span> spans;
				for( auto& ask : asks ){
					if( ask.Node ){
						if( let next = co_await ExecutorAwait<optional<TimePoint>>{ [&]{ return nextRecord( *group, ask.Node, ask.Span.Start, ask.Span.End, _settings.ReadLimit ); } }; next )
							ask.Span.End = std::min( ask.Span.End, *next );
					}
					merge( spans, ask.Span );
				}
				for( let& span : spans ){
					auto engine = Current();
					if( !engine || _ended )
						co_return;
					let processed = engine->Processed();
					co_await Written();//every row up to `processed` is in the table.
					auto rows = co_await Any( DS()->SelectAsync(OeeSql::SelectIntervalsOver(Id(), span)) );
					if( rows.empty() && span.End<processed ){
						DBG( "[{}]Nothing to rebuild over {} to {}:  no row is over it.", _row.Name, ToIsoString(span.Start), ToIsoString(span.End) );
						continue;
					}
					let start = rows.empty() ? processed : OeeSql::ToTime( rows.front(), 0 );
					let takeover = [&]( sv why ){ _takeover( Id(), this, start, engine->State().Offset, Ƒ("{} to {}:  {}", ToIsoString(span.Start), ToIsoString(span.End), why) ); };
					if( span.End>=processed || !Oee::Find(_inputs.Signals, Oee::ERole::State) ){
						takeover( span.End>=processed ? "the span reaches the open interval" : "the count is the machine's state, whose idle stops the clock finds" );
						co_return;
					}
					TimePoint end{};
					vector<Oee::Stop> stops;
					for( auto reach = span.End;; ){
						auto to = start;
						for( let& r : rows )
							to = std::max( to, OeeSql::ToTime(r, 0)+milliseconds{r.Get<uint32>(1)} );
						if( to<=end ){
							takeover( Ƒ("the rows over it stop at {}, inside a stop that ends at {}", ToIsoString(to), ToIsoString(reach)) );
							co_return;
						}
						end = to;
						stops.clear();
						for( let& r : co_await Any(DS()->SelectAsync(OeeSql::SelectStopsOver(Id(), start, end))) )
							stops.push_back( OeeSql::ToStop(r) );
						let spanning = std::ranges::find_if( stops, [end]( const Oee::Stop& s ){ return s.Start<end && (!s.End || *s.End>end); } );
						if( spanning==stops.end() )
							break;
						if( !spanning->End || *spanning->End>=processed ){
							takeover( !spanning->End ? "its end falls inside the stop in progress" : "the stop over its end reaches the open interval" );
							co_return;
						}
						reach = *spanning->End;
						rows = co_await Any( DS()->SelectAsync(OeeSql::SelectIntervalsOver(Id(), {span.Start, reach})) );
					}
					//The widths the rows were written at:  the first full row's, then each change from its first row on.
					optional<Duration> width;
					vector<std::pair<TimePoint,Duration>> widths;
					for( let& r : rows ){
						let w = rowWidth( r );
						if( !w )
							continue;
						if( !width )
							width = *w;
						else if( *w!=(widths.empty() ? *width : widths.back().second) )
							widths.emplace_back( OeeSql::ToTime(r, 0), *w );
					}
					Oee::Opening opening{ .From=start, .Widths=move(widths) };
					optional<Oee::Stop> over;//the stop over the start, which the replay ends again.
					if( let p = std::ranges::find_if(stops, [start]( const Oee::Stop& s ){ return s.Start<start; }); p!=stops.end() )
						opening.Open = over = *p;
					let before = UADateTime{ start }.UA()-1;//a record at `start` replays.
					vector<Hist::NodeIndex> nodes;
					for( let& signal : _inputs.Signals ){
						if( !std::ranges::contains(nodes, signal.Node) )
							nodes.push_back( signal.Node );
					}
					Hist::ReadRequest request{ .Nodes=move(nodes), .Start=before, .End=UADateTime{end}.UA(), .Bounds=true, .Limit=_settings.ReadLimit };
					auto page = co_await ExecutorAwait<Hist::ReadResult>{ [&]{ return group->Read( request ); } };
					for( let& v : page.Values ){
						if( v.Bound )
							opening.Bounds.push_back( v.Value );
					}
					opening.LastGood = co_await ExecutorAwait<vector<Hist::Proto::DataValue>>{ [&]{ return lastGood( *group, _inputs.Signals, opening.Bounds, before, _settings.ReadLimit ); } };
					auto sink = ms<Rebuilt>();
					Oee::Engine fresh{ Oee::EngineConfig{_inputs.Signals, _inputs.SmallStop, _inputs.IdleTimeout, width.value_or(_row.Width), _settings.Settle}, move(opening), sink, _clock };
					for( ;; ){
						fresh.Replay( page.Values );
						if( page.Continuation.empty() || _ended )
							break;
						request.Continuation = move( page.Continuation );
						page = co_await ExecutorAwait<Hist::ReadResult>{ [&]{ return group->Read( request ); } };
					}
					if( _ended )
						co_return;
					fresh.Finish( end );
					for( let& [node, asked] : sink->Asked )
						WARN( "[{}]The rebuild over {} to {} asked for one of its own, member {} over {} to {}:  a record of the span's is out of its order.", _row.Name, ToIsoString(start), ToIsoString(end), node, ToIsoString(asked.Start), ToIsoString(asked.End) );
					Duration covered{};
					for( let& row : sink->Rows )
						covered += row.Width;
					if( covered!=end-start ){
						takeover( Ƒ("the rows made again cover {} of the {} between {} and {}:  the width in force isn't the rows'", Chrono::ToString(covered), Chrono::ToString(end-start), ToIsoString(start), ToIsoString(end)) );
						co_return;
					}
					if( let open = std::ranges::find_if(sink->Stops, []( let& s ){ return !s.second.End; }); open!=sink->Stops.end() && open->first>=start && open->first<end ){
						takeover( Ƒ("it ends inside a stop from {} the rows didn't have", ToIsoString(open->first)) );
						co_return;
					}
					vector<DB::Sql> statements{ OeeSql::DeleteStopsBetween(Id(), start, end), OeeSql::DeleteIntervalsBetween(Id(), start, end) };
					if( over ){
						let p = sink->Stops.find( over->Start );
						statements.push_back( p==sink->Stops.end() ? OeeSql::DeleteStop(Id(), over->Start) : OeeSql::UpdateStop(Id(), p->second) );
					}
					uint inserted{};
					for( let& [at, stop] : sink->Stops ){
						if( at>=start && at<end ){
							statements.push_back( OeeSql::InsertStop(Id(), stop) );
							++inserted;
						}
					}
					for( uint i=0; i<sink->Rows.size(); i+=OeeSql::IntervalsPerInsert )
						statements.push_back( OeeSql::InsertIntervals(Id(), std::span{sink->Rows}.subspan(i, std::min<uint>(OeeSql::IntervalsPerInsert, sink->Rows.size()-i))) );
					for( auto& sql : statements ){
						if( _ended )
							co_return;
						co_await Any( DS()->Execute(move(sql)) );
						deleted = start;
					}
					deleted.reset();
					INFO( "[{}]Rebuilt {} to {} for {} to {}:  {} rows and {} stops{}.", _row.Name, ToIsoString(start), ToIsoString(end), ToIsoString(span.Start), ToIsoString(span.End), sink->Rows.size(), inserted, over ? Ƒ(", the stop from {} ended again", ToIsoString(over->Start)) : string{} );
				}
			}
		}
		catch( const std::exception& e ){
			failure = e.what();
		}
		if( failure.empty() || _ended )
			co_return;
		if( deleted ){
			auto engine = Current();
			_takeover( Id(), this, *deleted, engine ? engine->State().Offset : Duration::zero(), Ƒ("a rebuild from {} failed with its rows deleted:  {}", ToIsoString(*deleted), failure) );
		}
		else{
			End();
			_failed( Id(), this, Ƒ("a rebuild failed:  {}", failure) );
		}
	}
	α OeeMachineHost::End()ι->void{
		if( _ended.exchange(true) )
			return;
		_live = false;
		sp<Oee::Engine> engine;
		{
			lg _{ _queueMutex };
			engine = move( _engine );
			_early.clear();
		}
		if( engine )
			engine->End();
		_writer->End();
		lg _{ _pageMutex };
		if( _subscription )
			_subscription->End();
	}
	α OeeMachineHost::Quiet( SL sl )ι->QuietAwait{ return QuietAwait{ _writer, ForAll, sl }; }
	α OeeMachineHost::Written( SL sl )ι->QuietAwait{ return QuietAwait{ _writer, ForWrites, sl }; }
	α OeeMachineHost::Idle()Ι->bool{ return _writer->Idle(); }
	α OeeMachineHost::Current()Ι->sp<Oee::Engine>{
		lg _{ _queueMutex };
		return _engine;
	}
	α OeeMachineHost::State()Ι->optional<Oee::LiveState>{
		auto engine = Current();
		return engine ? optional<Oee::LiveState>{ engine->State() } : nullopt;
	}
	α OeeMachineHost::SetWidth( Duration width )ε->void{
		sp<Oee::Engine> engine;
		{
			lg _{ _queueMutex };
			_width = width;
			engine = _engine;
		}
		if( engine )
			engine->SetWidth( width );
	}
	α OeeMachineHost::Recompute( Oee::Span span )ι->void{ _writer->Asked( {0, span} ); }
	α OeeMachineHost::Rebuilds()Ι->uint{ return _writer->Rebuilds(); }

	α OeeMachineHost::OnRecords( vector<Hist::Proto::HistoryRecord>&& records )ι->void{
		if( _ended )
			return;
		sp<Oee::Engine> engine;
		{
			lg _{ _queueMutex };
			if( !_engine ){//the snapshot's first page is still out:  the engine is made from its bounds.
				std::ranges::move( records, std::back_inserter(_early) );
				return;
			}
			engine = _engine;
		}
		boost::asio::post( _strand, [engine=move(engine), records=move(records)]()mutable{ engine->Records( move(records) ); } );
	}
	α OeeMachineHost::OnEnded( Hist::ESubscriptionEnd why )ι->void{
		Post( [self=shared_from_this(), why]{
			if( !self->_ended )
				INFO( "[{}]OEE ended with its group's subscription:  the {}.", self->_row.Name, why==Hist::ESubscriptionEnd::Removed ? "group was removed" : "historian stopped" );
			self->End();
		} );
	}
}