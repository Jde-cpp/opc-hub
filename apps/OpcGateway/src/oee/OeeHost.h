#pragma once
#include "OeeMachineHost.h"

namespace Jde::Opc::Gateway{
	//The gateway's OEE (docs/oee/spec.md *Placement*, *The engine*):  every oee_machines row with its signals, kept in step with
	//the tables by OeeMAwait, whose live side runs here, and an OeeMachineHost for each machine that can run.  The rows are held
	//whatever the settings, since OeeMAwait refuses a group's delete and a member's purge from them;  the engines need an `oee`
	//block and the historian, without which the OEE fields answer with an error (TestEnabled) and the tables still work.
	//A machine runs once it is live, has a `processed` and a signal set the library's rules pass, and its group is hosted.  One
	//an edit makes runnable starts `settle` after its last edit, so a form's signals land before the engine reads the past with
	//them;  those loaded at the gateway's start, and a restart after a failure, don't wait.
	struct OeeHost final : noncopyable{
		Ω Instance()ι->OeeHost&;
		//The settings and the rows, then each machine's engine, on the executor.  Blocks on the rows' load:  Gateway::Connect's
		//thread, after the historian's start.  `clock`:  the engines' and the timers', the system's when none - a test hands a manual one.
		α Start( sp<Hist::IClock> clock={} )ε->void;
		//Every engine ends and its write in flight is waited for:  before the historian's stop, while the executor still runs.
		α Stop()ι->void;
		α Enabled()Ι->bool;
		//What an OEE field answers while the gateway computes none:  ServiceUnavailable, saying why - no oee block, no historian, or
		//the gateway's end.
		α TestEnabled( SRCE )Ε->void;
		α Settle()Ι->Duration{ lg _{ _mutex }; return _settle; }
		α Time()Ι->sp<Hist::IClock>{ lg _{ _mutex }; return _clock; }//Start's;  none before it.

		struct Machine final{ OeeMachineRow Row; vector<OeeSignalRow> Signals; };
		α Find( OeeMachinePK id )Ι->optional<Machine>;
		α Machines()Ι->vector<Machine>;
		α HasMachines()Ι->bool{ lg _{ _mutex }; return !_machines.empty(); }
		α HolderOf( HistGroupPK group )Ι->optional<Machine>;//the machine over a group, a deleted one too:  its row holds the group.
		α UserOf( HistGroupNodePK node )Ι->optional<Machine>;//a machine with a signal that names the member.
		α Engine( OeeMachinePK id )Ι->sp<OeeMachineHost>;//none for a machine that isn't running.

		//OeeMAwait's:  the live side of each table edit, lazy as HistHost's are.
		//Seeds `processed` with `from` to the second, now when none, and holds the row.
		α MachineCreated( OeeMachinePK id, optional<TimePoint> from )ι->AnyVoidTask;
		//A machine edited, deleted or restored, or a signal of its created, edited or purged:  its rows read again, and its engine
		//ended, given the new width, or started `settle` on, as they now say.
		α MachineChanged( DB::Key key )ι->AnyVoidTask;
		//Before its statement:  its engine ends and its stops, intervals, codings and signals go, which the keys would refuse.
		α MachinePurging( OeeMachinePK id )ι->AnyVoidTask;
		α MachinePurged( OeeMachinePK id )ι->void;
		static constexpr Duration RetryInterval{ 30s };//how soon a machine whose start or write failed is started again.
	private:
		struct Entry final{ Machine Rows; sp<OeeMachineHost> Engine; Hist::IClock::TimerId Timer{}; uint Serial{}; };
		α Loading( AnyVoidAwait& await )ι->VoidTask;
		//Why the rows can't run, empty when they can.
		α Blocked( const Machine& machine )Ι->string;
		//Under _mutex:  `id`'s engine starts after `after`, in place of any start it was waiting for.
		α Schedule( OeeMachinePK id, Entry& entry, Duration after )ι->void;
		//The rows read again, since `processed` moves under a running engine, and the engine made and started when they can run.
		//`serial`:  the entry's when this was asked for, so an edit or a stop since leaves it nothing to do.
		α Launch( OeeMachinePK id, uint serial )ι->VoidTask;
		α Failed( OeeMachinePK id, const OeeMachineHost* host, string why )ι->void;

		mutable std::mutex _mutex;
		flat_map<OeeMachinePK,Entry> _machines;
		sp<Hist::IClock> _clock;
		Duration _settle{ 5s };
		bool _configured{}, _stopped{};
	};
}