#include "OeeLineFeed.h"
#include <jde/fwk/process/execution.h>
#include "../hist/HistHost.h"
#include "OeeHost.h"
#include "OeeReads.h"
#include "OeeSubscriber.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };
	//Read on every one of `groups`, as OeeSubscribe asks it (spec *Authorization*).
	Ω gate( const flat_set<HistGroupPK>& groups, UserPK user )ε->void{
		for( let group : groups )
			HistHost::Instance().Gate( group, Access::ERights::Read, false, user, SRCE_CUR );
	}

	OeeLineFeed::OeeLineFeed( OeeLinePK line, sp<Hist::IClock> clock, Duration live )ι:
		_id{ line }, _clock{ move(clock) }, _live{ live }
	{}

	α OeeLineFeed::Moved( optional<OeeMachinePK> machine )ι->void{
		{
			lg _{ _mutex };
			//During a join every machine counts:  the first's reach is known only once its computation ends.
			if( !Active() || _queued || (machine && !_adding && !_reach.contains(*machine)) )
				return;
			_queued = true;
			if( let wait = _computed+_live-_clock->Now(); wait>Duration::zero() ){
				_timer = _clock->Schedule( wait, [weak=weak_from_this(), serial=_serial]{
					auto self = weak.lock();
					if( !self )
						return;
					{
						lg _{ self->_mutex };
						if( serial!=self->_serial )
							return;
						self->_timer = 0;
					}
					Post( [self]{ self->Computing(); } );
				} );
				return;
			}
		}
		Post( [self=shared_from_this()]{ self->Computing(); } );
	}
	α OeeLineFeed::Count()ι->void{
		if( let active = Active(); active!=_counted ){
			_counted = active;
			OeeHost::Instance().LineFeedActive( active );
		}
	}
	α OeeLineFeed::Idle()ι->void{
		++_serial;
		if( auto timer = std::exchange(_timer, 0); timer )
			_clock->Cancel( timer );
		_queued = false;
		_last.reset();
	}
	α OeeLineFeed::EndAll( EHttpStatus status, sv what, bool gone )ι->void{
		vector<sp<OeeSubscriber>> ended;
		{
			lg _{ _mutex };
			if( gone )
				_gone = { status, string{what} };
			ended = std::exchange( _subscribers, {} );
			Idle();
			Count();
		}
		for( let& subscriber : ended )
			subscriber->OnEnded( status, string{what} );
	}
	α OeeLineFeed::Remove( const OeeSubscriber* subscriber )ι->void{
		lg _{ _mutex };
		if( !std::erase_if(_subscribers, [subscriber]( let& s ){ return s.get()==subscriber; }) )
			return;
		if( !Active() )
			Idle();
		Count();
	}
	α OeeLineFeed::Regate( const flat_set<HistGroupPK>& groups )ι->void{
		if( groups==_groups )
			return;
		_groups = groups;
		vector<sp<OeeSubscriber>> subscribers;
		{
			lg _{ _mutex };
			subscribers = _subscribers;
		}
		vector<std::pair<sp<OeeSubscriber>,std::pair<EHttpStatus,string>>> refused;
		for( auto& subscriber : subscribers ){
			try{
				gate( groups, subscriber->User() );
			}
			catch( const std::exception& e ){
				let exception = dynamic_cast<const Exception*>( &e );
				refused.emplace_back( move(subscriber), std::pair{exception ? exception->HttpStatus() : EHttpStatus::Forbidden, string{e.what()}} );
			}
		}
		if( refused.empty() )
			return;
		{
			lg _{ _mutex };
			std::erase_if( _subscribers, [&]( let& s ){ return std::ranges::contains(refused, s, [](let& r){ return r.first; }); } );
			if( !Active() )
				Idle();
			Count();
		}
		for( auto& [subscriber, why] : refused )
			subscriber->OnEnded( why.first, move(why.second) );
	}

	α OeeLineFeed::Adding( AddAwait& await, sp<OeeSubscriber> subscriber )ι->VoidTask{
		let self = shared_from_this();
		bool joining{};
		//Under _mutex:  purged or OEE stopping, or the line deleted since OeeSubscribe's check, which LinesChanged makes the host's
		//before it ends the subscriptions:  one that joins past this is among them.
		let refuse = [this]{
			if( _gone )
				throw Exception{ SRCE_CUR, ExceptionArgs{_gone->first}, "{}", _gone->second };
			OeeHost::Instance().TestLine( _id );
		};
		try{
			{
				lg _{ _mutex };
				refuse();
				++_adding;
				joining = true;
				Count();
			}
			auto held = co_await _work.Lock();
			FromServer::OeeStatus status;
			flat_set<HistGroupPK> groups;
			let began = _clock->Now();
			co_await Compute( status, groups );
			gate( groups, subscriber->User() );//OeeSubscribe's check was over the reach as it was then.
			Regate( groups );
			sp<const string> pushed;
			vector<sp<OeeSubscriber>> others;
			{
				lg _{ _mutex };
				--_adding;
				joining = false;
				refuse();
				if( !subscriber->Ended() ){//else an OeeUnsubscribe or the close came first:  the ack still answers.
					auto bytes = Jde::ms<const string>( status.SerializeAsString() );
					if( _subscribers.size() && (!_last || *bytes!=*_last) ){//what the ack says, the others hear.
						pushed = bytes;
						others = _subscribers;
						++_pushed;
					}
					_last = move( bytes );
					_computed = began;
					_subscribers.push_back( move(subscriber) );
				}
				else if( !Active() )
					Idle();
				Count();
			}
			for( let& other : others )
				other->OnStatus( pushed );
			held.unlock();
			await.Resume( move(status) );
		}
		catch( runtime_error& e ){
			{
				lg _{ _mutex };
				if( joining )
					--_adding;
				if( !Active() )
					Idle();
				Count();
			}
			await.ResumeExp( move(e) );
		}
	}
	α OeeLineFeed::Computing()ι->VoidTask{
		let self = shared_from_this();
		try{
			auto held = co_await _work.Lock();
			{
				lg _{ _mutex };
				_queued = false;
				if( _subscribers.empty() )
					co_return;
				_computed = _clock->Now();//one that moves during this waits `live` from here.
			}
			FromServer::OeeStatus status;
			flat_set<HistGroupPK> groups;
			co_await Compute( status, groups );
			Regate( groups );
			auto bytes = Jde::ms<const string>( status.SerializeAsString() );
			vector<sp<OeeSubscriber>> subscribers;
			{
				lg _{ _mutex };
				if( _subscribers.empty() || (_last && *bytes==*_last) )
					co_return;
				_last = bytes;
				subscribers = _subscribers;
				++_pushed;
			}
			for( let& subscriber : subscribers )
				subscriber->OnStatus( bytes );
		}
		catch( const std::exception& e ){//the line's end is the host's to tell (LinesChanged);  a read that failed is made again at the next change.
			DBG( "Line {}'s live status couldn't be computed:  {}", _id, e.what() );
		}
	}
	α OeeLineFeed::Compute( FromServer::OeeStatus& y, flat_set<HistGroupPK>& groups )ι->AnyVoidTask{
		++_reads;
		auto& host = OeeHost::Instance();
		let reached = host.Reach( _id );
		if( !reached )
			throw Exception{ SRCE_CUR, ExceptionArgs{EHttpStatus::NotFound}, "Line {} not found.", _id };
		let& line = reached->Line;
		if( line.Deleted )
			throw Exception{ SRCE_CUR, ExceptionArgs{EHttpStatus::NotFound}, "Line '{}' is deleted.", line.Name };
		let now = _clock->Now();
		let calendar = OeeSchedule( line.Calendar );
		let window = CurrentOeeWindow( calendar.get(), now );
		SetOeeShift( y, line.Calendar, calendar.get(), window, now );
		auto& counts = *y.mutable_machines();
		flat_set<OeeMachinePK> reach;
		//Each machine with time in the window so far, its rows read at once with the others'.
		struct Read final{ const OeeHost::Machine& Machine; optional<Oee::LiveState> State; Oee::Span Range; OeeMachineRows Rows; };
		vector<Read> reads; reads.reserve( reached->Machines.size() );
		for( let& machine : reached->Machines ){
			let id = machine.Row.Id;
			reach.emplace( id );
			groups.emplace( machine.Row.Group );
			if( machine.Row.Deleted )//out of the line's figures until its restore (spec *The engine*).
				continue;
			let engine = host.Engine( id );
			auto state = engine && engine->Live() ? engine->State() : nullopt;
			if( state && state->State==Oee::EState::Running )
				counts.set_running( counts.running()+1 );
			else if( state && state->State==Oee::EState::Stopped )
				counts.set_stopped( counts.stopped()+1 );
			else
				counts.set_unknown( counts.unknown()+1 );
			if( !window )
				continue;
			const Oee::Span range{ window->Range.Start, std::min(state ? state->At : now, window->Range.End) };
			if( range.End>range.Start )
				reads.push_back( {machine, move(state), range, {}} );
		}
		vector<Oee::Reason> reasons;
		if( reads.size() ){
			vector<AnyVoidTask> tasks; tasks.reserve( reads.size()+1 );
			tasks.push_back( LoadOeeReasons(reasons) );
			for( auto& r : reads )
				tasks.push_back( LoadOeeRows(r.Machine.Row.Id, r.Range, r.State, false, r.Rows) );
			co_await AllTasks( move(tasks) );
		}
		vector<Oee::Bucket> buckets;
		TimePoint end{};
		for( auto& r : reads ){
			auto range = r.Range;
			if( !r.State )//no engine runs it:  its figures end where its rows do.
				range.End = std::min( range.End, r.Rows.Rows.empty() ? range.Start : r.Rows.Rows.back().End() );
			if( range.End<=range.Start )
				continue;
			SpliceOpenStop( r.Rows.Stops, r.State );
			let codings = r.Rows.Values();
			try{//each machine's planned time is its own calendar's (spec *Lines*).
				if( auto figures = Oee::Figures(ToMachine(r.Machine.Row, r.Machine.Signals), {r.Rows.Rows, r.Rows.Stops, codings, reasons}, OeeSchedule(r.Machine.Row.Calendar).get(), range.Start, range.End); figures.size() ){
					buckets.push_back( move(figures.front()) );
					end = std::max( end, range.End );
				}
			}
			catch( const std::exception& e ){
				DBG( "[{}]The live figures couldn't be computed:  {}", r.Machine.Row.Name, e.what() );
			}
		}
		if( window && buckets.size() ){
			let& shift = window->Shift;
			let combined = Oee::Lines::Combine( window->Range.Start, end, shift ? shift->Name : string{}, buckets );
			auto& figures = *y.mutable_figures() = ToOeeFigures( combined );
			figures.set_machines( combined.Machines );
			figures.set_performance_coverage( combined.PerformanceCoverage );
			figures.set_quality_coverage( combined.QualityCoverage );
		}
		lg _{ _mutex };
		_reach = move( reach );
	}
}