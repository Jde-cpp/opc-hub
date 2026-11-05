#include "ReleaseBrowseAwait.h"
#include "../UAClient.h"

namespace Jde::Opc::Gateway{
	static_assert( sizeof(Owned<UA_ByteString,UA_TYPES_BYTESTRING>)==sizeof(UA_ByteString), "the request takes _points as a UA_ByteString array" );

	ReleaseBrowseAwait::ReleaseBrowseAwait( const UA_BrowseResponse& browse, sp<UAClient> client, SL sl )ι:
		UAServiceAwait{ move(client), "releaseContinuationPoints", sl }{
		for( uint i=0; i<browse.resultsSize; ++i ){
			if( browse.results[i].continuationPoint.length )
				_points.emplace_back( browse.results[i].continuationPoint );
		}
	}
	α ReleaseBrowseAwait::Suspend()ι->void{
		if( _points.empty() )
			Resume( UA_STATUSCODE_GOOD );
		else
			UAServiceAwait::Suspend();
	}
	α ReleaseBrowseAwait::Request()ι->void*{
		_request = { {}, true, _points.size(), _points.data() };//not const:  sendRequest writes its header through a cast.
		return &_request;
	}
	α ReleaseBrowseAwait::Convert( UA_BrowseNextResponse&& response )ε->StatusCode{
		if( response.responseHeader.serviceResult )
			return response.responseHeader.serviceResult;
		for( uint i=0; i<response.resultsSize; ++i ){
			if( response.results[i].statusCode )
				return response.results[i].statusCode;
		}
		return UA_STATUSCODE_GOOD;
	}
}