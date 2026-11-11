#include "OeeLineFeed.h"
#include <jde/fwk/co/AllAwait.h>
#include <jde/fwk/process/execution.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/calendar/client/Calendars.h>
#include "OeeHost.h"
#include "OeeSubscriber.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };
	Ω stamp( TimePoint t )ι->google::protobuf::Timestamp{ return UADateTime{ t }.ToProto(); }
	Ω schedule( optional<uint32> calendar )ι->sp<const Calendar::Schedule>{ return calendar ? Calendar::Client::Calendars::Find( *calendar ) : nullptr; }

	OeeLineFeed::OeeLineFeed( OeeLinePK line, sp<Hist::IClock> clock, Duration live )ι:
		_id{ line }, _clock{ move(clock) }, _live{ live }
	{}

	α OeeLineFeed::Moved( optional<OeeMachinePK> machine )ι->void{
		{
			lg _{ _mutex };
			if( _subscribers.empty() || _queued || (machine && !_reach.contains(*machine)) )
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
		}
		for( let& subscriber : ended )
			subscriber->OnEnded( status, string{what} );
	}
	α OeeLineFeed::Remove( const OeeSubscriber* subscriber )ι->void{
		lg _{ _mutex };
		if( std::erase_if(_subscribers, [subscriber]( let& s ){ return s.get()==subscriber; }) && _subscribers.empty() )
			Idle();
	}

	α OeeLineFeed::Adding( AddAwait& await, sp<OeeSubscriber> subscriber )ι->VoidTask{
		let self = shared_from_this();
		let refuse = [this]{//under _mutex.
			if( _gone )
				throw Exception{ SRCE_CUR, ExceptionArgs{_gone->first}, "{}", _gone->second };
		};
		try{
			{
				lg _{ _mutex };
				refuse();
			}
			auto held = co_await _work.Lock();
			FromServer::OeeStatus status;
			let began = _clock->Now();
			co_await Compute( status );
			sp<const string> pushed;
			vector<sp<OeeSubscriber>> others;
			{
				lg _{ _mutex };
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
			}
			for( let& other : others )
				other->OnStatus( pushed );
			held.unlock();
			await.Resume( move(status) );
		}
		catch( runtime_error& e ){
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
			co_await Compute( status );
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
	α OeeLineFeed::Compute( FromServer::OeeStatus& y )ι->AnyVoidTask{
		++_reads;
		auto& host = OeeHost::Instance();
		let reached = host.Reach( _id );
		if( !reached )
			throw Exception{ SRCE_CUR, ExceptionArgs{EHttpStatus::NotFound}, "Line {} not found.", _id };
		let& line = reached->Line;
		if( line.Deleted )
			throw Exception{ SRCE_CUR, ExceptionArgs{EHttpStatus::NotFound}, "Line '{}' is deleted.", line.Name };
		let now = _clock->Now();
		let calendar = schedule( line.Calendar );
		let window = CurrentOeeWindow( calendar.get(), now );
		let& shift = window ? window->Shift : optional<Calendar::Occurrence>{};
		y.set_planned( !calendar || (shift && std::ranges::none_of(shift->Breaks, [now]( const Calendar::Span& b ){ return b.Start<=now && now<b.End; })) );
		y.set_calendar_missing( line.Calendar && !calendar );
		if( shift ){
			y.mutable_shift()->set_name( shift->Name );
			*y.mutable_shift()->mutable_start() = stamp( shift->Start );
			*y.mutable_shift()->mutable_end() = stamp( shift->End );
		}
		auto& counts = *y.mutable_machines();
		flat_set<OeeMachinePK> reach;
		vector<Oee::Bucket> buckets;
		optional<vector<Oee::Reason>> reasons;
		TimePoint end{};
		for( let& machine : reached->Machines ){
			let id = machine.Row.Id;
			reach.emplace( id );
			if( machine.Row.Deleted )//out of the line's figures until its restore (spec *The engine*).
				continue;
			//The engine's unwritten rows before the tables':  a row written between the two is then in both, and the table's is kept.
			let engine = host.Engine( id );
			let state = engine && engine->Live() ? engine->State() : nullopt;
			if( state && state->State==Oee::EState::Running )
				counts.set_running( counts.running()+1 );
			else if( state && state->State==Oee::EState::Stopped )
				counts.set_stopped( counts.stopped()+1 );
			else
				counts.set_unknown( counts.unknown()+1 );
			if( !window )
				continue;
			Oee::Span range{ window->Range.Start, std::min(state ? state->At : now, window->Range.End) };
			if( range.End<=range.Start )
				continue;
			if( !reasons ){
				reasons.emplace();
				for( let& row : co_await Any(DS()->SelectAsync(OeeSql::SelectReasons())) )
					reasons->push_back( OeeSql::ToReason(row) );
			}
			auto [intervalRows, stopRows] = co_await All( DS()->SelectAsync(OeeSql::SelectIntervalRows(id, range)), DS()->SelectAsync(OeeSql::SelectStopsOver(id, range.Start, range.End)) );
			vector<Oee::Interval> rows;
			for( let& row : intervalRows )
				rows.push_back( OeeSql::ToInterval(row) );
			if( state )
				AppendUnwritten( rows, state->Rows, range );
			else//no engine runs it:  its figures end where its rows do.
				range.End = std::min( range.End, rows.empty() ? range.Start : rows.back().End() );
			if( range.End<=range.Start )
				continue;
			vector<Oee::Stop> stops;
			for( let& row : stopRows )
				stops.push_back( OeeSql::ToStop(row) );
			if( state && state->Open ){//as the engine has it now:  its code may have been learned since its row.
				if( auto p = std::ranges::find(stops, state->Open->Start, &Oee::Stop::Start); p!=stops.end() )
					*p = *state->Open;
				else
					stops.insert( std::ranges::upper_bound(stops, state->Open->Start, {}, &Oee::Stop::Start), *state->Open );
			}
			//with the stops over the window, whole:  a stop that began before it is coded from its start.
			vector<Oee::Coding> codings;
			for( let& row : co_await Any(DS()->SelectAsync(OeeSql::SelectCodingsOver(id, {stops.empty() ? range.Start : std::min(range.Start, stops.front().Start), range.End}))) )
				codings.push_back( OeeSql::ToCoding(row).Value );
			try{//each machine's planned time is its own calendar's (spec *Lines*).
				if( auto figures = Oee::Figures(ToMachine(machine.Row, machine.Signals), {rows, stops, codings, *reasons}, schedule(machine.Row.Calendar).get(), range.Start, range.End); figures.size() ){
					buckets.push_back( move(figures.front()) );
					end = std::max( end, range.End );
				}
			}
			catch( const std::exception& e ){
				DBG( "[{}]The live figures couldn't be computed:  {}", machine.Row.Name, e.what() );
			}
		}
		if( window && buckets.size() ){
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