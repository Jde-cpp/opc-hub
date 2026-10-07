#include "HistoryReadAwait.h"
#include "../UAClient.h"

#define let const auto
namespace Jde::Opc::Gateway{
	HistoryReadRequest::HistoryReadRequest( const UA_ReadRawModifiedDetails& details, UA_TimestampsToReturn timestamps, bool release )ι:
		UA_HistoryReadRequest{}, _details{ details }{
		timestampsToReturn = timestamps;
		releaseContinuationPoints = release;
		historyReadDetails.encoding = UA_EXTENSIONOBJECT_DECODED_NODELETE;//_details is a member:  nothing frees it.
		historyReadDetails.content.decoded.type = &UA_TYPES[UA_TYPES_READRAWMODIFIEDDETAILS];
		SetNodes();
	}
	HistoryReadRequest::HistoryReadRequest( HistoryReadRequest&& x )ι:
		UA_HistoryReadRequest{ x }, _details{ x._details }, _nodes{ move(x._nodes) }{
		x.nodesToRead=nullptr; x.nodesToReadSize=0;//x no longer owns the identifiers this points at.
		SetNodes();
	}
	HistoryReadRequest::~HistoryReadRequest(){
		for( auto& node : _nodes )
			UA_HistoryReadValueId_clear( &node );
	}
	α HistoryReadRequest::operator=( HistoryReadRequest&& x )ι->HistoryReadRequest&{
		if( this!=&x ){
			for( auto& node : _nodes )
				UA_HistoryReadValueId_clear( &node );
			*( UA_HistoryReadRequest* )this = x;
			_details = x._details;
			_nodes = move( x._nodes );
			x.nodesToRead=nullptr; x.nodesToReadSize=0;
			SetNodes();
		}
		return *this;
	}
	α HistoryReadRequest::Add( const UA_NodeId& node, sv continuationPoint )ι->void{
		auto& id = _nodes.emplace_back( UA_HistoryReadValueId{} );
		UA_NodeId_copy( &node, &id.nodeId );//deep:  the dtor clears it, and the source may be gone by the time Suspend encodes.
		if( continuationPoint.size() && !UA_ByteString_allocBuffer(&id.continuationPoint, continuationPoint.size()) )
			memcpy( id.continuationPoint.data, continuationPoint.data(), continuationPoint.size() );
		SetNodes();
	}

	HistoryReadResponse::HistoryReadResponse( UA_HistoryReadResponse&& x, HistoryReadRequest&& request )ι:
		UA_HistoryReadResponse{ x }, Request{ move(request) }{
		UA_HistoryReadResponse_init( &x );
	}
	HistoryReadResponse::HistoryReadResponse( HistoryReadResponse&& x )ι:
		UA_HistoryReadResponse{ x }, Request{ move(x.Request) }{
		UA_HistoryReadResponse_init( &x );
	}
	α HistoryReadResponse::operator=( HistoryReadResponse&& x )ι->HistoryReadResponse&{
		if( this!=&x ){
			UA_HistoryReadResponse_clear( this );
			*( UA_HistoryReadResponse* )this = x;
			Request = move( x.Request );
			UA_HistoryReadResponse_init( &x );
		}
		return *this;
	}
	α HistoryReadResponse::Validate( Handle uahandle, SL sl )ε->void{
		THROW_IFX( responseHeader.serviceResult, UAClientException(responseHeader.serviceResult, uahandle, "HistoryRead", sl) );
		THROW_IFSL( !Request || resultsSize!=Request->Size(), "HistoryRead answered {} of {} nodes.", resultsSize, Request ? Request->Size() : 0 );
	}

	α HistoryReadAwait::Suspend()ι->void{
		_client->PostUA( [this]{//UA submissions must run on the client's strand; `this` outlives suspension (resume only via OnComplete).
			try{
				UACε( __UA_Client_AsyncService(*_client, &_request, &UA_TYPES[UA_TYPES_HISTORYREADREQUEST], OnResponse, &UA_TYPES[UA_TYPES_HISTORYREADRESPONSE], this, &_requestId) );
				_client->Process( _requestId, "historyRead" );
			}
			catch( UAException& e ){
				ResumeExp( move(e) );
			}
		});
	}
	α HistoryReadAwait::OnResponse( UA_Client* /*client*/, void* await, UA_UInt32 /*requestId*/, void* response )ι->void{
		ASSERT( await );
		static_cast<HistoryReadAwait*>( await )->OnComplete( static_cast<UA_HistoryReadResponse*>(response) );
	}
	α HistoryReadAwait::OnComplete( UA_HistoryReadResponse* rr )ι->void{
		_client->ClearRequest( _requestId );
		HistoryReadResponse response{ move(*rr), move(_request) };
		try{
			response.Validate( _client->Handle(), _sl );
			Resume( move(response) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}