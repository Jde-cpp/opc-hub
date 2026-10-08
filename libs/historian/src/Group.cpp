#include "Edits.h"
#include <absl/container/flat_hash_set.h>
#include <jde/opc/proto/opc.Common.h>
#include "Compress.h"
#include "Reads.h"
#include "Store.h"
#include "io/Applied.h"
#include "io/DayFiles.h"
#include "io/Records.h"

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
	//The Bad_DataLost that opens a gap:  at a dropped value's own times, or at `at` in the source's clock, with the server
	//time of the value that found the break.
	Ω marker( const UA_DataValue& like, optional<UA_DateTime> at={} )ι->Value{
		UA_DataValue y{};
		y.status = UA_STATUSCODE_BADDATALOST;
		y.hasStatus = true;
		y.sourceTimestamp = at.value_or( like.sourceTimestamp );
		y.hasSourceTimestamp = at || like.hasSourceTimestamp;
		y.serverTimestamp = like.serverTimestamp;
		y.hasServerTimestamp = like.hasServerTimestamp;
		return Value{ move(y) };
	}
	//Whether a value can be copied over another of its type with nothing to allocate:  a scalar that holds no pointer.
	Ω flat( const UA_DataValue& v )ι->bool{
		return v.hasValue && v.value.type && v.value.type->pointerFree && UA_Variant_isScalar( &v.value ) && v.value.storageType==UA_VARIANT_DATA;
	}
	//A node's last stored value, set under the group's lock:  a flat value goes over a held one of its type in place.
	Ω remember( optional<Value>& held, const UA_DataValue& value )ι->void{
		if( !held || !flat(*held) || !flat(value) || held->value.type!=value.value.type ){
			held.emplace( value );
			return;
		}
		let variant = held->value;
		memcpy( variant.data, value.value.data, value.value.type->memSize );
		static_cast<UA_DataValue&>( *held ) = value;
		held->value = variant;
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
		if( let& edited = _files->Edited(); !edited.empty() ){//the newest of a node an edit names may have moved:  read through the edits.
			absl::flat_hash_set<NodeIndex> named;
			for( let day : edited ){
				try{
					if( let served = _files->ServeMods(day, sl) ){
						for( let& [_,mods] : DayMods::Read(*served, sl).ByTime ){
							for( let& m : mods )
								named.insert( m.node_index() );
						}
					}
				}
				catch( Exception& e ){//the newest the files folded stands for the nodes it names, as for one whose walk fails.
					e.SetLevel( ELogLevel::Error );
				}
			}
			for( let& [_,index] : restored.Members ){
				if( !named.contains(index) )
					continue;
				try{
					_files->Relast( index, Last(*_files, tz, _store->Config.ReadLimit, index, std::numeric_limits<Ticks>::max(), sl) );
				}
				catch( Exception& e ){//the newest the files folded stands, as Newest leaves it.
					e.SetLevel( ELogLevel::Error );
				}
			}
		}
		absl::flat_hash_set<NodeIndex> restoredIndexes, kept;
		restoredIndexes.reserve( restored.Members.size() );
		for( let& [_,index] : restored.Members )
			restoredIndexes.emplace( index );
		//A member's newest record in the files stands in for what the process no longer remembers:  its last stored value,
		//and the last delivered one its first value is compared with - a value by its own SourceTimestamp, a heartbeat by
		//that of the value it repeats, and a marker by none.  Nor is there one when the value came with none, which a
		//heartbeat's record says of the value it repeats.
		let resume = [now]( Node& node, const Proto::DataValue& newest ){
			let primary = *PrimaryTime( newest );
			node.RecordTs = node.KeptTs = primary;
			node.StoredAt = std::min( UADateTime{primary}.Time(), now );//the source's clock, which may be ahead:  the wait is never longer.
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
			if( newest.has_heartbeat() )//as the value it repeats came:  the record's own times are the timer's.
				node.Stored->hasSourceTimestamp = !newest.heartbeat_unsourced();
			if( node.Stored->hasSourceTimestamp )
				node.Delivered = *node.ValueTs;
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
			auto& node = p->second;
			validate( node.Id, thresholds, sl );
			let held = std::exchange( node.Config, move(thresholds) ).MinTimeInterval;
			if( node.Pending && node.Config.MinTimeInterval!=held ){//its interval's end moves with the interval.
				let due = node.Due+( node.Config.MinTimeInterval-held );
				if( due<=now )
					Settle( index, node, now, effects );
				else
					ExpireAt( index, node, due, now );
			}
			Arm( index, node, now, effects );//a MaxTimeInterval it gained, or a shorter one.
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
		//What its node keeps of a value it stores is made here too, when the lock would have to allocate it.  It outlives
		//the lock, so what it replaces is cleared after it.
		Arrival arrival{ move(copy), size, unsupported, notUtf8, {} };
		if( !flat(arrival.Data) )
			arrival.Kept.emplace( arrival.Data );
		Effects effects;
		{
			ul _{ _mutex };
			auto p = _nodes.find( index );
			if( p==_nodes.end() || _stopped )
				return false;
			Collect( index, p->second, move(arrival), now, effects );
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
		//A value that arrives while the connection is down was in flight when it broke, so it was sent before the break:
		//it takes the ordinary test, and the break stands for the first value the connection brings back.  Not so for an
		//earlier break still standing:  this is the node's first value since, and the connection's break follows it.
		if( let broke = std::exchange(node.Break, _down); broke && broke!=_down ){
			if( node.Pending )//in flight and held since:  delivered before the break, as those Disconnected settles.
				Settle( index, node, now, effects );
			//An equal SourceTimestamp says nothing was lost, when the value agrees:  a source whose timestamps are coarse can
			//change it inside one.  It agrees when the node's test would drop it, or it repeats the last stored value.  A
			//marker holds no value to ask, so there the timestamp decides.
			let agrees = [&]{ return node.Marked || !node.Stored || !Passes( node.Config, *node.Stored, value ) || Repeats( *node.Stored, value ); };
			if( source && last && *source==*last && agrees() ){//nothing was lost.
				Arm( index, node, now, effects );
				return;
			}
			//Earlier, the source's clock or state went backwards:  at its own time the value would land before a record
			//reads have already returned.  A comparison with no SourceTimestamp on either side means nothing, so it is later.
			let earlier = source && last && *source<*last;
			if( !earlier )//before the marker, whose floor a heartbeat at or after the value would raise.
				DropBeats( index, node, time );
			//The break is in the historian's clock and the node's records are in the source's:  a marker before its newest
			//record would leave that one's value reading Good through the gap.
			let at = std::max( ticks(*broke), node.RecordTs );
			Mark( index, node, earlier ? at : std::min(at, time), value, now, effects );
			if( earlier ){
				effects.Warning = Ƒ( "'{}' in group '{}' came back from the break at {} with a value sourced at {}, earlier than the {} it last delivered:  the value is not stored, and the node reads Bad_DataLost until its next change.",
					node.Id.to_string(), Name(), ToIsoString(*broke), ToIsoString(UADateTime{*source}.Time()), ToIsoString(UADateTime{*last}.Time()) );
				return;
			}
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
		let left = UATick{ min-std::clamp<UA_DateTime>(time-*node.ValueTs, 0, min) };
		ExpireAt( index, node, now+std::chrono::duration_cast<Duration>(left), now );
	}
	α Group::ExpireAt( NodeIndex index, Node& node, TimePoint due, TimePoint now )ι->void{
		node.Due = due;
		_store->Time->Schedule( due-now, [weak=weak_from_this(), index, serial=++node.Serial]{
			if( auto group = weak.lock() )
				group->Expire( index, serial );
		});
	}
	α Group::StoreValue( NodeIndex index, Node& node, Arrival&& arrival, TimePoint now, Effects& effects )ι->void{
		let first = ( arrival.Unsupported && !std::exchange(node.Unsupported, true) ) || ( arrival.NotUtf8 && !std::exchange(node.NotUtf8, true) );
		let time = PrimaryTime( arrival.Data );
		if( arrival.Kept )
			std::swap( node.Stored, arrival.Kept );
		else
			remember( node.Stored, arrival.Data );
		node.Marked = false;
		node.ValueTs = time;
		node.Keep( time );
		node.StoredAt = now;
		effects += Push( DataValue{index, move(arrival.Data), {}, first}, arrival.Bytes );
		Arm( index, node, now, effects );
	}
	α Group::Mark( NodeIndex index, Node& node, UA_DateTime at, const UA_DataValue& ended, TimePoint now, Effects& effects )ι->void{
		auto lost = marker( ended, at );
		let size = bytes( lost );
		node.Stored.emplace( (StatusCode)UA_STATUSCODE_BADDATALOST );
		node.Marked = true;
		node.Keep( at );
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
			Arm( index, node, now, effects );//the heartbeat left it alone while it was pending.
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
	α Group::DropBeats( NodeIndex index, Node& node, UA_DateTime from, bool made )ι->void{
		if( !node.Beat || (!made && from>*node.Beat) )
			return;
		node.Beat.reset();
		uint freed{}, fresh{};
		std::erase_if( _values, [&]( const Buffered& b ){
			let& value = get<DataValue>( b.Item );
			if( value.Index!=index || !value.Heartbeat )
				return false;
			let time = PrimaryTime( value.Data );
			if( (made ? value.Data.serverTimestamp : time)<from ){
				node.Beat = std::max( node.Beat.value_or(time), time );
				return false;
			}
			freed += Cost( b );
			fresh += b.Bytes;
			return true;
		});
		_held -= freed;
		_fresh -= std::min( _fresh, fresh );
		_store->Subtract( freed );
		//The trim's too, which the next flush would write back as the end of the node's gap.
		if( auto p = _lost.find(index); p!=_lost.end() && p->second.Newest && get<DataValue>(p->second.Newest->Item).Heartbeat ){
			auto& lost = p->second;
			let& value = get<DataValue>( lost.Newest->Item );
			let time = PrimaryTime( value.Data );
			if( (made ? value.Data.serverTimestamp : time)<from )
				node.Beat = std::max( node.Beat.value_or(time), time );
			else{
				--lost.Count;
				lost.Sequence = lost.Newest->Sequence;
				lost.Newest.reset();
			}
		}
		node.RecordTs = std::max( node.KeptTs, node.Beat.value_or(node.KeptTs) );
	}

	α Group::Arm( NodeIndex index, Node& node, TimePoint now, Effects& effects )ι->void{
		if( !Beats(node) || _down || _stopped || _closed )
			return;
		//One it already waits on that comes sooner stays:  the timer finds then that the node stored since, and waits again.
		if( let due = std::max(node.StoredAt+node.Config.MaxTimeInterval, now); !node.BeatAt || *node.BeatAt>due )
			Watch( index, node, due );
		Arm( std::max(*node.BeatAt, now), now, effects );
	}
	α Group::Watch( NodeIndex index, Node& node, TimePoint due )ι->void{
		node.BeatAt = due;
		_beats.emplace( due, index );
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
			if( _down )//a dead feed stays apart from a flat line:  each node's first value after the break arms it again.
				return;
			//The wall clock was set back, and every deadline is from before it:  each node waits from now, so none for longer
			//than its own interval.  A timer never runs early otherwise, but for what the two clocks drift apart.
			if( now+1s<_beatDue ){
				_beats = {};
				for( auto&& [index,node] : _nodes ){
					node.BeatAt.reset();
					if( !Beats(node) )
						continue;
					node.StoredAt = std::min( node.StoredAt, now );
					Watch( index, node, node.StoredAt+node.Config.MaxTimeInterval );
				}
			}
			//Only the nodes whose deadline has come, each of which waits again from what it stored last.
			while( !_beats.empty() && _beats.top().first<=now ){
				let [at, index] = _beats.top();
				_beats.pop();
				auto p = _nodes.find( index );
				if( p==_nodes.end() || p->second.BeatAt!=at )//gone, or waiting on another since.
					continue;
				auto& node = p->second;
				node.BeatAt.reset();
				if( !Beats(node) )//what makes it one again arms it.
					continue;
				let max = node.Config.MaxTimeInterval;
				node.StoredAt = std::min( node.StoredAt, now );
				if( node.StoredAt+max<=now ){
					//In the source's clock, so it never sorts after a change sampled before it, and never earlier than
					//MaxTimeInterval after the node's last record.
					Value beat{ *node.Stored };
					let unsourced = !beat.hasSourceTimestamp;
					beat.sourceTimestamp = std::max( ticks(now-node.Offset), node.RecordTs+ticks(max) );
					beat.serverTimestamp = ticks( now );
					beat.hasSourceTimestamp = beat.hasServerTimestamp = true;
					beat.sourcePicoseconds = beat.serverPicoseconds = 0;
					beat.hasSourcePicoseconds = beat.hasServerPicoseconds = false;
					node.RecordTs = beat.sourceTimestamp;
					node.StoredAt = now;
					node.Beat = beat.sourceTimestamp;
					let size = bytes( beat );
					effects += Push( DataValue{index, move(beat), node.ValueTs, false, unsourced}, size );
				}
				Watch( index, node, node.StoredAt+max );
			}
			if( !_beats.empty() )
				ScheduleBeat( _beats.top().first, now );
		}
		Finish( move(effects) );
	}
	α Group::Finish( Effects&& effects )ι->void{
		Pushed( effects.Pushed );
		for( let id : effects.Stale )
			_store->Time->Cancel( id );
		if( !effects.Warning.empty() )
			_store->Time->Schedule( Duration::zero(), [warning=move(effects.Warning)]{ WARNT( ELogTags::IO, "{}", warning ); } );//IO, with the historian's other warnings about its records.
	}

	α Group::Disconnected( TimePoint at )ι->void{
		let now = _store->Time->Now();
		Effects effects;
		{
			ul _{ _mutex };
			if( !_down )
				_down = at;
			for( auto&& [index,node] : _nodes ){
				//Delivered before the break, and the first value after it goes to the comparison instead of replacing it.
				if( node.Pending )
					Settle( index, node, now, effects );
				if( node.Break )//no value since an earlier break, so that one still stands.
					continue;
				node.Break = at;
				if( node.StoredAt>=at ){//reported late:  a heartbeat made since repeats the value over a dead feed.
					DropBeats( index, node, ticks(at), true );
					node.StoredAt = at;
				}
			}
		}
		Finish( move(effects) );
	}
	α Group::Connected()ι->void{
		ul _{ _mutex };
		_down.reset();//each node's first value from here is judged against its break, and arms its heartbeat.
	}
	α Group::IsConnected()Ι->bool{
		ul _{ _mutex };
		return !_down;
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
		bool over{};
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
			effects.Pushed = { .Flush=true, .Trim=(over || effects.Pushed.Trim) && _failing };//the flush asked for here takes the rest, if it can write.
		}
		Finish( move(effects) );
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
			if( auto p = value.Heartbeat ? _nodes.find(value.Index) : _nodes.end(); p!=_nodes.end() )
				p->second.Keep( PrimaryTime(value.Data) );//out of a drop's reach.
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
	α Group::LostRecords( vector<Buffered>& y )Ι->void{
		for( let& [index,lost] : _lost ){
			if( lost.Count>(lost.Newest ? 1u : 0u) )//a node that lost one simply gets it back.
				y.push_back( {DataValue{index, lost.Marker}, lost.Newest ? lost.Newest->Sequence : lost.Sequence, bytes(lost.Marker)} );
			if( lost.Newest )
				y.push_back( *lost.Newest );
		}
	}
	α Group::MarkLost( vector<Buffered>& y )ι->void{
		LostRecords( y );
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
		for( auto p = _values.begin(); p!=kept; ++p ){//a heartbeat the flush takes is out of a drop's reach.
			let& value = get<DataValue>( p->Item );
			if( auto node = value.Heartbeat ? _nodes.find(value.Index) : _nodes.end(); node!=_nodes.end() )
				node->second.Keep( PrimaryTime(value.Data) );
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
	α Group::Snapshot( absl::FunctionRef<bool( NodeIndex )> wanted )Ι->vector<Buffered>{
		let keep = [wanted]( const Buffered& b ){ return wanted( std::visit([]( let& r ){ return r.Index; }, b.Item) ); };
		vector<Buffered> y;
		for( uint i=0; _taken && i<_taken->size(); ++i ){
			if( !_written[i] && keep((*_taken)[i]) )
				y.push_back( (*_taken)[i] );
		}
		let lost = y.size();
		LostRecords( y );
		y.erase( std::remove_if(y.begin()+lost, y.end(), std::not_fn(keep)), y.end() );
		std::ranges::merge( _changes | std::views::filter(keep), _values | std::views::filter(keep), std::back_inserter(y), {}, &Buffered::Sequence, &Buffered::Sequence );
		return y;
	}
	α Group::Taking( sp<vector<Buffered>> batch )ι->void{
		_written.assign( batch->size(), false );
		_taken = move( batch );
	}
	α Group::Wrote( uint from, uint to )ι->void{
		std::fill( _written.begin()+from, _written.begin()+to, true );
	}
	α Group::Return( vector<Buffered>& batch, vector<uint>&& held )ι->bool{
		std::ranges::stable_sort( held, {}, [&batch]( uint i ){ return batch[i].Sequence; } );
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
		for( let i : held | std::views::reverse ){//out of the batch in the same hold as it goes, so a read finds each in one place.
			auto& record = batch[i];
			cost += Cost( record );
			if( std::holds_alternative<DataValue>(record.Item) )
				_values.push_front( move(record) );
			else
				changes.push_back( move(record) );
		}
		_changes.insert( _changes.begin(), std::make_move_iterator(changes.rbegin()), std::make_move_iterator(changes.rend()) );
		_taken.reset();//those that stay out are in their files.
		_written.clear();
		_held += cost;
		return _store->Add( cost );
	}
	α Group::Written()Ι->bool{
		return _closed && _archived && _changes.empty() && _values.empty() && _lost.empty() && _edits.empty();
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