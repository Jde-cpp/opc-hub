#pragma once
#include <jde/opc/proto/Opc.Common.pb.h>
#include "Opc.FromClient.pb.h"
namespace Jde::Opc{ struct NodeId; struct Value; }

namespace Jde::Opc::Gateway::FromClientUtils{
	α Connection( SessionPK sessionId, RequestId requestId )ι->string;
	α Subscription( ServerCnnctnNK&& slug, const vector<NodeId>& nodes, RequestId requestId )ι->string;
	α Unsubscription( ServerCnnctnNK&& slug, const vector<NodeId>& nodes, RequestId requestId )ι->string;
	α Query( string&& query, jobject&& variables, bool returnRaw, RequestId requestId  )ι->string;
}
