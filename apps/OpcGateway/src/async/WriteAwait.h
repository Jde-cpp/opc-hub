#pragma once
#include <jde/opc/uatypes/Value.h>
#include "UAAwait.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	struct WriteResponse : Owned<UA_WriteResponse, UA_TYPES_WRITERESPONSE>{
		WriteResponse( UA_WriteResponse&& x )ι:Owned{ move(x) }{}
	};
	struct WriteAwait final : UAAwait<WriteResponse>{
		WriteAwait( NodeId nodeId, Value&& value, sp<UAClient> c, SRCE )ι:UAAwait{ move(c), sl }, _nodeId{move(nodeId)}, _value{move(value)}{}
		α Suspend()ι->void override;
		α OnComplete( UA_WriteResponse&& response )ι->void;
	private:
		NodeId _nodeId;
		Value _value;
	};
}