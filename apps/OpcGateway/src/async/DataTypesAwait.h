#pragma once
#include <jde/fwk/co/AnyAwait.h>
#include <jde/opc/uatypes/NodeId.h>
#include "ReadAwait.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	α FreeDataTypeArray( UA_DataTypeArray* a )ι->void;//what UA_Client_getRemoteDataTypes's caller frees:  the types with UA_DataType_clear, then the arrays.
	using DataTypeArrayPtr = up<UA_DataTypeArray, void(*)(UA_DataTypeArray*)>;
	//The server's definitions of the DataType nodes asked for, built as UA_Client_getRemoteDataTypes builds them (ua_client_util.c)
	//but over the asynchronous read:  the typed call is synchronous, and held the client's strand for the round trip.  Each node's
	//DataTypeDefinition and BrowseName in as few ReadRequests as the server takes - all at once, halved by each BadTooManyOperations
	//(reviews-issue/421-type-registry.md #2) - then UA_DataType_fromDescription over each structure definition - a node that is no
	//structure, or whose definition the server withholds, is left out, as the typed call leaves it.  A member's type resolves against
	//the types built so far and `known` - the connection's registry chain (TypeRegistry), else the client's own - and in passes, so a
	//type whose member is a type the same answer lists after it builds on the next.  Resumes with an exception when a read itself
	//fails.  The array is `cleanup`:  the pointer's deleter frees it, unless the registry takes it over.  Any family:  TypeRegistry::Run
	//awaits it from a VoidTask.
	struct DataTypesAwait final : AnyAwait<DataTypeArrayPtr>{
		DataTypesAwait( vector<NodeId>&& nodes, sp<UAClient> client, const UA_DataTypeArray* known=nullptr, SRCE )ι;
		α await_ready()ι->bool override;
	private:
		α Suspend()ι->void override{ Read(); }
		α Read()ι->VoidTask;
		//The array from the reads:  responses[b] answers _nodes from starts[b], two results a node, its definition then its name.
		α Build( const vector<ReadResponse>& responses, const vector<uint>& starts )ε->DataTypeArrayPtr;
		vector<NodeId> _nodes;
		sp<UAClient> _client;
		const UA_DataTypeArray* _known;
	};
}