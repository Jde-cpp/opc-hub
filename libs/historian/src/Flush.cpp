#include <jde/historian/Group.h>
#include <boost/asio/io_context.hpp>
#include <jde/fwk/process/execution.h>
#include "Store.h"
#include "io/DayFiles.h"

#define let const auto

namespace Jde::Opc::Hist{
	using namespace std::chrono;
	constexpr ELogTags _tags{ ELogTags::IO };

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
		let timer = Schedule( _store->Config.Delay );
		ul _{ _mutex };
		_timer = timer;
	}
	α Group::Stopping()ι->void{
		IClock::TimerId timer;
		bool start{};
		{
			ul _{ _mutex };
			_stopped = true;
			timer = std::exchange( _timer, 0 );
			if( running() ){//marked here, so Stopped waits for it however late the executor starts it.
				if( _flushing )
					_again = true;
				else
					start = _flushing = true;
			}
		}
		if( timer )
			_store->Time->Cancel( timer );
		if( start )//on the executor, so every group's runs at once, and a write that never returns holds none of this thread.
			Post( [self=shared_from_this()]{ self->Flushing( self ); } );
	}
	α Group::Stopped( steady_clock::time_point deadline )ι->void{
		let wait = running();
		bool out{};
		IClock::TimerId timer;
		uint held;
		for( ;; ){
			{
				ul _{ _mutex };
				if( !_flushing || !wait || out ){
					_ended = true;
					timer = std::exchange( _timer, 0 );
					held = _changes.size()+_values.size();
					break;
				}
			}
			out = !settled( ms<FlushAwait>(shared_from_this(), false, SRCE_CUR), deadline );
		}
		if( timer )
			_store->Time->Cancel( timer );
		if( out )
			ERR( "Group '{}' stopped with a flush still out at hist's stop limit:  what it took may not be in its files.", Name() );
		if( held )
			ERR( "Group '{}' stopped holding {} records it couldn't write.", Name(), held );
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
			bool stopping;
			{
				ul _{ _mutex };
				waiters = std::exchange( _waiters, {} );
				_again = false;
				taken = clock.Now();//with the buffer, so this flush holds every record that arrived before it and none after.
				batch = Take();
				members.NextIndex = Issued() ? _nextIndex : 0;
				stopping = _stopped;
			}
			//A host waiting on it, or the historian's end, tries every day again; the clock only those whose `delay` is up.
			let retryAll = !waiters.empty() || stopping;

			//A membership change also goes, at the start of its day, to each later day's file there already is, so every
			//file maps its own nodes and the newest holds the whole membership.  One made after takes it in its preamble.
			{
				ul _{ _filesMutex };
				_files->Present( DayOf(UADateTime{taken}.UA(), tz) );
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
			bool progressed{}, discarded{};
			for( uint done{}; done<batch.size(); ){
				let day = DayOf( PrimaryTime(batch[done].Item), tz );
				let next = StartOf( year_month_day{sys_days{day}+days{1}}, tz );
				auto end = done+1;
				while( end<batch.size() && PrimaryTime(batch[end].Item)<next )
					++end;
				optional<TimePoint> retry;//when the day, failing, is tried again.
				{
					ul _{ _filesMutex };
					if( auto p = _failingDays.find(day); p!=_failingDays.end() )
						retry = p->second;
				}
				bool failed = retry && !retryAll && taken<*retry;
				if( !failed && retry ){//one that failed is opened first, before its backlog is converted for a write that fails the same way.
					ul _{ _filesMutex };
					if( !_files->Openable(day) ){
						failed = true;
						_failingDays.insert_or_assign( day, taken+_store->Config.Delay );
					}
				}
				if( !failed ){
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
					try{
						if( !records.empty() ){
							optional<Pending> run;
							{
								ul _{ _filesMutex };
								run = _files->Prepare( day, move(records), members );
							}
							if( run ){
								co_await run->Write();
								ul _{ _filesMutex };
								_files->Commit( move(*run) );
								progressed = true;
							}
							else
								discarded = true;
						}
					}
					catch( Exception& e ){
						e.SetLevel( retry ? ELogLevel::Debug : ELogLevel::Error );//said once, at Error, when it began.
						failed = true;
					}
					catch( const std::exception& e ){
						LOG( retry ? ELogLevel::Debug : ELogLevel::Error, _tags, "Group '{}' could not write its records:  {}", Name(), e.what() );
						failed = true;
					}
					ul _{ _filesMutex };
					if( failed )
						_failingDays.insert_or_assign( day, taken+_store->Config.Delay );
					else if( retry ){
						_failingDays.erase( day );
						DBG( "Group '{}' is writing its {} file again.", Name(), DayDirectory(day).string() );
					}
				}
				if( failed )
					std::ranges::move( batch.begin()+done, batch.begin()+end, std::back_inserter(held) );
				done = end;
			}

			bool failed = !held.empty();
			{
				ul _{ _mutex };
				_failing = failed && !progressed;
				if( !failed )
					PruneGone();
			}
			if( failed ){
				if( Return(move(held)) )
					_store->Trim();
			}
			else{
				//After its data files, and only for a flush that wrote all it took, so it never claims records that aren't durable.
				if( !discarded ){
					try{
						Flushed::Slot slot;
						{
							ul _{ _filesMutex };
							slot = _files->LastFlush().Next( taken );
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

			IClock::TimerId stale;
			bool arm;
			{
				ul _{ _mutex };
				stale = std::exchange( _timer, 0 );
				arm = !Written() && !_stopped;//a stopped group's last flush isn't followed by another.
			}
			if( stale )
				clock.Cancel( stale );//this flush stands in for it.
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
