#include <jde/flows/Trace.h>

#define let const auto

namespace Jde::Opc::Flows{
	constexpr std::array<sv,4> _levels{ "off", "counts", "errors", "hops" };

	struct HopTrace::Impl final{
		//A message at the `Errors` level:  its finished records, held while it is queued or a node has it, Refs counting both,
		//and through Parent for as long as a message made from it is.
		struct Flight final{ vector<TraceRecord> Records; sp<Flight> Parent; uint Refs{}; };
		//A hop or a birth between its start and its Complete.  Before:  a hop's message as received, to tell a change by.
		struct Open final{ TraceRecord Record; optional<jobject> Before; };
		using Key = std::pair<const NodeDef*,MessageId>;

		α Writes()Ι->bool{ return Level>=ETraceLevel::Errors; }
		α Tracks()Ι->bool{ return Level==ETraceLevel::Errors; }
		α Record( const NodeDef& node, MessageId id, MessageId parent, ERecordKind kind )Ι->TraceRecord{
			return { Clock->Now(), id, parent, node.Id, kind, {}, {}, {}, {} };
		}
		α FlightOf( const Message& m )ι->Flight&{
			auto& flight = Flights[m.Id()];
			if( !flight ){
				flight = ms<Flight>();
				if( let p = Flights.find(m.Parent()); p!=Flights.end() )
					flight->Parent = p->second;
			}
			return *flight;
		}
		α Release( MessageId id )ι->void{
			if( let p = Flights.find(id); p!=Flights.end() && p->second->Refs && !--p->second->Refs )
				Flights.erase( p );
		}
		//A finished hop, birth or clone:  written, or at `Errors` kept with its message.
		α Keep( HopTrace& trace, TraceRecord&& record )ι->void{
			if( !Tracks() )
				trace.Write( move(record) );
			else if( let p = Flights.find(record.Id); p!=Flights.end() )
				p->second->Records.push_back( move(record) );
		}
		//The node's open record of the message is finished.  Its message's parent, 0 without one.
		α Close( HopTrace& trace, const NodeDef& node, MessageId id )ι->MessageId{
			let p = Opened.find( Key{&node, id} );
			if( p==Opened.end() )
				return 0;
			let parent = p->second.Record.Parent;
			Keep( trace, move(p->second.Record) );
			Opened.erase( p );
			return parent;
		}

		const ETraceLevel Level;
		const sp<Hist::IClock> Clock;
		std::map<Key,Open> Opened;
		std::unordered_map<MessageId,sp<Flight>> Flights;//at `Errors` alone.
	};

	HopTrace::HopTrace( ETraceLevel level, sp<Hist::IClock> clock )ι:
		_impl{ mu<Impl>(level, move(clock)) }
	{}
	HopTrace::~HopTrace()
	{}
	α HopTrace::Level()Ι->ETraceLevel{ return _impl->Level; }

	α HopTrace::Birth( const NodeDef& node, const Message& m )ι->void{
		if( !_impl->Writes() )
			return;
		if( _impl->Tracks() )
			++_impl->FlightOf( m ).Refs;
		auto record = _impl->Record( node, m.Id(), m.Parent(), ERecordKind::Birth );
		record.Json = serialize( m.Json );
		_impl->Opened.insert_or_assign( Impl::Key{&node, m.Id()}, Impl::Open{move(record), nullopt} );
	}
	α HopTrace::Clone( const NodeDef& node, const Message& clone, uint port )ι->void{
		if( !_impl->Writes() )
			return;
		if( _impl->Tracks() )
			++_impl->FlightOf( clone ).Refs;
		auto record = _impl->Record( node, clone.Id(), clone.Parent(), ERecordKind::Clone );
		record.Ports.push_back( port );
		_impl->Keep( *this, move(record) );
	}
	α HopTrace::Receive( const NodeDef& node, const Message& m )ι->void{
		if( !_impl->Writes() )
			return;
		if( auto flight = _impl->Tracks() ? &_impl->FlightOf( m ) : nullptr; flight && !flight->Refs )
			flight->Refs = 1;
		_impl->Opened.insert_or_assign( Impl::Key{&node, m.Id()}, Impl::Open{_impl->Record(node, m.Id(), m.Parent(), ERecordKind::Hop), m.Json} );
	}
	α HopTrace::Send( const NodeDef& node, uint port, const Message& m, uint wires )ι->void{
		let p = _impl->Opened.find( Impl::Key{&node, m.Id()} );
		if( p==_impl->Opened.end() )
			return;
		auto& open = p->second;
		if( find(open.Record.Ports, port)==open.Record.Ports.end() )
			open.Record.Ports.push_back( port );
		if( open.Before && !open.Record.Json && *open.Before!=m.Json )
			open.Record.Json = serialize( m.Json );
		if( wires && _impl->Tracks() )
			++_impl->FlightOf( m ).Refs;
	}
	α HopTrace::Complete( const NodeDef& node, MessageId id )ι->void{
		if( !_impl->Writes() )
			return;
		_impl->Close( *this, node, id );
		_impl->Release( id );
	}
	α HopTrace::Error( const NodeDef& node, MessageId id, sv error )ι->void{
		if( !_impl->Writes() )
			return;
		let parent = _impl->Close( *this, node, id );
		if( let p = _impl->Flights.find(id); p!=_impl->Flights.end() ){
			vector<Impl::Flight*> lineage;//the message's path:  what it was made from first.
			for( auto flight = p->second.get(); flight; flight = flight->Parent.get() )
				lineage.push_back( flight );
			for( auto flight : lineage | std::views::reverse ){
				for( auto& record : std::exchange(flight->Records, {}) )
					Write( move(record) );
			}
			_impl->Release( id );
		}
		auto record = _impl->Record( node, id, parent, ERecordKind::Error );
		record.Error = error;
		Write( move(record) );
	}
	α HopTrace::Status( const NodeDef& node, const NodeStatus& status )ι->void{
		if( !_impl->Writes() )
			return;
		auto record = _impl->Record( node, 0, 0, ERecordKind::Status );
		record.Status = status;
		Write( move(record) );
	}
	α HopTrace::Drop( const NodeDef& node, MessageId id )ι->void{
		if( !_impl->Writes() )
			return;
		let parent = _impl->Close( *this, node, id );
		_impl->Release( id );
		Write( _impl->Record(node, id, parent, ERecordKind::Drop) );
	}
}
namespace Jde::Opc{
	α Flows::ToString( ETraceLevel level )ι->sv{ return (uint8)level<_levels.size() ? _levels[(uint8)level] : sv{ "?" }; }
	α Flows::ToTraceLevel( sv name )ι->optional<ETraceLevel>{
		let p = find( _levels, name );
		return p==_levels.end() ? nullopt : optional{ (ETraceLevel)(p-_levels.begin()) };
	}
}