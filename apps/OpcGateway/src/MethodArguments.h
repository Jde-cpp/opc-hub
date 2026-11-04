#pragma once
#include <jde/fwk/co/AnyAwait.h>
#include <absl/synchronization/mutex.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Owned.h>
#include "LazySlot.h"

namespace Jde::QL{ struct TableQL; }
namespace Jde::Opc::Gateway{
	struct UAClient;
	//One of a method's InputArguments or OutputArguments:  its name, data type, value rank and description, as the property has it.
	struct Argument final : Owned<UA_Argument, UA_TYPES_ARGUMENT>{
		using Owned::Owned;
		α ToJson( const QL::TableQL& q )Ι->jobject;//{name, dataType, valueRank, arrayDimensions, description} projected by the query's columns.
		//The built-in type a json argument converts toward (ValueTypes::BuiltIn):  the data type's for one of namespace 0, Int32 for an
		//enumeration;  null for a type of another namespace, or an abstract one, which leaves the json to imply the type (Value::Set).
		α BuiltIn()Ι->const UA_DataType*;
	};
	//A method's arguments, read from its InputArguments and OutputArguments properties - a method without one takes, or answers,
	//none - and its DisplayName:  what `call` types the arguments from, and `method( opc, id )` answers the web's dialog with.
	struct MethodArguments final{
		NodeId Id;
		string Name;
		vector<Argument> Inputs, Outputs;
		α ToJson( const QL::TableQL& q )Ι->jobject;//{name, inputArguments:[…], outputArguments:[…]} by the query's columns and sub-tables.
	};
	//The per-connection method arguments behind `call` and `method` (ql/CallQLAwait, ql/GatewayQLAwait), read on first use and kept for
	//the connection's life, as the enumeration definitions are (EnumTypeCache):  the property is the server's declaration, and a
	//call needs it typed before it goes.  Owned by the UAClient, so a disconnect/TTL drop discards it with the client.
	struct MethodArgumentCache final : noncopyable{
		using Ptr = sp<const MethodArguments>;
		//AnyAwait, as EnumTypeCache::GetAwait:  the callers' coroutines have task types of their own, and a cached entry pre-completes.
		struct GetAwait final : AnyAwait<Ptr>{
			GetAwait( MethodArgumentCache& cache, sp<UAClient> client, NodeId id, SRCE )ι:AnyAwait<Ptr>{sl}, _cache{cache}, _client{move(client)}, _id{move(id)}{}
			α await_ready()ι->bool override;
		protected:
			α Suspend()ι->void override;
		private:
			MethodArgumentCache& _cache; sp<UAClient> _client; NodeId _id;
		};
		α Get( sp<UAClient> client, NodeId id, SRCE )ι->GetAwait{ return GetAwait{*this, move(client), move(id), sl}; }
		α Find( const NodeId& id )Ι->Ptr;//a Ready entry, else null.
		α Size()Ι->size_t{ rl _{_mutex}; return _slots.size(); }
	private:
		using Slot = LazySlot<Ptr>;//Failed:  the server's own answer that the id is no method, re-thrown to every later caller;  a lost session or an unknown id empties the slot instead.
		α StartLocked( sp<UAClient>&& client, NodeId&& id, GetAwait* waiter )ι->void;
		α Fetch( sp<UAClient> client, NodeId id )ι->VoidTask;//every co_await is Any-wrapped, so the task type is free.
		α Finish( const NodeId& id, Ptr arguments, up<Exception> error, bool retryable )ι->void;

		mutable absl::Mutex _mutex;
		flat_map<NodeId, Slot> _slots ABSL_GUARDED_BY(_mutex);
	};
}