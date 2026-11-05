#pragma once
#include <absl/synchronization/mutex.h>
#include <jde/opc/uatypes/NodeId.h>
#include "LazySlot.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//What EnumTypeCache and MethodArgumentCache share:  a LazySlot per node, read from the server on first use and kept for the
	//connection's life.  The owner says how a node is read - Fetch, which ends in Finish - and which failures are the server's answer.
	template<class T>
	struct LazyCache : noncopyable{
		using Ptr = sp<const T>;
		//AnyAwait, not TAwait:  the callers' coroutines have task types of their own - GatewayQLAwait::Query is a TAwait<jvalue>::Task,
		//and may only co_await its own family (the pairing rule, CLAUDE.md) - and the Any family carries its own storage, so a cached
		//entry pre-completes in await_ready.
		struct GetAwait final : AnyAwait<Ptr>{
			GetAwait( LazyCache& cache, sp<UAClient> client, NodeId id, SRCE )ι:AnyAwait<Ptr>{sl}, _cache{cache}, _client{move(client)}, _id{move(id)}{}
			α await_ready()ι->bool override{
				if( auto p = _cache.Find(_id); p ){
					this->_result = move( p );
					return true;
				}
				return false;
			}
		protected:
			α Suspend()ι->void override{ _cache.StartLocked( move(_client), move(_id), this ); }
		private:
			LazyCache& _cache; sp<UAClient> _client; NodeId _id;
		};
		α Get( sp<UAClient> client, NodeId id, SRCE )ι->GetAwait{ return GetAwait{*this, move(client), move(id), sl}; }
		α Find( const NodeId& id )Ι->Ptr{//a Ready entry, else null.
			rl _{ _mutex };
			auto p = _slots.find( id );
			return p!=_slots.end() && p->second.State()==Slot::EState::Ready ? p->second.Value() : Ptr{};
		}
		α Size()Ι->size_t{ rl _{_mutex}; return _slots.size(); }
	protected:
		using Slot = LazySlot<Ptr>;
		~LazyCache()=default;
		//Fire & forget, every co_await Any-wrapped or All, so the task type is free:  the task keeps the client - and so this cache -
		//alive until Finish.
		β Fetch( sp<UAClient> client, NodeId id )ι->VoidTask=0;
		//`retryable` - a lost session, an id the server does not have - empties the slot, so the next Get fetches again and a mistyped
		//id does not poison the connection;  any other error is the server's answer, rethrown to every later caller.
		α Finish( const NodeId& id, Ptr value, up<Exception> error, bool retryable )ι->void{
			typename Slot::Resumes resumes;
			{
				ul _{ _mutex };
				auto slot = _slots.find( id );
				ASSERT( slot!=_slots.end() );
				resumes = slot->second.Finish( move(value), move(error), retryable );
				if( slot->second.State()==Slot::EState::Empty )
					_slots.erase( slot );
			}
			resumes.Run();
		}
		mutable absl::Mutex _mutex;
		flat_map<NodeId, Slot> _slots ABSL_GUARDED_BY(_mutex);
	private:
		α StartLocked( sp<UAClient>&& client, NodeId&& id, GetAwait* waiter )ι->void{
			bool launch{};
			{
				ul _{ _mutex };
				launch = _slots.try_emplace( NodeId{id} ).first->second.Start( waiter );//a new slot:  own the fetch.
			}
			if( launch )
				Fetch( move(client), move(id) );
		}
	};
}