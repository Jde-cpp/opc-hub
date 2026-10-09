#pragma once

namespace Jde::QL{ struct TableQL; }
namespace Jde::Opc{ struct Variant; }
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
		Ω AtribString( UA_AttributeId id )->const string&;
	private:
		α Push( const UA_NodeId& nodeId, UA_AttributeId attrib )ι->void;//deep-copies nodeId into a new _readIds entry.
		α SetNodesToRead()ι->void{ nodesToReadSize=_readIds.size(); nodesToRead=_readIds.data(); }//_readIds may have reallocated.
		vector<UA_ReadValueId> _readIds;
	};
	struct ReadResponse final : UA_ReadResponse{
		ReadResponse():UA_ReadResponse{}{}
		ReadResponse( ReadResponse&& rhs )ι;
		ReadResponse( UA_ReadResponse&& rhs, ReadRequest&& request )ι;
		~ReadResponse(){ UA_ReadResponse_clear(this); }
		α operator=( ReadResponse&& rhs )ι->ReadResponse&;
		α ScalerDataType()ι->UA_DataType*;
		α ScalerNodeId()ε->NodeId;
		α ScalerValue()ε->optional<Variant>;
		α Validate( Handle uahandle, SL sl )ε->void;
		α GetJson()ι{ flat_map<NodeId, jobject> nodes; SetJson(nodes); return nodes; }
		α SetJson( flat_map<NodeId, jobject>& nodes )ι->void;
		α ToJson( const QL::TableQL& ql )ι->jvalue;
	private:
		optional<ReadRequest> Request;
	};

	struct ReadAwait final : TAwait<ReadResponse>{
		ReadAwait( ReadRequest&& req, sp<UAClient> c, bool keepAlive=false, SRCE )ι:TAwait<ReadResponse>{sl}, _request{move(req)}, _client{move(c)}, _keepAlive{keepAlive}{}//keepAlive:  AsyncRequest::Ping's - not user traffic.
		ReadAwait( NodeId nodeId, UA_AttributeId attrib, sp<UAClient> c, SRCE )ι:ReadAwait{{move(nodeId), attrib}, move(c), false, sl}{}
		ReadAwait( Browse::Response&& browse, QL::TableQL&& ql, sp<UAClient> c, SRCE )ι:ReadAwait{{move(browse), move(ql)}, move(c), false, sl}{}

		α await_ready()ι->bool override{ return !_request.nodesToReadSize; }
		α Suspend()ι->void override;
		Ω OnResponse( UA_Client* client, void* userdata, UA_UInt32 requestId, UA_ReadResponse* rr )ι->void;
		α OnComplete( UA_ReadResponse* rr )ι->void;
		α await_resume()ε->ReadResponse override;
	private:
		ReadRequest _request;
		RequestId 	_requestId{};
		sp<UAClient> _client;
		bool _keepAlive;
	};
}