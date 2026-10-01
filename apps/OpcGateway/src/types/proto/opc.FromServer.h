#pragma once
namespace Jde::Opc{ struct NodeId; struct Value; }

namespace Jde::Opc::Gateway::FromServer{
	α AckTrans( uint32 socketSessionId )ι->FromServer::Transmission;
	α CompleteTrans( RequestId requestId )ι->FromServer::Transmission;
	α ExceptionTrans( const runtime_error& e, optional<RequestId> requestId )ι->FromServer::Transmission;
	α MessageTrans( FromServer::Message&& m, RequestId requestId )ι->FromServer::Transmission;
	α ReadValuesTrans( string opcId, flat_map<NodeId, Opc::Value>&& values, RequestId requestId )ι->FromServer::Transmission;
	α SubscribeAckTrans( FromServer::SubscriptionAck&& ack, RequestId requestId )ι->FromServer::Transmission;
	α UnsubscribeTrans( uint32 id, flat_set<NodeId>&& successes, flat_set<NodeId>&& failures )ι->FromServer::Transmission;
	α QueryTrans( string&& result, RequestId requestId )ι->FromServer::Transmission;

	α ToProto( const ServerCnnctnNK& opcId, const NodeId& node, const Opc::Value& v, RequestId requestId )ι->FromServer::Message;
}