#include "ValueTypesAwait.h"
#include <jde/opc/uatypes/opcHelpers.h>
#include "../UAClient.h"

#define let const auto
namespace Jde::Opc::Gateway{
	Ω bad( const UA_DataValue& v )ι->bool{ return v.hasStatus && UA_StatusCode_isBad( v.status ); }
	//A built-in type for a DataType node of namespace 0;  null for any other, or an abstract one.
	Ω builtIn( const UA_DataValue& dataType )ι->const UA_DataType*{
		if( !dataType.hasValue || dataType.value.type!=&UA_TYPES[UA_TYPES_NODEID] )
			return nullptr;
		const NodeId id{ *(UA_NodeId*)dataType.value.data };
		return id.namespaceIndex ? nullptr : FindDataType( id );
	}

	α ValueTypesAwait::Execute()ι->TAwait<ReadResponse>::Task{
		try{
			let count = _nodes.size();
			ValueTypes y{ vector<const UA_DataType*>(count), vector<StatusCode>(count, UA_STATUSCODE_GOOD) };
			vector<NodeId> untyped; vector<uint> slots;
			let dataTypes = co_await ReadAwait{ ReadRequest{_nodes, UA_ATTRIBUTEID_DATATYPE}, _client, false, _sl };
			for( uint i=0; i<count; ++i ){
				if( i>=dataTypes.resultsSize )
					y.Statuses[i] = UA_STATUSCODE_BADUNEXPECTEDERROR;
				else if( bad(dataTypes.results[i]) )
					y.Statuses[i] = dataTypes.results[i].status;
				else if( !(y.Types[i] = builtIn(dataTypes.results[i])) ){
					untyped.push_back( _nodes[i] );
					slots.push_back( i );
				}
			}
			if( untyped.size() ){
				let values = co_await ReadAwait{ ReadRequest{untyped, UA_ATTRIBUTEID_VALUE}, _client, false, _sl };
				for( uint k=0; k<untyped.size() && k<values.resultsSize; ++k ){
					if( values.results[k].hasValue )
						y.Types[slots[k]] = values.results[k].value.type;
				}
			}
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}