#include "Records.h"
#include <jde/opc/UAException.h>
#include <jde/opc/proto/opc.Common.h>
#include <google/protobuf/util/field_comparator.h>
#include <google/protobuf/util/message_differencer.h>

#define let const auto

namespace Jde::Opc{
	constexpr ELogTags _tags{ ELogTags::IO };
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
				case HistoryRecord::kIssued:
				case HistoryRecord::RECORD_NOT_SET:
					break;
				}
			}
			const bool ToDisk;
			Ticks& Last;
		};

	Ω toProto( const DataValue& value, bool warn )ε->Proto::DataValue{
		auto y = ToProto( value.Data, value.Index, warn );
		if( value.Heartbeat )
			y.set_heartbeat( *value.Heartbeat );
		if( value.Unsourced )
			y.set_heartbeat_unsourced( true );
		return y;
	}
	Ω setWriter( auto& record, const optional<Writer>& by )ι->void{
		if( !by )
			return;
		let id = by->IdentityId.Value;
		record.set_identity_id( id==UserPK::System ? std::numeric_limits<uint32_t>::max() : (uint32_t)id );//Writer's constructor checked it fits.
		record.set_user_name( by->UserName );
	}
}
namespace Jde::Opc{
	α Hist::SetWriter( Proto::Modification& m, const Writer& by )ι->void{ Hist::setWriter( m, optional<Writer>{by} ); }
	α Hist::Same( const Proto::DataValue& a, const Proto::DataValue& b )ι->bool{
		let sameValue = [&]{
			google::protobuf::util::DefaultFieldComparator nanEqual;//MessageDifferencer's default takes NaN as unequal to itself.
			nanEqual.set_treat_nan_as_equal( true );
			google::protobuf::util::MessageDifferencer differencer;
			differencer.set_field_comparator( &nanEqual );
			return differencer.Compare( a.value(), b.value() );
		};
		return a.has_source_ts()==b.has_source_ts() && a.source_ts()==b.source_ts() && a.source_picoseconds()==b.source_picoseconds()
			&& a.has_server_ts()==b.has_server_ts() && a.server_ts()==b.server_ts() && a.server_picoseconds()==b.server_picoseconds()
			&& a.status()==b.status() && a.has_heartbeat()==b.has_heartbeat() && a.heartbeat()==b.heartbeat() && a.heartbeat_unsourced()==b.heartbeat_unsourced()
			&& a.has_value()==b.has_value() && ( !a.has_value() || sameValue() );
	}
	α Hist::ToDisk( HistoryRecord& r, Ticks& last )ι->void{ Convert{ true, last }( r ); }
	α Hist::ToMemory( HistoryRecord& r, Ticks& last )ι->void{ Convert{ false, last }( r ); }

	α Hist::PrimaryTime( const HistoryRecord& r )ι->optional<Ticks>{
		switch( r.record_case() ){
		case HistoryRecord::kNodeAdded: return r.node_added().ts();
		case HistoryRecord::kNodeRemoved: return r.node_removed().ts();
		case HistoryRecord::kModification: return r.modification().target_source_ts();
		case HistoryRecord::kValue: return PrimaryTime( r.value() );
		default: return nullopt;
		}
	}
	α Hist::PrimaryTime( const Proto::DataValue& v )ι->optional<Ticks>{
		return v.has_source_ts() ? v.source_ts() : v.has_server_ts() ? optional<Ticks>{ v.server_ts() } : nullopt;
	}
	α Hist::PrimaryTime( const UA_DataValue& v )ι->Ticks{
		return v.hasSourceTimestamp ? v.sourceTimestamp : v.serverTimestamp;
	}
	α Hist::PrimaryTime( const Record& r )ι->Ticks{
		if( let value = get_if<DataValue>(&r) )
			return PrimaryTime( value->Data );
		return UADateTime{ std::holds_alternative<NodeAdded>(r) ? get<NodeAdded>(r).Ts : get<NodeRemoved>(r).Ts }.UA();
	}

	α Hist::ToProto( const UA_DataValue& v, NodeIndex index, bool warn )ε->Proto::DataValue{
		Proto::DataValue y;
		y.set_node_index( index );
		if( v.hasSourceTimestamp )
			y.set_source_ts( v.sourceTimestamp );
		if( v.hasSourcePicoseconds )
			y.set_source_picoseconds( v.sourcePicoseconds );
		if( v.hasServerTimestamp )
			y.set_server_ts( v.serverTimestamp );
		if( v.hasServerPicoseconds )
			y.set_server_picoseconds( v.serverPicoseconds );
		if( v.hasStatus )
			y.set_status( v.status );
		if( v.hasValue && !UA_Variant_isEmpty(&v.value) ){
			if( !ProtoUtils::Supported(v.value) )
				y.set_status( UA_STATUSCODE_BADNOTSUPPORTED );
			else if( !ProtoUtils::Utf8(v.value) )
				y.set_status( UA_STATUSCODE_BADENCODINGERROR );
			else{
				try{
					*y.mutable_value() = ProtoUtils::ToValue( v.value );
				}
				catch( const UAException& e ){
					if( warn )
						WARN( "node_index {}'s '{}' value is stored without it:  {}", index, v.value.type->typeName, e.what() );
					y.set_status( (StatusCode)e.Code() );
				}
				if( let size = y.ByteSizeLong(); y.has_value() && size>MaxMemberBytes ){//no frame holds its record.
					if( warn )
						WARN( "node_index {}'s '{}' value is stored without it:  its {} bytes are past the {} a record holds.", index, v.value.type->typeName, size, MaxMemberBytes );
					y.clear_value();
					y.set_status( UA_STATUSCODE_BADENCODINGLIMITSEXCEEDED );
				}
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
		if( v.source_picoseconds() ){
			y.sourcePicoseconds = (UA_UInt16)v.source_picoseconds();
			y.hasSourcePicoseconds = true;
		}
		if( v.has_server_ts() ){
			y.serverTimestamp = v.server_ts();
			y.hasServerTimestamp = true;
		}
		if( v.server_picoseconds() ){
			y.serverPicoseconds = (UA_UInt16)v.server_picoseconds();
			y.hasServerPicoseconds = true;
		}
		return Value{ move(y) };
	}

	α Hist::BuiltIn( const UA_DataType* type )ι->uint32_t{
		if( !type )
			return 0;
		let kind = type->typeKind;
		if( kind<=UA_DATATYPEKIND_EXTENSIONOBJECT )
			return kind+1;
		if( kind==UA_DATATYPEKIND_ENUM )
			return UA_DATATYPEKIND_INT32+1;
		return kind==UA_DATATYPEKIND_STRUCTURE || kind==UA_DATATYPEKIND_OPTSTRUCT || kind==UA_DATATYPEKIND_UNION ? UA_DATATYPEKIND_EXTENSIONOBJECT+1 : 0;
	}
	α Hist::BuiltInType( uint32_t id )ι->const UA_DataType*{
		static_assert( UA_TYPES_BOOLEAN==UA_DATATYPEKIND_BOOLEAN && UA_TYPES_DOUBLE==UA_DATATYPEKIND_DOUBLE && UA_TYPES_EXTENSIONOBJECT==UA_DATATYPEKIND_EXTENSIONOBJECT, "UA_TYPES opens with the built-in types, in id order." );
		return id && id<=UA_DATATYPEKIND_EXTENSIONOBJECT+1 ? &UA_TYPES[id-1] : nullptr;
	}

	α Hist::ToProto( const Record& r )ε->HistoryRecord{
		HistoryRecord y;
		if( let added = get_if<NodeAdded>(&r) ){
			auto& record = *y.mutable_node_added();
			record.set_node_index( added->Index );
			*record.mutable_node() = ProtoUtils::ToExNodeId( added->Node );
			record.set_ts( UADateTime{added->Ts}.UA() );
			setWriter( record, added->By );
			record.set_data_type( BuiltIn(added->DataType) );
		}
		else if( let removed = get_if<NodeRemoved>(&r) ){
			auto& record = *y.mutable_node_removed();
			record.set_node_index( removed->Index );
			record.set_ts( UADateTime{removed->Ts}.UA() );
			setWriter( record, removed->By );
		}
		else{
			let& value = get<DataValue>( r );
			auto& stored = *y.mutable_value() = toProto( value, true );
			if( value.Unsupported ){
				let notUtf8 = stored.status()==UA_STATUSCODE_BADENCODINGERROR;
				WARN( "node_index {}'s '{}' value {}, so it and later ones like it are stored without it, as {}.", value.Index, value.Data.value.type->typeName,
					notUtf8 ? "has text that isn't UTF-8" : "has no file form", notUtf8 ? "BadEncodingError" : "BadNotSupported" );
			}
		}
		return y;
	}
	α Hist::ToProto( const DataValue& value )ε->Proto::DataValue{ return toProto( value, false ); }
}