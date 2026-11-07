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
				for( uint i=0; i<a->typesSize; ++i )
					UA_DataType_clear( &a->types[i] );//the names and members, and the type and encoding ids - a string or guid id leaked when only the first two went.
				UA_free( a->types );
				UA_free( a );
			}
			a = next;
		}
	}
	namespace Gateway{
	DataTypesAwait::DataTypesAwait( vector<NodeId>&& nodes, sp<UAClient> client, const UA_DataTypeArray* known, SL sl )ι:
		UAServiceAwait{ move(client), "read DataTypeDefinition", sl },
		_nodes{ move(nodes) },
		_request{ _nodes, {UA_ATTRIBUTEID_DATATYPEDEFINITION, UA_ATTRIBUTEID_BROWSENAME} },
		_known{ known }
	{}
	α DataTypesAwait::Suspend()ι->void{
		if( _nodes.empty() )
			Resume( DataTypeArrayPtr{nullptr, FreeDataTypeArray} );
		else
			UAServiceAwait::Suspend();
	}
	α DataTypesAwait::Request()ι->void*{
		return static_cast<UA_ReadRequest*>( &_request );
	}
	//open62541 clears the response once the completion returns:  fromDescription copies what it keeps.
	α DataTypesAwait::Convert( UA_ReadResponse&& response )ε->DataTypeArrayPtr{
		THROW_IFX( response.responseHeader.serviceResult, UAClientException(response.responseHeader.serviceResult, _client->Handle(), "read DataTypeDefinition", _sl) );
		THROW_IFX( response.resultsSize!=_request.nodesToReadSize, UAClientException(UA_STATUSCODE_BADUNEXPECTEDERROR, _client->Handle(), Ƒ("read answered {} results for {} attributes", response.resultsSize, _request.nodesToReadSize), _sl) );
		let count = _nodes.size();
		DataTypeArrayPtr y{ (UA_DataTypeArray*)UA_calloc(1, sizeof(UA_DataTypeArray)), FreeDataTypeArray };
		THROW_IF( !y, "Could not allocate the DataTypeArray." );
		y->cleanup = true;
		y->types = (UA_DataType*)UA_calloc( count, sizeof(UA_DataType) );
		THROW_IF( !y->types, "Could not allocate {} DataTypes.", count );
		vector<uint> pending;//the nodes with a definition to build, by index.
		for( uint i=0; i<count; ++i ){
			let& definition = response.results[i*2];
			let& name = response.results[i*2+1];
			if( (definition.hasStatus && definition.status) || !definition.hasValue || !UA_Variant_hasScalarType(&definition.value, &UA_TYPES[UA_TYPES_STRUCTUREDEFINITION]) )
				continue;
			if( (name.hasStatus && name.status) || !name.hasValue || !UA_Variant_hasScalarType(&name.value, &UA_TYPES[UA_TYPES_QUALIFIEDNAME]) )
				continue;
			pending.push_back( i );
		}
		flat_map<uint,StatusCode> failed;//each pending node's last refusal.
		for( bool progress=true; progress && pending.size(); ){//a pass per level of nesting the answer's order hides:  until a pass builds nothing.
			progress = false;
			vector<uint> left;
			for( let i : pending ){
				UA_StructureDescription description;
				description.structureDefinition = *(UA_StructureDefinition*)response.results[i*2].value.data;
				description.dataTypeId = _request.nodesToRead[i*2].nodeId;
				description.name = *(UA_QualifiedName*)response.results[i*2+1].value.data;
				UA_ExtensionObject eo;
				UA_ExtensionObject_setValue( &eo, &description, &UA_TYPES[UA_TYPES_STRUCTUREDESCRIPTION] );
				UA_DataTypeArray lookup = *y;//the types built so far, then the known ones:  a member's type may be one of them.
				lookup.next = const_cast<UA_DataTypeArray*>( _known ? _known : UA_Client_getConfig( *_client )->customDataTypes );//read alone:  fromDescription takes the chain by non-const pointer.
				if( let sc = UA_DataType_fromDescription(&y->types[y->typesSize], &eo, &lookup); sc ){//cleared on failure:  the slot is reused.
					failed[i] = sc;
					left.push_back( i );
					continue;
				}
				++y->typesSize;
				progress = true;
			}
			pending = move( left );
		}
		for( let i : pending )
			WARN( "[{}]Could not build the DataType {} from {}: {}", hex(_client->Handle()), ToSV(((UA_QualifiedName*)response.results[i*2+1].value.data)->name), _nodes[i].ToString(), UAException::Message(failed[i]) );
		if( !y->typesSize ){
			UA_free( y->types );
			y->types = nullptr;
		}
		return y;
	}
}}