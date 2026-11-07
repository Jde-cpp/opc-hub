#pragma once
#include <absl/synchronization/mutex.h>
#include <jde/fwk/co/AnyAwait.h>
#include <jde/opc/uatypes/NodeId.h>
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
	//lives is not refreshed, the spec's *Later*.  Filled after each activation, each reconnect, and once more, posted, when a value names an
	//encoding id the chain lacks (Missing);  one fetch at a time, a fetch asked while one runs following it.  Each array is a new node ahead
	//of the chain and never changes after, so a reader walks the chain off any thread;  cleanup is false on every one, so no client's config
	//clear frees them, and they are freed here once the last client holding this goes - a value decoded against the chain is serialized at
	//once (Value::ToJson) or re-encoded before it is kept (Variant::WithBuiltInTypes), so nothing outlives an array.
	struct TypeRegistry final : ITypes, std::enable_shared_from_this<TypeRegistry>, noncopyable{
		TypeRegistry( ServerCnnctnNK slug, ServerCnnctnPK row )ι:_slug{ move(slug) }, _row{ row }{}
		~TypeRegistry();
		α Chain()Ι->const UA_DataTypeArray* override{ return _head.load( std::memory_order_acquire ); }
		α Missing( const UA_NodeId& encodingId )Ι->void override;
		//A fetch of what the chain lacks, through `client`, one of this registry's:  started unless one runs, which the ask then follows.
		//Fire and forget - the activation's and the reconnect's.
		α Fetch( sp<UAClient> client )ι->void{ Start( move(client), nullptr ); }
		//The same, awaited:  resumes once a fetch has completed, the one running or the one this starts, with its failure where it failed.
		//Any family, so a QL await's coroutine may co_await it.
		struct FetchAwait final : AnyVoidAwait{
			FetchAwait( sp<TypeRegistry> registry, sp<UAClient> client, SRCE )ι:AnyVoidAwait{sl}, _registry{move(registry)}, _client{move(client)}{}
		protected:
			α Suspend()ι->void override{ _registry->Start( move(_client), this ); }
		private:
			sp<TypeRegistry> _registry; sp<UAClient> _client;
		};
		α Fetched( sp<UAClient> client, SRCE )ι->FetchAwait{ return { shared_from_this(), move(client), sl }; }
		α Find( const UA_NodeId& typeId )Ι->const UA_DataType*{ return FindDataType( typeId, this ); }//by DataType id:  UA_TYPES', else the chain's.
		α Types()Ι->vector<const UA_DataType*>;//every type the chain holds, the newest array first.
		α Count()Ι->uint;
		α Fetches()Ι->uint{ rl _{_mutex}; return _fetches; }//fetches completed, failed ones included - what a test waits on.
		α Fetching()Ι->bool{ rl _{_mutex}; return _fetching; }
		α Slug()Ι->const ServerCnnctnNK&{ return _slug; }
		α Row()Ι->ServerCnnctnPK{ return _row; }//the server_connections row this was made for:  a purge by id drops it (UAClient::ConnectionEdited).
	private:
		α Start( sp<UAClient>&& client, AnyVoidAwait* waiter )ι->void;
		//The fetch:  the Structure subtree browsed level by level for the DataTypes the chain lacks, their definitions read and built against it
		//(DataTypesAwait), the array joined to the chain, and every client of this registry re-pointed at the chain on its strand.  `self` holds
		//this through the awaits.
		α Run( sp<TypeRegistry> self, sp<UAClient> client )ι->VoidTask;
		α Append( DataTypeArrayPtr&& types )ι->uint;//the array as a new node ahead of the chain, less any type a fetch meanwhile added;  how many it added.
		α Finish( up<Exception> error )ι->void;//the fetch's end:  its waiters resumed, the fetch asked meanwhile started.
		const ServerCnnctnNK _slug;
		const ServerCnnctnPK _row;
		std::atomic<UA_DataTypeArray*> _head{};
		mutable absl::Mutex _mutex;
		bool _fetching ABSL_GUARDED_BY(_mutex){};
		sp<UAClient> _again ABSL_GUARDED_BY(_mutex);//the fetch asked while one ran, with the client to run it through.
		vector<AnyVoidAwait*> _waiters ABSL_GUARDED_BY(_mutex);
		mutable flat_set<NodeId> _missing ABSL_GUARDED_BY(_mutex);//the encoding ids values named that no fetch has explained:  each starts one fetch, once.
		uint _fetches ABSL_GUARDED_BY(_mutex){};
		constexpr static uint MaxDepth{ 64 }, MaxNodes{ 4096 };//open62541's own limits on the subtree (ua_client_util.c).
	};
}