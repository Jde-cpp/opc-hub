#include "TranslateBrowsePathsAwait.h"
#include "../UAClient.h"
#include "../types/UAClientException.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Browse };
	static_assert( sizeof(UABrowsePath)==sizeof(UA_BrowsePath), "the request takes _paths as a UA_BrowsePath array" );

	α TranslateBrowsePathsAwait::Suspend()ι->void{
		if( _paths.empty() ){
			Resume( {} );
			return;
		}
		_client->PostUA( [this]{//UA submissions must run on the client's strand; `this` outlives suspension (resume only via OnComplete).
			try{
				const UA_TranslateBrowsePathsToNodeIdsRequest request{ {}, _paths.size(), _paths.data() };//borrows _paths:  the service encodes it before it returns.
				UACε( __UA_Client_AsyncService(*_client, &request, &UA_TYPES[UA_TYPES_TRANSLATEBROWSEPATHSTONODEIDSREQUEST], OnResponse, &UA_TYPES[UA_TYPES_TRANSLATEBROWSEPATHSTONODEIDSRESPONSE], this, &_requestId) );
				_client->Process( _requestId, "translateBrowsePathsToNodeIds" );
			}
			catch( UAException& e ){
				ResumeExp( move(e) );
			}
		});
	}
	α TranslateBrowsePathsAwait::OnResponse( UA_Client* /*client*/, void* await, UA_UInt32 /*requestId*/, void* response )ι->void{
		ASSERT( await );
		static_cast<TranslateBrowsePathsAwait*>( await )->OnComplete( *static_cast<const UA_TranslateBrowsePathsToNodeIdsResponse*>(response) );
	}
	//open62541 clears the response once this returns, so each node is copied out of it.
	α TranslateBrowsePathsAwait::OnComplete( const UA_TranslateBrowsePathsToNodeIdsResponse& response )ι->void{
		_client->ClearRequest( _requestId );
		try{
			THROW_IFX( response.responseHeader.serviceResult, UAClientException(response.responseHeader.serviceResult, _client->Handle(), "translateBrowsePathsToNodeIds", _sl) );
			THROW_IFX( response.resultsSize!=_paths.size(), UAClientException(UA_STATUSCODE_BADUNEXPECTEDERROR, _client->Handle(), Ƒ("translateBrowsePathsToNodeIds answered {} results for {} paths", response.resultsSize, _paths.size()), _sl) );
			vector<ExpectedNodeId> y; y.reserve( response.resultsSize );
			for( uint i=0; i<response.resultsSize; ++i ){
				let& result = response.results[i];
				if( result.statusCode )
					y.emplace_back( std::unexpected{result.statusCode} );
				else if( !result.targetsSize )
					y.emplace_back( std::unexpected{UA_STATUSCODE_BADNOMATCH} );
				else{
					if( result.targetsSize>1 )
						WARN( "[{}]Path {} has {} targets - using the first.", hex(_client->Handle()), i, result.targetsSize );
					y.emplace_back( ExNodeId{result.targets[0].targetId} );
				}
			}
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}