#include "BrowseNextAwait.h"
#include "../UAClient.h"
#include "../types/UAClientException.h"

namespace Jde::Opc::Gateway{
	BrowseNextAwait::BrowseNextAwait( const UA_BrowseResponse& browse, sp<UAClient> client, bool release, SL sl )ι:
		UAServiceAwait{ move(client), release ? "releaseContinuationPoints" : "BrowseNext", sl },
		_release{ release }{
		for( uint i=0; i<browse.resultsSize; ++i ){
			if( browse.results[i].continuationPoint.length )
				_points.emplace_back( browse.results[i].continuationPoint );
		}
	}
	α BrowseNextAwait::Suspend()ι->void{
		if( _points.empty() )
			Resume( Browse::Response{UA_BrowseResponse{}} );
		else
			UAServiceAwait::Suspend();
	}
	α BrowseNextAwait::Request()ι->void*{
		_request = { {}, _release, _points.size(), _points.data() };//not const:  sendRequest writes its header through a cast.
		return &_request;
	}
	//open62541 clears the response once the completion returns:  its results are taken whole, and left empty there.
	α BrowseNextAwait::Convert( UA_BrowseNextResponse&& response )ε->Browse::Response{
		THROW_IFX( UA_StatusCode_isBad(response.responseHeader.serviceResult), UAClientException(response.responseHeader.serviceResult, _client->Handle(), "BrowseNext", _sl) );
		UA_BrowseResponse y{};
		y.resultsSize = exchange( response.resultsSize, 0 );
		y.results = exchange( response.results, nullptr );
		return Browse::Response{ move(y) };
	}
}