#pragma once
#include <jde/fwk/co/AnyAwait.h>
#include <absl/synchronization/mutex.h>
#include <jde/opc/uatypes/NodeId.h>

namespace Jde::QL{ struct TableQL; }
namespace Jde::Opc::Gateway{
	struct UAClient;
	//The per-connection enumeration definitions behind `__type( opc, ns, i ){ name enumValues{id name description} }` (ql/GatewayQLAwait).
	//These used to be hand-copied from the companion specs into config/introspection/*.jsonnet keyed by namespace *index*, so they
	//fit exactly one server's namespace layout and the `opc` argument was ignored.  The server carries every enum on its DataType
	//node - the DataTypeDefinition attribute (1.04+), else the EnumValues/EnumStrings property - so read it there on first use
	//and keep it for the connection's life.  Owned by the UAClient, so a disconnect/TTL drop discards it with the client.
	struct EnumTypeCache final : noncopyable{
		struct Field{ _int Value; string Name; string Description; };
		enum class ESource : uint8{ Definition, EnumValues, EnumStrings };
		struct EnumType{
			NodeId Id;
			string Name;			//the DataType node's DisplayName text, e.g. DeviceHealthEnumeration.
			vector<Field> Fields;	//server order.
			ESource Source{};
			α ToJson( const QL::TableQL& q )Ι->jobject;	//{name, enumValues:[{id,name,description}]} projected by the query's columns.
		};
		using Ptr = sp<const EnumType>;

		//AnyAwait, not TAwait:  GatewayQLAwait::Query is a TAwait<jvalue>::Task and may only co_await its own family (the pairing
		//rule, CLAUDE.md);  the Any family carries its own storage, so a cached type pre-completes in await_ready.
		struct GetAwait final : AnyAwait<Ptr>{
			GetAwait( EnumTypeCache& cache, sp<UAClient> client, NodeId id, SRCE )ι:AnyAwait<Ptr>{sl}, _cache{cache}, _client{move(client)}, _id{move(id)}{}
			α await_ready()ι->bool override;
		protected:
			α Suspend()ι->void override;
		private:
			EnumTypeCache& _cache; sp<UAClient> _client; NodeId _id;
		};
		α Get( sp<UAClient> client, NodeId id, SRCE )ι->GetAwait{ return GetAwait{*this, move(client), move(id), sl}; }
		α Find( const NodeId& id )Ι->Ptr;	//a Ready entry, else null.
		α Size()Ι->size_t{ rl _{_mutex}; return _slots.size(); }
		Ω SourceName( ESource s )ι->sv;
	private:
		enum class EState : uint8{ Fetching, Ready, Failed };
		struct Slot{
			EState State{ EState::Fetching };
			Ptr Type;					//Ready.
			up<Exception> Error;		//Failed:  a permanent answer (not an enum, no definition anywhere) - re-thrown to every later caller without another round trip.
			vector<GetAwait*> Waiters;	//parked on the in-flight fetch;  resumed by Finish.
		};
		α StartLocked( sp<UAClient>&& client, NodeId&& id, GetAwait* waiter )ι->void;
		α Fetch( sp<UAClient> client, NodeId id )ι->VoidTask;	//every co_await is Any-wrapped, so the task type is free.
		α Finish( const NodeId& id, Ptr type, up<Exception> error, bool retryable )ι->void;

		Ω FromDefinition( const UA_EnumDefinition& def )ι->vector<Field>;
		Ω FromEnumValues( const UA_Variant& v )ε->vector<Field>;	//EnumValueType[] - open62541 unwraps the ExtensionObjects since the type is in UA_TYPES.
		Ω FromEnumStrings( const UA_Variant& v )ε->vector<Field>;	//LocalizedText[], the value is the index.

		mutable absl::Mutex _mutex;
		flat_map<NodeId, Slot> _slots ABSL_GUARDED_BY(_mutex);
	};
}
