#pragma once
#include <jde/opc/uatypes/NodeId.h>
#include "ReadAwait.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	α FreeDataTypeArray( UA_DataTypeArray* a )ι->void;//what UA_Client_getRemoteDataTypes's caller frees:  the types with UA_DataType_clear, then the arrays.
	using DataTypeArrayPtr = up<UA_DataTypeArray, void(*)(UA_DataTypeArray*)>;
	//The server's definitions of the DataType nodes asked for, built as UA_Client_getRemoteDataTypes builds them (ua_client_util.c)
	//but over the asynchronous read:  the typed call is synchronous, and held the client's strand for the round trip.  Each node's
	//DataTypeDefinition and BrowseName in one ReadRequest, then UA_DataType_fromDescription over each structure definition - a node
	//that is no structure, or whose definition the server withholds, is left out, as the typed call leaves it.  A member's type resolves
	//against the types built so far and `known` - the connection's registry chain (TypeRegistry), else the client's own - and in passes,
	//so a type whose member is a type the same answer lists after it builds on the next.  Resumes with an exception when the read itself
	//fails.  The array is `cleanup`:  the pointer's deleter frees it, unless the registry takes it over.
	struct DataTypesAwait final : UAServiceAwait<DataTypeArrayPtr, UA_ReadResponse, UA_TYPES_READREQUEST, UA_TYPES_READRESPONSE>{
		DataTypesAwait( vector<NodeId>&& nodes, sp<UAClient> client, const UA_DataTypeArray* known=nullptr, SRCE )ι;
		α Suspend()ι->void override;
	private:
		α Request()ι->void* override;
		α Convert( UA_ReadResponse&& response )ε->DataTypeArrayPtr override;
		vector<NodeId> _nodes;
		ReadRequest _request;//each node's definition, then its name:  results[i*2] and results[i*2+1].
		const UA_DataTypeArray* _known;
	};
}