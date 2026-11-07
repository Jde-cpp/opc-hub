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
	//value alone.  `IndexRange`, Part 4's NumericRange ("2:4"), writes `Written` into that slice of an array value (clients spec *Values and
	//writes*, #423);  empty writes the whole.
	struct WriteItem final{
		NodeId Node;
		UA_AttributeId Attribute{ UA_ATTRIBUTEID_VALUE };
		Value Written;
		string IndexRange;
	};
	//One Write service of every item - a node's Value, or updateNode's attributes, one UA_WriteValue each (clients spec *Attributes*).
	//Any item the server refuses is the error (Refusal).
	struct WriteAwait final : UAAwait<WriteResponse>{
		WriteAwait( NodeId nodeId, Value&& value, sp<UAClient> c, SRCE )ι:WriteAwait{ move(nodeId), move(value), {}, move(c), sl }{}
		WriteAwait( NodeId nodeId, Value&& value, string indexRange, sp<UAClient> c, SRCE )ι:UAAwait{ move(c), sl }{ _items.push_back( WriteItem{move(nodeId), UA_ATTRIBUTEID_VALUE, move(value), move(indexRange)} ); }
		WriteAwait( vector<WriteItem>&& items, sp<UAClient> c, SRCE )ι:UAAwait{ move(c), sl }, _items{move(items)}{}
		α Suspend()ι->void override;
		α OnComplete( UA_WriteResponse&& response )ι->void;
		//The first refused item's code, and the error's text:  every item refused - each after the first with its status - and every one
		//written, since a Write applies each item on its own and a refusal leaves the rest written.  An item the server sent no result for
		//is BadUnexpectedError:  nothing says it was written.  {0, ""} when every item was.
		Ω Refusal( const UA_WriteResponse& response, const vector<WriteItem>& items )ι->std::pair<StatusCode,string>;
	private:
		vector<WriteItem> _items;
	};
}