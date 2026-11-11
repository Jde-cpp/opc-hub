#include "OeeReads.h"
#include <jde/fwk/co/AllAwait.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/calendar/client/Calendars.h>
#include "OeeHost.h"

#define let const auto
namespace Jde::Opc{
	α Gateway::OeeStamp( TimePoint t )ι->google::protobuf::Timestamp{ return UADateTime{ t }.ToProto(); }
	α Gateway::OeeSchedule( optional<uint32> calendar )ι->sp<const Calendar::Schedule>{ return calendar ? Calendar::Client::Calendars::Find( *calendar ) : nullptr; }

	α Gateway::SetOeeShift( FromServer::OeeStatus& y, optional<uint32> calendar, const Calendar::Schedule* schedule, const optional<OeeWindow>& window, TimePoint now )ι->void{
		let& shift = window ? window->Shift : optional<Calendar::Occurrence>{};
		y.set_planned( !schedule || (shift && std::ranges::none_of(shift->Breaks, [now]( const Calendar::Span& b ){ return b.Start<=now && now<b.End; })) );
		y.set_calendar_missing( calendar && !schedule );
		if( shift ){
			y.mutable_shift()->set_name( shift->Name );
			*y.mutable_shift()->mutable_start() = OeeStamp( shift->Start );
			*y.mutable_shift()->mutable_end() = OeeStamp( shift->End );
		}
	}
	α Gateway::SpliceOpenStop( vector<Oee::Stop>& stops, const optional<Oee::LiveState>& state )ι->void{
		if( !state || !state->Open )
			return;
		if( auto p = std::ranges::find(stops, state->Open->Start, &Oee::Stop::Start); p!=stops.end() )
			*p = *state->Open;
		else
			stops.insert( std::ranges::upper_bound(stops, state->Open->Start, {}, &Oee::Stop::Start), *state->Open );
	}

	α Gateway::OeeMachineRows::Values()Ι->vector<Oee::Coding>{
		vector<Oee::Coding> y; y.reserve( Codings.size() );
		for( let& coding : Codings )
			y.push_back( coding.Value );
		return y;
	}
	α Gateway::LoadOeeRows( OeeMachinePK machine, Oee::Span range, const optional<Oee::LiveState>& live, bool stopsOnly, OeeMachineRows& y )ι->AnyVoidTask{
		let clock = OeeHost::Instance().Time();
		y.Now = live ? live->At : clock ? clock->Now() : Clock::now();
		auto stopsSql = OeeSql::SelectStopsOver( machine, range.Start, range.End );
		if( stopsOnly ){
			auto [stopRows, machines] = co_await All( DS()->SelectAsync(move(stopsSql)), SelectOeeMachines({.Key=DB::Key{(uint)machine}, .IncludeDeleted=true}) );
			for( let& row : stopRows )
				y.Stops.push_back( OeeSql::ToStop(row) );
			if( !live && machines.size() && machines.front().Processed )
				y.Now = *machines.front().Processed;
		}
		else{
			auto [stopRows, intervalRows] = co_await All( DS()->SelectAsync(move(stopsSql)), DS()->SelectAsync(OeeSql::SelectIntervalRows(machine, range)) );
			for( let& row : stopRows )
				y.Stops.push_back( OeeSql::ToStop(row) );
			for( let& row : intervalRows )
				y.Rows.push_back( OeeSql::ToInterval(row) );
			if( live )
				AppendUnwritten( y.Rows, live->Rows, range );
		}
		auto coded = range;
		for( let& stop : y.Stops ){
			coded.Start = std::min( coded.Start, stop.Start );
			coded.End = std::max( coded.End, stop.End.value_or(y.Now) );
		}
		for( let& row : co_await Any(DS()->SelectAsync(OeeSql::SelectCodingsOver(machine, coded))) )
			y.Codings.push_back( OeeSql::ToCoding(row) );
	}
	α Gateway::LoadOeeReasons( vector<Oee::Reason>& y )ι->AnyVoidTask{
		for( let& row : co_await Any(DS()->SelectAsync(OeeSql::SelectReasons())) )
			y.push_back( OeeSql::ToReason(row) );
	}

	α Gateway::AllTasksAwait::Suspend()ι->void{
		let count = _tasks.size();
		_left = count+1;
		for( uint i=0; i<count; ++i )
			Run( i );
		Done();
	}
	α Gateway::AllTasksAwait::Run( uint i )ι->VoidTask{
		try{
			co_await _tasks[i];
		}
		catch( Exception& e ){
			_errors[i] = e.Move();
		}
		catch( std::runtime_error& e ){
			_errors[i] = mu<Exception>( move(e) );
		}
		catch( ... ){
			_errors[i] = Exception{ "Unknown exception from a task.", {ELogLevel::Critical} }.Move();
		}
		Done();
	}
	α Gateway::AllTasksAwait::Done()ι->void{
		if( --_left )
			return;
		for( auto& error : _errors ){
			if( error ){
				ResumeExp( move(error) );
				return;
			}
		}
		Resume();
	}
}