#include "WriteAwait.h"
#include "../UAClient.h"
#include "ReadAwait.h"
#define let const auto

namespace Jde::Opc::Gateway{
	Ω onResponse( UA_Client*, void* userdata, RequestId /*requestId*/, void* response )ι->void{
		auto& await = *( WriteAwait* )userdata;
		await.OnComplete( move(*static_cast<UA_WriteResponse*>(response)) );
	}
	//The service itself, not writeValueAttribute's helper, which sends the Variant alone;  the request borrows the nodes and values until the submit has encoded it.
	α WriteAwait::Suspend()ι->void{
		Submit( "write", [this]{
			vector<UA_WriteValue> items( _items.size() );
			for( uint i=0; i<_items.size(); ++i ){
				auto& item = items[i]; UA_WriteValue_init( &item );
				item.nodeId = _items[i].Node;
				item.attributeId = _items[i].Attribute;
				item.value = _items[i].Written;
			}
			UA_WriteRequest request; UA_WriteRequest_init( &request );
			request.nodesToWrite = items.data();
			request.nodesToWriteSize = items.size();
			return UAAsyncService( _client, &request, UA_TYPES_WRITEREQUEST, onResponse, UA_TYPES_WRITERESPONSE, this, _requestId );
		} );
	}
	α WriteAwait::OnComplete( UA_WriteResponse&& response )ι->void{
		ClearRequest();
		if( response.responseHeader.serviceResult ){
			ResumeExp( UAClientException{response.responseHeader.serviceResult, _client->Handle(), "write", _sl} );
			return;
		}
		for( uint i=0; i<response.resultsSize && i<_items.size(); ++i ){
			if( let sc = response.results[i]; sc ){
				ResumeExp( UAClientException{sc, _client->Handle(), Ƒ("write {}", ReadRequest::AttribName(_items[i].Attribute)), _sl} );
				return;
			}
		}
		Resume( move(response) );
	}
}