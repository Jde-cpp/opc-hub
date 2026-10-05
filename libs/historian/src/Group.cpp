#include <jde/historian/Group.h>
#include <absl/container/flat_hash_set.h>
#include <jde/opc/proto/opc.Common.h>
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
					_nodes.find( index )->second.Break = restored.Flushed;//the stop or the crash.
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
		Cancel( {_timer, _midnight} );
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
		Pushing pushing;
		{
			ul _{ _mutex };
			auto p = _nodes.find( index );
			THROW_IFSL( p==_nodes.end() || _stopped, "node_index {} is not in group '{}'.", index, Name() );
			_indexes.erase( p->second.Id );
			_gone.insert_or_assign( index, p->second.Id );
			_left.insert_or_assign( move(p->second.Id), p->second.Break.value_or(now) );
			_nodes.erase( p );
			pushing = Push( NodeRemoved{index, now, move(by)}, size );
		}
		Pushed( pushing );
	}

	α Group::SetThresholds( NodeIndex index, Thresholds thresholds, SL sl )ε->void{
		ul _{ _mutex };
		auto p = _nodes.find( index );
		THROW_IFSL( p==_nodes.end(), "node_index {} is not in group '{}'.", index, Name() );
		validate( p->second.Id, thresholds, sl );
		p->second.Config = move( thresholds );
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
		//A time no day holds would be filed at the last or first day, and a 9999 file would stay the newest for good.
		if( copy.hasSourceTimestamp && !Fileable(copy.sourceTimestamp) ){
			copy.hasSourceTimestamp = copy.hasSourcePicoseconds = false;
			copy.sourceTimestamp = copy.sourcePicoseconds = 0;
		}
		if( !copy.hasServerTimestamp || !Fileable(copy.serverTimestamp) ){
			copy.serverTimestamp = UADateTime{ _store->Time->Now() }.UA();
			copy.hasServerTimestamp = true;
		}
		let unsupported = copy.hasValue && !ProtoUtils::Supported( copy.value );
		let notUtf8 = copy.hasValue && !unsupported && !ProtoUtils::Utf8( copy.value );
		let size = bytes( copy );
		Pushing pushing;
		{
			ul _{ _mutex };
			auto p = _nodes.find( index );
			if( p==_nodes.end() || _stopped )
				return false;
			let first = ( unsupported && !std::exchange(p->second.Unsupported, true) ) || ( notUtf8 && !std::exchange(p->second.NotUtf8, true) );
			pushing = Push( DataValue{index, move(copy), std::exchange(p->second.Break, std::nullopt), first}, size );
		}
		Pushed( pushing );
		return true;
	}

	α Group::Disconnected( TimePoint at )ι->void{
		ul _{ _mutex };
		_connected = false;
		for( auto&& [_,node] : _nodes ){
			if( !node.Break )//no value since an earlier break, so that one still stands.
				node.Break = at;
		}
	}
	α Group::Connected()ι->void{
		ul _{ _mutex };
		_connected = true;
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
		{
			ul _{ _mutex };
			for( auto&& [index,node] : _nodes ){
				_gone.insert_or_assign( index, move(node.Id) );
				over = Hold( NodeRemoved{index, now, by}, size ) || over;
			}
			_nodes.clear();
			_indexes.clear();
			_closed = true;
			trim = over && _failing;//the flush asked for here takes the rest, if it can write.
		}
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
			//The break a dropped value carried goes to the one written back, for the flush to judge.
			auto& newest = get<DataValue>( lost.Newest->Item );
			let broke = newest.Break && value.Break ? std::min( *newest.Break, *value.Break ) : newest.Break ? newest.Break : value.Break;
			let replace = PrimaryTime( value.Data )>=PrimaryTime( newest.Data );
			Unflag( replace ? newest : value );//the one not written back.
			if( replace )
				lost.Newest = move( dropped );
			get<DataValue>( lost.Newest->Item ).Break = broke;
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
	α Group::Take()ι->vector<Buffered>{
		vector<Buffered> y;
		y.reserve( 2*_lost.size()+_changes.size()+_values.size() );
		MarkLost( y );
		_store->Subtract( std::exchange(_held, 0) );
		std::ranges::merge( _changes | std::views::as_rvalue, _values | std::views::as_rvalue, std::back_inserter(y), {}, &Buffered::Sequence, &Buffered::Sequence );
		_changes.clear();
		_values.clear();
		_fresh = 0;
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
		for( let id : {timers.Delay, timers.Midnight} ){
			if( id )
				_store->Time->Cancel( id );
		}
	}
}