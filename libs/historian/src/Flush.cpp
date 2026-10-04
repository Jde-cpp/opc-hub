#include <jde/historian/Group.h>
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

	α Group::Start()ι->void{
		ul _{ _writeMutex };
		_timer = Schedule( _store->Config.Delay );
	}
	α Group::Stop()ι->void{
		{
			ul _{ _mutex };
			_stopped = true;
		}
		Flush();
		ul write{ _writeMutex };
		_ended = true;
		if( _timer )
			_store->Time->Cancel( std::exchange(_timer, 0) );
		ul _{ _mutex };
		if( let held = _changes.size()+_values.size(); held )
			ERR( "Group '{}' stopped holding {} records it couldn't write.", Name(), held );
	}
	α Group::Flushed()Ι->optional<TimePoint>{
		ul _{ _writeMutex };
		return _files->LastFlush().Time();
	}
	α Group::Runs( year_month_day day )Ι->vector<Run>{
		ul _{ _writeMutex };
		let file = _files->Find( day );
		return file ? file->Runs : vector<Run>{};
	}

	α Group::Flush( SL sl )ι->bool{
		ul write{ _writeMutex };
		if( _ended )
			return true;
		auto& clock = *_store->Time;
		let& tz = *_store->Config.TimeZone;
		TimePoint taken;
		vector<Buffered> batch;
		Membership members;
		bool wasFailing;
		{
			ul _{ _mutex };
			taken = clock.Now();//with the buffer, so this flush holds every record that arrived before it and none after.
			batch = Take();
			members.NextIndex = Issued() ? _nextIndex : 0;
			wasFailing = _failing;
		}
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

		//A membership change also goes, at the start of its day, to each later day's file there already is, so every file
		//maps its own nodes and the newest holds the whole membership.  One made after takes it in its preamble.
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
				if( !records.empty() && !_files->Append(day, move(records), members, sl) )
					discarded = true;
				done = end;
			}
			catch( Exception& e ){
				if( wasFailing )//said once, at Error, when it began.
					e.SetLevel( ELogLevel::Debug );
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
					_files->LastFlush().Write( taken, sl );
					_store->Wrote();
				}
				catch( Exception& ){
					failed = true;
				}
			}
		}
		if( _timer )
			clock.Cancel( _timer );//this flush stands in for it.
		_timer = Idle() ? 0 : Schedule( _store->Config.Delay );
		return !failed && !discarded;
	}
}
