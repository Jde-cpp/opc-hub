#include "TranslateBrowsePathsAwait.h"
#include "../UAClient.h"
#include "../types/UAClientException.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Browse };
	static_assert( sizeof(UABrowsePath)==sizeof(UA_BrowsePath), "the request takes _paths as a UA_BrowsePath array" );

	α TranslateBrowsePathsAwait::Suspend()ι->void{
		if( _paths.empty() )
			Resume( {} );
		else
			UAServiceAwait::Suspend();
	}
	α TranslateBrowsePathsAwait::Request()ι->void*{
		_request = { {}, _paths.size(), _paths.data() };//not const:  sendRequest writes its header through a cast.
		return &_request;
	}
	//open62541 clears the response once the completion returns, so each node is copied out of it.
	α TranslateBrowsePathsAwait::Convert( UA_TranslateBrowsePathsToNodeIdsResponse&& response )ε->vector<ExpectedNodeId>{
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
		return y;
	}
}