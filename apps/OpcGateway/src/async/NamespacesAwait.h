#pragma once
#include "ReadValueAwait.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//A server's NamespaceArray, index by index, and the lookups the historian makes across it (spec *Templates*):  a stored
	//node carries its namespace URI, the server's items take its index.
	struct Namespaces final{
		α Index( sv uri )Ι->optional<NsIndex>;
		α Uri( NsIndex index )Ι->optional<sv>{ return index<Uris.size() ? optional<sv>{ Uris[index] } : nullopt; }
		//The node by the server's index, for a stored node whose URI the server has; none when it doesn't.
		α ToNodeId( const ExNodeId& node )Ι->optional<NodeId>;
		//The node as the historian stores it:  its URI in place of the server's index, which is left 0.
		α ToExNodeId( const UA_NodeId& node )Ι->optional<ExNodeId>;
		vector<string> Uris;
	};
	//The server's NamespaceArray, ns=0;i=2255, read as a value rather than taken from open62541's client copy
	//(UA_Client_getNamespaceUri), which a read it fires after the session activates fills:  a lookup right behind the connect can
	//find that empty.  The two index alike - the client appends the server's uris in order, and the gateway predefines none of
	//its own.  The namespaces query, testConnection and the historian's node spelling all read it here.
	struct NamespacesAwait final : TAwaitEx<Namespaces,TAwait<flat_map<NodeId,Value>>::Task>{
		using base = TAwaitEx<Namespaces,TAwait<flat_map<NodeId,Value>>::Task>;
		NamespacesAwait( sp<UAClient> client, SRCE )ι:base{ sl }, _client{ move(client) }{}
		α Execute()ι->TAwait<flat_map<NodeId,Value>>::Task override;
	private:
		sp<UAClient> _client;
	};
}