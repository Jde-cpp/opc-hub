#pragma once
#include "LazyCache.h"

namespace Jde::Opc::Gateway{
	//The per-connection first namespace-0 supertype of each DataType of the server's own, which a method argument of that type is
	//encoded by (Argument::BuiltIn):  an enumeration's Enumeration, so Int32;  a subtype of a built-in, that built-in;  a structure's
	//Structure, which no argument can be sent as yet (#421).  Read up the inverse HasSubtype references on first use and kept for the
	//connection's life.  Owned by the UAClient, so a disconnect/TTL drop discards it with the client.
	struct BaseTypeCache final : LazyCache<NodeId>{
	private:
		//Failed:  the server's own answer that the type derives from nothing of namespace 0, re-thrown to every later caller;  a lost
		//session empties the slot instead.
		α Fetch( sp<UAClient> client, NodeId id )ι->VoidTask override;
		constexpr static uint MaxSteps{ 16 };//a cycle in a server's type tree is a broken server, not a reason to browse forever.
	};
}