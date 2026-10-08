#include "Edits.h"
#include <jde/opc/UAException.h>
#include <jde/opc/proto/opc.Common.h>
#include "Reads.h"
#include "Store.h"
#include "io/Applied.h"
#include "io/Records.h"

#define let const auto

namespace Jde::Opc::Hist{
	using namespace std::chrono;
	using Proto::HistoryRecord;
	namespace{
		constexpr Ticks Latest{ std::numeric_limits<Ticks>::max() };
		Ω nodeOf( const EditDetails& d )ι->NodeIndex{ return std::visit( []( let& e ){ return e.Node; }, d ); }
		//The day a time falls on:  none for one no day holds.
		Ω dayOf( UA_DateTime t, const time_zone& tz )ι->optional<Day>{ return Fileable( t ) ? optional<Day>{ DayOf(t, tz) } : nullopt; }
		Ω updateType( UA_PerformUpdateType type )ι->Proto::UpdateType{
			return type==UA_PERFORMUPDATETYPE_INSERT ? Proto::UPDATE_TYPE_INSERT : type==UA_PERFORMUPDATETYPE_REPLACE ? Proto::UPDATE_TYPE_REPLACE : Proto::UPDATE_TYPE_UPDATE;
		}
	}

	α EditAwait::Suspend()ι->void{ _group->Request( *this ); }
	α Group::Edit( vector<EditDetails> details, Writer by, SL sl )ι->EditAwait{ return EditAwait{ shared_from_this(), move(details), move(by), sl }; }

	α Group::Request( EditAwait& waiter )ι->void{
		bool start{};
		optional<Exception> refused;
		{
			ul _{ _mutex };
			if( _closed )
				refused.emplace( Exception{waiter.Source(), {ELogLevel::Debug}, "Group '{}' was removed.", Name()} );
			else if( _stopped || _ended )
				refused.emplace( Exception{waiter.Source(), {ELogLevel::Debug}, "Group '{}' has stopped.", Name()} );
			else{
				_edits.push_back( mu<Editing>(move(waiter._details), move(waiter._by), &waiter, waiter.Source()) );
				start = !std::exchange( _flushing, true );
			}
		}
		if( refused )
			waiter.ResumeExp( move(*refused) );
		else if( start )
			Flushing( shared_from_this() );
	}
	α Group::Failed( vector<up<Editing>>& edits, string why )ι->void{
		for( auto& edit : edits )
			edit->Waiter->ResumeExp( Exception{edit->Sl, {ELogLevel::Warning}, "{}", why} );
		edits.clear();
	}

	α Group::Step( uint step, vector<up<Editing>>& edits, const Membership& members, TimePoint taken, SL sl )ι->optional<vector<Job>>{
		if( step==0 ){
			ul _{ _filesMutex };
			return Corrections( members, taken, sl );
		}
		let i = (step-1)/3, phase = (step-1)%3;
		if( i>=edits.size() )
			return nullopt;
		auto& edit = *edits[i];
		switch( phase ){
		case 0: return Creations( edit, members, taken, sl );
		case 1: return Modifications( edit, members, taken, sl );
		default:{
			Newest( edit, sl );
			ul _{ _filesMutex };
			return Corrections( members, taken, sl );
		}}
	}
	α Group::Corrections( const Membership& members, TimePoint taken, SL sl )ι->vector<Job>{
		vector<Job> y;
		for( auto& [day,write] : _files->Corrections(members, taken, sl) )
			y.push_back( {day, move(write), {}} );
		return y;
	}

	//Checks each entry and value, notes the day each falls on, and makes the value file of each day an UpdateData may put a
	//record on that has none, with its preamble alone:  its modifications file follows it.
	α Group::Creations( Editing& edit, const Membership& members, TimePoint taken, SL sl )ι->vector<Job>{
		let& tz = *_store->Config.TimeZone;
		flat_set<Day> needFile;
		absl::flat_hash_set<NodeIndex> members0;
		{
			ul _{ _mutex };
			for( let& [index,_] : _nodes )
				members0.insert( index );
		}
		for( uint e=0; e<edit.Details.size(); ++e ){
			auto& result = edit.Results[e];
			let& details = edit.Details[e];
			if( !members0.contains(nodeOf(details)) ){
				result.Status = UA_STATUSCODE_BADNODEIDUNKNOWN;
				continue;
			}
			if( let update = get_if<UpdateData>(&details) ){
				result.Results.assign( update->Values.size(), UA_STATUSCODE_GOOD );
				if( update->Type!=UA_PERFORMUPDATETYPE_INSERT && update->Type!=UA_PERFORMUPDATETYPE_REPLACE && update->Type!=UA_PERFORMUPDATETYPE_UPDATE ){
					result.Status = UA_STATUSCODE_BADHISTORYOPERATIONUNSUPPORTED;
					continue;
				}
				for( uint i=0; i<update->Values.size(); ++i ){
					let& v = update->Values[i];
					let day = v.hasSourceTimestamp ? dayOf( v.sourceTimestamp, tz ) : nullopt;
					if( !day ){
						result.Results[i] = UA_STATUSCODE_BADINVALIDTIMESTAMPARGUMENT;
						continue;
					}
					edit.ByDay[*day].emplace_back( e, i );
					if( update->Type!=UA_PERFORMUPDATETYPE_REPLACE )
						needFile.insert( *day );
				}
			}
			else if( let erase = get_if<DeleteRaw>(&details) ){
				if( !Fileable(erase->Start) || !Fileable(erase->End) )
					result.Status = UA_STATUSCODE_BADINVALIDTIMESTAMPARGUMENT;
				else if( erase->Start>erase->End )
					result.Status = UA_STATUSCODE_BADINVALIDARGUMENT;
			}
			else{
				let& at = get<DeleteAtTime>( details );
				result.Results.assign( at.Times.size(), UA_STATUSCODE_GOOD );
				for( uint i=0; i<at.Times.size(); ++i ){
					if( let day = dayOf(at.Times[i], tz); day )
						edit.ByDay[*day].emplace_back( e, i );
					else
						result.Results[i] = UA_STATUSCODE_BADINVALIDTIMESTAMPARGUMENT;
				}
			}
		}
		for( let day : edit.Unflushed ){
			if( edit.ByDay.contains(day) )
				edit.Fail( day );
		}
		vector<Job> y;
		ul _{ _filesMutex };
		for( let day : needFile ){
			if( edit.Failed.contains(day) || _files->HasFile(day) )
				continue;
			try{
				auto write = _files->Prepare( day, {}, members, taken, sl, true );
				if( std::holds_alternative<std::monostate>(write) )//one the historian leaves alone.
					edit.Fail( day );
				else
					y.push_back( {day, move(write), [&edit, day]( bool ok ){ if( !ok ) edit.Fail( day ); }} );
			}
			catch( Exception& e ){
				e.SetLevel( ELogLevel::Error );
				edit.Fail( day );
			}
		}
		return y;
	}

	//Each day's records of the edit's nodes, the modifications applied, then each entry against them in order, which
	//says what each value does and writes its Modification record, one append per day.
	α Group::Modifications( Editing& edit, const Membership& members, TimePoint taken, SL sl )ι->vector<Job>{
		let& tz = *_store->Config.TimeZone;
		absl::flat_hash_set<NodeIndex> wanted;
		flat_set<Day> days;
		for( let& [day,_] : edit.ByDay )
			days.insert( day );
		optional<vector<Day>> onDisk;//for a range delete:  each day with a file inside its range.
		bool unlisted{};//the days couldn't be listed, so a range delete can't know where its records are.
		for( uint e=0; e<edit.Details.size(); ++e ){
			if( UA_StatusCode_isBad(edit.Results[e].Status) )
				continue;
			wanted.insert( nodeOf(edit.Details[e]) );
			let erase = get_if<DeleteRaw>( &edit.Details[e] );
			if( !erase )
				continue;
			if( !onDisk ){
				try{
					onDisk = Days( _store->Config.Path, Name(), false, sl );
				}
				catch( Exception& x ){
					x.SetLevel( ELogLevel::Error );
					onDisk.emplace();
					unlisted = true;
				}
			}
			if( unlisted ){
				edit.Results[e].Status = UA_STATUSCODE_BADUNEXPECTEDERROR;
				continue;
			}
			let first = DayOf( erase->Start, tz ), last = DayOf( erase->End, tz );
			//A day the flush before couldn't write fails it, whether or not its file is made yet.
			for( let day : edit.Unflushed ){
				if( day>=first && day<=last ){
					edit.ByDay[day].emplace_back( e, nullopt );
					edit.Fail( day );
				}
			}
			for( let day : *onDisk ){
				if( day>=first && day<=last && !edit.Unflushed.contains(day) ){
					days.insert( day );
					edit.ByDay[day].emplace_back( e, nullopt );
				}
			}
		}
		//Served under the files lock and read outside it:  nothing but this flush writes the files meanwhile.
		struct Served final{ optional<GroupFiles::Served> Values, Mods; };
		std::map<Day,Served> served;
		{
			ul _{ _filesMutex };
			for( let day : days ){
				if( edit.Failed.contains(day) )
					continue;
				try{
					served[day] = { _files->Serve(day, sl), _files->ServeMods(day, sl) };
				}
				catch( Exception& x ){
					x.SetLevel( ELogLevel::Error );
					edit.Fail( day );
				}
			}
		}
		using Key = std::pair<NodeIndex,Ticks>;
		std::map<Key,vector<Proto::DataValue>> series;//the edit's nodes' records, each time's in order.
		for( auto& [day,s] : served ){
			try{
				Applied stream{ s.Values ? s.Values->File : nullptr, s.Values ? s.Values->Runs : vector<Run>{}, {}, s.Mods ? DayMods::Read(*s.Mods, sl) : DayMods{}, sl };
				HistoryRecord r;
				optional<Merge::Position> where;
				while( stream.Next(r, where) ){
					if( !r.has_value() || !wanted.contains(r.value().node_index()) )
						continue;
					if( let t = PrimaryTime(r.value()); t )
						series[{r.value().node_index(), *t}].push_back( move(*r.mutable_value()) );
				}
			}
			catch( Exception& x ){
				x.SetLevel( ELogLevel::Error );
				edit.Fail( day );
			}
		}

		std::map<Day,vector<HistoryRecord>> records;//each day's Modification records, in the order made.
		std::map<Day,vector<Key>> changed;
		let now = ticks( taken );
		let modify = [&]( NodeIndex index, Ticks target, Proto::UpdateType type, const Proto::DataValue* original, const Proto::DataValue* value ){
			HistoryRecord y;
			auto& m = *y.mutable_modification();
			m.set_node_index( index );
			m.set_target_source_ts( target );
			m.set_update_type( type );
			if( original ){
				*m.mutable_original() = *original;
				m.mutable_original()->clear_node_index();
			}
			if( value ){
				*m.mutable_new_value() = *value;
				m.mutable_new_value()->clear_node_index();
			}
			m.set_ts( now );
			SetWriter( m, edit.By );
			let day = DayOf( target, tz );
			records[day].push_back( move(y) );
			changed[day].emplace_back( index, target );
		};
		for( uint e=0; e<edit.Details.size(); ++e ){
			auto& result = edit.Results[e];
			if( UA_StatusCode_isBad(result.Status) )
				continue;
			let& details = edit.Details[e];
			let index = nodeOf( details );
			if( let update = get_if<UpdateData>(&details) ){
				for( uint i=0; i<update->Values.size(); ++i ){
					auto& status = result.Results[i];
					if( UA_StatusCode_isBad(status) )
						continue;
					let& v = update->Values[i];
					Proto::DataValue stored;
					try{
						stored = ToProto( v, index );
					}
					catch( Exception& x ){
						x.SetLevel( ELogLevel::Warning );
						status = UA_STATUSCODE_BADUNEXPECTEDERROR;
						continue;
					}
					if( v.hasValue && !UA_Variant_isEmpty(&v.value) && !stored.has_value() ){//no file form:  the status it would be stored with.
						status = stored.status() ? stored.status() : UA_STATUSCODE_BADNOTSUPPORTED;
						continue;
					}
					auto& at = series[{index, v.sourceTimestamp}];
					if( update->Type==UA_PERFORMUPDATETYPE_INSERT && !at.empty() ){
						status = UA_STATUSCODE_BADENTRYEXISTS;
						continue;
					}
					if( update->Type==UA_PERFORMUPDATETYPE_REPLACE && at.empty() ){
						status = UA_STATUSCODE_BADNOENTRYEXISTS;
						continue;
					}
					let replacing = !at.empty() && update->Type!=UA_PERFORMUPDATETYPE_INSERT;
					modify( index, v.sourceTimestamp, updateType(update->Type), replacing ? &at.back() : nullptr, &stored );
					if( replacing )
						at.back() = stored;
					else
						at.push_back( stored );
					status = replacing ? UA_STATUSCODE_GOODENTRYREPLACED : UA_STATUSCODE_GOODENTRYINSERTED;
				}
			}
			else if( let erase = get_if<DeleteRaw>(&details) ){
				uint count{};
				for( auto p = series.lower_bound({index, erase->Start}); p!=series.end() && p->first.first==index && p->first.second<=erase->End; ++p ){
					for( let& original : p->second )
						modify( index, p->first.second, Proto::UPDATE_TYPE_DELETE, &original, nullptr );
					count += p->second.size();
					p->second.clear();
				}
				result.Status = count ? UA_STATUSCODE_GOOD : UA_STATUSCODE_BADNODATA;
			}
			else{
				let& atTime = get<DeleteAtTime>( details );
				for( uint i=0; i<atTime.Times.size(); ++i ){
					auto& status = result.Results[i];
					if( UA_StatusCode_isBad(status) )
						continue;
					auto& at = series[{index, atTime.Times[i]}];
					if( at.empty() ){
						status = UA_STATUSCODE_BADNOENTRYEXISTS;
						continue;
					}
					for( let& original : at )
						modify( index, atTime.Times[i], Proto::UPDATE_TYPE_DELETE, &original, nullptr );
					at.clear();
				}
			}
		}
		vector<Job> y;
		ul _{ _filesMutex };
		for( auto& [day,list] : records ){
			if( edit.Failed.contains(day) )
				continue;
			std::ranges::stable_sort( list, {}, []( let& r ){ return r.modification().target_source_ts(); } );
			try{
				auto write = _files->PrepareMods( day, move(list), members, sl );
				y.push_back( {day, DayWrite{move(write)}, [this, &edit, day, changes=move(changed[day])]( bool ok ){
					if( !ok ){
						edit.Fail( day );
						return;
					}
					ul _{ _filesMutex };
					for( let& [index,time] : changes ){
						_files->Touch( index, time );
						edit.Changed.emplace_back( index, time );
					}
				}} );
			}
			catch( Exception& x ){
				x.SetLevel( ELogLevel::Error );
				edit.Fail( day );
			}
		}
		return y;
	}

	//Each node's newest record as the edit leaves it:  one at or after the newest the files held moves it, by the walk
	//back through the edits applied.
	α Group::Newest( Editing& edit, SL sl )ι->void{
		absl::flat_hash_map<NodeIndex,Ticks> latest;
		for( let& [index,time] : edit.Changed ){
			auto [p, inserted] = latest.try_emplace( index, time );
			p->second = std::max( p->second, time );
		}
		ul _{ _filesMutex };
		for( let& [index,time] : latest ){
			if( let newest = _files->Newest(index); newest && *PrimaryTime(*newest)>time )
				continue;
			try{
				_files->Relast( index, Last(*_files, *_store->Config.TimeZone, _store->Config.ReadLimit, index, Latest, sl) );
			}
			catch( Exception& x ){//the newest stands as it was, which a later file's preamble may copy.
				x.SetLevel( ELogLevel::Error );
			}
		}
	}
}