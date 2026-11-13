#pragma once
#include <jde/fwk/co/CoMutex.h>
#include <jde/ql/IQLSession.h>
#include <jde/flows/Flow.h>
#include "FlowRows.h"

namespace Jde::Web::Server{ struct SessionInfo; }
namespace Jde::Opc::Gateway{
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
	//check that could not be made is tried again at a start, RetryInterval on, and leaves a running flow running.
	//The sources and sinks are a later phase's:  until then a hosted node is made as a stand-in, a source that emits
	//nothing and a sink whose message is its error, each with a status that says so.
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

		//deployFlow's (spec *Flows*):  Update on the flow, the graph read and admitted for the deployer, the version written
		//and made the flow's current one, and the flow, when it is enabled, restarted from it alone.  Dropped:  what the
		//run before it held or had queued, counted as its Stop counts them.  A refusal is thrown with its node, and the
		//flow runs on as it was.
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
		//setFlowContext's (spec *Context*), the Context tab's edit:  a value of the gateway's scope with no flow, of the flow's
		//with no node, else of that node's, a null one taking the key away.  Set in the store of a flow that is running, so
		//it is there at the flow's next get, and its row written before this ends, where a flow's own change waits
		//ContextDelay;  for a flow that is not running the row alone, which its next start loads.  Lazy, as the edits' sides are.
		α SetContext( optional<FlowPK> flow, string node, string key, jvalue value, SRCE )ι->AnyVoidTask;
		//FlowMAwait's:  the live side of each table edit, lazy as HistHost's are.
		α Changed( DB::Key key )ι->AnyVoidTask;//created, updated, deleted or restored:  the row read again, and the flow started or stopped as it now says.
		α Purging( FlowPK id, optional<FlowRow>& row )ι->AnyVoidTask;//before its statement:  the flow stops and its versions, runs and context rows go;  row:  its, for Purged.
		α Purged( FlowRow row )ι->AnyVoidTask;//its trace directory, and its access row and the grants on it, best effort.
		//A group's rows changed - its nodes, its deletion, its purge (HistGroupMAwait):  every running flow is admitted
		//again, since a node that left its group has no enforced row behind it.  Only queues.
		α GroupChanged( HistGroupPK group )ι->void;
		α Recheck()ι->void;//the access observer's, and GroupChanged's:  one pass at a time, and one more for a change during it.
		α Rechecks()Ι->uint{ return _rechecks; }//the passes finished, for tests.
		static constexpr Duration ContextDelay{ 1s };//the context's debounce:  a change is written this long after the first not yet written.
		static constexpr Duration RetryInterval{ 30s };//how soon a start whose admission could not be checked is tried again.
	private:
		//A flow's run:  one Flows::Flow, which runs once, with what it was made from.
		struct Run final{
			Run( Flows::Graph graph, Flows::Flow::Config&& config, uint32 version, UserPK deployer )ι:
				Graph{ graph }, Flow{ move(graph), move(config) }, Version{ version }, Deployer{ deployer }
			{}
			const Flows::Graph Graph;
			Flows::Flow Flow;
			const uint32 Version;
			const UserPK Deployer;
		};
		//Links:  the names of the current version's `link in` nodes.  Retry:  the timer of a start to try again.
		struct Entry final{ FlowRow Row; flat_set<string> Links; sp<Run> Live; Hist::IClock::TimerId Retry{}; };
		struct AdmitAwait final : AnyAwait<optional<FlowRefusal>>{
			AdmitAwait( FlowHost& host, FlowPK flow, const Flows::Graph& graph, UserPK deployer, sp<Web::Server::SessionInfo> session, bool deploying, SL sl )ι:
				AnyAwait<optional<FlowRefusal>>{ sl }, Flow{ flow }, Graph{ graph }, Deployer{ deployer }, Session{ move(session) }, Deploying{ deploying }, _host{ host }
			{}
			const FlowPK Flow;
			const Flows::Graph& Graph;
			const UserPK Deployer;
			const sp<Web::Server::SessionInfo> Session;//the deployer's, at a deploy that has one.
			const bool Deploying;
		protected:
			α Suspend()ι->void override{ _host.Admitting( *this ); }
		private:
			FlowHost& _host;
		};
		//The graph's first refusal for its deployer, in node order, or none.  `deploying`:  the checks a deploy alone makes.
		α Admit( FlowPK flow, const Flows::Graph& graph, UserPK deployer, sp<Web::Server::SessionInfo> session, bool deploying, SRCE )ι->AdmitAwait{ return { *this, flow, graph, deployer, move(session), deploying, sl }; }
		α Admitting( AdmitAwait& await )ι->VoidTask;
		α Deploying( DeployAwait& await )ι->VoidTask;
		α Loading( AnyVoidAwait& await )ι->VoidTask;
		//Each under _edits.
		//The flow's current version read, admitted for its deployer and started:  its entry holds the run, running or
		//stopped with its fault.  A check that could not be made schedules another Launch.
		α Launch( FlowRow row )ι->AnyVoidTask;
		//The run made over the flow's context rows and started, the entry's from here.
		α Begin( FlowRow row, Flows::Graph graph, uint32 version, UserPK deployer )ι->AnyVoidTask;
		//A start that was refused:  the entry's run is one that never started, stopped with the fault.
		α Refused( const FlowRow& row, Flows::Graph graph, uint32 version, UserPK deployer, Flows::Fault fault )ι->void;
		struct HaltAwait;//a run stopped, its teardown done.
		struct SetAwait;//a context value set on a run's strand.
		α Retry( FlowPK id )ι->VoidTask;//the retry timer's.
		α Time()Ι->sp<Hist::IClock>{ lg _{ _mutex }; return _clock; }
		α Rechecking()ι->VoidTask;
		α Links( const Flows::Graph& graph )Ι->flat_set<string>;
		//The other flows whose current version has a `link in` of the name:  each one's row.
		α Receivers( sv link, FlowPK but )Ι->vector<FlowRow>;
		//A context value changed, on a flow's strand or under the global store's lock:  queued, and the debounce armed.
		α ContextChanged( optional<FlowPK> flow, sv node, sv key, const jvalue* value )ι->void;
		α Flush()ι->void;//the debounce's.
		α Flushing( AnyVoidAwait* await )ι->VoidTask;//takes _edits.
		α WriteContext()ι->AnyVoidTask;//under _edits:  what is queued, written.

		mutable std::mutex _mutex;
		flat_map<FlowPK,Entry> _flows;
		sp<Hist::IClock> _clock;
		sp<Flows::MessageIds> _ids;
		sp<Flows::GlobalStore> _global;
		sp<Flows::IHost> _nodes;//what makes the hosted nodes.
		fs::path _path;//the trace's, a directory per flow by its slug.
		uint _queue{ 1000 };
		bool _configured{}, _stopped{};
		optional<uint> _observer;//the access listener's.
		bool _recheck{}, _rechecking{};
		std::atomic<uint> _rechecks{};
		CoMutex _edits;//every start, stop, deploy, purge and check of a flow, and each write of the context.
		std::mutex _contextMutex;
		using ContextKey = tuple<FlowPK,string,string>;//a flow, 0 for the global scope, a node and a key.
		flat_map<ContextKey,optional<string>> _pending;//the values to write, json text:  none for a key that went.
		bool _contextArmed{};
	};
}