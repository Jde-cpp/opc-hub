#pragma once
#include "OeeFeed.h"

namespace Jde::Opc::Gateway{
	//A line's live feed (docs/oee/spec.md *Live*, *Lines*):  what every OeeSubscribe of the line hears.  Its status is the
	//machines it reaches as counts, running, stopped and unknown, its current shift by its own calendar, and the shift's
	//figures so far, its machines' combined, each read from its rows and its engine's unwritten ones:  a read, not kept sums.
	//So it is computed once per change - a machine it reaches changing state, closing an interval or having its rows made
	//again, or an edit of the line, its members, a machine, a coding, a reason or a calendar - every `live` at most, and what
	//differs from the last is pushed to every subscription of the line (docs/demo/spec.md *Live*).  A subscription that joins
	//is answered with a computation of its own, which the others hear too when it differs.  When the groups of the machines
	//reached change, each subscription is gated again (spec *Authorization*), and one refused ends.  The host makes one when a
	//line is first subscribed and holds it until the line's purge;  with no subscriber it computes nothing.
	struct OeeLineFeed final : IOeeFeed, std::enable_shared_from_this<OeeLineFeed>{
		OeeLineFeed( OeeLinePK line, sp<Hist::IClock> clock, Duration live )ι;
		//Something the status is made from changed:  `machine`'s rows or state, or with none anything else.  Computed at once
		//when the last was `live` ago, else once it is;  nothing without a subscriber or a join, or for a machine the line
		//doesn't reach.
		α Moved( optional<OeeMachinePK> machine={} )ι->void;
		//Every subscription ends with `what` under its id, a `status` exception:  the line deleted, or with `gone`, which refuses
		//any after it the same, purged or OEE stopping.
		α EndAll( EHttpStatus status, sv what, bool gone=true )ι->void;
		α Subscribers()Ι->uint{ lg _{ _mutex }; return _subscribers.size(); }//for tests and diagnostics.
		α Pushed()Ι->uint{ return _pushed; }//the statuses pushed, each to every subscriber - for tests and diagnostics.
		α Reads()Ι->uint{ return _reads; }//the computations, each a read of the machines' rows - for tests and diagnostics.

		//NotFound for a line purged or deleted, and Read on every group reached, as OeeSubscribe asks it, over the reach its
		//computation read.
		α Add( sp<OeeSubscriber> subscriber, SRCE )ι->AddAwait override{ return { shared_from_this(), move(subscriber), sl }; }
		α Remove( const OeeSubscriber* subscriber )ι->void override;
	private:
		α Adding( AddAwait& await, sp<OeeSubscriber> subscriber )ι->VoidTask override;
		α Computing()ι->VoidTask;
		//Under _work:  NotFound for a line that's gone or deleted.  `groups`:  those of the machines reached, the deleted among them.
		α Compute( FromServer::OeeStatus& status, flat_set<HistGroupPK>& groups )ι->AnyVoidTask;
		//Under _work, after a computation that reached `groups`:  when they differ from those the subscribers were last gated
		//over, each is gated again, and those refused end.
		α Regate( const flat_set<HistGroupPK>& groups )ι->void;
		//Under _mutex.
		α Active()Ι->bool{ return _adding || !_subscribers.empty(); }
		α Count()ι->void;//the host told when Active changes:  MachineMoved posts nothing while no line's feed is.
		α Idle()ι->void;//the last subscriber left.

		const OeeLinePK _id;
		const sp<Hist::IClock> _clock;
		const Duration _live;
		CoMutex _work;//a computation at a time, its push with it, so two reach a subscriber in the order computed.
		mutable std::mutex _mutex;
		vector<sp<OeeSubscriber>> _subscribers;
		optional<std::pair<EHttpStatus,string>> _gone;
		uint _adding{};//joins in flight:  a change during one is computed after it.
		bool _counted{};//Active, as the host last heard it.
		flat_set<OeeMachinePK> _reach;//the machines the last computation reached.
		flat_set<HistGroupPK> _groups;//under _work:  their groups, which the subscribers were gated over.
		Hist::IClock::TimerId _timer{};
		bool _queued{};//a computation is posted, or waits for its timer.
		uint _serial{};//Idle's:  a timer it outlived does nothing.
		TimePoint _computed{};//when the last computation began.
		sp<const string> _last;//the last status pushed, serialized once for the comparison and every subscriber's send.
		std::atomic<uint> _pushed{}, _reads{};
	};
}