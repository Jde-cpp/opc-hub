#pragma once
#include <jde/ql/types/MutationQL.h>
#include "../async/WriteAwait.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//updateNode( opc, id, attributes:{ … } ){ … } (clients spec *Attributes*, #417):  a node's attributes other than Value, which updateVariable
	//writes - one UA_WriteValue per attribute in one Write, each typed from a table by attribute, not from the node's DataType as a value
	//is (VariableQLAwait):  a LocalizedText for name (DisplayName), description and inverseName, a UInt32 for writeMask, userWriteMask and
	//accessLevelEx, a Double for minimumSamplingInterval, a Boolean, Byte, UInt16 or Int32 for the rest.  The keys are the reads' spellings
	//(ReadRequest::FindAttrib);  one the table has no type for - browse, dataType, arrayDimensions, the permissions, value - is refused
	//before the write, as is an unknown one.  A LocalizedText given as a bare string keeps the locale the node's text has, read first:
	//a server keeps a text per locale (Part 3 §8.5), and open62541 answers a session without one the first text ever added, so a text
	//written under no locale beside the nodeset's `en` would never read back;  {locale, text} names the locale instead.  An empty text
	//removes its locale's (open62541), so an empty description is gone, and a bare empty name is refused:  a node keeps its DisplayName.
	//The server's refusal of any attribute - the node's WriteMask lacks its bit, or the user's rights do, BadUserAccessDenied either way -
	//is the mutation's error, as updateVariable's is, and names the attributes written beside it (WriteAwait::Refusal);  otherwise the
	//result request reads the node back.
	struct NodeQLMAwait final : TAwaitEx<jvalue,TAwait<WriteResponse>::Task>{
		using base = TAwaitEx<jvalue,TAwait<WriteResponse>::Task>;
		NodeQLMAwait( QL::MutationQL&& mutation, sp<UAClient> client, SRCE )ι:base{ sl }, _mutation{move(mutation)}, _client{move(client)}{}
		α Execute()ι->TAwait<WriteResponse>::Task override;//the write's own type;  the read-back goes through Any().
		Ω AttributeType( UA_AttributeId attribute )ι->const UA_DataType*;//the type an attribute is written as;  null for one updateNode refuses.
	private:
		QL::MutationQL _mutation;
		sp<UAClient> _client;
	};
}