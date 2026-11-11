#pragma once
#include <jde/fwk/co/CoMutex.h>
#include <jde/oee/Figures.h>
#include "OeeRows.h"

namespace Jde::Opc::Gateway{
	struct OeeSubscriber;
	//A machine's live feed (docs/oee/spec.md *Live*):  what every OeeSubscribe of the machine hears, built once a push.  While
	//it has a subscriber it holds the current shift's rows, its stops, their codings and the reasons - read from the tables when
	//the first subscribes, kept from then by what the engine tells its sink - so a push is the formulas over what is held and the
	//engine's state, with no read.  It is read again when the rows were made by other hands:  an engine going live, a rebuild in
	//place, a coding, a reason's edit, the machine's edit, or the calendar client naming the machine's calendar.  A state change
	//is pushed at once, anything else every `live` at most, and only a status that differs from the last.  The host holds one per
	//machine from its rows' load to its purge, and each engine of the machine tells it;  it holds no engine, asking the host for
	//the one that runs.  The engine calls under its lock, so nothing here calls an engine under _mutex.
	struct OeeFeed final : std::enable_shared_from_this<OeeFeed>{
		enum EReload : uint8{ CodingsOnly=1, ReasonsOnly=2, Everything=0xff };//what a reload reads.
		OeeFeed( OeeMachineRow row, vector<OeeSignalRow> signals, sp<Hist::IClock> clock, Duration live )ι;
		//The engine's sink, under the engine's lock:  held when the feed has a subscriber, else dropped.
		α Closed( const Oee::Interval& row )ι->void;
		α Told( const Oee::Stop& stop )ι->void;//a stop's row began or ended.
		α Rewritten( TimePoint from, const vector<Oee::Stop>& stops )ι->void;
		α Changed()ι->void;//the state did, or the engine ended:  a push at once, from the executor.
		//What is held is no longer the tables':  read again, then pushed.  Nothing without a subscriber;  one asked while another
		//waits its turn joins it.
		α Reload( EReload what=Everything )ι->void;
		//The machine's rows as its host's `read`th MachineChanged left them:  a deleted machine's subscriptions end, another's rows
		//are read again.  An older read than one taken changes nothing.
		α Edited( OeeMachineRow row, vector<OeeSignalRow> signals, uint read )ι->void;
		//Every subscription ends with `what` under its id, a `status` exception:  the machine deleted, or with `gone`, which
		//refuses any after it the same, purged or OEE stopping.
		α EndAll( EHttpStatus status, sv what, bool gone=true )ι->void;
		α Restored()ι->void;//the purge failed:  the machine stands, and is subscribed again.
		α Subscribers()Ι->uint{ lg _{ _mutex }; return _subscribers.size(); }//for tests and diagnostics.
		α Pushed()Ι->uint{ return _pushed; }//the statuses pushed, each to every subscriber - for tests and diagnostics.

		//`subscriber` joins:  the rows read first when it is the first, then the status now, which the ack carries.  What is pushed
		//from then on reaches it too.  NotFound for a deleted machine.
		struct AddAwait final : AnyAwait<FromServer::OeeStatus>{
			AddAwait( sp<OeeFeed> feed, sp<OeeSubscriber> subscriber, SL sl )ι:AnyAwait<FromServer::OeeStatus>{ sl }, _feed{ move(feed) }, _subscriber{ move(subscriber) }{}
		protected:
			α Suspend()ι->void override{ _feed->Adding( *this, move(_subscriber) ); }
		private:
			sp<OeeFeed> _feed;
			sp<OeeSubscriber> _subscriber;
		};
		α Add( sp<OeeSubscriber> subscriber, SRCE )ι->AddAwait{ return { shared_from_this(), move(subscriber), sl }; }
		α Remove( const OeeSubscriber* subscriber )ι->void;
	private:
		struct Window final{ optional<Calendar::Occurrence> Shift; Oee::Span Range; };
		α Adding( AddAwait& await, sp<OeeSubscriber> subscriber )ι->VoidTask;
		α Reloading()ι->VoidTask;
		α Load()ι->AnyVoidTask;//under _work.
		α Reread( uint8 what )ι->AnyVoidTask;//under _work:  the codings or the reasons alone, over what is held.
		α Failed( const std::exception& e )ι->void;//a reload's:  nothing is held, and the next tick past the backoff reads again.
		α Push()ι->void;
		α Tick( uint serial )ι->void;
		α State()Ι->optional<Oee::LiveState>;//the running engine's, none while none is live:  never under _mutex.
		//Under _mutex.
		α Active()Ι->bool{ return _adding || !_subscribers.empty(); }
		α Holding()Ι->bool{ return _loaded || _collecting; }
		α Arm()ι->void;
		α Idle()ι->void;//the last subscriber left:  the timer and what was held go.
		α Build( const optional<Oee::LiveState>& state )ι->FromServer::OeeStatus;
		α Schedule()Ι->sp<const Calendar::Schedule>;
		α Current( const Calendar::Schedule* schedule, TimePoint now )Ι->optional<Window>;//the shift over `now`, or without a calendar its UTC day.

		const OeeMachinePK _id;
		const sp<Hist::IClock> _clock;
		const Duration _live;
		CoMutex _work;//a load at a time, and a subscriber's join after any in flight.
		std::mutex _sendMutex;//a push's build and its sends, so two reach a subscriber in the order built;  taken before _mutex.
		mutable std::mutex _mutex;
		OeeMachineRow _row;
		Oee::Machine _machine;
		vector<sp<OeeSubscriber>> _subscribers;
		optional<std::pair<EHttpStatus,string>> _gone;
		uint _adding{};//joins in flight, which hold the rows as a subscriber does.
		uint _edit{};//the latest MachineChanged read Edited took.
		Hist::IClock::TimerId _timer{};
		uint _serial{};//Idle's:  a load it outlived keeps nothing.
		bool _loaded{}, _collecting{};
		uint8 _pending{};//EReload bits a queued Reloading reads.
		Duration _backoff{};//since a reload failed:  `live` doubled to a minute, zero once one succeeds.
		TimePoint _retryAt{};
		optional<TimePoint> _rewritten;//while collecting:  the least `from` a Rewritten named.
		TimePoint _from{};//the window's start at the load, from which the codings are read.
		vector<Oee::Interval> _rows;//by Start, from the window's start as it was at the load.
		std::map<TimePoint,Oee::Stop> _stops;
		vector<Oee::Coding> _codings;
		vector<Oee::Reason> _reasons;
		sp<const string> _last;//the last status pushed, serialized once for the comparison and every subscriber's send.
		std::atomic<uint> _pushed{};
	};
}