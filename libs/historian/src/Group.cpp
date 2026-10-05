#include <jde/historian/Group.h>
#include <absl/container/flat_hash_set.h>
#include <jde/opc/proto/opc.Common.h>
#include "Compress.h"
#include "Store.h"
#include "io/DayFiles.h"

#define let const auto

namespace Jde::Opc::Hist{
	constexpr ELogTags _tags{ ELogTags::Settings };

	//What a record takes in a file, near enough:  its UA encoding, and what frames it.
	Ω bytes( const UA_DataValue& value )ι->uint32_t{
		return (uint32_t)UA_calcSizeBinary( &value, &UA_TYPES[UA_TYPES_DATAVALUE], nullptr )+8;
	}
	Ω bytes( const optional<Writer>& by, const UA_ExpandedNodeId* node=nullptr )ι->uint32_t{
		return 16+( by ? (uint32_t)by->UserName.size()+6 : 0 )+( node ? (uint32_t)UA_calcSizeBinary(node, &UA_TYPES[UA_TYPES_EXPANDEDNODEID], nullptr) : 0 );
	}
	//The Bad_DataLost that opens a gap at a dropped value's time.
	Ω marker( const UA_DataValue& dropped )ι->Value{
		UA_DataValue y{};
		y.status = UA_STATUSCODE_BADDATALOST;
		y.hasStatus = true;
		y.sourceTimestamp = dropped.sourceTimestamp;
		y.hasSourceTimestamp = dropped.hasSourceTimestamp;
		y.serverTimestamp = dropped.serverTimestamp;
		y.hasServerTimestamp = dropped.hasServerTimestamp;
		return Value{ move(y) };
	}
	//The Bad_DataLost a break leaves, found by the value that ended it.
	Ω marker( UA_DateTime at, const UA_DataValue& ended )ι->Value{
		UA_DataValue y{};
		y.status = UA_STATUSCODE_BADDATALOST;
		y.hasStatus = true;
		y.sourceTimestamp = at;
		y.hasSourceTimestamp = true;
		y.serverTimestamp = ended.serverTimestamp;
		y.hasServerTimestamp = ended.hasServerTimestamp;
		return Value{ move(y) };
	}
	using UATicks = std::chrono::duration<UA_DateTime,std::ratio<1,10'000'000>>;
	Ω ticks( Duration d )ι->UA_DateTime{ return std::chrono::duration_cast<UATicks>( d ).count(); }
	Ω ticks( TimePoint t )ι->UA_DateTime{ return UADateTime{ t }.UA(); }

	//A percent-of-range format with no usable range stores every change, warned of when the node is added; anything else
	//wrong is the host's config, named by its node.
	Ω validate( const ExNodeId& node, Thresholds& t, SL sl )ε->void{
		THROW_IFSL( t.MinTimeInterval<Duration::zero() || t.MaxTimeInterval<Duration::zero(), "'{}' has a negative MinTimeInterval or MaxTimeInterval.", node.to_string() );
		if( !t.ExceptionDeviation )
			return;
		let deviation = *t.ExceptionDeviation;
		THROW_IFSL( !std::isfinite(deviation) || deviation<0, "'{}' has ExceptionDeviation {}, not a finite, non-negative number.", node.to_string(), deviation );
		THROW_IFSL( t.DeviationFormat>UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFEURANGE, "'{}' has an unknown ExceptionDeviationFormat, {}.", node.to_string(), underlying(t.DeviationFormat) );
		let ranged = t.DeviationFormat==UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFRANGE || t.DeviationFormat==UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFEURANGE;
		if( ranged && (!t.Range || !std::isfinite(t.Range->Low) || !std::isfinite(t.Range->High) || t.Range->High<=t.Range->Low) ){
			WARN( "'{}' has no usable range for its percent-of-range ExceptionDeviation, so it stores every change.", node.to_string() );
			t.ExceptionDeviation.reset();
		}
	}

	Writer::Writer( UserPK identityId, string userName, SL sl )ε:
		IdentityId{ identityId },
		UserName{ move(userName) }{
		THROW_IFSL( identityId.Value!=UserPK::System && identityId.Value>=std::numeric_limits<uint32_t>::max(), "Identity {} doesn't fit a record's 32-bit identity_id.", identityId.Value );
		THROW_IFSL( !ProtoUtils::Utf8(UserName), "Identity {}'s user name isn't UTF-8, which a record's user_name must be.", identityId.Value );
	}

	Group::Group( GroupConfig config, sp<Store> store, vector<Member> members, SL sl )ε:
		_store{ move(store) },
		_config{ move(config) }{
		let now = _store->Time->Now();
		let& tz = *_store->Config.TimeZone;
		_files = mu<GroupFiles>( _store->Config.Path, _config.Name, tz, _store->Config.Delay, DayOf(now, tz), sl );
		let restored = _files->TakeRestored();
		absl::flat_hash_set<NodeIndex> restoredIndexes, kept;
		restoredIndexes.reserve( restored.Members.size() );
		for( let& [_,index] : restored.Members )
			restoredIndexes.emplace( index );
		//A member's newest record in the files stands in for what the process no longer remembers:  its last stored value,
		//and the last delivered one its first value is compared with - a value by its own SourceTimestamp, a heartbeat by
		//that of the value it repeats, and a marker by none.
		let resume = []( Node& node, const Proto::DataValue& newest ){
			let primary = *PrimaryTime( newest );
			node.RecordTs = primary;
			node.StoredAt = UADateTime{ primary }.Time();
			if( !newest.has_value() && !newest.has_heartbeat() && newest.status()==UA_STATUSCODE_BADDATALOST ){
				node.Stored.emplace( (StatusCode)UA_STATUSCODE_BADDATALOST );
				node.Marked = true;
				return;
			}
			try{
				node.Stored.emplace( ToUA(newest) );
			}
			catch( Exception& e ){//whatever comes next is stored.
				e.SetLevel( ELogLevel::Warning );
				return;
			}
			node.ValueTs = newest.has_heartbeat() ? newest.heartbeat() : primary;
			if( newest.has_heartbeat() || newest.has_source_ts() )
				node.Delivered = newest.has_heartbeat() ? newest.heartbeat() : newest.source_ts();
		};
		vector<NodeAdded> added;
		{
			ul _{ _mutex };
			_indexes.reserve( members.size() );
			_nextIndex = restored.NextIndex;
			for( auto& member : members ){
				auto node = Normalize( member, sl );
				if( auto p = restored.Members.find(node); p!=restored.Members.end() && (Issued() || p->second==member.Index) ){
					kept.emplace( p->second );
					let index = Insert( move(node), move(member.Config), p->second, sl );
					//The stop or the crash is its break, unless the files hold nothing of it:  there was no value to lose.
					if( let newest = _files->Newest(index) ){
						auto& known = _nodes.find( index )->second;
						known.Break = restored.Flushed;
						resume( known, *newest );
					}
				}
				else{
					let index = Insert( node, move(member.Config), member.Index, sl );
					THROW_IFSL( restoredIndexes.contains(index), "Group '{}' cannot give '{}' node_index {}: its files have it as another node's.", Name(), node.to_string(), index );
					added.push_back( {index, move(node), now, {}} );
				}
			}
			//Nothing throws from here, so what is buffered is counted once.
			for( let& [node,index] : restored.Members ){
				if( kept.contains(index) )
					continue;
				_gone.emplace( index, node );
				Hold( NodeRemoved{index, now, {}}, bytes(optional<Writer>{}) );
			}
			for( auto& record : added ){
				let size = bytes( {}, &record.Node );
				Hold( move(record), size );
			}
		}
		_store->Register( *this );//outside _mutex, which the Store's lock comes before.
	}
	Group::~Group(){
		_store->Unregister( *this );
		Cancel( {_timer, _midnight, _beat} );
		_store->Subtract( _held );
	}

	//The URI names the namespace, so the index - the host's own numbering - is dropped and never compared.
	α Group::Normalize( Member& member, SL sl )Ε->ExNodeId{
		auto node = move( member.Node );
		THROW_IFSL( !node.namespaceUri.length, "'{}' has no namespace URI - the historian keeps none by its index.", node.to_string() );
		THROW_IFSL( !ProtoUtils::Utf8(node), "'{}' has a namespace URI or string identifier that isn't UTF-8, which a file can't hold.", node.to_string() );
		node.nodeId.namespaceIndex = 0;
		THROW_IFSL( Issued()==(member.Index!=0), "Group '{}' {} node_index, but '{}' came with {}.", Name(), Issued() ? "issues its own" : "takes the host's", node.to_string(), member.Index );
		THROW_IFSL( !std::in_range<uint32_t>(member.Index), "Group '{}' cannot give '{}' node_index {}: it doesn't fit a record's 32 bits.", Name(), node.to_string(), member.Index );
		validate( node, member.Config, sl );
		return node;
	}
	α Group::Insert( ExNodeId node, Thresholds config, NodeIndex index, SL sl )ε->NodeIndex{
		THROW_IFSL( _indexes.contains(node), "'{}' is already in group '{}'.", node.to_string(), Name() );
		if( !index )
			index = _nextIndex++;
		THROW_IFSL( _nodes.contains(index), "node_index {} is already in group '{}'.", index, Name() );
		_indexes.emplace( node, index );
		optional<TimePoint> rejoin;
		if( auto p = _left.find(node); p!=_left.end() ){
			rejoin = p->second;
			_left.erase( p );
		}
		_nodes.emplace( index, Node{move(node), move(config), rejoin} );
		return index;
	}

	α Group::Hold( Record&& record, uint32_t bytes )ι->bool{
		if( _deferred && !_deferredDays.contains(DayOf(PrimaryTime(record), *_store->Config.TimeZone)) )
			_deferred = false;
		let isValue = std::holds_alternative<DataValue>( record );
		Buffered buffered{ move(record), _store->Sequence(), bytes };
		let cost = Cost( buffered );
		if( isValue )
			_values.push_back( move(buffered) );
		else
			_changes.push_back( move(buffered) );
		_held += cost;
		_fresh += bytes;
		return _store->Add( cost );
	}
	α Group::ClaimFlush( bool over )ι->bool{
		if( (_fresh<Settings::FlushBytes && !over) || _requested || _failing || (_deferred && !over) )
			return false;
		return _requested = true;
	}
	α Group::Push( Record&& record, uint32_t bytes )ι->Pushing{
		let over = Hold( move(record), bytes );
		let flush = ClaimFlush( over );
		return { flush, over && !flush };
	}
	α Group::Pushed( Pushing pushing )ι->void{
		if( pushing.Flush )
			Schedule( Duration::zero() );
		if( pushing.Trim )
			_store->RequestTrim();
	}
	α Group::Schedule( Duration after )ι->IClock::TimerId{
		return _store->Time->Schedule( after, [weak=weak_from_this()]{
			if( auto group = weak.lock() )
				group->Request();
		});
	}

	α Group::Add( Member member, optional<Writer> by, SL sl )ε->NodeIndex{
		auto node = Normalize( member, sl );
		let now = _store->Time->Now();
		let size = bytes( by, &node );
		NodeIndex index;
		Pushing pushing;
		{
			ul _{ _mutex };
			THROW_IFSL( _closed || _stopped, "Group '{}' was removed.", Name() );
			index = Insert( node, move(member.Config), member.Index, sl );
			pushing = Push( NodeAdded{index, move(node), now, move(by)}, size );
		}
		Pushed( pushing );
		return index;
	}

	α Group::Remove( NodeIndex index, optional<Writer> by, SL sl )ε->void{
		let now = _store->Time->Now();
		let size = bytes( by );
		Effects effects;
		{
			ul _{ _mutex };
			auto p = _nodes.find( index );
			THROW_IFSL( p==_nodes.end() || _stopped, "node_index {} is not in group '{}'.", index, Name() );
			if( p->second.Pending )//delivered while it was a member.
				Settle( index, p->second, now, effects );
			_indexes.erase( p->second.Id );
			_gone.insert_or_assign( index, p->second.Id );
			_left.insert_or_assign( move(p->second.Id), p->second.Break.value_or(now) );
			_nodes.erase( p );
			effects += Push( NodeRemoved{index, now, move(by)}, size );
		}
		Finish( move(effects) );
	}

	α Group::SetThresholds( NodeIndex index, Thresholds thresholds, SL sl )ε->void{
		let now = _store->Time->Now();
		Effects effects;
		{
			ul _{ _mutex };
			auto p = _nodes.find( index );
			THROW_IFSL( p==_nodes.end(), "node_index {} is not in group '{}'.", index, Name() );
			validate( p->second.Id, thresholds, sl );
			p->second.Config = move( thresholds );
			Arm( p->second, now, effects );//a MaxTimeInterval it gained, or a shorter one.
		}
		Finish( move(effects) );
	}
	α Group::FindThresholds( NodeIndex index )Ι->optional<Thresholds>{
		ul _{ _mutex };
		auto p = _nodes.find( index );
		return p==_nodes.end() ? optional<Thresholds>{} : p->second.Config;
	}
	α Group::Find( const ExNodeId& node )Ι->optional<NodeIndex>{
		ExNodeId key{ node };
		key.nodeId.namespaceIndex = 0;
		ul _{ _mutex };
		auto p = _indexes.find( key );
		return p==_indexes.end() ? optional<NodeIndex>{} : p->second;
	}

	α Group::Enqueue( NodeIndex index, const UA_DataValue& value )ι->bool{
		Value copy{ value };//outside the lock, which the host's own lock is already held over.
		let now = _store->Time->Now();
		//A time no day holds would be filed at the last or first day, and a 9999 file would stay the newest for good.
		if( copy.hasSourceTimestamp && !Fileable(copy.sourceTimestamp) ){
			copy.hasSourceTimestamp = copy.hasSourcePicoseconds = false;
			copy.sourceTimestamp = copy.sourcePicoseconds = 0;
		}
		if( !copy.hasServerTimestamp || !Fileable(copy.serverTimestamp) ){
			copy.serverTimestamp = ticks( now );
			copy.hasServerTimestamp = true;
		}
		let unsupported = copy.hasValue && !ProtoUtils::Supported( copy.value );
		let notUtf8 = copy.hasValue && !unsupported && !ProtoUtils::Utf8( copy.value );
		let size = bytes( copy );
		Effects effects;
		{
			ul _{ _mutex };
			auto p = _nodes.find( index );
			if( p==_nodes.end() || _stopped )
				return false;
			Collect( index, p->second, {move(copy), size, unsupported, notUtf8}, now, effects );
		}
		Finish( move(effects) );
		return true;
	}

	α Group::Collect( NodeIndex index, Node& node, Arrival&& arrival, TimePoint now, Effects& effects )ι->void{
		let& value = arrival.Data;
		let source = value.hasSourceTimestamp ? optional<UA_DateTime>{ value.sourceTimestamp } : nullopt;
		let time = PrimaryTime( value );
		let last = std::exchange( node.Delivered, source );
		let joined = std::exchange( node.Joined, false );
		if( let broke = std::exchange(node.Break, nullopt) ){
			if( source && last && *source==*last ){//nothing was lost.
				Arm( node, now, effects );
				return;
			}
			//Earlier, the source's clock or state went backwards:  at its own time the value would land before a record
			//reads have already returned.  A comparison with no SourceTimestamp on either side means nothing, so it is later.
			let earlier = source && last && *source<*last;
			Mark( index, node, earlier ? ticks(*broke) : std::min(ticks(*broke), time), value, now, effects );
			if( earlier ){
				effects.Warning = Ƒ( "'{}' in group '{}' came back from the break at {} with a value sourced at {}, earlier than the {} it last delivered:  the value is not stored, and the node reads Bad_DataLost until its next change.",
					node.Id.to_string(), Name(), ToIsoString(*broke), ToIsoString(UADateTime{*source}.Time()), ToIsoString(UADateTime{*last}.Time()) );
				return;
			}
			DropBeats( index, node, time );
			StoreValue( index, node, move(arrival), now, effects );
			return;
		}
		//Not from its first value after a join or a break, whose SourceTimestamp is when the value last changed, not when
		//it was sent.
		if( !joined && source )
			node.Offset = now-UADateTime{ *source }.Time();

		let min = ticks( node.Config.MinTimeInterval );
		let within = [&]{ return min>0 && node.ValueTs && time-*node.ValueTs<min; };
		if( node.Pending && !within() )//by the source's clock its interval ended before this change.
			Settle( index, node, now, effects );
		let passes = !node.Stored || Passes( node.Config, *node.Stored, value );
		if( passes )
			DropBeats( index, node, time );
		if( node.Pending ){//replaced, passing or not:  what the interval's end stores is where the value settled.
			*node.Pending = move( arrival );
			return;
		}
		if( !passes )
			return;
		if( !within() ){
			StoreValue( index, node, move(arrival), now, effects );
			return;
		}
		node.Pending = move( arrival );
		let left = UATicks{ min-std::clamp<UA_DateTime>(time-*node.ValueTs, 0, min) };
		_store->Time->Schedule( std::chrono::duration_cast<Duration>(left), [weak=weak_from_this(), index, serial=++node.Serial]{
			if( auto group = weak.lock() )
				group->Expire( index, serial );
		});
	}
	α Group::StoreValue( NodeIndex index, Node& node, Arrival&& arrival, TimePoint now, Effects& effects )ι->void{
		let first = ( arrival.Unsupported && !std::exchange(node.Unsupported, true) ) || ( arrival.NotUtf8 && !std::exchange(node.NotUtf8, true) );
		let time = PrimaryTime( arrival.Data );
		node.Stored.emplace( arrival.Data );
		node.Marked = false;
		node.ValueTs = time;
		node.RecordTs = std::max( node.RecordTs, time );
		node.StoredAt = now;
		effects += Push( DataValue{index, move(arrival.Data), {}, first}, arrival.Bytes );
		Arm( node, now, effects );
	}
	α Group::Mark( NodeIndex index, Node& node, UA_DateTime at, const UA_DataValue& ended, TimePoint now, Effects& effects )ι->void{
		auto lost = marker( at, ended );
		let size = bytes( lost );
		node.Stored.emplace( (StatusCode)UA_STATUSCODE_BADDATALOST );
		node.Marked = true;
		node.RecordTs = std::max( node.RecordTs, at );
		node.StoredAt = now;
		effects += Push( DataValue{index, move(lost)}, size );
	}
	α Group::Settle( NodeIndex index, Node& node, TimePoint now, Effects& effects )ι->void{
		auto pending = move( *node.Pending );
		node.Pending.reset();
		++node.Serial;//its timer, if that is still to run, finds nothing of its own.
		if( !node.Stored || Passes(node.Config, *node.Stored, pending.Data) )
			StoreValue( index, node, move(pending), now, effects );
		else
			Arm( node, now, effects );//the heartbeat left it alone while it was pending.
	}
	α Group::Expire( NodeIndex index, uint serial )ι->void{
		let now = _store->Time->Now();
		Effects effects;
		{
			ul _{ _mutex };
			auto p = _nodes.find( index );
			if( p==_nodes.end() || !p->second.Pending || p->second.Serial!=serial || _stopped )
				return;
			Settle( index, p->second, now, effects );
		}
		Finish( move(effects) );
	}
	α Group::DropBeats( NodeIndex index, Node& node, UA_DateTime from )ι->void{
		if( !node.Beat || from>*node.Beat )
			return;
		node.Beat.reset();
		uint freed{}, fresh{};
		std::erase_if( _values, [&]( const Buffered& b ){
			let& value = get<DataValue>( b.Item );
			if( value.Index!=index || !value.Heartbeat )
				return false;
			let made = PrimaryTime( value.Data );
			if( made<from ){
				node.Beat = std::max( node.Beat.value_or(made), made );
				return false;
			}
			freed += Cost( b );
			fresh += b.Bytes;
			return true;
		});
		_held -= freed;
		_fresh -= std::min( _fresh, fresh );
		_store->Subtract( freed );
	}

	α Group::Arm( const Node& node, TimePoint now, Effects& effects )ι->void{
		if( !Beats(node) || !_connected || _stopped || _closed )
			return;
		Arm( std::max(node.StoredAt+node.Config.MaxTimeInterval, now), now, effects );
	}
	α Group::Arm( TimePoint due, TimePoint now, Effects& effects )ι->void{
		if( _beat && _beatDue<=due )
			return;
		if( _beat )
			effects.Stale.push_back( _beat );
		ScheduleBeat( due, now );
	}
	α Group::ScheduleBeat( TimePoint due, TimePoint now )ι->void{
		_beatDue = due;
		_beat = _store->Time->Schedule( due-now, [weak=weak_from_this(), serial=++_beatSerial]{
			if( auto group = weak.lock() )
				group->Beat( serial );
		});
	}
	α Group::Beat( uint serial )ι->void{
		let now = _store->Time->Now();
		Effects effects;
		{
			ul _{ _mutex };
			if( serial!=_beatSerial || _stopped || _ended )
				return;
			_beat = 0;
			if( !_connected )//a dead feed stays apart from a flat line:  each node's first value after the break arms it again.
				return;
			optional<TimePoint> next;
			for( auto&& [index,node] : _nodes ){
				if( !Beats(node) )
					continue;
				let max = node.Config.MaxTimeInterval;
				node.StoredAt = std::min( node.StoredAt, now );//one after now is from before a clock set back:  the wait is never longer.
				if( node.StoredAt+max<=now ){
					//In the source's clock, so it never sorts after a change sampled before it, and never earlier than
					//MaxTimeInterval after the node's last record.
					Value beat{ *node.Stored };
					beat.sourceTimestamp = std::max( ticks(now-node.Offset), node.RecordTs+ticks(max) );
					beat.serverTimestamp = ticks( now );
					beat.hasSourceTimestamp = beat.hasServerTimestamp = true;
					beat.sourcePicoseconds = beat.serverPicoseconds = 0;
					beat.hasSourcePicoseconds = beat.hasServerPicoseconds = false;
					node.RecordTs = beat.sourceTimestamp;
					node.StoredAt = now;
					node.Beat = beat.sourceTimestamp;
					let size = bytes( beat );
					effects += Push( DataValue{index, move(beat), node.ValueTs}, size );
				}
				let due = node.StoredAt+max;
				next = next ? std::min( *next, due ) : due;
			}
			if( next )
				ScheduleBeat( *next, now );
		}
		Finish( move(effects) );
	}
	α Group::Finish( Effects&& effects )ι->void{
		Pushed( effects.Pushed );
		for( let id : effects.Stale )
			_store->Time->Cancel( id );
		if( !effects.Warning.empty() )
			_store->Time->Schedule( Duration::zero(), [warning=move(effects.Warning)]{ WARN( "{}", warning ); } );
	}

	α Group::Disconnected( TimePoint at )ι->void{
		let now = _store->Time->Now();
		Effects effects;
		{
			ul _{ _mutex };
			_connected = false;
			for( auto&& [index,node] : _nodes ){
				//Delivered before the break, and the first value after it goes to the comparison instead of replacing it.
				if( node.Pending )
					Settle( index, node, now, effects );
				if( !node.Break )//no value since an earlier break, so that one still stands.
					node.Break = at;
			}
		}
		Finish( move(effects) );
	}
	α Group::Connected()ι->void{
		let now = _store->Time->Now();
		Effects effects;
		{
			ul _{ _mutex };
			_connected = true;
			//A node's first value after the break arms the heartbeat, but not one whose break a value in flight already took.
			optional<TimePoint> due;
			for( let& [_,node] : _nodes ){
				if( Beats(node) )
					due = std::min( due.value_or(TimePoint::max()), std::max(node.StoredAt+node.Config.MaxTimeInterval, now) );
			}
			if( due )
				Arm( *due, now, effects );
		}
		Finish( move(effects) );
	}
	α Group::IsConnected()Ι->bool{
		ul _{ _mutex };
		return _connected;
	}
	α Group::FindBreak( NodeIndex index )Ι->optional<TimePoint>{
		ul _{ _mutex };
		auto p = _nodes.find( index );
		return p==_nodes.end() ? optional<TimePoint>{} : p->second.Break;
	}

	α Group::Buffer()Ι->vector<Record>{
		ul _{ _mutex };
		vector<Record> y;
		y.reserve( _changes.size()+_values.size() );
		auto change = _changes.begin();
		for( let& value : _values ){
			for( ; change!=_changes.end() && change->Sequence<value.Sequence; ++change )
				y.push_back( change->Item );
			y.push_back( value.Item );
		}
		for( ; change!=_changes.end(); ++change )
			y.push_back( change->Item );
		return y;
	}
	α Group::Close( optional<Writer> by )ι->void{
		let now = _store->Time->Now();
		let size = bytes( by );
		bool over{}, trim;
		Effects effects;
		{
			ul _{ _mutex };
			for( auto&& [index,node] : _nodes ){
				if( node.Pending )
					Settle( index, node, now, effects );
				_gone.insert_or_assign( index, move(node.Id) );
				over = Hold( NodeRemoved{index, now, by}, size ) || over;
			}
			_nodes.clear();
			_indexes.clear();
			_closed = true;
			trim = ( over || effects.Pushed.Trim ) && _failing;//the flush asked for here takes the rest, if it can write.
		}
		for( let id : effects.Stale )
			_store->Time->Cancel( id );
		Pushed( {.Flush=true, .Trim=trim} );
	}

	α Group::Oldest()Ι->optional<uint>{
		ul _{ _mutex };
		return _values.empty() ? optional<uint>{} : _values.front().Sequence;
	}
	α Group::Drop( uint before, uint need, bool& began )ι->uint{
		uint freed{}, count{};
		ul _{ _mutex };
		for( ; !_values.empty() && _values.front().Sequence<before && freed<need; ++count ){
			auto dropped = move( _values.front() );
			_values.pop_front();
			freed += Cost( dropped );
			auto& value = get<DataValue>( dropped.Item );
			auto& lost = _lost[value.Index];
			//By source time, as a gap is read:  it opens at the earliest that was lost, and ends at the latest, which is
			//written back.  A late value can make that differ from the order they arrived in.
			if( !lost.Count++ || PrimaryTime(value.Data)<PrimaryTime(lost.Marker) )
				lost.Marker = marker( value.Data );
			if( !lost.Newest ){
				lost.Newest = move( dropped );
				continue;
			}
			auto& newest = get<DataValue>( lost.Newest->Item );
			let replace = PrimaryTime( value.Data )>=PrimaryTime( newest.Data );
			Unflag( replace ? newest : value );//the one not written back.
			if( replace )
				lost.Newest = move( dropped );
		}
		_held -= freed;
		_store->Subtract( freed );
		began = count && !std::exchange( _dropping, true );
		return count;
	}

	α Group::PruneGone()ι->void{
		if( _gone.empty() )
			return;
		absl::flat_hash_set<NodeIndex> buffered;
		for( let& value : _values )
			buffered.insert( get<DataValue>(value.Item).Index );
		for( let& [index,_] : _lost )
			buffered.insert( index );
		absl::erase_if( _gone, [&]( let& gone ){ return !buffered.contains(gone.first); } );
	}
	α Group::Unflag( const DataValue& gone )ι->void{
		if( !gone.Unsupported )
			return;
		if( auto p = _nodes.find(gone.Index); p!=_nodes.end() )
			( ProtoUtils::Supported(gone.Data.value) ? p->second.NotUtf8 : p->second.Unsupported ) = false;
	}
	α Group::MarkLost( vector<Buffered>& y )ι->void{
		for( auto&& [index,lost] : _lost ){
			if( lost.Count>1 ){//a node that lost one simply gets it back.
				let size = bytes( lost.Marker );
				y.push_back( {DataValue{index, move(lost.Marker)}, lost.Newest->Sequence, size} );
			}
			y.push_back( move(*lost.Newest) );
		}
		_lost.clear();
	}
	α Group::Take( TimePoint taken )ι->vector<Buffered>{
		vector<Buffered> y;
		y.reserve( 2*_lost.size()+_changes.size()+_values.size() );
		MarkLost( y );
		auto kept = _values.end();
		if( let interval = _config.PublishingInterval; interval>Duration::zero() && !_stopped && !_closed ){
			let now = ticks( taken ), from = ticks( taken-interval );
			kept = std::stable_partition( _values.begin(), _values.end(), [=]( const Buffered& b ){
				let& value = get<DataValue>( b.Item );
				let made = value.Data.serverTimestamp;
				return !value.Heartbeat || made<=from || made>now;//one after now is from before a clock set back, so it waits no longer.
			});
		}
		uint held{}, fresh{};
		for( auto p = kept; p!=_values.end(); ++p ){
			held += Cost( *p );
			fresh += p->Bytes;
		}
		_store->Subtract( std::exchange(_held, held)-held );
		std::ranges::merge( _changes | std::views::as_rvalue, std::ranges::subrange{_values.begin(), kept} | std::views::as_rvalue, std::back_inserter(y), {}, &Buffered::Sequence, &Buffered::Sequence );
		_changes.clear();
		_values.erase( _values.begin(), kept );
		_fresh = fresh;
		_requested = false;
		return y;
	}
	α Group::Return( vector<Buffered>&& records )ι->bool{
		std::ranges::stable_sort( records, {}, &Buffered::Sequence );
		vector<Buffered> changes;
		uint cost{};
		ul _{ _mutex };
		//What was dropped while these were out is a gap of its own, after them:  marked now, so dropping these begins
		//another, rather than moving its marker back over the values between.  Every such drop came after the flush took
		//these, and before what is still buffered.
		vector<Buffered> gaps;
		MarkLost( gaps );
		std::ranges::stable_sort( gaps, {}, &Buffered::Sequence );
		for( auto&& record : gaps | std::views::reverse ){
			cost += Cost( record );
			_values.push_front( move(record) );
		}
		for( auto&& record : records | std::views::reverse ){
			cost += Cost( record );
			if( std::holds_alternative<DataValue>(record.Item) )
				_values.push_front( move(record) );
			else
				changes.push_back( move(record) );
		}
		_changes.insert( _changes.begin(), std::make_move_iterator(changes.rbegin()), std::make_move_iterator(changes.rend()) );
		_held += cost;
		return _store->Add( cost );
	}
	α Group::Written()Ι->bool{
		return _closed && _archived && _changes.empty() && _values.empty() && _lost.empty();
	}
	α Group::EndIfWritten()ι->bool{
		Timers timers;
		{
			ul _{ _mutex };
			if( !Written() || _flushing )
				return false;
			_ended = true;
			timers = Disarm();
		}
		Cancel( timers );
		return true;
	}
	α Group::Cancel( Timers timers )ι->void{
		for( let id : {timers.Delay, timers.Midnight, timers.Beat} ){
			if( id )
				_store->Time->Cancel( id );
		}
	}
}