#pragma once
#include <open62541/client.h>
#include <jde/opc/uatypes/Owned.h>
#include "UAAwait.h"

namespace Jde::QL{ struct TableQL; }
namespace Jde::Opc::Gateway{
	namespace Browse{ struct Response; }
	struct UAClient;
	//Owns its node ids:  every UA_ReadValueId gets a UA_NodeId_copy, so the NodeId/vector<NodeId>/Browse::Response a ctor
	//reads from need not outlive the request.  A shallow UA_NodeId slice here shares identifier.string.data with its
	//source, and the request outlives that source on both async paths (a temporary vector<NodeId>, and the caller's frame
	//while the read is in flight) - freed-identifier encoding for any string/GUID/bytestring id.
	struct ReadRequest final : UA_ReadRequest{
		ReadRequest( const NodeId& nodeId, UA_AttributeId attrib )ι:ReadRequest{nodeId, {attrib}}{}
		ReadRequest( const NodeId& nodeId, std::initializer_list<UA_AttributeId> attribs )ι;//one UA_ReadValueId per attribute, in order - results[i] answers attribs[i].
		ReadRequest( const vector<NodeId>& ids, const QL::TableQL& ql )ι;
		ReadRequest( const vector<NodeId>& ids, UA_AttributeId attrib )ι;//results[i] answers ids[i].
		ReadRequest( Browse::Response&& browse, QL::TableQL&& ql )ι;
		ReadRequest( ReadRequest&& x )ι;//move-only:  the implicit copy would double-free the identifiers.
		~ReadRequest();
		α operator=( ReadRequest&& x )ι->ReadRequest&;
		α Add( const QL::TableQL& ql, const flat_map<NodeId, jobject>& nodes )ι->void;
		Ω AttribName( UA_AttributeId id )ι->const string&;
	private:
		α Push( const UA_NodeId& nodeId, UA_AttributeId attrib )ι->void;//deep-copies nodeId into a new _readIds entry.
		α SetNodesToRead()ι->void{ nodesToReadSize=_readIds.size(); nodesToRead=_readIds.data(); }//_readIds may have reallocated.
		vector<UA_ReadValueId> _readIds;
	};
	struct ReadResponse final : Owned<UA_ReadResponse, UA_TYPES_READRESPONSE>{
		ReadResponse()ι=default;
		ReadResponse( UA_ReadResponse&& x, ReadRequest&& request )ι:Owned{ move(x) }, Request{ move(request) }{}
		α ScalerDataType()ι->UA_DataType*;
		α Validate( Handle uahandle, SL sl )ε->void;
		α GetJson()ι{ flat_map<NodeId, jobject> nodes; SetJson(nodes); return nodes; }
		α SetJson( flat_map<NodeId, jobject>& nodes )ι->void;
		α ToJson( const QL::TableQL& ql )ι->jvalue;
	private:
		optional<ReadRequest> Request;
	};

	struct ReadAwait final : UAAwait<ReadResponse>{
		ReadAwait( ReadRequest&& req, sp<UAClient> c, bool keepAlive=false, SRCE )ι:UAAwait{ move(c), sl }, _request{move(req)}, _keepAlive{keepAlive}{}//keepAlive:  AsyncRequest::Ping's - not user traffic.
		ReadAwait( NodeId nodeId, UA_AttributeId attrib, sp<UAClient> c, SRCE )ι:ReadAwait{{move(nodeId), attrib}, move(c), false, sl}{}
		ReadAwait( Browse::Response&& browse, QL::TableQL&& ql, sp<UAClient> c, SRCE )ι:ReadAwait{{move(browse), move(ql)}, move(c), false, sl}{}

		α await_ready()ι->bool override{ return !_request.nodesToReadSize; }
		α Suspend()ι->void override;
		Ω OnResponse( UA_Client* client, void* userdata, UA_UInt32 requestId, UA_ReadResponse* rr )ι->void;
		α OnComplete( UA_ReadResponse* rr )ι->void;
		α await_resume()ε->ReadResponse override;
	private:
		ReadRequest _request;
		bool _keepAlive;
	};
}