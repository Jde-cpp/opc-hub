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
		if( auto [sc, text] = Refusal(response, _items); sc )
			ResumeExp( UAClientException{sc, _client->Handle(), move(text), _sl} );
		else
			Resume( move(response) );
	}
	α WriteAwait::Refusal( const UA_WriteResponse& response, const vector<WriteItem>& items )ι->std::pair<StatusCode,string>{
		StatusCode first{};
		string refused, written;
		for( uint i=0; i<items.size(); ++i ){
			let sc = i<response.resultsSize ? response.results[i] : UA_STATUSCODE_BADUNEXPECTEDERROR;
			let& name = ReadRequest::AttribName( items[i].Attribute );
			if( !sc )
				written += written.empty() ? name : Ƒ( ", {}", name );
			else if( !first ){
				first = sc;
				refused = name;
			}
			else
				refused += Ƒ( ", {} {}", name, UAException::Message(sc) );
		}
		if( !first )
			return {};
		return { first, written.empty() ? Ƒ("write {}", refused) : Ƒ("write {}; written: {}", refused, written) };
	}
}