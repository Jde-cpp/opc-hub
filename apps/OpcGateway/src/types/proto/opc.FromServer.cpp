#include "opc.FromServer.h"
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Value.h>
#include <jde/opc/proto/opc.Common.h>
DISABLE_WARNINGS
#include <jde/historian/proto/Hist.Records.pb.h>
ENABLE_WARNINGS
#define let const auto

namespace Jde::Opc::Gateway{
	Ω set( RequestId requestId, function<void(FromServer::Message&)> f )ι->FromServer::Transmission{
		FromServer::Transmission t;
		auto& m = *t.add_messages();
		m.set_request_id( requestId );
		f( m );
		return t;
	}
	α FromServer::AckTrans( uint32 socketSessionId )ι->FromServer::Transmission{
		return set( 0, [socketSessionId](FromServer::Message& m){m.set_ack(socketSessionId);} );
	}

	α FromServer::CompleteTrans( RequestId requestId )ι->FromServer::Transmission{
		FromServer::Message m;
		return MessageTrans( move(m), requestId );
	}
	α FromServer::ToProto( const runtime_error& e )ι->Jde::Proto::Exception{
		Jde::Proto::Exception proto;
		proto.set_what( string{e.what()} );
		if( auto p = dynamic_cast<const Exception*>(&e) ){
			proto.set_code( p->Code() );
			proto.set_status_code( p->HttpStatus() );
			proto.set_category( (Jde::Proto::ECategory)p->Category() );
			proto.set_category_code( p->CategoryCode() );
		}
		else
			proto.set_status_code( 500 );
		return proto;
	}
	α FromServer::ExceptionTrans( const runtime_error& e, optional<RequestId> requestId )ι->FromServer::Transmission{
		FromServer::Transmission t;
		auto& m = *t.add_messages();
		if( requestId )
			m.set_request_id( *requestId );
		*m.mutable_exception() = ToProto( e );
		return t;
	}
	α FromServer::MessageTrans( FromServer::Message&& m, RequestId requestId )ι->FromServer::Transmission{
		FromServer::Transmission t;
		m.set_request_id( requestId );
		*t.add_messages() = move( m );
		return t;
	}
	α FromServer::QueryTrans( string&& result, RequestId requestId )ι->FromServer::Transmission{
		return set( requestId, [&](FromServer::Message& m){
			*m.mutable_query() = move( result );
		} );
	}

	α FromServer::ReadValuesTrans( string opcId, flat_map<NodeId, Opc::Value>&& values, RequestId requestId )ι->FromServer::Transmission{
		FromServer::Transmission t;
		for( auto&& [nodeId, v] : values )
			*t.add_messages() = ToProto( opcId, nodeId, v, requestId );
		return t;
	}

	α FromServer::SubscribeAckTrans( FromServer::SubscriptionAck&& ack, RequestId requestId )ι->FromServer::Transmission{
		FromServer::Transmission t;
		auto& m = *t.add_messages();
		m.set_request_id( requestId );
		m.set_allocated_subscription_ack( new FromServer::SubscriptionAck{move(ack)} );
		return t;
	}
	α FromServer::UnsubscribeTrans( uint32 id, flat_set<NodeId>&& successes, flat_set<NodeId>&& failures )ι->FromServer::Transmission{
		FromServer::Transmission t;
		auto& m = *t.add_messages();
		m.set_request_id( id );
		auto ack = m.mutable_unsubscribe_ack();
		for_each( move(successes), [&ack](let& n){*ack->add_successes() = ProtoUtils::ToNodeId(n);} );
		for_each( move(failures), [&ack](let& n){*ack->add_failures() = ProtoUtils::ToNodeId(n);} );
		return t;
	}

	α FromServer::ToProto( const ServerCnnctnNK& opcId, const NodeId& node, const Opc::Value& v, RequestId requestId )ι->FromServer::Message{
		auto nv = mu<FromServer::NodeValues>();
		*nv->mutable_node() = ProtoUtils::ToNodeId( node );
		nv->set_opc_id( opcId );
		if( v.hasSourceTimestamp )
			*nv->mutable_source() = UADateTime{ v.sourceTimestamp }.ToProto();
		if( v.hasServerTimestamp )
			*nv->mutable_server() = UADateTime{ v.serverTimestamp }.ToProto();
		StatusCode sc = v.status;//the reading's quality - without it a Bad or Uncertain reading was indistinguishable from a Good one on the socket (proto3 omits 0/Good from the wire).
		try{
			*nv->mutable_value() = ProtoUtils::ToValue( v.value );
		}
		catch( const UAException& e ){//the whole reading, as the historian refuses it - a Variant array's good elements alone would pass for the array.
			WARNT( IotReadTag, "{} - {}", node.ToString(), e.what() );
			sc = (StatusCode)e.Code();//BadNotSupported for a DataValue or DiagnosticInfo, with no value, as the historian stores it.  A status_code value read as a StatusCode reading.
		}
		nv->set_sc( sc );
		FromServer::Message m;
		m.set_request_id( requestId );
		m.set_allocated_node_values( nv.release() );
		return m;
	}

	α FromServer::HistSubscribeAckTrans( FromServer::HistSubscribeAck&& ack, RequestId requestId )ι->FromServer::Transmission{
		return set( requestId, [&ack](FromServer::Message& m){ *m.mutable_hist_subscribe_ack() = move( ack ); } );
	}
	α FromServer::HistSnapshotTrans( FromServer::HistSnapshot&& page, RequestId requestId )ι->FromServer::Transmission{
		return set( requestId, [&page](FromServer::Message& m){ *m.mutable_hist_snapshot() = move( page ); } );
	}
	α FromServer::HistRecordsTrans( FromServer::HistRecords&& records, RequestId requestId )ι->FromServer::Transmission{
		return set( requestId, [&records](FromServer::Message& m){ *m.mutable_hist_records() = move( records ); } );
	}
	α FromServer::ToProto( const Hist::Proto::DataValue& v, uint32 slot, bool bound )ι->FromServer::HistValue{
		FromServer::HistValue y;
		y.set_node( slot );
		if( v.has_value() )
			*y.mutable_value() = v.value();
		y.set_sc( v.status() );
		if( v.has_source_ts() )
			*y.mutable_source() = UADateTime{ (UA_DateTime)v.source_ts() }.ToProto();
		if( v.has_server_ts() )
			*y.mutable_server() = UADateTime{ (UA_DateTime)v.server_ts() }.ToProto();
		y.set_heartbeat( v.has_heartbeat() );
		y.set_bound( bound );
		return y;
	}
	α FromServer::ToProto( const Hist::Proto::Modification& m, uint32 slot )ι->FromServer::HistModification{
		FromServer::HistModification y;
		y.set_node( slot );
		*y.mutable_target() = UADateTime{ (UA_DateTime)m.target_source_ts() }.ToProto();
		y.set_type( (uint32)m.update_type() );
		if( m.has_new_value() )
			*y.mutable_value() = ToProto( m.new_value(), slot, false );
		*y.mutable_time() = UADateTime{ (UA_DateTime)m.ts() }.ToProto();
		y.set_user( m.user_name() );
		return y;
	}
}