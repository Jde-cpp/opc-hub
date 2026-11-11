#pragma once
#include "OeeFeed.h"

//What a machine's feed, a line's feed and the reads (OeeQLAwait) share:  a machine's rows read over a range, the reasons, a
//status's shift, and several reads awaited at once.
namespace Jde::Opc::Gateway{
	α OeeStamp( TimePoint t )ι->google::protobuf::Timestamp;
	α OeeSchedule( optional<uint32> calendar )ι->sp<const Calendar::Schedule>;//the client's snapshot:  none without a calendar, or one it doesn't hold.
	//A status's planned, calendar_missing and shift, by `schedule`, `calendar`'s snapshot, at `now` in `window`.
	α SetOeeShift( FromServer::OeeStatus& y, optional<uint32> calendar, const Calendar::Schedule* schedule, const optional<OeeWindow>& window, TimePoint now )ι->void;
	//The engine's open stop as it has it now in place of its row, whose code may have been learned since, or added when unwritten.
	α SpliceOpenStop( vector<Oee::Stop>& stops, const optional<Oee::LiveState>& state )ι->void;

	//A machine's rows for a read or a status.  Now:  where an open stop ends, the engine's time, else the clock's, or for a
	//`stopsOnly` read the table's processed, where the rows end, which the host's copy lags.
	struct OeeMachineRows final{
		TimePoint Now;
		vector<Oee::Interval> Rows;//by Start, the engine's unwritten after the table's.
		vector<Oee::Stop> Stops;//by Start, those that overlap the range.
		vector<OeeCodingRow> Codings;//by Start, over the stops whole:  a stop is coded from its start.
		α Values()Ι->vector<Oee::Coding>;
	};
	//`machine`'s stops over `range`, its intervals unless `stopsOnly`, with `live`'s unwritten rows after them - a row written
	//between the two is then in both, and the table's kept - then the codings over the stops.
	α LoadOeeRows( OeeMachinePK machine, Oee::Span range, const optional<Oee::LiveState>& live, bool stopsOnly, OeeMachineRows& y )ι->AnyVoidTask;
	α LoadOeeReasons( vector<Oee::Reason>& y )ι->AnyVoidTask;//every reason, the deleted among them.

	//Each task started at once, the awaiter resumed once every one has ended:  with the first failure in the tasks' order, else
	//none.  All() for a number known only at run time.
	struct AllTasksAwait final : AnyVoidAwait{
		AllTasksAwait( vector<AnyVoidTask>&& tasks, SRCE )ι:AnyVoidAwait{ sl }, _tasks{ move(tasks) }, _errors( _tasks.size() ){}
	protected:
		α Suspend()ι->void override;
	private:
		α Run( uint i )ι->VoidTask;
		α Done()ι->void;//the last of the tasks and Suspend itself resumes the awaiter, so Suspend's loop never outlives it.
		vector<AnyVoidTask> _tasks;
		vector<up<Exception>> _errors;
		std::atomic<uint> _left;
	};
	Ξ AllTasks( vector<AnyVoidTask>&& tasks, SRCE )ι->AllTasksAwait{ return AllTasksAwait{ move(tasks), sl }; }
}