#include "WriteAwait.h"
#include "../UAClient.h"
#define let const auto

namespace Jde::Opc::Gateway{
	Ω onResponse( UA_Client*, void* userdata, RequestId /*requestId*/, void* response )ι->void{
		auto& await = *( WriteAwait* )userdata;
		await.OnComplete( move(*static_cast<UA_WriteResponse*>(response)) );
	}
	//The service itself, not writeValueAttribute's helper, which sends the Variant alone;  the request borrows the node and value until the submit has encoded it.
	α WriteAwait::Suspend()ι->void{
		Submit( "write", [this]{
			UA_WriteValue item; UA_WriteValue_init( &item );
			item.nodeId = _nodeId;
			item.attributeId = UA_ATTRIBUTEID_VALUE;
			item.value = _value;
			UA_WriteRequest request; UA_WriteRequest_init( &request );
			request.nodesToWrite = &item;
			request.nodesToWriteSize = 1;
			return UAAsyncService( _client, &request, UA_TYPES_WRITEREQUEST, onResponse, UA_TYPES_WRITERESPONSE, this, _requestId );
		} );
	}
	α WriteAwait::OnComplete( UA_WriteResponse&& response )ι->void{
		ClearRequest();
		if( response.responseHeader.serviceResult )
			ResumeExp( UAClientException{response.responseHeader.serviceResult, _client->Handle(), "write", _sl} );
		else if( response.resultsSize && *response.results )
			ResumeExp( UAClientException{response.results[0], _client->Handle(), "write", _sl} );
		else
			Resume( move(response) );
	}
}