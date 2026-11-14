#pragma once
#include <condition_variable>
#include <jde/fwk/co/CoMutex.h>
#include <jde/ql/IQLSession.h>
#include <jde/flows/FileTrace.h>
#include <jde/flows/Replay.h>
#include "FlowRows.h"

namespace Jde::Web::Server{ struct SessionInfo; }
namespace Jde::Opc::Gateway{
	struct FlowFeed;
	//Why a graph may not run for its deployer (docs/flows/spec.md *Rights*), by the node the editor marks.  Unknown:  it
	//could not be checked - a server out of reach, a statement that failed - and may pass later.
	struct FlowRefusal final{ Flows::Fault Fault; EHttpStatus Status{ EHttpStatus::BadRequest }; bool Unknown{}; };

	//The gateway's flows (spec *Placement*, *Flows*, *Rights*, *Context*):  the library over /gateway/flows, every flows row
	//held and kept in step with the table by FlowMAwait, whose live side runs here, and a Flows::Flow for each enabled flow
	//from its current version.  No flows block, no flows:  the tables still work, and a deploy answers with an error
	//(TestEnabled).
	//A flow runs under the collector's credential and may do what its deployer may, so it is admitted before it starts, at
	//a deploy and at every start after it, and again while it runs when the access cache changes or a group does:
	//  `opc out`:  Update on an enforced access row of the group that holds the node, a node no group holds refused;
	//  `opc in`:  Read on the group that holds the node, and for a node no group holds the server's own answer to a read
	//  under the deployer's session, asked at the deploy alone and never repeated;
	//  `group in`:  Read on the group, which the gateway must historize;
	//  `link out` to another flow's `link in`:  Update on each flow that holds it, at the deploy alone.
	//A refusal at a deploy refuses the deploy and writes nothing.  At a start it leaves the flow stopped, and for a running
	//flow it stops it, with an Error and the node's status saying why, until someone with the rights deploys it again;  a
	//check that could not be made, a start whose statement failed, or a node that could not be made or started, but a
	//Function node, is tried again RetryInterval on, and a check that could not be made leaves a running flow running.  A
	//node in index form is spelled through its server at its run's admission, and the run's re-checks stand on that
	//spelling.
	//The sources and sinks are FlowNodes', one per run, made from the run's flow, its deployer and what its admission
	//resolved;  a link to another flow is delivered here (Link).
	//A replay (spec *Replay*, Replay) is a run of a flow's current version beside its live one, over its sources' history:
	//the library's Run on the stream's clock, its nodes FlowNodes' with the range, its trace the flow's own under the run's
	//id, and its flow_runs row written as it starts and as it ends.
	struct FlowHost final : noncopyable{
		Ω Instance()ι->FlowHost&;
		//The settings, the rows and the global context, then every enabled flow from its current version, behind it on the
		//executor:  a flow's admission may wait on a server.  Blocks on the rows' load:  Gateway::Connect's thread, after the
		//historian's start.  `clock`:  the flows' and the context's debounce, the system's when none.
		α Start( sp<Hist::IClock> clock={} )ε->void;
		//Every flow stops and the context still to write is written:  before the historian's stop, while the executor runs.
		α Stop()ι->void;
		α Enabled()Ι->bool;
		α TestEnabled( SRCE )Ε->void;//ServiceUnavailable, saying why:  no flows block, or the gateway's end.

		//A flow as the host holds it.  Fault:  why it is enabled and not running.
		struct State final{ FlowRow Row; bool Running{}; optional<Flows::Fault> Fault; };
		α Find( FlowPK id )Ι->optional<State>;
		α Status( FlowPK id, sv node )Ι->Flows::NodeStatus;//of the flow's run, running or stopped;  empty with none.
		α Counts( FlowPK id )Ι->vector<Flows::NodeCounts>;
		α Global()Ι->sp<Flows::GlobalStore>{ lg _{ _mutex }; return _global; }
		//The trace's files (spec *Trace*, *History*):  under /gateway/flows/path, each flow's at its row's level else the
		//block's `trace`, flushed at `delay`, cut at `valueLimit`, rolled daily past `retention` and `maxSize`, and read by
		//FlowQLAwait a page of `readLimit` at a time.  Null before Start, and for a gateway with no flows block.
		α Store()Ι->sp<Flows::TraceStore>{ lg _{ _mutex }; return _store; }
		α ReadLimit()Ι->uint{ lg _{ _mutex }; return _readLimit; }
		//The flow's live feed (spec *Live*), which FlowSubscribe joins:  one a flow, from its first run to its purge, told each
		//run that starts, whose ack its subscribers hear, and ended when the flow has none.  None for a flow that never ran.
		//It pushes every /gateway/flows/live at most, a message's json cut at `valueLimit` as the trace's is.
		α Feed( FlowPK id )Ι->sp<FlowFeed>;
		α Live()Ι->Duration{ lg _{ _mutex }; return _live; }

		//deployFlow's (spec *Flows*):  Update on the flow, the graph read and admitted for the deployer, the version written
		//and made the flow's current one, and the flow, when it is enabled, restarted from it alone.  Dropped:  what the
		//run before it held or had queued, counted as its Stop counts them.  A refusal is thrown with its node, and the
		//flow runs on as it was.  A start that fails once the version is written answers with its Fault, and is tried again.
		struct Deployed final{ uint32 Version; TimePoint Time; string UserName; bool Running; optional<Flows::Fault> Fault; uint Dropped; };
		struct DeployAwait final : AnyAwait<Deployed>{
			DeployAwait( FlowHost& host, FlowPK flow, jvalue&& graph, string&& comment, QL::Creds&& deployer, SL sl )ι:
				AnyAwait<Deployed>{ sl }, Flow{ flow }, Graph{ move(graph) }, Comment{ move(comment) }, Deployer{ move(deployer) }, _host{ host }
			{}
			const FlowPK Flow;
			jvalue Graph;
			string Comment;
			QL::Creds Deployer;
		protected:
			α Suspend()ι->void override{ _host.Deploying( *this ); }
		private:
			FlowHost& _host;
		};
		α Deploy( FlowPK id, jvalue graph, string comment, QL::Creds deployer, SRCE )ι->DeployAwait{ return { *this, id, move(graph), move(comment), move(deployer), sl }; }
		//injectFlow's:  the inject node's message, or `message` in its place.  Refused for a flow that is not running and
		//for a node that is not an inject of it.
		α Inject( FlowPK id, sv node, const jobject* message, SRCE )ε->void;
		//replayFlow's (spec *Replay*):  Update on the flow, whose current version is read and admitted for its deployer as a
		//start admits it, enabled or not, each `opc in` a node a group holds, since a replay has no other records of one, each
		//source's records found, and Read for the caller on each group a source reads, which is history it would read:  a
		//refusal is thrown with its node, and writes nothing.  The run's row is then written and the run started over
		//`from` to `to`, each inside the range and `to` no later than now, on a strand, a sandbox and a clock of its own, with a copy of the flow's
		//context as its rows and what is still to write stand, and of the gateway's, of which it writes none back.  Its
		//trace is the flow's, each record under the run's id at the stream's time, at the flow's level or `errors` when
		//that is below it, so the captures are always written.  Every sink is captured, in either mode:  what `commit`
		//writes, a `variable`, comes with the flow server.  Answered as the run starts:  how it ends is its row's `status`,
		//`completed`, `cancelled` or `failed:  <why>`, null while it is under way, written once its records are on file, and
		//`failed` for a run whose trace could not be, which is ended at the first flush that fails.
		//One run at a time per flow:  a second is refused, Conflict.  A deploy meanwhile leaves the run on its version.
		struct Replayed final{ FlowRunPK Id; uint32 Version; TimePoint Started; string UserName; };
		struct ReplayAwait final : AnyAwait<Replayed>{
			ReplayAwait( FlowHost& host, FlowPK flow, TimePoint from, TimePoint to, bool commit, QL::Creds&& by, SL sl )ι:
				AnyAwait<Replayed>{ sl }, Flow{ flow }, From{ from }, To{ to }, Commit{ commit }, By{ move(by) }, _host{ host }
			{}
			const FlowPK Flow;
			const TimePoint From, To;
			const bool Commit;
			QL::Creds By;
		protected:
			α Suspend()ι->void override{ _host.Replaying( *this ); }
		private:
			FlowHost& _host;
		};
		α Replay( FlowPK id, TimePoint from, TimePoint to, bool commit, QL::Creds by, SRCE )ι->ReplayAwait{ return { *this, id, from, to, commit, move(by), sl }; }
		//cancelFlowRun's:  Update on the run's flow, the table's for a run that isn't there.  The run stops, what its nodes
		//held dropped and counted, and this answers how it ended once its row says so, which for one that finished as the
		//cancel came is `completed`.  A run that is not under way is refused, Conflict.
		struct RunEnd final{ string Status; TimePoint Finished; };
		struct CancelAwait final : AnyAwait<RunEnd>{
			CancelAwait( FlowHost& host, FlowRunPK run, UserPK by, SL sl )ι:AnyAwait<RunEnd>{ sl }, Run{ run }, By{ by }, _host{ host }{}
			const FlowRunPK Run;
			const UserPK By;
		protected:
			α Suspend()ι->void override{ _host.Cancelling( *this ); }
		private:
			FlowHost& _host;
		};
		α CancelRun( FlowRunPK run, UserPK by, SRCE )ι->CancelAwait{ return { *this, run, by, sl }; }
		α ReplayOf( FlowPK id )Ι->optional<FlowRunPK>;//the flow's run under way.
		static constexpr uint ReplayBuffer{ Flows::TraceStore::MaxBuffer/4 };//the bytes of trace a replay lets wait before it stops for their flush, so it drops no record of its own or of the live flow's.
		//setFlowContext's (spec *Context*), the Context tab's edit:  a value of the gateway's scope with no flow, of the flow's
		//with no node, else of that node's, a null one taking the key away.  Set in the store of a flow that is running, so
		//it is there at the flow's next get, and its row written before this ends, where a flow's own change waits
		//ContextDelay;  for a flow that is not running the row alone, which its next start loads.  Lazy, as the edits' sides are.
		α SetContext( optional<FlowPK> flow, string node, string key, jvalue value, SRCE )ι->AnyVoidTask;
		//A `link out`'s message to the other flows whose current version holds a `link in` of the name (spec *Nodes*,
		//*Rights*), from the sender's strand:  a clone onto each one's strand that is running.  The sender's deployer needs
		//Update on each running one, as an inject into it does, asked at every message, so one who loses it stops delivering:
		//thrown, the sender's node's error, and none takes the message.  A flow that is not running is neither asked about
		//nor sent to.  The flows that took it.
		α Link( FlowPK from, UserPK deployer, sv name, const Flows::Message& m, SRCE )ε->uint;
		//FlowMAwait's:  the live side of each table edit, lazy as HistHost's are.
		α Changed( DB::Key key )ι->AnyVoidTask;//created, updated, deleted or restored:  the row read again, and the flow started or stopped as it now says.
		//Before its statement, flow_purge, which takes the versions, runs and context rows with the row:  the flow stops and
		//what it left of its context is written, so a purge that fails leaves it whole.  row:  its, for Purged.
		α Purging( FlowPK id, optional<FlowRow>& row )ι->AnyVoidTask;
		//The entry again, which an edit while the statement ran may have made, its trace directory, and its access row and the
		//grants on it, best effort.
		α Purged( FlowRow row )ι->AnyVoidTask;
		//A group's rows changed - its nodes, its deletion, its purge (HistGroupMAwait):  every running flow is admitted
		//again, since a node that left its group has no enforced row behind it.  Only queues.
		α GroupChanged( HistGroupPK group )ι->void;
		α Recheck()ι->void;//the access observer's, and GroupChanged's:  one pass at a time, and one more for a change during it.
		α Rechecks()Ι->uint{ return _rechecks; }//the passes finished, for tests.
		static constexpr Duration ContextDelay{ 1s };//the context's debounce:  a change is written this long after the first not yet written.
		static constexpr Duration RetryInterval{ 30s };//how soon a start that could not be checked or made, or a context write that failed, is tried again.
		static constexpr uint ContextTries{ 3 };//the writes a context value may fail while others are written, before it is dropped with an Error.
	private:
		//What a run's admission resolved, by a node's id in the graph.  Nodes:  an index-form node's spelling (HistNodeText),
		//its server asked once a run.  Groups:  a `group in`'s group, and the group that holds an `opc in`'s node, which a
		//replay reads, as the last admission found it.
		struct Resolved final{ flat_map<string,string> Nodes; flat_map<string,HistGroupPK> Groups; };
		//A flow's run:  one Flows::Flow, which runs once, with what it was made from.
		struct Run final{
			Run( Flows::Graph graph, Flows::Flow::Config&& config, uint32 version, UserPK deployer, Resolved spelled )ι:
				Graph{ graph }, Flow{ move(graph), move(config) }, Version{ version }, Deployer{ deployer }, Spelled{ move(spelled) }
			{}
			const Flows::Graph Graph;
			Flows::Flow Flow;
			const uint32 Version;
			const UserPK Deployer;
			Resolved Spelled;//under _edits.
			optional<Flows::ETraceLevel> Trace;//the row's level it was made at:  a row whose level changes restarts it (Changed).
			uint Serial{};//which run it is to the flow's feed, whose hooks carry it;  0 for one never started.
		};
		//Links:  the names of the current version's `link in` nodes.  Retry:  the timer of a start to try again.
		struct Entry final{ FlowRow Row; flat_set<string> Links; sp<Run> Live; Hist::IClock::TimerId Retry{}; };
		struct AdmitAwait final : AnyAwait<optional<FlowRefusal>>{
			AdmitAwait( FlowHost& host, FlowPK flow, const Flows::Graph& graph, UserPK deployer, sp<Web::Server::SessionInfo> session, bool deploying, Resolved& spelled, SL sl )ι:
				AnyAwait<optional<FlowRefusal>>{ sl }, Flow{ flow }, Graph{ graph }, Deployer{ deployer }, Session{ move(session) }, Deploying{ deploying }, Spelled{ spelled }, _host{ host }
			{}
			const FlowPK Flow;
			const Flows::Graph& Graph;
			const UserPK Deployer;
			const sp<Web::Server::SessionInfo> Session;//the deployer's, at a deploy that has one.
			const bool Deploying;
			Resolved& Spelled;//the run's:  read, and added to for a node it lacks.
		protected:
			α Suspend()ι->void override{ _host.Admitting( *this ); }
		private:
			FlowHost& _host;
		};
		//The graph's first refusal for its deployer, in node order, or none.  `deploying`:  the checks a deploy alone makes.
		α Admit( FlowPK flow, const Flows::Graph& graph, UserPK deployer, sp<Web::Server::SessionInfo> session, bool deploying, Resolved& spelled, SRCE )ι->AdmitAwait{ return { *this, flow, graph, deployer, move(session), deploying, spelled, sl }; }
		α Admitting( AdmitAwait& await )ι->VoidTask;
		α Deploying( DeployAwait& await )ι->VoidTask;
		α Loading( AnyVoidAwait& await )ι->VoidTask;
		//Each under _edits.
		//The flow's current version read, admitted for its deployer and started:  its entry holds the run, running or
		//stopped with its fault.  A check that could not be made, or a statement that failed, schedules another Launch.
		α Launch( FlowRow row )ι->AnyVoidTask;
		//The run made over the flow's context rows, and what of its context is still to write, and started, the entry's from
		//here, once its nodes' Start has run:  one that threw has stopped it, with its fault.  A select that fails leaves the
		//entry a refused run, to try again.
		α Begin( FlowRow row, Flows::Graph graph, uint32 version, UserPK deployer, Resolved spelled )ι->AnyVoidTask;
		//A start that was refused:  the entry's run is one that never started, stopped with the fault.  `retry`:  Launch again,
		//RetryInterval on.
		α Refused( const FlowRow& row, Flows::Graph graph, uint32 version, UserPK deployer, Flows::Fault fault, bool retry=false )ι->void;
		struct HaltAwait;//a run stopped, its teardown done.
		struct StartAwait;//a run started, its nodes' Start run.
		struct SetAwait;//a context value set on a run's strand.
		//A replay under way, from its row's insert until its row says how it ended.  Waiting:  what awaits that, under Mutex.
		struct ReplayRun final{
			FlowRunPK Id; FlowPK Flow; string Slug; string Name; sp<Flows::Run> Run;
			std::mutex Mutex; optional<RunEnd> End; vector<absl::AnyInvocable<void()>> Waiting;
		};
		struct EndAwait;//a replay's end, its row written.
		α Replaying( ReplayAwait& await )ι->VoidTask;
		α Cancelling( CancelAwait& await )ι->VoidTask;
		//A replay's end, from its strand:  the flow's trace written, so the run's records are on file, then its row, and
		//what waits for it.
		α Ending( sp<ReplayRun> replay, Flows::ERunEnd end, string why )ι->VoidTask;
		//A flow's context for a run, from the scope's rows with what is still to write laid over them.
		α Loaded( FlowPK flow, str slug, vector<FlowContextRow>&& rows, Flows::Flow::Config& config )Ι->void;
		α Retry( FlowPK id )ι->VoidTask;//the retry timer's.
		//The flow's subscribers hear what its run is now, under _edits:  the ack of a run that started since they last heard,
		//or with none running their end, saying why.  After each start and each stop, by hand:  a deploy and a restart at
		//another trace level halt a run and keep its subscriptions, which a stop that told the feed itself could not tell from
		//one that ends them, so a new path that starts or halts a run calls this after it.
		α Tell( FlowPK id )ι->void;
		α Time()Ι->sp<Hist::IClock>{ lg _{ _mutex }; return _clock; }
		α Function()Ι->Flows::FunctionLimits{ lg _{ _mutex }; return _function; }
		α Rechecking()ι->VoidTask;
		α Links( const Flows::Graph& graph )Ι->flat_set<string>;
		//The other flows whose current version has a `link in` of the name:  each one's row.
		α Receivers( sv link, FlowPK but )Ι->vector<FlowRow>;
		//A context value changed, on a flow's strand or under the global store's lock:  queued, and the debounce armed.
		α ContextChanged( optional<FlowPK> flow, sv node, sv key, const jvalue* value )ι->void;
		α Flush()ι->void;//the debounce's.
		α Flushing( AnyVoidAwait* await )ι->VoidTask;//takes _edits.
		//Under _edits:  what is queued, written, each key on its own.  One that fails is queued again, RetryInterval on, and
		//dropped once it has failed ContextTries passes in which others were written:  a pass that writes nothing is the
		//database's failure, not the key's.
		α WriteContext()ι->AnyVoidTask;
		//A scope's context:  json text by node and key, the global scope's with flow 0.
		using ContextValues = flat_map<std::pair<string,string>,optional<string>>;
		α Unwritten( FlowPK flow, ContextValues& values )Ι->void;//what is still to write laid over `values`, the rows read:  a write that failed left the row behind it.
		α Forget( FlowPK flow )ι->void;//a purged flow's context still to write.

		mutable std::mutex _mutex;
		flat_map<FlowPK,Entry> _flows;
		sp<Hist::IClock> _clock;
		sp<Flows::MessageIds> _ids;
		sp<Flows::GlobalStore> _global;
		sp<Flows::TraceStore> _store;//the trace's files, a directory per flow by its slug.
		fs::path _tracePath;//the store's root, which a purge with no store removes the flow's directory from.
		uint _readLimit{ 10'000 };//the most records one trace read answers:  /gateway/flows/readLimit, 0 for no limit.
		flat_map<FlowPK,sp<ReplayRun>> _replays;//each flow's replay under way, one at the most.
		std::condition_variable _replayEnded;//_replays lost one:  with _mutex.
		flat_map<FlowPK,sp<FlowFeed>> _feeds;//each flow's live feed, made with its first run.
		Duration _live{ 200ms };//how often at most a feed pushes:  /gateway/flows/live.
		uint _valueLimit{ 4096 };//the block's, for the feeds' json.
		std::atomic<uint> _serials{};//the runs started.
		uint _queue{ 1000 };
		Flows::FunctionLimits _function;//`/gateway/flows/function`, each flow's sandbox.
		jobject _env;//`/gateway/flows/env`.
		bool _configured{}, _stopped{};
		optional<uint> _observer;//the access listener's.
		bool _recheck{}, _rechecking{};
		std::atomic<uint> _rechecks{};
		CoMutex _edits;//every start, stop, deploy, purge and check of a flow, and each write of the context.
		mutable std::mutex _contextMutex;
		using ContextKey = tuple<FlowPK,string,string>;//a flow, 0 for the global scope, a node and a key.
		flat_map<ContextKey,optional<string>> _pending;//the values to write, json text:  none for a key that went.
		flat_map<ContextKey,uint> _contextFailures;//the passes a key has failed while others were written.
		bool _contextArmed{};
	};
}