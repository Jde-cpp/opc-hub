#pragma once
#include <jde/fwk/co/CoMutex.h>
#include "OeeMachineHost.h"

namespace Jde::Opc::Gateway{
	//The gateway's OEE (docs/oee/spec.md *Placement*, *The engine*):  every oee_machines row with its signals, kept in step with
	//the tables by OeeMAwait, whose live side runs here, and an OeeMachineHost for each machine that can run.  The rows are held
	//whatever the settings, since OeeMAwait refuses a group's delete and a member's purge from them;  the engines need an `oee`
	//block and the historian, without which the OEE fields answer with an error (TestEnabled) and the tables still work.
	//A machine runs once it is live, has a `processed` and a signal set the library's rules pass, and its group is hosted.  One
	//an edit makes runnable starts `settle` after its last edit, so a form's signals land before the engine reads the past with
	//them;  those loaded at the gateway's start, and a restart after a failure, don't wait.
	//The lines (spec *Lines*) are held as the machines are:  both tables' rows, read again after each edit of either, for
	//OeeMAwait's rules, the reads' reach and each line's live feed, made when it is first subscribed.
	struct OeeFeed; struct OeeLineFeed;
	struct OeeHost final : noncopyable{
		Ω Instance()ι->OeeHost&;
		//The settings and the rows, then each machine's engine, on the executor.  Blocks on the rows' load:  Gateway::Connect's
		//thread, after the historian's start.  `clock`:  the engines' and the timers', the system's when none - a test hands a manual one.
		α Start( sp<Hist::IClock> clock={} )ε->void;
		//Every engine ends and its start and write in flight are waited for:  before the historian's stop, while the executor still runs.
		α Stop()ι->void;
		α Enabled()Ι->bool;
		//What an OEE field answers while the gateway computes none:  ServiceUnavailable, saying why - no oee block, no historian, or
		//the gateway's end.
		α TestEnabled( SRCE )Ε->void;
		α Settle()Ι->Duration{ lg _{ _mutex }; return _settle; }
		α Live()Ι->Duration{ lg _{ _mutex }; return _live; }//how often at most a machine's feed pushes what isn't a state change.
		α Time()Ι->sp<Hist::IClock>{ lg _{ _mutex }; return _clock; }//Start's;  none before it.

		struct Machine final{ OeeMachineRow Row; vector<OeeSignalRow> Signals; };
		α Find( OeeMachinePK id )Ι->optional<Machine>;
		α HasMachines()Ι->bool{ lg _{ _mutex }; return !_machines.empty(); }
		α HolderOf( HistGroupPK group )Ι->optional<Machine>;//the machine over a group, a deleted one too:  its row holds the group.
		α UserOf( HistGroupNodePK node )Ι->optional<Machine>;//a machine with a signal that names the member.
		α Engine( OeeMachinePK id )Ι->sp<OeeMachineHost>;//none for a machine that isn't running.
		//The machine's live feed (spec *Live*), which outlasts its engines:  none for no machine, and once stopped.
		α Feed( OeeMachinePK id )Ι->sp<OeeFeed>;
		α Coded( OeeMachinePK id )ι->void;//codeOee's and uncodeOee's:  the machine's feed reads its codings again.
		α ReasonsChanged()ι->void;//a reason made, edited, deleted, restored or purged:  every feed reads them again.
		α Blocked( const Machine& machine )Ι->string;//why the rows can't run, empty when they can.
		//recomputeOee's (spec *The engine*):  the machine's rows over the span made again from the group's history, as a late record
		//has them made (OeeMachineHost).  Queued:  the rebuild runs after the machine's writes, and a machine between engines -
		//`settle` after an edit, a retry, a restart - holds it for the next.  NotFound for no machine, and a BadRequest for one
		//that can't run, saying why, after TestEnabled.
		α Recompute( OeeMachinePK id, Oee::Span span, SRCE )ε->void;

		α Lines()Ι->OeeLines{ lg _{ _mutex }; return _lines; }
		α MachineNames()Ι->flat_map<uint32,string>;//for the rules' messages.
		//A line and the machines it reaches, each once, in member order, the deleted among them;  none of a deleted member line's.
		struct Reached final{ OeeLineRow Line; vector<Machine> Machines; OeeLines Lines; };
		α Reach( OeeLinePK line )Ι->optional<Reached>;//none for no line.
		//Past the gate (spec *Authorization*, *Open questions* #14):  Read on the group of every machine reached, one refused
		//refusing the line.  A line that isn't there is NotFound, once the caller has passed the lines' own table.
		α Reach( OeeLinePK line, UserPK by, SRCE )Ε->Reached;
		//The line's live feed, made at its first ask:  none for no line, and once stopped.
		α LineFeed( OeeLinePK line )ι->sp<OeeLineFeed>;
		//A machine's feed's, under its engine's lock:  the machine's state changed or it closed an interval, so the lines that
		//reach it compute again, from the executor.  Nothing while no line has a feed.
		α MachineMoved( OeeMachinePK id )ι->void;
		α LineEdits()ι->CoMutex&{ return _lineEdits; }//OeeMAwait's, held over a line's or a member's checks, its write and LinesChanged.
		//A line or a member created, edited, deleted, restored or purged, or a machine purged, whose memberships went with it:  both
		//tables read again, and each line's feed ended, for one deleted or gone, or computed again.
		α LinesChanged()ι->AnyVoidTask;

		//OeeMAwait's:  the live side of each table edit, lazy as HistHost's are.
		//Seeds `processed` with `from` to the second, now when none, and holds the row.
		α MachineCreated( OeeMachinePK id, optional<TimePoint> from )ι->AnyVoidTask;
		//A machine edited, deleted or restored, or a signal of its created, edited or purged:  its rows read again, and its engine
		//ended, given the new width, or started `settle` on, as they now say.
		α MachineChanged( DB::Key key )ι->AnyVoidTask;
		//Before its statement, whose proc takes the machine's stops, intervals, codings and signals with it:  its engine ends.
		α MachinePurging( OeeMachinePK id )ι->AnyVoidTask;
		α PurgeFailed( OeeMachinePK id )ι->void;//its statement failed, so the machine stands:  its feed takes subscribers again.
		α MachinePurged( OeeMachinePK id )ι->void;
		//codeOee's and uncodeOee's, held over a coding's checks and its write:  this gateway alone writes oee_codings.
		α Codings()ι->CoMutex&{ return _codings; }
		static constexpr Duration RetryInterval{ 30s };//how soon a machine whose start or write failed is started again.
	private:
		//Read:  the MachineChanged whose rows these are.  Offset:  the source offset the next engine goes live with, a takeover's.
		//Pending:  the rebuilds the next engine runs once it starts, those an ended one left and a recomputeOee between engines.
		struct Entry final{ Machine Rows; sp<OeeFeed> Feed; sp<OeeMachineHost> Engine; Hist::IClock::TimerId Timer{}; uint Serial{}; uint Read{}; Duration Offset{}; vector<OeeMachineHost::Ask> Pending; };
		α Loading( AnyVoidAwait& await )ι->VoidTask;
		//Under _mutex:  `id`'s engine starts after `after`, in place of any start it was waiting for.
		α Schedule( OeeMachinePK id, Entry& entry, Duration after )ι->void;
		//The rows read again, since `processed` moves under a running engine, and the engine made and started when they can run.
		//`serial`:  the entry's when this was asked for, so an edit or a stop since leaves it nothing to do.
		α Launch( OeeMachinePK id, uint serial )ι->VoidTask;
		α Failed( OeeMachinePK id, const OeeMachineHost* host, string why )ι->void;
		α Carry( OeeMachinePK id, OeeMachineHost& ended )ι->void;//once `ended` is Quiet:  its Unfinished to the entry's Pending.
		α Settled( OeeMachinePK id, sp<OeeMachineHost> ended )ι->VoidTask;//Carry, once `ended` is Quiet:  a failure's, which can't wait.
		//A rebuild that is a restart (OeeMachineHost::Takeover):  the engine ends and its writes are waited for, `processed` is set
		//back to `from`, and another starts at once with `offset`, so the live state is re-derived from the history.
		α Rebuild( OeeMachinePK id, const OeeMachineHost* host, TimePoint from, Duration offset, string why )ι->VoidTask;
		α MoveLines( optional<OeeMachinePK> machine={} )ι->void;//every line's feed, or those that reach `machine`, computes again.
		α SetLines( vector<OeeLineRow>&& lines, vector<OeeLineMemberRow>&& members )ι->void;//under _mutex.

		mutable std::mutex _mutex;
		flat_map<OeeMachinePK,Entry> _machines;
		sp<Hist::IClock> _clock;
		Duration _settle{ 5s };
		Duration _live{ 1s };
		optional<uint> _calendars;//the calendar client's Listen:  a calendar changed, and the feeds of the machines under it read again.
		bool _configured{}, _stopped{};
		uint _reads{};//MachineChanged's, so an older read that finishes last changes nothing.
		CoMutex _codings;
		OeeLines _lines;
		flat_map<OeeLinePK,sp<OeeLineFeed>> _lineFeeds;
		std::atomic<uint> _lineFeedCount{};
		uint _lineReads{};//LinesChanged's, so an older read that finishes last changes nothing.
		CoMutex _lineEdits;
	};
}