#include "EnumTypeCache.h"
#include <jde/fwk/process/execution.h>
#include <jde/ql/types/TableQL.h>
#include "UAClient.h"
#include "async/ReadAwait.h"
#include "async/ReadValueAwait.h"
#include "uatypes/Browse.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };

	α EnumTypeCache::SourceName( ESource s )ι->sv{
		switch( s ){
			case ESource::Definition: return "DataTypeDefinition";
			case ESource::EnumValues: return "EnumValues";
			case ESource::EnumStrings: return "EnumStrings";
		}
		return "?";
	}

	α EnumTypeCache::EnumType::ToJson( const QL::TableQL& q )Ι->jobject{
		jobject y;
		y["name"] = Name;//unconditionally, as QL::Object::ToJson does - the SPA's Type.name is not optional.
		if( auto valuesQL = q.FindTable("enumValues"); valuesQL ){
			const bool id=valuesQL->FindColumn("id"), name=valuesQL->FindColumn("name"), description=valuesQL->FindColumn("description");
			jarray values;
			for( let& f : Fields ){
				jobject j;
				if( id )
					j["id"] = f.Value;//a number:  the SPA's <mat-option [value]> compares it against the numeric reading.
				if( name )
					j["name"] = f.Name;
				if( description )
					j["description"] = f.Description.size() ? jvalue{f.Description} : jvalue{};
				values.push_back( move(j) );
			}
			y["enumValues"] = move( values );
		}
		return y;
	}

	α EnumTypeCache::GetAwait::await_ready()ι->bool{
		if( auto p = _cache.Find(_id); p ){
			_result = move( p );
			return true;
		}
		return false;
	}
	α EnumTypeCache::GetAwait::Suspend()ι->void{
		_cache.StartLocked( move(_client), move(_id), this );
	}

	α EnumTypeCache::Find( const NodeId& id )Ι->Ptr{
		rl _{ _mutex };
		auto p = _slots.find( id );
		return p!=_slots.end() && p->second.State()==Slot::EState::Ready ? p->second.Value() : Ptr{};
	}

	α EnumTypeCache::Refused( const NodeId& id )Ι->bool{
		rl _{ _mutex };
		auto p = _slots.find( id );
		return p!=_slots.end() && p->second.State()==Slot::EState::Failed;
	}

	α EnumTypeCache::StartLocked( sp<UAClient>&& client, NodeId&& id, GetAwait* waiter )ι->void{
		bool launch{};
		{
			ul _{ _mutex };
			launch = _slots.try_emplace( NodeId{id} ).first->second.Start( waiter );//a new slot:  own the fetch.
		}
		if( launch )
			Fetch( move(client), move(id) );//fire & forget:  the task keeps the client (and so this cache) alive until Finish.
	}

	α EnumTypeCache::Finish( const NodeId& id, Ptr type, up<Exception> error, bool retryable )ι->void{
		Slot::Resumes resumes;
		{
			ul _{ _mutex };
			auto slot = _slots.find( id );
			ASSERT( slot!=_slots.end() );
			resumes = slot->second.Finish( move(type), move(error), retryable );
			if( slot->second.State()==Slot::EState::Empty )
				_slots.erase( slot );//a transport/session failure or an unknown id - the next request retries from scratch, and a mistyped id does not poison the connection.
		}
		resumes.Run();
	}

	Ω browseIs( const UA_ReferenceDescription& ref, sv name )ι->bool{ return ref.browseName.namespaceIndex==0 && ToSV(ref.browseName.name)==name; }

	//Source order:  the DataTypeDefinition attribute (normative since 1.04), else the DataType node's EnumValues property (has
	//descriptions), else EnumStrings (names only).  One coroutine with Any-wrapped awaits rather than a hand-off chain - three
	//awaitable families for a fallback most servers never take.  The awaitables are lvalues:  Any() stores an rvalue by value
	//and FoldersAwait is noncopyable.
	α EnumTypeCache::Fetch( sp<UAClient> client, NodeId id )ι->VoidTask{
		sp<EnumType> type; up<Exception> error; bool retryable{};
		try{
			ReadAwait read{ ReadRequest{id, {UA_ATTRIBUTEID_DATATYPEDEFINITION, UA_ATTRIBUTEID_DISPLAYNAME, UA_ATTRIBUTEID_NODECLASS}}, client };
			auto resp = co_await Any( read );
			THROW_IF( resp.resultsSize!=3, "Reading '{}' returned {} results for 3 attributes.", id.ToString(), resp.resultsSize );
			const UA_DataValue &def = resp.results[0], &displayName = resp.results[1], &nodeClass = resp.results[2];
			if( !nodeClass.hasValue )//BadNodeIdUnknown, a dead session:  the caller's problem, not the cache's.
				throw UAClientException{ (StatusCode)nodeClass.status, client->Handle(), Ƒ("read nodeClass of '{}'", id.ToString()) };
			THROW_IF( *(UA_NodeClass*)nodeClass.value.data!=UA_NODECLASS_DATATYPE, "'{}' is not a DataType node (class {}).", id.ToString(), (uint)*(UA_NodeClass*)nodeClass.value.data );
			type = ms<EnumType>();
			type->Id = id;
			if( displayName.hasValue && displayName.value.type==&UA_TYPES[UA_TYPES_LOCALIZEDTEXT] )
				type->Name = Opc::ToString( ((UA_LocalizedText*)displayName.value.data)->text );
			if( def.hasValue && def.value.type==&UA_TYPES[UA_TYPES_ENUMDEFINITION] ){
				type->Fields = FromDefinition( *(UA_EnumDefinition*)def.value.data );
				type->Source = ESource::Definition;
			}
			else if( def.hasValue && def.value.type==&UA_TYPES[UA_TYPES_STRUCTUREDEFINITION] )
				THROW( "'{}' ({}) is a structure, not an enumeration.", type->Name, id.ToString() );
			else{//BadAttributeIdInvalid (a pre-1.04 server) or an empty definition:  the spec's property fallback.
				DBG( "[{}]'{}' ({}) has no DataTypeDefinition ({}) - reading its EnumValues/EnumStrings property.", hex(client->Handle()), type->Name, id.ToString(), UAException::Message(def.status) );
				Browse::FoldersAwait browse{ Browse::Request::Properties(NodeId{id}), client };
				auto refs = co_await Any( browse );
				optional<NodeId> enumValues, enumStrings; bool optionSet{};
				if( refs.resultsSize )
					refs.VisitWhile( 0, [&]( const UA_ReferenceDescription& ref ){
						if( browseIs(ref, "EnumValues") )
							enumValues = NodeId{ ref.nodeId.nodeId };
						else if( browseIs(ref, "EnumStrings") )
							enumStrings = NodeId{ ref.nodeId.nodeId };
						else if( browseIs(ref, "OptionSetValues") )
							optionSet = true;
						return true;
					} );
				THROW_IF( !enumValues && !enumStrings, "'{}' ({}) has neither a DataTypeDefinition nor an EnumValues/EnumStrings property{}.", type->Name, id.ToString(), optionSet ? " - an OptionSet is not supported" : "" );
				let& propId = enumValues ? *enumValues : *enumStrings;
				let propName = enumValues ? "EnumValues" : "EnumStrings";
				ReadValueAwait readProp{ {propId}, client };
				auto values = co_await Any( readProp );
				auto v = values.find( propId ); THROW_IF( v==values.end(), "No value returned for the {} property of '{}'.", propName, id.ToString() );
				if( !v->second.hasValue )
					throw UAClientException{ (StatusCode)v->second.status, client->Handle(), Ƒ("read {} of '{}'", propName, id.ToString()) };
				type->Fields = enumValues ? FromEnumValues( v->second.value ) : FromEnumStrings( v->second.value );
				type->Source = enumValues ? ESource::EnumValues : ESource::EnumStrings;
			}
			INFO( "[{}]Enum '{}' ({}) - {} fields from {}.", hex(client->Handle()), type->Name, id.ToString(), type->Fields.size(), SourceName(type->Source) );
		}
		catch( UAClientException& e ){//session/transport:  do not pin the failure on the cache.
			retryable = true;
			error = e.Move();
		}
		catch( runtime_error& e ){
			error = Exception::From( move(e) );
		}
		Finish( id, move(type), move(error), retryable );
	}

	α EnumTypeCache::FromDefinition( const UA_EnumDefinition& def )ι->vector<Field>{
		vector<Field> y; y.reserve( def.fieldsSize );
		for( size_t i=0; i<def.fieldsSize; ++i ){
			let& f = def.fields[i];
			string name = Opc::ToString( f.name );
			if( name.empty() )
				name = Opc::ToString( f.displayName.text );
			y.push_back( Field{ f.value, move(name), Opc::ToString(f.description.text) } );
		}
		return y;
	}
	α EnumTypeCache::FromEnumValues( const UA_Variant& v )ε->vector<Field>{
		THROW_IF( v.type!=&UA_TYPES[UA_TYPES_ENUMVALUETYPE] || UA_Variant_isScalar(&v), "EnumValues is a '{}' {}, not an EnumValueType array.", v.type ? v.type->typeName : "null", UA_Variant_isScalar(&v) ? "scalar" : "array" );
		vector<Field> y; y.reserve( v.arrayLength );
		for( size_t i=0; i<v.arrayLength; ++i ){
			let& e = ((UA_EnumValueType*)v.data)[i];
			y.push_back( Field{ e.value, Opc::ToString(e.displayName.text), Opc::ToString(e.description.text) } );
		}
		return y;
	}
	α EnumTypeCache::FromEnumStrings( const UA_Variant& v )ε->vector<Field>{
		THROW_IF( v.type!=&UA_TYPES[UA_TYPES_LOCALIZEDTEXT] || UA_Variant_isScalar(&v), "EnumStrings is a '{}' {}, not a LocalizedText array.", v.type ? v.type->typeName : "null", UA_Variant_isScalar(&v) ? "scalar" : "array" );
		vector<Field> y; y.reserve( v.arrayLength );
		for( size_t i=0; i<v.arrayLength; ++i )
			y.push_back( Field{ (_int)i, Opc::ToString(((UA_LocalizedText*)v.data)[i].text), {} } );
		return y;
	}
}