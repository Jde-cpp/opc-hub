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
		AnyAwait{ sl },
		_nodes{ move(nodes) },
		_client{ move(client) },
		_known{ known }
	{}
	α DataTypesAwait::await_ready()ι->bool{
		if( !_nodes.empty() )
			return false;
		_result = DataTypeArrayPtr{ nullptr, FreeDataTypeArray };
		return true;
	}
	α DataTypesAwait::Read()ι->VoidTask{
		try{
			vector<ReadResponse> responses;
			vector<uint> starts;
			uint batch = _nodes.size();
			for( uint start=0; start<_nodes.size(); ){
				let count = std::min<uint>( batch, _nodes.size()-start );
				vector<NodeId> asked; asked.reserve( count );
				for( uint i=start; i<start+count; ++i )
					asked.push_back( NodeId{_nodes[i]} );
				optional<ReadResponse> response; bool tooMany{};
				try{
					ReadAwait read{ ReadRequest{asked, {UA_ATTRIBUTEID_DATATYPEDEFINITION, UA_ATTRIBUTEID_BROWSENAME}}, _client };
					response = co_await Any( read );
				}
				catch( const UAException& e ){
					if( e.Code()!=UA_STATUSCODE_BADTOOMANYOPERATIONS || count==1 )
						throw;
					tooMany = true;
				}
				if( tooMany ){//the server's MaxNodesPerRead is below the request:  fewer, and again.
					batch = std::max<uint>( 1, count/2 );
					DBG( "[{}]A read of {} DataType definitions was refused as too many - {} at a time.", hex(_client->Handle()), count, batch );
					continue;
				}
				THROW_IFX( response->resultsSize!=count*2, UAClientException(UA_STATUSCODE_BADUNEXPECTEDERROR, _client->Handle(), Ƒ("read answered {} results for {} attributes", response->resultsSize, count*2), _sl) );
				starts.push_back( start );
				responses.push_back( move(*response) );
				start += count;
			}
			Resume( Build(responses, starts) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	//fromDescription copies what it keeps.
	α DataTypesAwait::Build( const vector<ReadResponse>& responses, const vector<uint>& starts )ε->DataTypeArrayPtr{
		let count = _nodes.size();
		vector<std::pair<const UA_DataValue*,const UA_DataValue*>> answers( count );//each node's definition and name.
		for( uint b=0; b<responses.size(); ++b ){
			let end = b+1<starts.size() ? starts[b+1] : count;
			for( uint i=starts[b]; i<end; ++i )
				answers[i] = { &responses[b].results[(i-starts[b])*2], &responses[b].results[(i-starts[b])*2+1] };
		}
		DataTypeArrayPtr y{ (UA_DataTypeArray*)UA_calloc(1, sizeof(UA_DataTypeArray)), FreeDataTypeArray };
		THROW_IF( !y, "Could not allocate the DataTypeArray." );
		y->cleanup = true;
		y->types = (UA_DataType*)UA_calloc( count, sizeof(UA_DataType) );
		THROW_IF( !y->types, "Could not allocate {} DataTypes.", count );
		vector<uint> pending;//the nodes with a definition to build, by index.
		for( uint i=0; i<count; ++i ){
			let& definition = *answers[i].first;
			let& name = *answers[i].second;
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
				description.structureDefinition = *(UA_StructureDefinition*)answers[i].first->value.data;
				description.dataTypeId = _nodes[i];
				description.name = *(UA_QualifiedName*)answers[i].second->value.data;
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
			WARN( "[{}]Could not build the DataType {} from {}: {}", hex(_client->Handle()), ToSV(((UA_QualifiedName*)answers[i].second->value.data)->name), _nodes[i].ToString(), UAException::Message(failed[i]) );
		if( !y->typesSize ){
			UA_free( y->types );
			y->types = nullptr;
		}
		return y;
	}
}}