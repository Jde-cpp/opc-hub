#include "OeeFeed.h"
#include <jde/fwk/co/AllAwait.h>
#include <jde/fwk/process/execution.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/calendar/client/Calendars.h>
#include "OeeHost.h"
#include "OeeSubscriber.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };

	Ω machine( const OeeMachineRow& row, const vector<OeeSignalRow>& rows )ι->Oee::Machine{
		let signals = ToSignals( rows );
		return { row.Id, row.Name, row.IdealCycle, row.SmallStop, Oee::HasCount(signals), Oee::MeasuresQuality(signals) };
	}
	Ω stamp( TimePoint t )ι->google::protobuf::Timestamp{ return UADateTime{ t }.ToProto(); }
	Ω ms( Duration d )ι->int64_t{ return std::chrono::duration_cast<std::chrono::milliseconds>( d ).count(); }
	Ω figures( const Oee::Bucket& b )ι->FromServer::OeeFigures{
		FromServer::OeeFigures y;
		*y.mutable_start() = stamp( b.Start );
		*y.mutable_end() = stamp( b.End );
		y.set_planned( ms(b.Planned) ); y.set_run( ms(b.Run) ); y.set_unplanned( ms(b.Unplanned) ); y.set_planned_stop( ms(b.PlannedStop) );
		y.set_small( ms(b.Small) ); y.set_excluded( ms(b.Excluded) ); y.set_unknown( ms(b.Unknown) );
		y.set_total( b.Total ); y.set_good( b.Good ); y.set_reject( b.Reject ); y.set_outside( b.Outside ); y.set_ideal( ms(b.Ideal) );
		if( b.Availability )
			y.set_availability( *b.Availability );
		if( b.Performance )
			y.set_performance( *b.Performance );
		if( b.Quality )
			y.set_quality( *b.Quality );
		if( b.OEE )
			y.set_oee( *b.OEE );
		if( b.TEEP )
			y.set_teep( *b.TEEP );
		y.set_measured( (uint32)b.Measured );
		y.set_partial( b.Partial );
		return y;
	}

	OeeFeed::OeeFeed( OeeMachineRow row, vector<OeeSignalRow> signals, sp<Hist::IClock> clock, Duration live )ι:
		_id{ row.Id }, _clock{ move(clock) }, _live{ live }, _row{ move(row) }, _machine{ machine(_row, signals) }
	{}

	α OeeFeed::Closed( const Oee::Interval& row )ι->void{
		lg _{ _mutex };
		if( !Holding() )
			return;
		if( _rows.empty() || row.Start>=_rows.back().End() )
			_rows.push_back( row );
		else if( auto p = std::ranges::find(_rows, row.Start, &Oee::Interval::Start); p!=_rows.end() )
			*p = row;
	}
	α OeeFeed::Told( const Oee::Stop& stop )ι->void{
		lg _{ _mutex };
		if( Holding() )
			_stops.insert_or_assign( stop.Start, stop );
	}
	α OeeFeed::Rewritten( TimePoint from, const vector<Oee::Stop>& stops )ι->void{
		lg _{ _mutex };
		if( !Holding() )
			return;
		_stops.erase( _stops.lower_bound(from), _stops.end() );
		for( let& stop : stops )
			_stops.insert_or_assign( stop.Start, stop );
		if( _collecting )
			_rewritten = _rewritten ? std::min( *_rewritten, from ) : from;
	}
	α OeeFeed::Changed()ι->void{
		{
			lg _{ _mutex };
			if( _subscribers.empty() )
				return;
		}
		Post( [self=shared_from_this()]{ self->Push(); } );
	}
	α OeeFeed::Reload()ι->void{
		{
			lg _{ _mutex };
			if( !Active() )
				return;
		}
		Post( [self=shared_from_this()]{ self->Reloading(); } );
	}
	α OeeFeed::Edited( OeeMachineRow row, vector<OeeSignalRow> signals )ι->void{
		let deleted = row.Deleted;
		{
			lg _{ _mutex };
			_row = move( row );
			_machine = machine( _row, signals );
			_gone.reset();//a purge that failed:  the machine stands.
		}
		if( deleted )
			EndAll( EHttpStatus::NotFound, "The machine was deleted, which ends the subscription.", false );
		else
			Reload();
	}
	α OeeFeed::EndAll( EHttpStatus status, sv what, bool gone )ι->void{
		vector<sp<OeeSubscriber>> ended;
		{
			lg _{ _mutex };
			if( gone )
				_gone = { status, string{what} };
			ended = std::exchange( _subscribers, {} );
			if( !Active() )
				Idle();
		}
		for( let& subscriber : ended )
			subscriber->OnEnded( status, string{what} );
	}
	α OeeFeed::Remove( const OeeSubscriber* subscriber )ι->void{
		lg _{ _mutex };
		if( std::erase_if(_subscribers, [subscriber]( let& s ){ return s.get()==subscriber; }) && !Active() )
			Idle();
	}
	α OeeFeed::Arm()ι->void{
		if( !_timer && !_subscribers.empty() )
			_timer = _clock->Schedule( _live, [weak=weak_from_this(), serial=_serial]{
				if( auto self = weak.lock(); self )
					self->Tick( serial );
			} );
	}
	α OeeFeed::Idle()ι->void{
		++_serial;
		if( auto timer = std::exchange(_timer, 0); timer )
			_clock->Cancel( timer );
		_loaded = false;
		_rows.clear();
		_stops.clear();
		_codings.clear();
		_reasons.clear();
		_last.clear();
	}
	α OeeFeed::Tick( uint serial )ι->void{
		bool unread;
		{
			lg _{ _mutex };
			if( serial!=_serial )
				return;
			_timer = 0;
			Arm();
			unread = !Holding();//a load that failed:  tried again.
		}
		if( unread )
			Reload();
		else
			Push();
	}
	α OeeFeed::State()Ι->optional<Oee::LiveState>{
		let engine = OeeHost::Instance().Engine( _id );
		return engine && engine->Live() ? engine->State() : nullopt;
	}
	α OeeFeed::Schedule()Ι->sp<const Calendar::Schedule>{
		return _row.Calendar ? Calendar::Client::Calendars::Find( *_row.Calendar ) : nullptr;
	}
	α OeeFeed::Current( const Calendar::Schedule* schedule, TimePoint now )Ι->optional<Window>{
		if( !schedule ){
			const TimePoint day{ std::chrono::floor<std::chrono::days>(now) };
			return Window{ nullopt, {day, day+std::chrono::days{1}} };
		}
		auto shift = schedule->At( now );
		if( !shift )
			return nullopt;
		const Oee::Span range{ shift->Start, shift->End };
		return Window{ move(shift), range };
	}

	α OeeFeed::Adding( AddAwait& await, sp<OeeSubscriber> subscriber )ι->VoidTask{
		let self = shared_from_this();
		bool joining{};
		let refuse = [this]{//under _mutex.
			if( _gone )
				throw Exception{ SRCE_CUR, ExceptionArgs{_gone->first}, "{}", _gone->second };
			if( _row.Deleted )
				throw Exception{ SRCE_CUR, ExceptionArgs{EHttpStatus::NotFound}, "Machine '{}' is deleted.", _row.Name };
		};
		try{
			{
				lg _{ _mutex };
				refuse();
				++_adding;
				joining = true;
			}
			auto held = co_await _work.Lock();
			bool loaded;
			{
				lg _{ _mutex };
				loaded = _loaded;
			}
			if( !loaded )
				co_await Load();
			let state = State();
			FromServer::OeeStatus status;
			{
				lg _{ _mutex };
				--_adding;
				joining = false;
				if( (_gone || _row.Deleted) && !Active() )
					Idle();
				refuse();
				status = Build( state );
				if( !subscriber->Ended() ){
					if( _subscribers.empty() )
						_last = status.SerializeAsString();
					_subscribers.push_back( move(subscriber) );
					Arm();
				}
				else if( !Active() )//an OeeUnsubscribe or the close came first:  the ack still answers.
					Idle();
			}
			held.unlock();
			await.Resume( move(status) );
		}
		catch( runtime_error& e ){
			if( joining ){
				lg _{ _mutex };
				--_adding;
				if( !Active() )
					Idle();
			}
			await.ResumeExp( move(e) );
		}
	}
	α OeeFeed::Reloading()ι->VoidTask{
		let self = shared_from_this();
		try{
			auto held = co_await _work.Lock();
			bool active;
			{
				lg _{ _mutex };
				active = Active();
			}
			if( active )
				co_await Load();
		}
		catch( const std::exception& e ){
			WARN( "[{}]The live feed's rows couldn't be read, and are read again in {}:  {}", _machine.Name, Chrono::ToString(_live), e.what() );
		}
		Push();
	}
	//The rows the sink tells from here on are held beside the read's:  one the engine closed before it is written by the time
	//the tables are read, the engine's writes being waited for first, and one closed after is held.
	α OeeFeed::Load()ι->AnyVoidTask{
		let engine = OeeHost::Instance().Engine( _id );
		{
			lg _{ _mutex };
			_loaded = false;
			_collecting = true;
			_rows.clear();
			_stops.clear();
			_rewritten.reset();
		}
		try{
			if( engine && engine->Live() )
				co_await engine->Written();
			let state = State();
			let now = state ? state->At : _clock->Now();
			optional<Window> window;
			{
				lg _{ _mutex };
				window = Current( Schedule().get(), now );
			}
			let from = window ? window->Range.Start : now;
			let until = now+hours{ 24 };
			auto [intervalRows, stopRows, reasonRows] = co_await All( DS()->SelectAsync(OeeSql::SelectIntervalRows(_id, {from, until})), DS()->SelectAsync(OeeSql::SelectStopsOver(_id, from, until)), DS()->SelectAsync(OeeSql::SelectReasons()) );
			vector<Oee::Stop> stops;
			for( let& row : stopRows )
				stops.push_back( OeeSql::ToStop(row) );
			//with the stops over the window, whole:  a stop that began before it is coded from its start.
			let codingRows = co_await Any( DS()->SelectAsync(OeeSql::SelectCodingsOver(_id, {stops.empty() ? from : std::min(from, stops.front().Start), until})) );
			lg _{ _mutex };
			vector<Oee::Interval> rows;
			for( let& row : intervalRows )
				rows.push_back( OeeSql::ToInterval(row) );
			for( let& row : _rows ){
				if( rows.empty() || row.Start>=rows.back().End() )
					rows.push_back( row );
			}
			_rows = move( rows );
			std::map<TimePoint,Oee::Stop> told = move( _stops );
			_stops.clear();
			for( let& stop : stops ){
				if( !_rewritten || stop.Start<*_rewritten )
					_stops.emplace( stop.Start, stop );
			}
			for( let& [start, stop] : told )
				_stops.insert_or_assign( start, stop );
			_codings.clear();
			for( let& row : codingRows )
				_codings.push_back( OeeSql::ToCoding(row).Value );
			_reasons.clear();
			for( let& row : reasonRows )
				_reasons.push_back( OeeSql::ToReason(row) );
			_collecting = false;
			_loaded = true;
		}
		catch( ... ){
			lg _{ _mutex };
			_collecting = false;
			_rows.clear();
			_stops.clear();
			throw;
		}
	}
	α OeeFeed::Push()ι->void{
		lg sending{ _sendMutex };
		let state = State();
		FromServer::OeeStatus status;
		vector<sp<OeeSubscriber>> subscribers;
		{
			lg _{ _mutex };
			if( !_loaded || _subscribers.empty() )
				return;
			status = Build( state );
			auto bytes = status.SerializeAsString();
			if( bytes==_last )
				return;
			_last = move( bytes );
			subscribers = _subscribers;
			++_pushed;
		}
		for( let& subscriber : subscribers )
			subscriber->OnStatus( status );
	}
	α OeeFeed::Build( const optional<Oee::LiveState>& state )ι->FromServer::OeeStatus{
		FromServer::OeeStatus y;
		let now = state ? state->At : _clock->Now();
		let schedule = Schedule();
		let window = Current( schedule.get(), now );
		let stopped = state && state->State==Oee::EState::Stopped && state->Open && !state->Open->Unknown;
		if( state ){
			y.set_state( state->State==Oee::EState::Running ? FromServer::OeeRunning : state->State==Oee::EState::Stopped ? FromServer::OeeStopped : FromServer::OeeUnknown );
			*y.mutable_since() = stamp( state->Since );
			if( state->LastCount )
				*y.mutable_last_count() = stamp( *state->LastCount );
		}
		if( stopped ){
			let& open = *state->Open;
			auto& stop = *y.mutable_stop();
			*stop.mutable_start() = stamp( open.Start );
			stop.set_small( state->Small );
			if( open.Code )
				stop.set_code( *open.Code );
			auto reason = _reasons.end();
			if( let coding = std::ranges::find_if(_codings, [&]( const Oee::Coding& c ){ return c.Start<=open.Start && c.End>=now; }); coding!=_codings.end() )
				reason = std::ranges::find( _reasons, coding->ReasonId, &Oee::Reason::Id );
			if( reason==_reasons.end() && open.Code )
				reason = std::ranges::find( _reasons, open.Code, &Oee::Reason::Code );
			if( reason!=_reasons.end() ){
				auto& cause = *stop.mutable_reason();
				cause.set_id( reason->Id );
				cause.set_name( reason->Name );
				cause.set_category( string{Oee::ToString(reason->Category)} );
			}
		}
		let& shift = window ? window->Shift : optional<Calendar::Occurrence>{};
		y.set_planned( !schedule || (shift && std::ranges::none_of(shift->Breaks, [now]( const Calendar::Span& b ){ return b.Start<=now && now<b.End; })) );
		if( shift ){
			y.mutable_shift()->set_name( shift->Name );
			*y.mutable_shift()->mutable_start() = stamp( shift->Start );
			*y.mutable_shift()->mutable_end() = stamp( shift->End );
		}
		if( !window ){//between shifts:  nothing held so far is in the next one.
			std::erase_if( _rows, [now]( const Oee::Interval& row ){ return row.End()<=now; } );
			std::erase_if( _stops, [now]( let& stop ){ return stop.second.End && *stop.second.End<=now; } );
		}
		if( !window || !_loaded )
			return y;
		let start = window->Range.Start;
		let end = std::min( now, window->Range.End );
		std::erase_if( _rows, [start]( const Oee::Interval& row ){ return row.End()<=start; } );
		std::erase_if( _stops, [start]( let& stop ){ return stop.second.End && *stop.second.End<=start; } );
		if( end<=start )
			return y;
		let held = _rows.size();
		if( state ){//the rows not yet written and the one still filling, after those held.
			for( let& row : state->Rows ){
				if( row.Width>Duration::zero() && row.End()>start && (_rows.empty() || row.Start>=_rows.back().End()) )
					_rows.push_back( row );
			}
		}
		vector<Oee::Stop> stops;
		for( let& [_, stop] : _stops )
			stops.push_back( stop );
		if( state && state->Open ){//as the engine has it now:  its code may have been learned since its row.
			if( auto p = std::ranges::find(stops, state->Open->Start, &Oee::Stop::Start); p!=stops.end() )
				*p = *state->Open;
			else
				stops.insert( std::ranges::upper_bound(stops, state->Open->Start, {}, &Oee::Stop::Start), *state->Open );
		}
		try{
			if( let buckets = Oee::Figures(_machine, {_rows, stops, _codings, _reasons}, schedule.get(), start, end); buckets.size() )
				*y.mutable_figures() = figures( buckets.front() );
		}
		catch( const std::exception& e ){
			DBG( "[{}]The live figures couldn't be computed:  {}", _machine.Name, e.what() );
		}
		_rows.resize( held );
		return y;
	}
}