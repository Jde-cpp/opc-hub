#pragma once
#include <absl/synchronization/mutex.h>
#include <jde/opc/uatypes/Types.h>
#include "async/DataTypesAwait.h"
#include "types/ServerCnnctn.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//A connection's structure types (clients spec *Structures*, #421):  the server's own definitions, fetched from its Structure subtree and
	//kept as the UA_DataTypeArrays open62541 builds, an append-only chain every client of the connection decodes by (UAClient::Create,
	//RechainTypes) and the serializers decode an encoded body against (ITypes:  the attribute reads, updateVariable's answer, the history
	//rows, the dataType query).  Keyed by slug beside the client map (UAClient::Types) and outliving its clients - the historian's reads
	//have none - and dropped when the row is edited or purged (UAClient::ConnectionEdited), so a type redefined on the server while the row
	//lives is not refreshed, the spec's *Later*.  Built by the NamespaceArray it was fetched under:  a fetch that finds the array moved -
	//a server restarted with its namespaces reordered - starts the chain over (reviews-issue/421-type-registry.md #4).
	//Asked by each client's activation and reconnect (Check), by a value naming an encoding id the chain lacks (Missing, posted), and by
	//the dataType query (Fetched);  one fetch at a time, an ask while one runs following it.  Each array is a new node ahead of the chain
	//and never changes after, so a reader walks the chain off any thread;  cleanup is false on every one, so no client's config clear frees
	//them, and they are freed here, a chain set aside by a move with them, once the last client holding this goes - a value decoded against
	//the chain is serialized at once (Value::ToJson) or re-encoded before it is kept (Variant::WithBuiltInTypes), so nothing outlives an array.
	struct TypeRegistry final : ITypes, std::enable_shared_from_this<TypeRegistry>, noncopyable{
		enum class EFetch : uint8{
			Check,//the NamespaceArray read, and the subtree walked only when no walk has succeeded yet or the namespaces moved.
			Walk//the subtree walked whatever:  a type the server added since.
		};
		TypeRegistry( ServerCnnctnNK slug, ServerCnnctnPK row )ι:_slug{ move(slug) }, _row{ row }{}
		~TypeRegistry();
		α Chain()Ι->const UA_DataTypeArray* override{ return _head.load( std::memory_order_acquire ); }
		//A walk through a connected client, posted - once for each id, unless the walk it starts fails, after which the id may ask again
		//once RetryAfter has passed.  Nothing is noted while no client is connected:  the next value naming it asks again.
		α Missing( const UA_NodeId& encodingId )Ι->void override;
		//Through `client`, one of this registry's:  started unless one runs, which the ask then follows.  Fire and forget - the
		//activation's and the reconnect's Check, so a second client of the slug costs one read, not a walk (#13).
		α Fetch( sp<UAClient> client, EFetch kind )ι->void{ Start( move(client), nullptr, kind ); }
		//A walk, awaited:  resumes once a walk begun after the ask has completed - the one this starts, or the one following the fetch
		//running, which may have browsed past a type added since - with its failure where it failed.  Any family, so a QL await's
		//coroutine may co_await it.  The dataType query's.
		struct FetchAwait final : AnyVoidAwait{
			FetchAwait( sp<TypeRegistry> registry, sp<UAClient> client, SRCE )ι:AnyVoidAwait{sl}, _registry{move(registry)}, _client{move(client)}{}
		protected:
			α Suspend()ι->void override{ _registry->Start( move(_client), this, EFetch::Walk ); }
		private:
			sp<TypeRegistry> _registry; sp<UAClient> _client;
		};
		α Fetched( sp<UAClient> client, SRCE )ι->FetchAwait{ return { shared_from_this(), move(client), sl }; }
		//Resumes once the fetch running has ended, at once when none runs, never with its failure:  a query reading values waits out the
		//activation's walk, so the read that caused a cold connect decodes by what it fetched (#7).
		struct SettledAwait final : AnyVoidAwait{
			SettledAwait( sp<TypeRegistry> registry, SRCE )ι:AnyVoidAwait{sl}, _registry{move(registry)}{}
			α await_ready()ι->bool override{ return !_registry->Fetching(); }
		protected:
			α Suspend()ι->void override{ _registry->Join( this ); }
		private:
			sp<TypeRegistry> _registry;
		};
		α Settled( SRCE )ι->SettledAwait{ return { shared_from_this(), sl }; }
		α Find( const UA_NodeId& typeId )Ι->const UA_DataType*{ return FindDataType( typeId, this ); }//by DataType id:  UA_TYPES', else the chain's.
		//A DataType id a completed walk asked by the dataType query did not explain - no structure the server defines, or one that would not
		//build:  never walked for again (#10), until the namespaces move.
		α Unexplained( const UA_NodeId& typeId )Ι->bool;
		α NoteUnexplained( const UA_NodeId& typeId )ι->void;
		α Types()Ι->vector<const UA_DataType*>;//every type the chain holds, the newest array first.
		α Count()Ι->uint;
		α Fetches()Ι->uint{ rl _{_mutex}; return _fetches; }//fetches completed, failed ones included - what a test waits on.
		α Walks()Ι->uint{ rl _{_mutex}; return _walks; }//walks of the subtree completed, failed ones included.
		α Fetching()Ι->bool{ rl _{_mutex}; return _fetching; }
		α Slug()Ι->const ServerCnnctnNK&{ return _slug; }
		α Row()Ι->ServerCnnctnPK{ return _row; }//the server_connections row this was made for:  a purge by id drops it (UAClient::ConnectionEdited).
		constexpr static std::chrono::seconds RetryAfter{ 60 };//after a failed fetch, before an id Missing noted may ask again.
	private:
		α Start( sp<UAClient>&& client, AnyVoidAwait* waiter, EFetch kind )ι->void;
		α Join( AnyVoidAwait* waiter )ι->void;
		//The fetch:  the NamespaceArray read and compared, then - a walk asked, none done yet, or the namespaces moved - the Structure
		//subtree browsed level by level for the DataTypes the chain lacks, their definitions read and built against it (DataTypesAwait),
		//the array joined to the chain, and every client of this registry re-pointed at the chain on its strand.  `self` holds this through
		//the awaits.
		α Run( sp<TypeRegistry> self, sp<UAClient> client, EFetch kind )ι->VoidTask;
		//The browse of one level of the subtree, in as few requests as the server takes, each node's references followed past the server's
		//cap (BrowseNext):  `visit` gets each subtype reference.
		α BrowseLevel( const vector<NodeId>& level, sp<UAClient> client, uint& batch, function<void(const UA_ReferenceDescription&)> visit )ι->AnyVoidTask;
		α Append( DataTypeArrayPtr&& types )ι->uint;//the array as a new node ahead of the chain;  how many it added.
		α Finish( up<Exception> error )ι->void;//the fetch's end:  its waiters resumed, the fetch asked meanwhile started.
		const ServerCnnctnNK _slug;
		const ServerCnnctnPK _row;
		std::atomic<UA_DataTypeArray*> _head{};
		mutable absl::Mutex _mutex;
		vector<UA_DataTypeArray*> _retired ABSL_GUARDED_BY(_mutex);//the chains a namespace move set aside:  a value decoded against one may still be on its way.
		vector<string> _namespaces ABSL_GUARDED_BY(_mutex);//the NamespaceArray the chain was built under.
		bool _fetching ABSL_GUARDED_BY(_mutex){};
		bool _walked ABSL_GUARDED_BY(_mutex){};//a walk has succeeded since the chain began.
		sp<UAClient> _again ABSL_GUARDED_BY(_mutex);//the fetch asked while one ran, with the client to run it through.
		EFetch _againKind ABSL_GUARDED_BY(_mutex){ EFetch::Check };
		vector<AnyVoidAwait*> _waiters ABSL_GUARDED_BY(_mutex);//resumed by the fetch running.
		vector<AnyVoidAwait*> _nextWaiters ABSL_GUARDED_BY(_mutex);//resumed by the one after it, _again.
		mutable flat_set<NodeId> _missing ABSL_GUARDED_BY(_mutex);//the encoding ids values named that started a walk:  each once, unless it failed.
		mutable std::chrono::steady_clock::time_point _retryAt ABSL_GUARDED_BY(_mutex);//before which a failed fetch's ids don't ask again.
		flat_set<NodeId> _unexplained ABSL_GUARDED_BY(_mutex);
		uint _fetches ABSL_GUARDED_BY(_mutex){};
		uint _walks ABSL_GUARDED_BY(_mutex){};
		constexpr static uint MaxDepth{ 64 }, MaxNodes{ 4096 };//open62541's own limits on the subtree (ua_client_util.c).
	};
}