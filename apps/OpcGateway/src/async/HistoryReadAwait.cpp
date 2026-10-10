#include "HistoryReadAwait.h"
#include "../UAClient.h"

#define let const auto
namespace Jde::Opc::Gateway{
	HistoryReadRequest::HistoryReadRequest( UA_TimestampsToReturn timestamps, bool release, const UA_DataType& type )ι:
		UA_HistoryReadRequest{}{
		timestampsToReturn = timestamps;
		releaseContinuationPoints = release;
		historyReadDetails.encoding = UA_EXTENSIONOBJECT_DECODED_NODELETE;//the details are a member:  nothing frees them.
		historyReadDetails.content.decoded.type = &type;
	}
	HistoryReadRequest::HistoryReadRequest( const UA_ReadRawModifiedDetails& details, UA_TimestampsToReturn timestamps, bool release )ι:
		HistoryReadRequest{ timestamps, release, UA_TYPES[UA_TYPES_READRAWMODIFIEDDETAILS] }{
		_details = details;
		SetNodes();
	}
	HistoryReadRequest::HistoryReadRequest( vector<UA_DateTime>&& times, bool simpleBounds, UA_TimestampsToReturn timestamps, bool release )ι:
		HistoryReadRequest{ timestamps, release, UA_TYPES[UA_TYPES_READATTIMEDETAILS] }{
		AtTime atTime{ {}, move(times) };
		UA_ReadAtTimeDetails_init( &atTime.Details );
		atTime.Details.useSimpleBounds = simpleBounds;
		_details = move( atTime );
		SetNodes();
	}
	HistoryReadRequest::HistoryReadRequest( UA_DateTime start, UA_DateTime end, double interval, const UA_NodeId& aggregate, UA_TimestampsToReturn timestamps, bool release )ι:
		HistoryReadRequest{ timestamps, release, UA_TYPES[UA_TYPES_READPROCESSEDDETAILS] }{
		Processed processed{ {}, NodeId{aggregate}, {} };
		UA_ReadProcessedDetails_init( &processed.Details );
		processed.Details.startTime = start;
		processed.Details.endTime = end;
		processed.Details.processingInterval = interval;
		processed.Details.aggregateConfiguration.useServerCapabilitiesDefaults = true;
		_details = move( processed );
		SetNodes();
	}
	HistoryReadRequest::HistoryReadRequest( HistoryReadRequest&& x )ι:
		UA_HistoryReadRequest{ x }, _details{ move(x._details) }, _nodes{ move(x._nodes) }{
		x.nodesToRead=nullptr; x.nodesToReadSize=0;//x no longer owns the identifiers this points at, nor the aggregates:  its vectors are empty.
		SetNodes();
	}
	HistoryReadRequest::~HistoryReadRequest(){
		Clear();
	}
	α HistoryReadRequest::operator=( HistoryReadRequest&& x )ι->HistoryReadRequest&{
		if( this!=&x ){
			Clear();
			*( UA_HistoryReadRequest* )this = x;
			_details = move( x._details );
			_nodes = move( x._nodes );
			x.nodesToRead=nullptr; x.nodesToReadSize=0;
			SetNodes();
		}
		return *this;
	}
	α HistoryReadRequest::Clear()ι->void{
		for( auto& node : _nodes )
			UA_HistoryReadValueId_clear( &node );
		if( auto p = std::get_if<Processed>(&_details); p ){
			for( auto& aggregate : p->Aggregates )
				UA_NodeId_clear( &aggregate );
		}
	}
	α HistoryReadRequest::SetNodes()ι->void{
		nodesToReadSize=_nodes.size(); nodesToRead=_nodes.data();
		std::visit( [this]( auto& d ){
			using T = std::decay_t<decltype(d)>;
			if constexpr( std::is_same_v<T,UA_ReadRawModifiedDetails> )
				historyReadDetails.content.decoded.data = &d;
			else if constexpr( std::is_same_v<T,AtTime> ){
				d.Details.reqTimes=d.Times.data(); d.Details.reqTimesSize=d.Times.size();
				historyReadDetails.content.decoded.data = &d.Details;
			}
			else{
				d.Details.aggregateType=d.Aggregates.data(); d.Details.aggregateTypeSize=d.Aggregates.size();
				historyReadDetails.content.decoded.data = &d.Details;
			}
		}, _details );
	}
	α HistoryReadRequest::Add( const UA_NodeId& node, sv continuationPoint )ι->void{
		auto& id = _nodes.emplace_back( UA_HistoryReadValueId{} );
		UA_NodeId_copy( &node, &id.nodeId );//deep:  the dtor clears it, and the source may be gone by the time Suspend encodes.
		if( continuationPoint.size() && !UA_ByteString_allocBuffer(&id.continuationPoint, continuationPoint.size()) )
			memcpy( id.continuationPoint.data, continuationPoint.data(), continuationPoint.size() );
		if( auto p = std::get_if<Processed>(&_details); p )//one aggregate per node (Part 11 §6.5.4.2).
			UA_NodeId_copy( &p->Aggregate, &p->Aggregates.emplace_back(UA_NodeId{}) );
		SetNodes();
	}

	HistoryReadResponse::HistoryReadResponse( UA_HistoryReadResponse&& x, uint requested )ι:
		UA_HistoryReadResponse{ x }, Requested{ requested }{
		UA_HistoryReadResponse_init( &x );
	}
	HistoryReadResponse::HistoryReadResponse( HistoryReadResponse&& x )ι:
		UA_HistoryReadResponse{ x }, Requested{ x.Requested }{
		UA_HistoryReadResponse_init( &x );
	}
	α HistoryReadResponse::operator=( HistoryReadResponse&& x )ι->HistoryReadResponse&{
		if( this!=&x ){
			UA_HistoryReadResponse_clear( this );
			*( UA_HistoryReadResponse* )this = x;
			Requested = x.Requested;
			UA_HistoryReadResponse_init( &x );
		}
		return *this;
	}
	α HistoryReadResponse::Validate( Handle uahandle, SL sl )ε->void{
		THROW_IFX( responseHeader.serviceResult, UAClientException(responseHeader.serviceResult, uahandle, "HistoryRead", sl) );
		THROW_IFSL( resultsSize!=Requested, "HistoryRead answered {} of {} nodes.", resultsSize, Requested );
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
		HistoryReadResponse response{ move(*rr), _request.Size() };
		try{
			response.Validate( _client->Handle(), _sl );
			Resume( move(response) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}