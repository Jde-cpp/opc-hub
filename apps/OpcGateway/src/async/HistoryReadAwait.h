#pragma once
#include <open62541/client.h>
#include <jde/opc/uatypes/NodeId.h>
#include "UAAwait.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//A HistoryRead for the nodes it names (spec *Pass-through*):  of raw values, Part 11's ReadRawModifiedDetails, at
	//times, ReadAtTimeDetails, or processed, ReadProcessedDetails.  Owns its node ids and continuation points, deep-copied
	//as ReadRequest copies its ids, and its details, a member the extension object points at:  move-only, since a copy
	//would double-free the ids and point at the other's details.
	struct HistoryReadRequest final : UA_HistoryReadRequest{
		HistoryReadRequest( const UA_ReadRawModifiedDetails& details, UA_TimestampsToReturn timestamps=UA_TIMESTAMPSTORETURN_BOTH, bool release=false )ι;
		//At times, with useSimpleBounds as given.
		HistoryReadRequest( vector<UA_DateTime>&& times, bool simpleBounds, UA_TimestampsToReturn timestamps=UA_TIMESTAMPSTORETURN_BOTH, bool release=false )ι;
		//Processed:  one aggregate, named for each node Add adds, under the server's defaults (useServerCapabilitiesDefaults).
		//The interval is in milliseconds, as processingInterval is.
		HistoryReadRequest( UA_DateTime start, UA_DateTime end, double interval, const UA_NodeId& aggregate, UA_TimestampsToReturn timestamps=UA_TIMESTAMPSTORETURN_BOTH, bool release=false )ι;
		HistoryReadRequest( HistoryReadRequest&& x )ι;
		~HistoryReadRequest();
		α operator=( HistoryReadRequest&& x )ι->HistoryReadRequest&;
		//Adds a node, with the continuation point its last page returned, if any.
		α Add( const UA_NodeId& node, sv continuationPoint={} )ι->void;
		α Size()Ι->uint{ return _nodes.size(); }
	private:
		struct AtTime final{ UA_ReadAtTimeDetails Details; vector<UA_DateTime> Times; };
		struct Processed final{ UA_ReadProcessedDetails Details; NodeId Aggregate; vector<UA_NodeId> Aggregates; };//Aggregates:  Aggregate once per node, deep copies the dtor clears.
		HistoryReadRequest( UA_TimestampsToReturn timestamps, bool release, const UA_DataType& type )ι;
		α SetNodes()ι->void;//_nodes may have reallocated, and _details moved.
		α Clear()ι->void;
		variant<UA_ReadRawModifiedDetails,AtTime,Processed> _details;
		vector<UA_HistoryReadValueId> _nodes;
	};
	//The response:  results[i] answers the request's i-th node, its historyData decoded by the client to a HistoryData, or
	//a HistoryModifiedData for a modified read.
	struct HistoryReadResponse final : Owned<UA_HistoryReadResponse, UA_TYPES_HISTORYREADRESPONSE>{
		HistoryReadResponse()ι=default;
		HistoryReadResponse( UA_HistoryReadResponse&& x, uint requested )ι:Owned{ move(x) }, Requested{ requested }{}
		α Validate( Handle uahandle, SL sl )ε->void;//throws for a service result that is bad, or an answer that isn't one per node.
		α Result( uint i )ι->UA_HistoryReadResult&{ return results[i]; }
		uint Requested{};//the nodes the request named.
	};
	//Not UA_Client_HistoryRead_raw and the rest:  they are synchronous and take one node.
	struct HistoryReadAwait final : UAServiceAwait<HistoryReadResponse, UA_HistoryReadResponse, UA_TYPES_HISTORYREADREQUEST, UA_TYPES_HISTORYREADRESPONSE>{
		HistoryReadAwait( HistoryReadRequest&& request, sp<UAClient> client, SRCE )ι:UAServiceAwait{ move(client), "historyRead", sl }, _request{ move(request) }{}
	private:
		α Request()ι->void* override{ return &_request; }
		α Convert( UA_HistoryReadResponse&& response )ε->HistoryReadResponse override;
		HistoryReadRequest _request;
	};
}