#pragma once
#include <deque>
#include <jde/flows/Flow.h>
#include "FlowRows.h"

namespace Jde::Opc::Gateway{
	struct FlowSubscriber;
	//A flow's live feed (docs/flows/spec.md *Live*):  what every FlowSubscribe of the flow hears, built once a push.  While it
	//has a subscriber its run is watched, so the engine keeps each node's figures and each wire's last message between two
	//pushes and says when it has the first of them;  the feed takes them then, `live` after the push before at the soonest, so
	//a push costs no read, and adds the records the trace made since for the subscribers that asked for hops.  It outlasts
	//its flow's runs:  the host hands it each run that starts, whose ack every subscriber hears in the first's place, and ends
	//them when the flow has none.  The host holds one per flow from its first run to its purge.  The run is held weakly:  its
	//engine's hooks hold the feed.
	struct FlowFeed final : std::enable_shared_from_this<FlowFeed>{
		FlowFeed( FlowPK id, sp<Hist::IClock> clock, Duration live, uint valueLimit )ι;
		//A run of the flow.  Serial:  the host's, which the run's hooks carry.  Nodes:  its graph's ids, in the graph's order.
		struct Run final{ uint Serial{}; wp<Flows::Flow> Flow; vector<string> Nodes; uint32 Version{}; };
		//The flow's run now, running:  each subscriber hears its ack, under its id, and the pushes go on from it.  Nothing
		//for the run the feed is on.
		α Started( Run run )ι->void;
		//The flow has no run:  every subscription ends with `what` under its id, a `status` exception.
		α Ended( EHttpStatus status, sv what )ι->void;
		//`subscriber` joins:  its ack, the run as it stands, and what is pushed from then on reaches it too.  What was still to
		//push goes first to those already subscribed, so the ack and the pushes after it count nothing twice.  Conflict
		//with no run.
		α Add( sp<FlowSubscriber> subscriber, SRCE )ε->FromServer::FlowSubscribeAck;
		α Remove( const FlowSubscriber* subscriber )ι->void;
		//A run's hooks, on its strand:  the engine's cue, and the trace's tap.
		α OnActive( uint serial )ι->void;
		α OnRecord( uint serial, const Flows::TraceRecord& record )ι->void;
		α Subscribers()Ι->uint{ lg _{ _mutex }; return _subscribers.size(); }//for tests and diagnostics.
		α Pushed()Ι->uint{ return _pushed; }//the pushes sent, each to every subscriber it had something for - for tests and diagnostics.
		static constexpr uint MaxHops{ 1000 };//the records a push carries:  past it the oldest go, counted.
	private:
		α Tick( uint tick )ι->void;
		//Under _sendMutex.
		α Push()ι->void;
		α Send( const vector<sp<FlowSubscriber>>& to, Flows::Activity&& activity, const Run& run, std::deque<Flows::TraceRecord>&& records, uint dropped )ι->void;
		//Under _mutex.
		α Arm()ι->void;//the push's timer, when none is set:  now, or `live` after the last.
		α Disarm()ι->void;//the timer and the records held go.

		const FlowPK _id;
		const sp<Hist::IClock> _clock;
		const Duration _live;
		const uint _valueLimit;
		std::mutex _sendMutex;//a push's take, build and sends, a join and a run's change, so a subscriber hears them in the order made;  taken before _mutex.
		mutable std::mutex _mutex;
		Run _run;
		vector<sp<FlowSubscriber>> _subscribers;
		std::atomic<bool> _hops{};//a subscriber asked for the records:  the tap's first test.
		std::deque<Flows::TraceRecord> _records;//the trace's since the last push.
		uint _dropped{};
		Hist::IClock::TimerId _timer{};
		uint _tick{};//the timer's serial:  one cancelled too late finds another's.
		optional<TimePoint> _last;//the last push;  none with no subscriber.
		std::atomic<uint> _pushed{};
	};
}