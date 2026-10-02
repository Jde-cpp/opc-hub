#include "Records.h"
#include <jde/fwk/io/crc.h>
#include <jde/opc/UAException.h>
#include <jde/opc/proto/opc.Common.h>

#define let const auto

namespace Jde::Opc{
	constexpr ELogTags _tags{ ELogTags::IO };
	using google::protobuf::io::CodedOutputStream;
	using Hist::Proto::HistoryRecord;
}
namespace Jde::Opc::Hist{
		//Wrapping, so a garbled time read from disk can't overflow, and a delta always undoes exactly.
		Ξ add( Ticks a, Ticks b )ι->Ticks{ return (Ticks)( (uint64_t)a+(uint64_t)b ); }
		Ξ sub( Ticks a, Ticks b )ι->Ticks{ return (Ticks)( (uint64_t)a-(uint64_t)b ); }

		//One direction of the conversion.  After Primary, Last is the record's absolute primary time either way.
		struct Convert final{
			α Primary( Ticks stored )ι->Ticks{
				if( !ToDisk )
					return Last = add( Last, stored );
				let delta = sub( stored, Last );
				Last = stored;
				return delta;
			}
			α Other( Ticks stored, Ticks primary )Ι->Ticks{ return ToDisk ? sub( stored, primary ) : add( stored, primary ); }
			//Inside another record, every time is relative to that record's primary.
			α Nested( Proto::DataValue& v )Ι->void{
				if( v.has_source_ts() )
					v.set_source_ts( Other(v.source_ts(), Last) );
				if( v.has_server_ts() )
					v.set_server_ts( Other(v.server_ts(), Last) );
				if( v.has_heartbeat() )
					v.set_heartbeat( Other(v.heartbeat(), Last) );
			}
			//A DataValue that is its own record.
			α Top( Proto::DataValue& v )ι->void{
				if( v.has_source_ts() ){
					v.set_source_ts( Primary(v.source_ts()) );
					if( v.has_server_ts() )
						v.set_server_ts( Other(v.server_ts(), Last) );
				}
				else if( v.has_server_ts() )
					v.set_server_ts( Primary(v.server_ts()) );
				if( v.has_heartbeat() )
					v.set_heartbeat( Other(v.heartbeat(), Last) );
			}
			α operator()( HistoryRecord& r )ι->void{
				switch( r.record_case() ){
				case HistoryRecord::kFileStart:
					Last = r.file_start().ts();
					break;
				case HistoryRecord::kNodeAdded:{
					auto& added = *r.mutable_node_added();
					added.set_ts( Primary(added.ts()) );
					if( added.has_start() )
						Nested( *added.mutable_start() );
					break;}
				case HistoryRecord::kNodeRemoved:
					r.mutable_node_removed()->set_ts( Primary(r.node_removed().ts()) );
					break;
				case HistoryRecord::kValue:
					Top( *r.mutable_value() );
					break;
				case HistoryRecord::kModification:{
					auto& m = *r.mutable_modification();
					m.set_target_source_ts( Primary(m.target_source_ts()) );
					m.set_ts( Other(m.ts(), Last) );
					if( m.has_original() )
						Nested( *m.mutable_original() );
					if( m.has_new_value() )
						Nested( *m.mutable_new_value() );
					break;}
				case HistoryRecord::kCheckpoint:
				case HistoryRecord::RECORD_NOT_SET:
					break;
				}
			}
			const bool ToDisk;
			Ticks& Last;
		};

	Ω setWriter( auto& record, const optional<Writer>& by )ι->void{
		if( !by )
			return;
		record.set_identity_id( (uint32_t)by->IdentityId.Value );
		record.set_user_name( by->UserName );
	}

	α Appender::Add( HistoryRecord&& r )ε->void{
		ToDisk( r, _chain );
		Write( r );
	}
	α Appender::Write( const HistoryRecord& r )ε->void{
		let size = r.ByteSizeLong();
		THROW_IF( size>(uint)std::numeric_limits<int>::max(), "A {} byte record exceeds protobuf's limit.", size );
		let offset = _out.size();
		_out.resize( offset+CodedOutputStream::VarintSize32((uint32_t)size)+size );
		auto p = reinterpret_cast<uint8_t*>( _out.data()+offset );
		p = CodedOutputStream::WriteVarint32ToArray( (uint32_t)size, p );
		(void)r.SerializeWithCachedSizesToArray( p );
	}
	α Appender::Seal()ι->Ticks{
		HistoryRecord checkpoint;
		checkpoint.mutable_checkpoint()->set_crc( IO::Crc::Calc32c(sv{_out}.substr(_start)) );
		Write( checkpoint );
		_start = _out.size();
		return _chain;
	}
}
namespace Jde::Opc{
	α Hist::ToDisk( HistoryRecord& r, Ticks& last )ι->void{ Convert{ true, last }( r ); }
	α Hist::ToMemory( HistoryRecord& r, Ticks& last )ι->void{ Convert{ false, last }( r ); }

	α Hist::PrimaryTime( const HistoryRecord& r )ι->optional<Ticks>{
		switch( r.record_case() ){
		case HistoryRecord::kNodeAdded: return r.node_added().ts();
		case HistoryRecord::kNodeRemoved: return r.node_removed().ts();
		case HistoryRecord::kModification: return r.modification().target_source_ts();
		case HistoryRecord::kValue:{
			let& v = r.value();
			return v.has_source_ts() ? v.source_ts() : v.has_server_ts() ? optional<Ticks>{ v.server_ts() } : nullopt;}
		default: return nullopt;
		}
	}

	α Hist::ToProto( const UA_DataValue& v, NodeIndex index )ε->Proto::DataValue{
		Proto::DataValue y;
		y.set_node_index( index );
		if( v.hasSourceTimestamp )
			y.set_source_ts( v.sourceTimestamp );
		if( v.hasServerTimestamp )
			y.set_server_ts( v.serverTimestamp );
		if( v.hasStatus )
			y.set_status( v.status );
		if( v.hasValue && !UA_Variant_isEmpty(&v.value) ){
			try{
				*y.mutable_value() = ProtoUtils::ToValue( v.value );
			}
			catch( const UAException& e ){
				WARN( "node_index {}'s '{}' value is stored without it:  {}", index, v.value.type->typeName, e.what() );
				y.set_status( (StatusCode)e.Code() );
			}
		}
		return y;
	}

	α Hist::ToUA( const Proto::DataValue& v )ε->Value{
		UA_DataValue y{};
		if( v.has_value() ){
			y.value = ProtoUtils::ToVariant( v.value() ).Move();
			y.hasValue = true;
		}
		if( v.status() ){
			y.status = v.status();
			y.hasStatus = true;
		}
		if( v.has_source_ts() ){
			y.sourceTimestamp = v.source_ts();
			y.hasSourceTimestamp = true;
		}
		if( v.has_server_ts() ){
			y.serverTimestamp = v.server_ts();
			y.hasServerTimestamp = true;
		}
		return Value{ move(y) };
	}

	α Hist::ToProto( const Record& r )ε->HistoryRecord{
		HistoryRecord y;
		if( let added = get_if<NodeAdded>(&r) ){
			auto& record = *y.mutable_node_added();
			record.set_node_index( added->Index );
			*record.mutable_node() = ProtoUtils::ToExNodeId( added->Node );
			record.set_ts( UADateTime{added->Ts}.UA() );
			setWriter( record, added->By );
		}
		else if( let removed = get_if<NodeRemoved>(&r) ){
			auto& record = *y.mutable_node_removed();
			record.set_node_index( removed->Index );
			record.set_ts( UADateTime{removed->Ts}.UA() );
			setWriter( record, removed->By );
		}
		else{
			let& value = get<DataValue>( r );
			*y.mutable_value() = ToProto( value.Data, value.Index );
		}
		return y;
	}
}