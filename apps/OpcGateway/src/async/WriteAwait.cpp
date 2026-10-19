#include "WriteAwait.h"
#include "../UAClient.h"
#define let const auto

namespace Jde::Opc::Gateway{
	α WriteResponse::operator=( WriteResponse&& x )ι->WriteResponse&{
		if( this != &x ){
			UA_WriteResponse_clear( this );
			*( UA_WriteResponse* )this = x;
			UA_WriteResponse_init( &x );
		}
		return *this;
	}
	Ω onResponse( UA_Client*, void* userdata, RequestId requestId, UA_WriteResponse* response )ι->void{
		auto& await = *( WriteAwait* )userdata;
		await.AddResponse( requestId, move(*response) );
	}
	α WriteAwait::Suspend()ι->void{
		_client->PostUA( [this]{//UA submissions must run on the client's strand.
			let sc = UA_Client_writeValueAttribute_async( *_client, _nodeId, &_value.value, onResponse, this, &_requestId );
			if( sc )
				ResumeExp( UAClientException{sc, _client->Handle(), "writeValueAttribute", _sl} );
			else
				_client->Process( _requestId, "writeValueAttribute" );
		});
	}
	α WriteAwait::AddResponse( RequestId reqId, UA_WriteResponse&& response )ι->void{
		_client->ClearRequest( reqId );
		if( response.responseHeader.serviceResult )
			ResumeExp( UAClientException{response.responseHeader.serviceResult, _client->Handle(), "writeValueAttribute", _sl} );
		else if( response.resultsSize && *response.results )
			ResumeExp( UAClientException{response.results[0], _client->Handle(), "writeValueAttribute", _sl} );
		else
			Resume( move(response) );
	}
}