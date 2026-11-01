#include "DataTypesAwait.h"
#include "../UAClient.h"
#include "../types/UAClientException.h"

#define let const auto
namespace Jde::Opc{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Browse };

	α Gateway::FreeDataTypeArray( UA_DataTypeArray* a )ι->void{
		while( a ){
			auto next = a->next;
			if( a->cleanup ){
				for( size_t i=0; i<a->typesSize; ++i )
					UA_DataType_clear( &a->types[i] );//the names and members, and the type and encoding ids - a string or guid id leaked when only the first two went.
				UA_free( a->types );
				UA_free( a );
			}
			a = next;
		}
	}
	namespace Gateway{
	DataTypesAwait::DataTypesAwait( vector<NodeId>&& nodes, sp<UAClient> client, SL sl )ι:
		UAServiceAwait{ move(client), "read DataTypeDefinition", sl },
		_nodes{ move(nodes) },
		_toRead( _nodes.size()*2 ){
		for( size_t i=0; i<_nodes.size(); ++i ){
			UA_ReadValueId_init( &_toRead[i] );
			_toRead[i].nodeId = _nodes[i];
			_toRead[i].attributeId = UA_ATTRIBUTEID_DATATYPEDEFINITION;
			UA_ReadValueId_init( &_toRead[i+_nodes.size()] );
			_toRead[i+_nodes.size()].nodeId = _nodes[i];
			_toRead[i+_nodes.size()].attributeId = UA_ATTRIBUTEID_BROWSENAME;
		}
	}
	α DataTypesAwait::Suspend()ι->void{
		if( _nodes.empty() )
			Resume( DataTypeArrayPtr{nullptr, FreeDataTypeArray} );
		else
			UAServiceAwait::Suspend();
	}
	α DataTypesAwait::Request()ι->void*{
		_request = {};
		_request.nodesToReadSize = _toRead.size();
		_request.nodesToRead = _toRead.data();
		return &_request;
	}
	//open62541 clears the response once the completion returns:  fromDescription copies what it keeps.
	α DataTypesAwait::Convert( UA_ReadResponse&& response )ε->DataTypeArrayPtr{
		THROW_IFX( response.responseHeader.serviceResult, UAClientException(response.responseHeader.serviceResult, _client->Handle(), "read DataTypeDefinition", _sl) );
		THROW_IFX( response.resultsSize!=_toRead.size(), UAClientException(UA_STATUSCODE_BADUNEXPECTEDERROR, _client->Handle(), Ƒ("read answered {} results for {} attributes", response.resultsSize, _toRead.size()), _sl) );
		let count = _nodes.size();
		DataTypeArrayPtr y{ (UA_DataTypeArray*)UA_calloc(1, sizeof(UA_DataTypeArray)), FreeDataTypeArray };
		THROW_IF( !y, "Could not allocate the DataTypeArray." );
		y->cleanup = true;
		y->types = (UA_DataType*)UA_calloc( count, sizeof(UA_DataType) );
		THROW_IF( !y->types, "Could not allocate {} DataTypes.", count );
		for( size_t i=0; i<count; ++i ){
			let& definition = response.results[i];
			let& name = response.results[i+count];
			if( (definition.hasStatus && definition.status) || !definition.hasValue || !UA_Variant_hasScalarType(&definition.value, &UA_TYPES[UA_TYPES_STRUCTUREDEFINITION]) )
				continue;
			if( (name.hasStatus && name.status) || !name.hasValue || !UA_Variant_hasScalarType(&name.value, &UA_TYPES[UA_TYPES_QUALIFIEDNAME]) )
				continue;
			UA_StructureDescription description;
			description.structureDefinition = *(UA_StructureDefinition*)definition.value.data;
			description.dataTypeId = _toRead[i].nodeId;
			description.name = *(UA_QualifiedName*)name.value.data;
			UA_ExtensionObject eo;
			UA_ExtensionObject_setValue( &eo, &description, &UA_TYPES[UA_TYPES_STRUCTUREDESCRIPTION] );
			UA_DataTypeArray lookup = *y;//the types built so far, then the client's own:  a member's type may be one of them.
			lookup.next = UA_Client_getConfig( *_client )->customDataTypes;
			if( let sc = UA_DataType_fromDescription(&y->types[y->typesSize], &eo, &lookup); sc ){
				WARN( "[{}]Could not build the DataType {} from {}: {}", hex(_client->Handle()), ToSV(description.name.name), _nodes[i].ToString(), UAException::Message(sc) );
				continue;
			}
			++y->typesSize;
		}
		if( !y->typesSize ){
			UA_free( y->types );
			y->types = nullptr;
		}
		return y;
	}
}}