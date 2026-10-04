#include <jde/historian/Group.h>
#include <jde/fwk/process/execution.h>
#include "Store.h"
#include "io/DayFiles.h"

#define let const auto

namespace Jde::Opc::Hist{
	using namespace std::chrono;
	constexpr ELogTags _tags{ ELogTags::IO };

	//What a buffered record is filed by:  a membership change's time, or a value's source time, or its server's when it
	//came with no source time.
	Ω primary( const Record& record )ι->Ticks{
		if( let value = get_if<DataValue>(&record) )
			return value->Data.hasSourceTimestamp ? value->Data.sourceTimestamp : value->Data.serverTimestamp;
		return UADateTime{ std::holds_alternative<NodeAdded>(record) ? get<NodeAdded>(record).Ts : get<NodeRemoved>(record).Ts }.UA();
	}
	Ω setTime( Record& change, TimePoint ts )ι->void{
		if( auto added = get_if<NodeAdded>(&change) )
			added->Ts = ts;
		else
			get<NodeRemoved>( change ).Ts = ts;
	}
	//From a thread that isn't the executor's, which a flush's writes return on.
	Ω wait( FlushAwait&& flush )ι->void{
		try{
			BlockAny( move(flush) );
		}
		catch( const Exception& )
		{}
	}

	α FlushAwait::Suspend()ι->void{ _group->Request( this, _flush, _sl ); }
	α Group::Flush( SL sl )ι->FlushAwait{ return FlushAwait{ shared_from_this(), true, sl }; }
	α Group::Settled( SL sl )ι->FlushAwait{ return FlushAwait{ shared_from_this(), false, sl }; }

	α Group::Request( FlushAwait* waiter, bool flush, SL sl )ι->void{
		bool start{};
		{
			ul _{ _mutex };
			if( !_ended && (flush || _flushing) ){
				if( waiter )
					( flush ? _waiters : _settling ).push_back( std::exchange(waiter, nullptr) );
				else
					_again = true;
				start = !std::exchange( _flushing, true );
			}
		}
		if( waiter )//nothing to wait for.
			waiter->Resume( true );
		else if( start )
			Flushing( shared_from_this(), sl );
	}

	α Group::Start()ι->void{
		let timer = Schedule( _store->Config.Delay );
		ul _{ _mutex };
		_timer = timer;
	}
	α Group::Stop()ι->void{
		{
			ul _{ _mutex };
			_stopped = true;
		}
		//Gone once the process is finalizing, and without it no write returns.
		let executor = (bool)Executor();
		if( executor )
			wait( Flush() );
		IClock::TimerId timer;
		uint held;
		for( ;; ){
			{
				ul _{ _mutex };
				if( !_flushing || !executor ){
					_ended = true;
					timer = std::exchange( _timer, 0 );
					held = _changes.size()+_values.size();
					break;
				}
			}
			wait( Settled() );//one the clock started since.
		}
		if( timer )
			_store->Time->Cancel( timer );
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

	α Group::Flushing( sp<Group> /*self*/, SL sl )ι->VoidTask{
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
		members.Find = [this]( NodeIndex index )->optional<ExNodeId> {
			ul _{ _mutex };
			if( auto p = _nodes.find(index); p!=_nodes.end() )
				return p->second.Id;
			auto p = _gone.find( index );
			return p==_gone.end() ? optional<ExNodeId>{} : p->second;
		};
		for( bool again{ true }; again; ){
			TimePoint taken;
			vector<Buffered> batch;
			vector<FlushAwait*> waiters, settled;
			bool wasFailing;
			{
				ul _{ _mutex };
				waiters = std::exchange( _waiters, {} );
				_again = false;
				taken = clock.Now();//with the buffer, so this flush holds every record that arrived before it and none after.
				batch = Take();
				members.NextIndex = Issued() ? _nextIndex : 0;
				wasFailing = _failing;
			}

			//A membership change also goes, at the start of its day, to each later day's file there already is, so every
			//file maps its own nodes and the newest holds the whole membership.  One made after takes it in its preamble.
			{
				ul _{ _filesMutex };
				for( uint i=0, size=batch.size(); i<size; ++i ){
					if( batch[i].Copied || std::holds_alternative<DataValue>(batch[i].Item) )
						continue;
					batch[i].Copied = true;
					for( let later : _files->LaterDays(DayOf(primary(batch[i].Item), tz)) ){
						auto copy = batch[i];
						setTime( copy.Item, UADateTime{StartOf(later, tz)}.Time() );
						batch.push_back( move(copy) );
					}
				}
			}
			//Stable, so of two records at one time the first to arrive stays first:  a marker before the value that ends its gap.
			std::ranges::stable_sort( batch, {}, []( let& buffered ){ return primary(buffered.Item); } );

			bool failed{}, discarded{};
			uint done{};
			while( done<batch.size() && !failed ){
				let day = DayOf( primary(batch[done].Item), tz );
				let next = StartOf( year_month_day{sys_days{day}+days{1}}, tz );
				vector<Proto::HistoryRecord> records;
				auto end = done;
				for( ; end<batch.size() && (end==done || primary(batch[end].Item)<next); ++end ){
					try{
						records.push_back( ToProto(batch[end].Item) );
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
							run = _files->Prepare( day, move(records), members, sl );
						}
						if( run ){
							co_await run->Write( sl );
							ul _{ _filesMutex };
							_files->Commit( move(*run), sl );
						}
						else
							discarded = true;
					}
					done = end;
				}
				catch( Exception& e ){
					e.SetLevel( wasFailing ? ELogLevel::Debug : ELogLevel::Error );//said once, at Error, when it began.
					failed = true;
				}
				catch( const std::exception& e ){
					LOG( wasFailing ? ELogLevel::Debug : ELogLevel::Error, _tags, "Group '{}' could not write its records:  {}", Name(), e.what() );
					failed = true;
				}
			}

			if( failed ){
				//The day that couldn't be written and every later one wait for the next flush, so no later file takes a start
				//value that an earlier record, still unwritten, would change.
				batch.erase( batch.begin(), batch.begin()+done );
				if( Return(move(batch)) )
					_store->Trim();
			}
			else{
				{
					ul _{ _mutex };
					_failing = false;
				}
				if( wasFailing )
					DBG( "Group '{}' is writing again.", Name() );
				//After its data files, and only for a flush that wrote all it took, so it never claims records that aren't durable.
				if( !discarded ){
					try{
						Flushed::Slot slot;
						{
							ul _{ _filesMutex };
							slot = _files->LastFlush().Next( taken );
						}
						co_await slot.Write( sl );
						ul _{ _filesMutex };
						_files->LastFlush().Wrote( slot );
						_store->Wrote();
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
				arm = !Written();
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
