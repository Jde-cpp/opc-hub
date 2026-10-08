#include "Applied.h"
#include <deque>

#define let const auto

namespace Jde::Opc::Hist{
	using Proto::HistoryRecord;
	constexpr Ticks Earliest{ std::numeric_limits<Ticks>::min() };
	Ω time( const HistoryRecord& r )ι->Ticks{ return PrimaryTime( r ).value_or( Earliest ); }

	α DayMods::Read( const GroupFiles::Served& served, SL sl )ε->DayMods{
		DayMods y;
		//One run from its start, in the file's order, which is the edits':  a modifications file's own runs are each one
		//edit's, so merging them would put the edits in target-time order instead.
		Merge all{ served.File, {Run{.Offset=0, .End=served.Size, .Chain=0, .First=Earliest, .Last=std::numeric_limits<Ticks>::max()}}, {}, sl };
		HistoryRecord r;
		while( all.Next(r) ){
			if( r.has_modification() )
				y.ByTime[r.modification().target_source_ts()].push_back( move(*r.mutable_modification()) );
		}
		return y;
	}
	α DayMods::Records()Ι->vector<HistoryRecord>{
		vector<HistoryRecord> y;
		for( let& [_,mods] : ByTime ){
			for( let& m : mods )
				*y.emplace_back().mutable_modification() = m;
		}
		return y;
	}
}
namespace Jde::Opc{
	α Hist::Apply( vector<HistoryRecord>& at, vector<optional<Merge::Position>>* where, const vector<Proto::Modification>& mods )ι->void{
		for( let& m : mods ){
			let index = m.node_index();
			let replacement = [&]{
				HistoryRecord y;
				*y.mutable_value() = m.new_value();
				y.mutable_value()->set_node_index( index );
				return y;
			};
			//The last record of the node here that is the original.
			auto target = at.end();
			if( m.has_original() ){
				for( auto p = at.rbegin(); p!=at.rend(); ++p ){
					if( p->has_value() && p->value().node_index()==index && Same(p->value(), m.original()) ){
						target = std::prev( p.base() );
						break;
					}
				}
			}
			switch( m.update_type() ){
			case Proto::UPDATE_TYPE_DELETE:
				if( target!=at.end() ){
					if( where )
						where->erase( where->begin()+(target-at.begin()) );
					at.erase( target );
				}
				break;
			case Proto::UPDATE_TYPE_INSERT:
			case Proto::UPDATE_TYPE_REPLACE:
			case Proto::UPDATE_TYPE_UPDATE:
				if( target!=at.end() && m.update_type()!=Proto::UPDATE_TYPE_INSERT )
					*target = replacement();
				else{
					at.push_back( replacement() );
					if( where )
						where->emplace_back();
				}
				break;
			default:
				break;
			}
		}
	}
namespace Hist{
	Applied::Applied( sp<ReadHandle> file, vector<Run> runs, vector<HistoryRecord> late, DayMods mods, SL sl )ι:
		_merge{ move(file), move(runs), move(late), sl },
		_mods{ move(mods) },
		_nextMod{ _mods.ByTime.begin() }
	{}

	α Applied::Advance()ε->void{
		Placed next;
		if( _merge.Next(next.Record) ){
			next.Where = _merge.Where();
			_ahead = move( next );
		}
		else
			_ahead.reset();
	}

	α Applied::Next( HistoryRecord& r, optional<Merge::Position>& where )ε->bool{
		while( _queue.empty() ){//empty again when every record at t was deleted:  the next time.
			if( !_ahead )
				Advance();
			let modsLeft = _nextMod!=_mods.ByTime.end();
			if( !_ahead && !modsLeft )
				return false;
			//The records at the earliest time either has, together, then the modifications there.
			let t = _ahead && modsLeft ? std::min( time(_ahead->Record), _nextMod->first ) : _ahead ? time( _ahead->Record ) : _nextMod->first;
			vector<HistoryRecord> at;
			vector<optional<Merge::Position>> places;
			while( _ahead && time(_ahead->Record)==t ){
				at.push_back( move(_ahead->Record) );
				places.push_back( _ahead->Where );
				Advance();
			}
			if( modsLeft && _nextMod->first==t ){
				Apply( at, &places, _nextMod->second );
				std::ranges::fill( places, nullopt );
				++_nextMod;
			}
			for( uint i=0; i<at.size(); ++i )
				_queue.push_back( {move(at[i]), places[i]} );
		}
		r = move( _queue.front().Record );
		where = _queue.front().Where;
		_queue.pop_front();
		return true;
	}
}}