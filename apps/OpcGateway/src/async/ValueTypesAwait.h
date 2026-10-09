#pragma once
#include <jde/fwk/co/AnyAwait.h>
#include "ReadAwait.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//The type a value written to each node takes, read from the server for updateVariable and the hist edits:  the node's
	//DataType where the gateway has a built-in type for it, of namespace 0, else the type of the node's value, whatever that
	//value's status, since a node whose source is down still holds its type.  A node with neither has no type, so a value's
	//json implies one (Value::Set).  A node whose DataType read is refused has that read's status, and no type.
	struct ValueTypes final{
		vector<const UA_DataType*> Types;
		vector<StatusCode> Statuses;
	};
	struct ValueTypesAwait final : AnyAwait<ValueTypes>{
		ValueTypesAwait( vector<NodeId> nodes, sp<UAClient> client, SRCE )ι:AnyAwait<ValueTypes>{sl}, _nodes{move(nodes)}, _client{move(client)}{}
	private:
		α Suspend()ι->void override{ Execute(); }
		α Execute()ι->TAwait<ReadResponse>::Task;
		vector<NodeId> _nodes;
		sp<UAClient> _client;
	};
}