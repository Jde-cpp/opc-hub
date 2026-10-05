#include <jde/historian/Group.h>
#include <boost/asio/io_context.hpp>
#include <jde/fwk/process/execution.h>
#include "Store.h"
#include "io/DayFiles.h"

#define let const auto

namespace Jde::Opc::Hist{
	using namespace std::chrono;
	constexpr ELogTags _tags{ ELogTags::IO };

	//The historian's end gave up on the flush, and has let its lock go:  no write of the flush starts after.  One already
	//under way, a scan or a write the OS has, still finishes.
	struct Abandoned final : std::exception{};

	Ω setTime( Record& change, TimePoint ts )ι->void{
		if( auto added = get_if<NodeAdded>(&change) )
			added->Ts = ts;
		else
			get<NodeRemoved>( change ).Ts = ts;
	}
	//A flush's writes return on the executor, so a stop waits for one only while that runs.  ExecutorIoc, not Executor,
	//which makes an io_context when there is none, and returns one that is winding down.
	Ω running()ι->bool{
		let ioc = ExecutorIoc();
		return ioc && !ioc->stopped();
	}
	//BlockAny's, until deadline:  false when the group's flushes haven't settled by then, as when the io_context stopped
	//with a write out, which drops its resume.  The coroutine owns the awaitable, so a resume after that is harmless.
	Ω settled( sp<FlushAwait> settling, steady_clock::time_point deadline )ι->bool{
		auto done = ms<absl::Notification>();
		[]( sp<FlushAwait> settling, sp<absl::Notification> done )->VoidTask{
			try{
				co_await *settling;
			}
			catch( const Exception& )
			{}
			done->Notify();
		}( move(settling), done );
		return done->WaitForNotificationWithTimeout( absl::FromChrono(deadline-steady_clock::now()) );
	}

	α FlushAwait::await_ready()ι->bool{
		if( _group->Waits(_flush) )
			return false;
		_result = true;
		return true;
	}
	α FlushAwait::Suspend()ι->void{ _group->Request( *this ); }
	α Group::Waits( bool flush )Ι->bool{
		ul _{ _mutex };
		return !_ended && ( flush || _flushing );
	}
	α Group::Flush( SL sl )ι->FlushAwait{ return FlushAwait{ shared_from_this(), true, sl }; }
	α Group::Settled( SL sl )ι->FlushAwait{ return FlushAwait{ shared_from_this(), false, sl }; }

	α Group::Request( FlushAwait& waiter )ι->void{
		bool waits{}, start{};
		{
			ul _{ _mutex };
			if( (waits = !_ended && (waiter._flush || _flushing)) ){
				( waiter._flush ? _waiters : _settling ).push_back( &waiter );
				start = !std::exchange( _flushing, true );
			}
		}
		if( !waits )//a flush ended since await_ready.
			waiter.Resume( true );
		else if( start )
			Flushing( shared_from_this() );
	}
	α Group::Request()ι->void{
		bool start;
		{
			ul _{ _mutex };
			if( _ended )
				return;
			_again = true;
			start = !std::exchange( _flushing, true );
		}
		if( start )
			Flushing( shared_from_this() );
	}

	α Group::Start()ι->void{
		bool recovers, archived;
		{
			ul _{ _filesMutex };
			recovers = _files->Recovers();
			archived = _files->Archived();
		}
		{
			ul _{ _mutex };//so neither timer runs, and stores the next id, before its own is stored.
			_timer = Schedule( _store->Config.Delay );
			_midnight = ScheduleMidnight();
			_archived = archived;
		}
		if( recovers )//on the clock's hop:  the historian's lock is held here.
			Schedule( Duration::zero() );
	}
	//A day's rewrite is due `delay` after the day ends, so the next is the first such time after now.
	α Group::ScheduleMidnight()ι->IClock::TimerId{
		auto& clock = *_store->Time;
		let delay = _store->Config.Delay;
		return clock.Schedule( NextDayStart(clock.Now()-delay, *_store->Config.TimeZone)+delay, [weak=weak_from_this()]{
			if( auto group = weak.lock() )
				group->Midnight();
		});
	}
	α Group::Midnight()ι->void{
		{
			ul _{ _mutex };//with the check, so a stop that comes between can't miss the id.
			if( _ended || _stopped )
				return;
			_midnight = ScheduleMidnight();
		}
		Request();
	}
	α Group::Stopping()ι->void{
		Timers timers;
		bool start{};
		let now = _store->Time->Now();
		Effects effects;
		{
			ul _{ _mutex };
			_stopped = true;
			timers = Disarm();
			for( auto&& [index,node] : _nodes ){//the stop is a break, so each pending value is stored as at its interval's end.
				if( node.Pending )
					Settle( index, node, now, effects );
			}
			if( running() ){//marked here, so Stopped waits for it however late the executor starts it.
				if( _flushing )
					_again = true;
				else
					start = _flushing = true;
			}
		}
		Cancel( timers );
		if( start )//on the executor, so every group's runs at once, and a write that never returns holds none of this thread.
			Post( [self=shared_from_this()]{ self->Flushing( self ); } );
	}
	α Group::Stopped( steady_clock::time_point deadline )ι->void{
		let wait = running();
		bool out{};
		Timers timers;
		uint held;
		for( ;; ){
			{
				ul _{ _mutex };
				if( !_flushing || !wait || out ){
					_ended = true;
					timers = Disarm();
					held = _changes.size()+_values.size();
					break;
				}
			}
			out = !settled( ms<FlushAwait>(shared_from_this(), false, SRCE_CUR), deadline );
		}
		Cancel( timers );
		if( out )
			ERR( "Group '{}' stopped with a flush still out at hist's stop limit:  what it took may not be in its files.", Name() );
		if( held )
			ERR( "Group '{}' stopped holding {} records it couldn't write.", Name(), held );
	}
	α Group::Ended()Ι->bool{
		ul _{ _mutex };
		return _ended;
	}
	α Group::Flushed()Ι->optional<TimePoint>{
		ul _{ _filesMutex };
		return _files->LastFlush().Time();
	}
	α Group::Runs( year_month_day day )Ι->vector<Run>{
		ul _{ _filesMutex };
		let file = _files->Find( day );
		return file ? file->Runs : vector<Run>{};
	}

	α Group::Flushing( sp<Group> /*self*/ )ι->VoidTask{
		auto& clock = *_store->Time;
		let& tz = *_store->Config.TimeZone;
		Membership members;
		members.Current = [this]{
			ul _{ _mutex };
			vector<std::pair<NodeIndex,ExNodeId>> y;
			y.reserve( _nodes.size() );
			for( let& [index,node] : _nodes )
				y.emplace_back( index, node.Id );
			return y;
		};
		members.Find = [this]( NodeIndex index )->optional<Membership::Node> {
			ul _{ _mutex };
			if( auto p = _nodes.find(index); p!=_nodes.end() )
				return Membership::Node{ p->second.Id, false };
			auto p = _gone.find( index );
			return p==_gone.end() ? optional<Membership::Node>{} : Membership::Node{ p->second, true };
		};
		for( bool again{ true }; again; ){
			TimePoint taken;
			vector<Buffered> batch;
			vector<FlushAwait*> waiters, settled;
			bool stopping, over, closed;
			{
				ul _{ _mutex };
				waiters = std::exchange( _waiters, {} );
				_again = false;
				taken = clock.Now();//with the buffer, so this flush holds every record that arrived before it and none after.
				over = _store->Buffered()>_store->Config.MaxBuffer;
				batch = Take( taken );
				members.NextIndex = Issued() ? _nextIndex : 0;
				stopping = _stopped;
				closed = _closed;
			}
			//A host waiting on it, or the historian's end, tries every day again; the clock only those whose `delay` is up.
			let retryAll = !waiters.empty() || stopping;
			//So too for an archive, which the clock's flushes rewrite at most once per `delay`, holding its records meanwhile,
			//unless the buffers are past maxBuffer:  those are flushed, not trimmed.
			let mergeNow = retryAll || over;

			vector<Day> due;//each day whose file becomes its archive, records for it or not:  the ending leaves them to the next start.
			{
				ul _{ _filesMutex };
				_files->Present( DayOf(UADateTime{taken}.UA(), tz) );
				if( closed )
					_files->Retire();
				if( !stopping )
					due = _files->Due( taken );
				//A membership change also goes, at the start of its day, to each later day's file there already is, so every
				//file maps its own nodes and the newest holds the whole membership.  One made after takes it in its preamble.
				for( uint i=0, size=batch.size(); i<size; ++i ){
					if( batch[i].Copied || std::holds_alternative<DataValue>(batch[i].Item) )
						continue;
					batch[i].Copied = true;
					for( let later : _files->LaterDays(DayOf(PrimaryTime(batch[i].Item), tz)) ){
						auto copy = batch[i];
						setTime( copy.Item, UADateTime{StartOf(later, tz)}.Time() );
						batch.push_back( move(copy) );
					}
				}
			}
			//Stable, so of two records at one time the first to arrive stays first:  a marker before the value that ends its gap.
			std::ranges::stable_sort( batch, {}, []( let& buffered ){ return PrimaryTime(buffered.Item); } );

			//Each day on its own, so one that stays unwritable holds back only its own records.  A later day's file made
			//meanwhile takes its start values without them, and they land as late records do.
			vector<Buffered> held;
			bool progressed{}, discarded{}, failing{};
			flat_set<Day> deferring;//each archive whose records it held for the archive's next rewrite.
			auto nextDue = due.begin();
			for( uint done{}; done<batch.size() || nextDue!=due.end(); ){
				let first = done<batch.size() ? optional<Day>{ DayOf(PrimaryTime(batch[done].Item), tz) } : nullopt;
				let day = first && ( nextDue==due.end() || *first<=*nextDue ) ? *first : *nextDue;
				if( nextDue!=due.end() && *nextDue==day )
					++nextDue;
				auto end = done;//none of the batch, for a day that is only due.
				if( first==day ){
					let next = StartOf( year_month_day{sys_days{day}+days{1}}, tz );
					for( ++end; end<batch.size() && PrimaryTime(batch[end].Item)<next; )
						++end;
				}
				optional<TimePoint> retry;//when the day, failing, is tried again.
				bool deferred{};//an archive's records, held for its next rewrite.
				{
					ul _{ _filesMutex };
					if( auto p = _failingDays.find(day); p!=_failingDays.end() )
						retry = p->second;
					deferred = done<end && !mergeNow && _files->Deferred( day, taken );
				}
				//One more than `delay` off was set before a clock set back since:  due, so the wait is never longer.
				bool failed = retry && !retryAll && taken<*retry && *retry-taken<=_store->Config.Delay;
				if( !failed && !deferred && retry && done<end ){//one that failed is opened first, before its backlog is converted for a write that fails the same way.
					ul _{ _filesMutex };
					if( !_files->Openable(day) ){
						failed = true;
						_failingDays.insert_or_assign( day, taken+_store->Config.Delay );
					}
				}
				if( !failed && !deferred ){
					vector<Proto::HistoryRecord> records;
					for( auto i = done; i<end; ++i ){
						try{
							records.push_back( ToProto(batch[i].Item) );
							if( auto value = get_if<DataValue>(&batch[i].Item) )
								value->Unsupported = false;//warned of, so a day that fails doesn't again on each retry.
						}
						catch( Exception& e ){//no file form, so it is left out.
							e.SetLevel( ELogLevel::Error );
						}
					}
					DayWrite write;
					try{
						if( !records.empty() || done==end ){
							{
								ul _{ _filesMutex };
								if( Ended() )
									throw Abandoned{};
								write = _files->Prepare( day, move(records), members, taken );
							}
							if( auto run = get_if<Pending>(&write) ){
								if( Ended() )
									throw Abandoned{};
								co_await run->Write();
								ul _{ _filesMutex };
								_files->Commit( move(*run) );
								progressed = true;
							}
							else if( auto archive = get_if<Rewrite>(&write) ){
								while( archive->Next() ){
									if( Ended() )
										throw Abandoned{};
									co_await archive->Write();
								}
								ul _{ _filesMutex };
								if( Ended() )//the rename, above all, which would replace the day's file outside the lock.
									throw Abandoned{};
								_files->Commit( move(*archive), taken );
								progressed = true;
							}
							else if( done<end )
								discarded = true;
						}
					}
					catch( Exception& e ){
						e.SetLevel( retry ? ELogLevel::Debug : ELogLevel::Error );//said once, at Error, when it began.
						failed = true;
					}
					catch( const Abandoned& ){//said by Stopped.
						failed = true;
					}
					catch( const std::exception& e ){
						LOG( retry ? ELogLevel::Debug : ELogLevel::Error, _tags, "Group '{}' could not write its records:  {}", Name(), e.what() );
						failed = true;
					}
					ul _{ _filesMutex };
					if( failed ){
						_failingDays.insert_or_assign( day, taken+_store->Config.Delay );
						if( let archive = get_if<Rewrite>(&write) )
							_files->Abandon( *archive );
					}
					else if( retry ){
						_failingDays.erase( day );
						DBG( "Group '{}' is writing its {} file again.", Name(), DayDirectory(day).string() );
					}
				}
				if( failed || deferred )
					std::ranges::move( batch.begin()+done, batch.begin()+end, std::back_inserter(held) );
				failing = failing || ( failed && done<end );
				if( deferred )
					deferring.insert( day );
				done = end;
			}

			bool failed = !held.empty();
			{
				ul _{ _mutex };
				_failing = failing && !progressed;
				_deferred = !deferring.empty() && !failing && !progressed;
				_deferredDays = move( deferring );
				if( !failed )
					PruneGone();
			}
			if( failed ){
				if( Return(move(held)) ){
					if( failing )
						_store->Trim();
					else{//held only for an archive's next rewrite, which the buffers passing maxBuffer brings on.
						ul _{ _mutex };
						_again = true;
					}
				}
			}
			else{
				{
					ul _{ _filesMutex };
					failed = !_files->SyncRenamed();
				}
				//After its data files, and only for a flush that wrote all it took, so it never claims records that aren't durable:
				//an archive's whose rename may not yet survive a power loss among them.
				failed = failed || Ended();
				if( !discarded && !failed ){
					try{
						Flushed::Slot slot;
						{
							ul _{ _filesMutex };
							slot = _files->LastFlush().Next( taken, _files->RecoverFrom(taken) );
						}
						co_await slot.Write();
						{
							ul _{ _filesMutex };
							_files->LastFlush().Wrote( slot );
						}
						ul _{ _mutex };
						_dropping = false;//so a drop after begins a new streak, warned of again.
					}
					catch( Exception& e ){
						e.SetLevel( ELogLevel::Error );
						failed = true;
					}
				}
			}

			Timers stale;
			bool arm, archived;
			{
				ul _{ _filesMutex };
				archived = _files->Archived();
			}
			{
				ul _{ _mutex };
				_archived = archived;//so a removed group whose rewrite failed is tried again at `delay`.
				arm = !Written() && !_stopped;//a stopped group's last flush isn't followed by another.
				stale = arm ? Timers{ .Delay=std::exchange(_timer, 0) } : Disarm();//this flush stands in for `delay`'s.
			}
			Cancel( stale );
			if( arm ){
				let timer = Schedule( _store->Config.Delay );
				ul _{ _mutex };
				_timer = timer;
			}
			{
				ul _{ _mutex };
				again = _again || !_waiters.empty();
				if( !again ){
					_flushing = false;
					settled = std::exchange( _settling, {} );
				}
			}
			let wrote = !failed && !discarded;
			for( auto waiter : waiters )
				waiter->Resume( bool{wrote} );
			for( auto waiter : settled )
				waiter->Resume( bool{wrote} );
		}
	}
}
