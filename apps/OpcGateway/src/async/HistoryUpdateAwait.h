#pragma once
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Value.h>

namespace Jde::Opc::Gateway{
	struct UAClient;
	//A HistoryUpdate, Part 11's, one entry per node (spec *Pass-through*):  UpdateDataDetails, DeleteRawModifiedDetails or
	//DeleteAtTimeDetails.  Each entry's details are owned by its extension object, deep-copied node id and values included,
	//so clearing the request frees them all:  move-only, since a copy would double-free them.
	struct HistoryUpdateRequest final : UA_HistoryUpdateRequest{
		HistoryUpdateRequest()ι:UA_HistoryUpdateRequest{}{}
		HistoryUpdateRequest( HistoryUpdateRequest&& x )ι;
		~HistoryUpdateRequest(){ UA_HistoryUpdateRequest_clear(this); }
		α operator=( HistoryUpdateRequest&& x )ι->HistoryUpdateRequest&;
		α Update( const UA_NodeId& node, UA_PerformUpdateType type, vector<Value>&& values )ε->void;
		α DeleteRaw( const UA_NodeId& node, UA_DateTime start, UA_DateTime end )ε->void;
		α DeleteAtTime( const UA_NodeId& node, const vector<UA_DateTime>& times )ε->void;
		α Size()Ι->uint{ return historyUpdateDetailsSize; }
	private:
		α Add( void* details, const UA_DataType& type )ε->void;//takes details, which it frees if it can't add them.
	};
	//The response:  results[i] answers the request's i-th entry, its operationResults one per value or time, which a
	//server that refuses the entry whole leaves out.
	struct HistoryUpdateResponse final : UA_HistoryUpdateResponse{
		HistoryUpdateResponse()ι:UA_HistoryUpdateResponse{}{}
		HistoryUpdateResponse( UA_HistoryUpdateResponse&& x, uint requested )ι;
		HistoryUpdateResponse( HistoryUpdateResponse&& x )ι;
		~HistoryUpdateResponse(){ UA_HistoryUpdateResponse_clear(this); }
		α operator=( HistoryUpdateResponse&& x )ι->HistoryUpdateResponse&;
		α Validate( Handle uahandle, SL sl )ε->void;//throws for a service result that is bad, or an answer that isn't one per entry.
		α Result( uint i )Ι->const UA_HistoryUpdateResult&{ return results[i]; }
		uint Requested{};//the entries the request held.
	};
	//Sends the request through open62541's generic asynchronous service on the client's strand, as HistoryReadAwait does:
	//the typed helpers, UA_Client_HistoryUpdate_insert and the rest, are synchronous and take one value.
	struct HistoryUpdateAwait final : TAwait<HistoryUpdateResponse>{
		HistoryUpdateAwait( HistoryUpdateRequest&& request, sp<UAClient> client, SRCE )ι:TAwait<HistoryUpdateResponse>{sl}, _request{move(request)}, _client{move(client)}{}
		α await_ready()ι->bool override{ return !_request.Size(); }
		α Suspend()ι->void override;
		α await_resume()ε->HistoryUpdateResponse override{ return Promise() ? TAwait<HistoryUpdateResponse>::await_resume() : HistoryUpdateResponse{}; }
		Ω OnResponse( UA_Client* client, void* await, UA_UInt32 requestId, void* response )ι->void;
		α OnComplete( UA_HistoryUpdateResponse* response )ι->void;
	private:
		HistoryUpdateRequest _request;
		RequestId _requestId{};
		sp<UAClient> _client;
	};
}