#pragma once
#include <jde/opc/proto/Opc.Common.pb.h>
#include "Opc.FromClient.pb.h"
#include "../../uatypes/MonitoringParameters.h"
namespace Jde::Opc{ struct NodeId; struct ExNodeId; struct Value; }

namespace Jde::Opc::Gateway::FromClientUtils{
	α Connection( SessionPK sessionId, RequestId requestId )ι->string;
	//A Subscribe, its parameters by position in `nodes` - fewer than nodes, or none, leaves the rest the defaults (spec *Watch list*).
	α Subscription( ServerCnnctnNK&& slug, const vector<NodeId>& nodes, RequestId requestId, const vector<MonitoringParameters>& parameters={} )ι->string;
	//A node's parameters as a Subscribe carries them, and back:  an unset field is the gateway's default, an absent filter none.
	α ToParameters( const FromClient::MonitoringParameters& proto )ι->MonitoringParameters;
	α ToProto( const MonitoringParameters& parameters )ι->FromClient::MonitoringParameters;
	α Unsubscription( ServerCnnctnNK&& slug, const vector<NodeId>& nodes, RequestId requestId )ι->string;
	α Query( string&& query, jobject&& variables, bool returnRaw, RequestId requestId  )ι->string;
	//A historian subscription and its end (spec *Subscriptions*), as a client sends them:  what the tests' GatewayClientSocket writes.
	α HistSubscription( uint32 group, const vector<ExNodeId>& nodes, optional<TimePoint> from, RequestId requestId )ι->string;
	α HistUnsubscription( RequestId subscription, RequestId requestId )ι->string;
}