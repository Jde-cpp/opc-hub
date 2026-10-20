#pragma once
#include <open62541/client.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Value.h>
#include "UAAwait.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//A HistoryUpdate, Part 11's, one entry per node (spec *Pass-through*):  UpdateDataDetails, DeleteRawModifiedDetails or
	//DeleteAtTimeDetails.  Each entry's details are owned by its extension object, deep-copied node id and values included,
	//so clearing the request frees them all.
	struct HistoryUpdateRequest final : Owned<UA_HistoryUpdateRequest, UA_TYPES_HISTORYUPDATEREQUEST>{
		α Update( const UA_NodeId& node, UA_PerformUpdateType type, vector<Value>&& values )ε->void;
		α DeleteRaw( const UA_NodeId& node, UA_DateTime start, UA_DateTime end )ε->void;
		α DeleteAtTime( const UA_NodeId& node, const vector<UA_DateTime>& times )ε->void;
		α Size()Ι->uint{ return historyUpdateDetailsSize; }
	private:
		α Add( void* details, const UA_DataType& type )ε->void;//takes details, which it frees if it can't add them.
	};
	//The response:  results[i] answers the request's i-th entry, its operationResults one per value or time, which a
	//server that refuses the entry whole leaves out.
	struct HistoryUpdateResponse final : Owned<UA_HistoryUpdateResponse, UA_TYPES_HISTORYUPDATERESPONSE>{
		HistoryUpdateResponse()ι=default;
		HistoryUpdateResponse( UA_HistoryUpdateResponse&& x, uint requested )ι:Owned{ move(x) }, Requested{ requested }{}
		α Validate( Handle uahandle, SL sl )ε->void;//throws for a service result that is bad, or an answer that isn't one per entry.
		α Result( uint i )Ι->const UA_HistoryUpdateResult&{ return results[i]; }
		uint Requested{};//the entries the request held.
	};
	//Not UA_Client_HistoryUpdate_insert and the rest:  they are synchronous and take one value.
	struct HistoryUpdateAwait final : UAServiceAwait<HistoryUpdateResponse, UA_HistoryUpdateResponse, UA_TYPES_HISTORYUPDATEREQUEST, UA_TYPES_HISTORYUPDATERESPONSE>{
		HistoryUpdateAwait( HistoryUpdateRequest&& request, sp<UAClient> client, SRCE )ι:UAServiceAwait{ move(client), "historyUpdate", sl }, _request{ move(request) }{}
		α await_ready()ι->bool override{ return !_request.Size(); }
		α await_resume()ε->HistoryUpdateResponse override{ return Promise() ? TAwait<HistoryUpdateResponse>::await_resume() : HistoryUpdateResponse{}; }
	private:
		α Request()ι->void* override{ return &_request; }
		α Convert( UA_HistoryUpdateResponse&& response )ε->HistoryUpdateResponse override;
		HistoryUpdateRequest _request;
	};
}