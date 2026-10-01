#include "opc.FromClient.h"
#include <jde/fwk/io/protobuf.h>
#include <jde/app/proto/common.h>
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
	α FromClientUtils::Subscription( ServerCnnctnNK&& slug, const vector<NodeId>& nodes, RequestId requestId )ι->string{
		return setMessage( requestId, [&](FromClient::Message& m){
			auto& s = *m.mutable_subscribe();
			s.set_opc_id( move(slug) );
			for( let& n : nodes )
				*s.add_nodes() = ProtoUtils::ToNodeId( n );
		});
	}

	α FromClientUtils::Unsubscription( ServerCnnctnNK&& slug, const vector<NodeId>& nodes, RequestId requestId )ι->string{
		return setMessage( requestId, [&](FromClient::Message& m){
			auto& u = *m.mutable_unsubscribe();
			u.set_opc_id( move(slug) );
			for( let& n : nodes )
				*u.add_nodes() = ProtoUtils::ToNodeId( n );
		});
	}
}