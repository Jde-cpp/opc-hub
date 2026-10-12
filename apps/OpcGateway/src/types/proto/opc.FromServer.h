#pragma once
namespace Jde::Opc{ struct NodeId; struct Value; }
namespace Jde::Opc::Hist::Proto{ class DataValue; class Modification; }

namespace Jde::Opc::Gateway::FromServer{
	α AckTrans( uint32 socketSessionId )ι->FromServer::Transmission;
	α CompleteTrans( RequestId requestId )ι->FromServer::Transmission;
	α ExceptionTrans( const runtime_error& e, optional<RequestId> requestId )ι->FromServer::Transmission;
	α MessageTrans( FromServer::Message&& m, RequestId requestId )ι->FromServer::Transmission;
	α ReadValuesTrans( string opcId, flat_map<NodeId, Opc::Value>&& values, RequestId requestId )ι->FromServer::Transmission;
	α SubscribeAckTrans( FromServer::SubscriptionAck&& ack, RequestId requestId )ι->FromServer::Transmission;
	α UnsubscribeTrans( uint32 id, flat_set<NodeId>&& successes, flat_set<NodeId>&& failures )ι->FromServer::Transmission;
	α QueryTrans( string&& result, RequestId requestId )ι->FromServer::Transmission;
	//A historian subscription's messages (spec *Subscriptions*), each under its HistSubscribe's request id.
	α HistSubscribeAckTrans( FromServer::HistSubscribeAck&& ack, RequestId requestId )ι->FromServer::Transmission;
	α HistSnapshotTrans( FromServer::HistSnapshot&& page, RequestId requestId )ι->FromServer::Transmission;
	α HistRecordsTrans( FromServer::HistRecords&& records, RequestId requestId )ι->FromServer::Transmission;

	α ToProto( const ServerCnnctnNK& opcId, const NodeId& node, const Opc::Value& v, RequestId requestId )ι->FromServer::Message;
	//An error as a socket answers it:  what, and for an Exception its code, status and category, which ride the base virtuals since
	//the type can't cross the wire;  a plain std::exception is an unclassified server fault, 500.
	α ToProto( const runtime_error& e )ι->Jde::Proto::Exception;
	//A historian record as its file holds it, onto the wire:  node is the slot, its node's place in the HistSubscribe's nodes.
	α ToProto( const Hist::Proto::DataValue& v, uint32 slot, bool bound )ι->FromServer::HistValue;
	α ToProto( const Hist::Proto::Modification& m, uint32 slot )ι->FromServer::HistModification;
}