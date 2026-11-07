#pragma once
#include <absl/synchronization/mutex.h>
#include <jde/fwk/co/AnyAwait.h>
#include <jde/opc/uatypes/NodeId.h>

namespace Jde::Opc::Gateway{
	struct UAClient;
	using DisplayNames = flat_map<NodeId,string>;
	//The per-connection DisplayNames of the nodes a `references` row names beside its target - the reference type and the type
	//definition (ql/NodeQLAwait) - read on first use, in one batch for the ids not yet seen, and kept for the connection's life, as
	//the enumeration definitions are (EnumTypeCache).  Owned by the UAClient, so a disconnect/TTL drop discards it with the client.
	struct DisplayNameCache final : noncopyable{
		//AnyAwait, as LazyCache's:  the callers' coroutines have task types of their own, and a batch the cache holds every id of
		//pre-completes.  Answers the names of `ids` the cache holds once the batch is read - an id the server answered with a status,
		//or one in a read it refused, has none, and is asked again next time.  Never throws:  the names are decoration.
		struct GetAwait final : AnyAwait<DisplayNames>{
			GetAwait( DisplayNameCache& cache, sp<UAClient> client, vector<NodeId> ids, SRCE )ι:AnyAwait<DisplayNames>{sl}, _cache{cache}, _client{move(client)}, _ids{move(ids)}{}
			α await_ready()ι->bool override;
		private:
			α Suspend()ι->void override{ Fetch(); }
			α Fetch()ι->VoidTask;
			DisplayNameCache& _cache; sp<UAClient> _client; vector<NodeId> _ids;
			vector<NodeId> _missing;//what await_ready found unread, for Fetch.
		};
		α Get( sp<UAClient> client, vector<NodeId> ids, SRCE )ι->GetAwait{ return GetAwait{*this, move(client), move(ids), sl}; }
		α Find( const vector<NodeId>& ids, vector<NodeId>* missing=nullptr )Ι->DisplayNames;//the names held of `ids`;  `missing` takes the rest, in order.
		α Erase( const NodeId& id )ι->void;//a name this client wrote (NodeQLMAwait):  read again on next use.  Another client's write is never seen.
		α Size()Ι->uint{ rl _{_mutex}; return _names.size(); }
	private:
		α Add( DisplayNames&& names )ι->void;
		mutable absl::Mutex _mutex;
		DisplayNames _names ABSL_GUARDED_BY(_mutex);
	};
}