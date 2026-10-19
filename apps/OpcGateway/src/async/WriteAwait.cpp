#include "WriteAwait.h"
#include "../UAClient.h"
#define let const auto

namespace Jde::Opc::Gateway{
	Ω onResponse( UA_Client*, void* userdata, RequestId /*requestId*/, UA_WriteResponse* response )ι->void{
		auto& await = *( WriteAwait* )userdata;
		await.OnComplete( move(*response) );
	}
	α WriteAwait::Suspend()ι->void{
		Submit( "writeValueAttribute", [this]{ return UA_Client_writeValueAttribute_async(*_client, _nodeId, &_value.value, onResponse, this, &_requestId); } );
	}
	α WriteAwait::OnComplete( UA_WriteResponse&& response )ι->void{
		ClearRequest();
		if( response.responseHeader.serviceResult )
			ResumeExp( UAClientException{response.responseHeader.serviceResult, _client->Handle(), "writeValueAttribute", _sl} );
		else if( response.resultsSize && *response.results )
			ResumeExp( UAClientException{response.results[0], _client->Handle(), "writeValueAttribute", _sl} );
		else
			Resume( move(response) );
	}
}