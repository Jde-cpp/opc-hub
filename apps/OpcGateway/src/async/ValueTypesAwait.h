#pragma once
#include <jde/fwk/co/AnyAwait.h>
#include "ReadAwait.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//The type a value written to each node takes, read from the server for updateVariable and the hist edits:  its DataType's
	//where that settles one (BuiltIn), else the type of the node's value, whatever that value's status, since a node whose
	//source is down still holds its type.  A node with neither has no type, so a value's json implies one (Value::Set).  A node
	//whose DataType read is refused has that read's status, and no type.
	struct ValueTypes final{
		//A DataType's built-in type for one of namespace 0, Int32 for an enumeration, as one is encoded.  None for one of another
		//namespace, an abstract one, as Number, or BaseDataType, whose built-in Variant says nothing of a value's type.
		Ω BuiltIn( const UA_NodeId* dataType )ι->const UA_DataType*;
		vector<const UA_DataType*> Types;
		vector<StatusCode> Statuses;
		vector<optional<NodeId>> DataTypes;//each node's DataType attribute:  none where its read was refused.
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