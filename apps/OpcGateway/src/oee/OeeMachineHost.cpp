#include "OeeMachineHost.h"
#include <boost/asio/post.hpp>
#include <jde/opc/uatypes/DateTime.h>
#include "../async/ExecutorAwait.h"
#include "../hist/HistGroupHost.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };

	//What the engine tells its sink, queued under the engine's lock and written here, on the executor:  one batch at a time,
	//each what was queued when the last ended, so a settle tick's rows go out together.  Consecutive interval rows share an
	//insert, IntervalsPerInsert at most, and `processed` moves after each, so a backfill a crash interrupts resumes where its
	//last insert ended.  It holds no host:  the engine holds it, and a host that held the engine would be held by it.
	struct OeeMachineHost::Writer final : Oee::ISink, std::enable_shared_from_this<Writer>{
		struct StopOpened final{ Oee::Stop Row; };
		struct StopClosed final{ Oee::Stop Row; };
		struct StopsRewritten final{ TimePoint From; vector<Oee::Stop> Rows; };
		using Op = std::variant<Oee::Interval,StopOpened,StopClosed,StopsRewritten>;
		Writer( OeeMachinePK machine, string name )ι:_machine{ machine }, _name{ move(name) }{}

		α Closed( const Oee::Interval& row )ι->void override{ Queue( row ); }
		α Opened( const Oee::Stop& stop )ι->void override{ Queue( StopOpened{stop} ); }
		α Closed( const Oee::Stop& stop )ι->void override{ Queue( StopClosed{stop} ); }
		α Rewritten( TimePoint from, const vector<Oee::Stop>& stops )ι->void override{ Queue( StopsRewritten{from, stops} ); }
		//Nothing runs one yet (#401):  the rows a late record, an edit or an idle stop reaches stay as they were written.
		α Rebuild( uint32 node, Oee::Span span )ι->void override{
			++_rebuilds;
			DBG( "[{}]A rebuild is wanted for member {}, {} to {}.", _name, node, ToIsoString(span.Start), ToIsoString(span.End) );
		}
		α Live( const Oee::LiveState& )ι->void override{}//the live feed's (#403).

		α End()ι->void{ lg _{ _mutex }; _ended = true; _ops.clear(); }
		α Idle()Ι->bool{ lg _{ _mutex }; return !_writing && _ops.empty(); }
		α Writing()Ι->bool{ lg _{ _mutex }; return _writing; }
		α Wait( QuietAwait* waiter )ι->bool{//false when no write is running:  nothing to wait for.
			lg _{ _mutex };
			if( _writing )
				_waiters.push_back( waiter );
			return _writing;
		}
		α Rebuilds()Ι->uint{ return _rebuilds; }
		function<void( string why )> OnFailure;//set before the engine is made, so before anything is queued.
	private:
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
					y.push_back( OeeSql::DeleteStopsFrom(_machine, rewritten.From) );
					for( let& stop : rewritten.Rows )
						y.push_back( OeeSql::InsertStop(_machine, stop) );
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
						waiters = std::exchange( _waiters, {} );
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
		vector<QuietAwait*> _waiters;
		bool _writing{}, _ended{};
		std::atomic<uint> _rebuilds{};
	};
	α OeeMachineHost::QuietAwait::await_ready()ι->bool{ return !_writer->Writing(); }
	α OeeMachineHost::QuietAwait::Suspend()ι->void{
		if( !_writer->Wait(this) )//the write ended since await_ready.
			Resume();
	}

	Ω good( const Hist::Proto::DataValue& v )ι->bool{ return v.has_value() && !UA_StatusCode_isBad( v.status() ) && !UA_StatusCode_isUncertain( v.status() ); }
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

	OeeMachineHost::OeeMachineHost( OeeMachineRow row, Inputs inputs, sp<HistGroupHost> group, sp<Hist::IClock> clock, Settings settings )ι:
		_row{ move(row) }, _inputs{ move(inputs) }, _group{ move(group) }, _clock{ move(clock) }, _settings{ settings },
		_writer{ ms<Writer>(_row.Id, _row.Name) }, _strand{ boost::asio::make_strand(*Executor()) }
	{}
	OeeMachineHost::~OeeMachineHost(){ End(); }

	α OeeMachineHost::Start( Failed failed )ι->void{
		_writer->OnFailure = [weak=weak_from_this(), failed]( string why ){
			if( auto self = weak.lock(); self ){
				self->End();
				failed( self->Id(), self.get(), move(why) );
			}
		};
		Post( [self=shared_from_this(), failed=move(failed)]()mutable{ self->Starting( move(failed) ); } );
	}
	α OeeMachineHost::Starting( Failed failed )ι->VoidTask{
		let self = shared_from_this();
		string failure;
		try{
			THROW_IF( !_row.Processed, "it has no `processed`:  its create's live side didn't run." );
			let from = *_row.Processed;
			//What a run before this one wrote past `processed` is about to be replayed.
			co_await Any( DS()->Execute(OeeSql::DeleteIntervalsFrom(Id(), from)) );
			co_await Any( DS()->Execute(OeeSql::DeleteStopsFrom(Id(), from)) );
			Oee::Opening opening{ .From=from };
			if( auto over = co_await Any( DS()->SelectAsync(OeeSql::SelectStopOver(Id(), from)) ); over.size() ){
				opening.Open = OeeSql::ToStop( over.front() );
				if( opening.Open->End )//the replay ends it again.
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
			{
				lg _{ _queueMutex };
				if( _ended )
					co_return;
				_engine = engine;
				early = move( _early );
			}
			engine->Records( move(early) );//held until Live, as what OnRecords hands it from here on is.
			for( ;; ){
				co_await ExecutorAwait<void>{ [&]{ engine->Replay( page.Values ); } };
				co_await Quiet();//a page's rows are written before the next is read:  a year's backfill queues a page's worth.
				if( page.Continuation.empty() || _ended )
					break;
				page = co_await ExecutorAwait<Hist::ReadResult>{ [&]{ return Next(); } };
			}
			if( _ended )
				co_return;
			engine->Live();
			_live = true;
			INFO( "[{}]OEE is live from {}.", _row.Name, ToIsoString(from) );
		}
		catch( const std::exception& e ){
			failure = e.what();
		}
		if( failure.size() ){
			End();
			failed( Id(), this, move(failure) );
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
	α OeeMachineHost::Quiet( SL sl )ι->QuietAwait{ return QuietAwait{ _writer, sl }; }
	α OeeMachineHost::Idle()Ι->bool{ return _writer->Idle(); }
	α OeeMachineHost::State()Ι->optional<Oee::LiveState>{
		sp<Oee::Engine> engine;
		{
			lg _{ _queueMutex };
			engine = _engine;
		}
		return engine ? optional<Oee::LiveState>{ engine->State() } : nullopt;
	}
	α OeeMachineHost::SetWidth( Duration width )ε->void{
		sp<Oee::Engine> engine;
		{
			lg _{ _queueMutex };
			engine = _engine;
		}
		if( engine )
			engine->SetWidth( width );
	}
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