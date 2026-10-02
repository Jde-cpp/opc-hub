#pragma once
#include <deque>
#include <absl/synchronization/mutex.h>
#include <jde/fwk/co/AnyAwait.h>
#include <jde/opc/uatypes/NodeId.h>
#include "uatypes/Browse.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//The per-connection name index behind the `search` query (ql/SearchQLAwait).  The gateway persists no node names and OPC UA has
	//no search service, so the first search crawls the hierarchy under Objects once (caps: /gateway/search/maxDepth|maxNodes,
	//the ns=0 Server subtree only with includeServer) and later searches match in memory.  Owned by the UAClient, so a
	//disconnect/TTL drop discards it with the client;  `refresh:true` on the query rebuilds it.
	struct NodeIndex final : noncopyable{
		struct Entry{
			NodeId Id;
			string Path;	//browse-path segments from Objects, the UABrowsePath/NodeRoute convention:  `name` in the connection's default browse ns, else `<ns>~<name>`.
			string Name;	//displayName text.
			string Browse;	//browseName, original case.
			string NameLower, BrowseLower;
			NsIndex BrowseNs{};
			UA_NodeClass Class{};
			uint8 Depth{};
			uint8 Rank{};	//set by Search:  0 name starts with the text, 1 browse name does, 2 either contains it.
		};
		enum class EState : uint8{ Empty, Crawling, Ready, Failed };

		//AnyVoidAwait, not TAwait:  SearchQLAwait::Query is a TAwait<jvalue>::Task and may only co_await its own family (the
		//pairing rule, CLAUDE.md);  the Any family carries its own storage, so it can also pre-complete when the index is built.
		struct ReadyAwait final : AnyVoidAwait{
			ReadyAwait( NodeIndex& index, sp<UAClient> client, bool refresh, SRCE )ι:AnyVoidAwait{sl}, _index{index}, _client{move(client)}, _refresh{refresh}{}
			α await_ready()ι->bool override;
		protected:
			α Suspend()ι->void override;
		private:
			NodeIndex& _index; sp<UAClient> _client; bool _refresh;
		};
		α Ready( sp<UAClient> client, bool refresh, SRCE )ι->ReadyAwait{ return ReadyAwait{*this, move(client), refresh, sl}; }
		α Start( sp<UAClient> client, bool refresh )ι->void{ StartLocked( move(client), refresh, nullptr ); }	//kicks off a crawl if one is needed, without waiting - a fan-out starts every connection's crawl, then awaits each.
		α Search( sv textLower, uint limit )Ι->vector<Entry>;	//sorted (Rank, Depth, Name), at most `limit` (0 = all).
		α State()Ι->EState{ rl _{_mutex}; return _state; }
		α Size()Ι->size_t{ rl _{_mutex}; return _entries.size(); }
		α Truncated()Ι->bool{ rl _{_mutex}; return _truncated; }
	private:
		α StartLocked( sp<UAClient>&& client, bool refresh, AnyVoidAwait* waiter )ι->void;
		α Crawl( sp<UAClient> client )ι->TAwait<Browse::Response>::Task;	//co_awaits only Browse::FoldersAwait - one awaitable type per coroutine.
		α Finish( vector<Entry>&& entries, bool truncated, up<Exception> error )ι->void;

		vector<Entry> _entries ABSL_GUARDED_BY(_mutex);
		up<Exception> _error ABSL_GUARDED_BY(_mutex);	//a Failed crawl's error, answered for _failedHold without crawling again - keystrokes must not each re-crawl a refusing server.
		steady_clock::time_point _failedAt ABSL_GUARDED_BY(_mutex);
		vector<AnyVoidAwait*> _waiters ABSL_GUARDED_BY(_mutex);	//parked on the in-flight crawl;  resumed by Finish.
		EState _state ABSL_GUARDED_BY(_mutex){ EState::Empty };
		bool _truncated ABSL_GUARDED_BY(_mutex){};
		mutable absl::Mutex _mutex;
	};
}
