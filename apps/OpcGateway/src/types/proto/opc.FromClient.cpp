#include "opc.FromClient.h"
#include <jde/fwk/io/protobuf.h>
#include <jde/app/proto/common.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/opc/uatypes/ExNodeId.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/proto/opc.Common.h>

#define let const auto

namespace Jde::Opc::Gateway{
	Ω setMessage( RequestId requestId, function<void(FromClient::Message&)> f )ι->string{
		FromClient::Transmission t;
		auto& m = *t.add_messages();
		m.set_request_id( requestId );
		f( m );
		return Protobuf::ToString( t );
	}
	α FromClientUtils::Connection( SessionPK sessionId, RequestId requestId )ι->string{
		return setMessage( requestId, [&](FromClient::Message& m){
			m.set_session_id( Ƒ("{:x}", sessionId) );
		});
	}
	α FromClientUtils::Query( string&& query, jobject&& variables, bool returnRaw, RequestId requestId )ι->string{
		return setMessage( requestId, [&](FromClient::Message& m){
			*m.mutable_query() = App::ProtoUtils::ToQuery( move(query), move(variables), returnRaw );
		});
	}
	α FromClientUtils::Subscription( ServerCnnctnNK&& slug, const vector<NodeId>& nodes, RequestId requestId, const vector<MonitoringParameters>& parameters )ι->string{
		return setMessage( requestId, [&](FromClient::Message& m){
			auto& s = *m.mutable_subscribe();
			s.set_opc_id( move(slug) );
			for( let& n : nodes )
				*s.add_nodes() = ProtoUtils::ToNodeId( n );
			for( let& p : parameters )
				*s.add_parameters() = ToProto( p );
		});
	}
	α FromClientUtils::ToParameters( const FromClient::MonitoringParameters& proto, const NodeId& node, SL sl )ε->MonitoringParameters{
		let refuse = [&]( string&& why ){ throw Exception{ sl, ExceptionArgs{EHttpStatus::BadRequest}, "'{}' asks {}.", node.ToString(), why }; };
		MonitoringParameters y;
		if( proto.has_sampling_interval() ){
			if( !std::isfinite(proto.sampling_interval()) )
				refuse( Ƒ("a sampling_interval of {}, which is not a number of ms", proto.sampling_interval()) );
			y.SamplingInterval = proto.sampling_interval();
		}
		if( proto.has_queue_size() )
			y.QueueSize = proto.queue_size();
		if( proto.has_discard_oldest() )
			y.DiscardOldest = proto.discard_oldest();
		if( proto.has_filter() ){
			let& f = proto.filter();
			if( f.has_trigger() && f.trigger()>UA_DATACHANGETRIGGER_STATUSVALUETIMESTAMP )
				refuse( Ƒ("a trigger of {}, not 0 Status, 1 StatusValue or 2 StatusValueTimestamp", f.trigger()) );
			if( f.deadband_type()>UA_DEADBANDTYPE_PERCENT )
				refuse( Ƒ("a deadband_type of {}, not 0 None, 1 Absolute or 2 Percent", f.deadband_type()) );
			if( !std::isfinite(f.deadband_value()) || f.deadband_value()<0 )
				refuse( Ƒ("a deadband_value of {}, which is not a number of at least 0", f.deadband_value()) );
			if( f.deadband_type()==UA_DEADBANDTYPE_PERCENT && f.deadband_value()>100 )
				refuse( Ƒ("a percent deadband_value of {}, over 100", f.deadband_value()) );
			auto& filter = y.Filter.emplace( DataChangeFilter{.DeadbandType=(UA_DeadbandType)f.deadband_type(), .DeadbandValue=f.deadband_value()} );
			if( f.has_trigger() )
				filter.Trigger = (UA_DataChangeTrigger)f.trigger();
		}
		return y;
	}
	α FromClientUtils::ToProto( const MonitoringParameters& parameters )ι->FromClient::MonitoringParameters{
		FromClient::MonitoringParameters y;
		y.set_sampling_interval( parameters.SamplingInterval );
		y.set_queue_size( parameters.QueueSize );
		y.set_discard_oldest( parameters.DiscardOldest );
		if( parameters.Filter ){
			auto& f = *y.mutable_filter();
			f.set_trigger( parameters.Filter->Trigger );
			f.set_deadband_type( parameters.Filter->DeadbandType );
			f.set_deadband_value( parameters.Filter->DeadbandValue );
		}
		return y;
	}

	α FromClientUtils::Unsubscription( ServerCnnctnNK&& slug, const vector<NodeId>& nodes, RequestId requestId )ι->string{
		return setMessage( requestId, [&](FromClient::Message& m){
			auto& u = *m.mutable_unsubscribe();
			u.set_opc_id( move(slug) );
			for( let& n : nodes )
				*u.add_nodes() = ProtoUtils::ToNodeId( n );
		});
	}

	α FromClientUtils::EventSubscription( ServerCnnctnNK&& slug, const NodeId& node, const vector<string>& select, RequestId requestId )ι->string{
		return setMessage( requestId, [&](FromClient::Message& m){
			auto& s = *m.mutable_subscribe_events();
			s.set_opc_id( move(slug) );
			*s.mutable_node() = ProtoUtils::ToNodeId( node );
			for( let& path : select )
				s.add_select( path );
		});
	}
	α FromClientUtils::EventUnsubscription( ServerCnnctnNK&& slug, const NodeId& node, RequestId requestId )ι->string{
		return setMessage( requestId, [&](FromClient::Message& m){
			auto& u = *m.mutable_unsubscribe_events();
			u.set_opc_id( move(slug) );
			*u.mutable_node() = ProtoUtils::ToNodeId( node );
		});
	}

	α FromClientUtils::HistSubscription( uint32 group, const vector<ExNodeId>& nodes, optional<TimePoint> from, RequestId requestId )ι->string{
		return setMessage( requestId, [&](FromClient::Message& m){
			auto& s = *m.mutable_hist_subscribe();
			s.set_group( group );
			for( let& n : nodes )
				*s.add_nodes() = ProtoUtils::ToExNodeId( n );
			if( from )
				*s.mutable_from_time() = UADateTime{ *from }.ToProto();
		});
	}
	α FromClientUtils::HistUnsubscription( RequestId subscription, RequestId requestId )ι->string{
		return setMessage( requestId, [&](FromClient::Message& m){ m.mutable_hist_unsubscribe()->set_subscription( subscription ); } );
	}
	α FromClientUtils::OeeSubscription( uint32 machine, RequestId requestId )ι->string{
		return setMessage( requestId, [&](FromClient::Message& m){ m.mutable_oee_subscribe()->set_machine( machine ); } );
	}
	α FromClientUtils::OeeUnsubscription( RequestId subscription, RequestId requestId )ι->string{
		return setMessage( requestId, [&](FromClient::Message& m){ m.mutable_oee_unsubscribe()->set_subscription( subscription ); } );
	}
}