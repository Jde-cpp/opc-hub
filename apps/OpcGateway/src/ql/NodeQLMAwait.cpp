#include "NodeQLMAwait.h"
#include <jde/fwk/co/AnyAwait.h>
#include "../async/ReadAwait.h"

#define let const auto
namespace Jde::Opc::Gateway{
	α NodeQLMAwait::AttributeType( UA_AttributeId attribute )ι->const UA_DataType*{
		switch( attribute ){
		case UA_ATTRIBUTEID_DISPLAYNAME: case UA_ATTRIBUTEID_DESCRIPTION: case UA_ATTRIBUTEID_INVERSENAME: return &UA_TYPES[UA_TYPES_LOCALIZEDTEXT];
		case UA_ATTRIBUTEID_WRITEMASK: case UA_ATTRIBUTEID_USERWRITEMASK: case UA_ATTRIBUTEID_ACCESSLEVELEX: return &UA_TYPES[UA_TYPES_UINT32];
		case UA_ATTRIBUTEID_MINIMUMSAMPLINGINTERVAL: return &UA_TYPES[UA_TYPES_DOUBLE];
		case UA_ATTRIBUTEID_ISABSTRACT: case UA_ATTRIBUTEID_SYMMETRIC: case UA_ATTRIBUTEID_CONTAINSNOLOOPS: case UA_ATTRIBUTEID_HISTORIZING: case UA_ATTRIBUTEID_EXECUTABLE: case UA_ATTRIBUTEID_USEREXECUTABLE: return &UA_TYPES[UA_TYPES_BOOLEAN];
		case UA_ATTRIBUTEID_EVENTNOTIFIER: case UA_ATTRIBUTEID_ACCESSLEVEL: case UA_ATTRIBUTEID_USERACCESSLEVEL: return &UA_TYPES[UA_TYPES_BYTE];
		case UA_ATTRIBUTEID_ACCESSRESTRICTIONS: return &UA_TYPES[UA_TYPES_UINT16];
		case UA_ATTRIBUTEID_VALUERANK: return &UA_TYPES[UA_TYPES_INT32];
		default: return nullptr;
		}
	}
	α NodeQLMAwait::Execute()ι->TAwait<WriteResponse>::Task{
		try{
			THROW_IF( _mutation.Type!=QL::EMutationQL::Update, "Only updateNode is supported." );
			NodeId nodeId{ _mutation.As<>("id") };
			let& jAttributes = _mutation.As<>( "attributes" );
			THROW_IF( !jAttributes.is_object() || jAttributes.get_object().empty(), "attributes is an object with one member per attribute - '{}'.", serialize(jAttributes) );
			struct Asked{ UA_AttributeId Attribute; const UA_DataType* Type; jvalue Json; };
			vector<Asked> asked; asked.reserve( jAttributes.get_object().size() );
			vector<UA_AttributeId> localized;//those given as a bare string:  their locale is the node's, read first.
			for( let& [name, json] : jAttributes.get_object() ){
				let attribute = ReadRequest::FindAttrib( name );
				THROW_IF( !attribute, "'{}' is no attribute.", name );
				let type = AttributeType( *attribute );
				THROW_IF( !type, "'{}' is not written through updateNode{}.", name, *attribute==UA_ATTRIBUTEID_VALUE ? " - updateVariable writes a value" : "" );
				if( type->typeKind==UA_DATATYPEKIND_LOCALIZEDTEXT && json.is_string() )
					localized.push_back( *attribute );
				asked.push_back( Asked{*attribute, type, json} );
			}
			if( localized.size() ){
				ReadAwait read{ ReadRequest{nodeId, localized}, _client };
				auto response = co_await Any( read );
				for( uint i=0; i<localized.size() && i<response.resultsSize; ++i ){
					let& result = response.results[i];
					if( result.status || !UA_Variant_hasScalarType(&result.value, &UA_TYPES[UA_TYPES_LOCALIZEDTEXT]) )
						continue;//no text to take a locale from:  written under none, and the server's answer is the answer.
					let locale = ToSV( ((const UA_LocalizedText*)result.value.data)->locale );
					for( auto& a : asked ){
						if( a.Attribute==localized[i] )
							a.Json = jobject{ {"locale", locale}, {"text", a.Json.get_string()} };
					}
				}
			}
			vector<WriteItem> items; items.reserve( asked.size() );
			for( let& a : asked )
				items.push_back( WriteItem{nodeId, a.Attribute, Value{a.Json, a.Type, _sl}} );
			WriteAwait write{ move(items), _client, _sl };
			co_await write;
			jvalue y;
			if( auto& ql = _mutation.ResultRequest; ql ){
				ReadAwait read{ {{nodeId}, *ql}, _client };
				auto response = co_await Any( read );
				y = response.ToJson( *ql );
			}
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}