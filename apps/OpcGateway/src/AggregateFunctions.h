#pragma once
#include <jde/fwk/co/AnyAwait.h>
#include <absl/synchronization/mutex.h>
#include <jde/opc/uatypes/NodeId.h>

namespace Jde::Opc::Gateway{
	struct UAClient;
	//The aggregates an aggregate `history` names (spec *Pass-through*):  Part 13's by name, each mapped to its standard
	//AggregateFunction object in namespace 0, and any other, OpcServer's Median, found by browse name in the server's
	//HistoryServerCapabilities/AggregateFunctions folder, which is browsed on first use and kept for the connection's life,
	//so each client reads it once.  Owned by the UAClient, so a disconnect/TTL drop discards it with the client.
	struct AggregateFunctions final : noncopyable{
		using Folder = flat_map<string,NodeId>;//by browse name.
		using Ptr = sp<const Folder>;
		//Part 13's aggregate, by the browse name of its object in namespace 0, which needs no browse.
		Ω Part13( sv name )ι->optional<NodeId>;

		//AnyAwait, not TAwait:  the read that needs the folder is a TAwait<HistoryReadResponse>::Task and may only co_await
		//its own family (the pairing rule, CLAUDE.md);  the Any family carries its own storage, so a read folder pre-completes
		//in await_ready.
		struct GetAwait final : AnyAwait<Ptr>{
			GetAwait( AggregateFunctions& cache, sp<UAClient> client, SRCE )ι:AnyAwait<Ptr>{sl}, _cache{cache}, _client{move(client)}{}
			α await_ready()ι->bool override;
		protected:
			α Suspend()ι->void override;
		private:
			AggregateFunctions& _cache; sp<UAClient> _client;
		};
		α Get( sp<UAClient> client, SRCE )ι->GetAwait{ return GetAwait{*this, move(client), sl}; }
		α Find()Ι->Ptr{ rl _{_mutex}; return _folder; }//the folder once read, else null.
	private:
		α Start( sp<UAClient>&& client, GetAwait* waiter )ι->void;
		α Fetch( sp<UAClient> client )ι->VoidTask;//the one browse;  its co_await is Any-wrapped, so the task type is free.
		α Finish( Ptr folder, up<Exception> error )ι->void;

		mutable absl::Mutex _mutex;
		Ptr _folder ABSL_GUARDED_BY(_mutex);
		bool _fetching ABSL_GUARDED_BY(_mutex){};
		vector<GetAwait*> _waiters ABSL_GUARDED_BY(_mutex);//parked on the in-flight browse;  resumed by Finish.
	};
}