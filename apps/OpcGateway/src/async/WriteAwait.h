#pragma once
#include <jde/opc/uatypes/Value.h>
#include "UAAwait.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	struct WriteResponse : Owned<UA_WriteResponse, UA_TYPES_WRITERESPONSE>{
		WriteResponse( UA_WriteResponse&& x )ι:Owned{ move(x) }{}
	};
	//One attribute of one node, written as the DataValue `Written` has it:  a Value's status and source timestamp go with it where set,
	//which the server gates by the node's StatusWrite and TimestampWrite bits (servers spec *Writes*);  a Value made from json carries the
	//value alone.
	struct WriteItem final{
		NodeId Node;
		UA_AttributeId Attribute{ UA_ATTRIBUTEID_VALUE };
		Value Written;
	};
	//One Write service of every item - a node's Value, or updateNode's attributes, one UA_WriteValue each (clients spec *Attributes*).
	//The first item the server refuses is the error, named by its attribute, as a lone Value's was.
	struct WriteAwait final : UAAwait<WriteResponse>{
		WriteAwait( NodeId nodeId, Value&& value, sp<UAClient> c, SRCE )ι:UAAwait{ move(c), sl }{ _items.push_back( WriteItem{move(nodeId), UA_ATTRIBUTEID_VALUE, move(value)} ); }
		WriteAwait( vector<WriteItem>&& items, sp<UAClient> c, SRCE )ι:UAAwait{ move(c), sl }, _items{move(items)}{}
		α Suspend()ι->void override;
		α OnComplete( UA_WriteResponse&& response )ι->void;
	private:
		vector<WriteItem> _items;
	};
}