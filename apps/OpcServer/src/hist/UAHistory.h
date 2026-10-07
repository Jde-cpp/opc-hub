#pragma once
#include <span>
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/synchronization/mutex.h>
#include <jde/historian/Historian.h>
#include <jde/opc/uatypes/NodeId.h>

namespace Jde::Opc::Server{
	//OpcServer's historian (historian spec, *OpcServer* and *UA backend*):  the history of the variables its nodesets mark
	//Historizing, in one group, `server`, collected through open62541's setValue and served through its HistoryRead.
	//open62541 calls the backend inside its service lock, on the server's one thread, so collecting only enqueues and
	//a node's read ends at maxReturnDataValues values or one day file, with a continuation point.  That point is the
	//library's stateless continuation, so the server holds nothing between calls and has none to release.
	struct UAHistory final : noncopyable{
		//Reads /opcServer/hist and takes the lock on its path.  With no such block OpcServer keeps no history, as when
		//another process holds the lock:  no backend is installed and no node is changed.
		UAHistory()ε;
		~UAHistory();//after the server is gone, while the executor still runs:  writes what is buffered.
		α Enabled()Ι->bool{ return _historian && _historian->Enabled(); }
		//UAConfig's:  the UA_HistoryDatabase, with its context set, and maxReturnDataValues, hist.readLimit.
		α Database()ι->UA_HistoryDatabase;
		α ReadLimit()Ι->UA_UInt32;
		//UAConfig's too, the server's node lifecycle:  while the nodesets load it notes each HA Configuration that
		//open62541 gives the type's Stepped, false, for want of one its nodeset declares, which Load makes the default, true.
		Ω Lifecycle()ι->UA_GlobalNodeLifecycle*;
		α Instantiated( const UA_NodeId& configuration )ι->void;
		//After the last nodeset and before Run:  makes the group of the variables marked Historizing (Historized::Load),
		//which keeps the indexes its files hold and removes a node the nodesets no longer historize, takes each one's
		//current value as its first after the stop, and writes StartOfArchive, now and after each midnight.
		α Load( UA_Server& ua )ε->void;
		α Stop()ι->void;//before the server is deleted:  the midnight timer writes to it.
		α Group()Ι->sp<Hist::Group>{ return _group; }
		α Find( const UA_NodeId& node )Ι->optional<Hist::NodeIndex>;

		//How long open62541's callbacks ran, which is how long each held the service lock.
		struct Timing final{ uint Count{}; steady_clock::duration Total{}; steady_clock::duration Longest{}; };
		α Reads()Ι->Timing{ return _reads.Get(); }
		α Collections()Ι->Timing{ return _collections.Get(); }

		//open62541's callbacks.
		α Collect( UA_Server& ua, const UA_NodeId& node, const UA_DataValue& value )ι->void;
		α ReadRaw( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, const UA_ReadRawModifiedDetails& details, UA_TimestampsToReturn timestamps, bool release,
			std::span<const UA_HistoryReadValueId> nodes, UA_HistoryReadResult* results, UA_HistoryData* const* data )ι->void;
	private:
		struct Timer final{
			α Add( steady_clock::duration elapsed )ι->void;
			α Get()Ι->Timing;
		private:
			std::atomic<uint> _count{};
			std::atomic<steady_clock::rep> _total{}, _longest{};
		};
		struct NodeHash final{
			using is_transparent = void;
			α operator()( const UA_NodeId& node )Ι->size_t{ return UA_NodeId_hash( &node ); }
		};
		struct NodeEqual final{
			using is_transparent = void;
			α operator()( const UA_NodeId& a, const UA_NodeId& b )Ι->bool{ return UA_NodeId_equal( &a, &b ); }
		};
		//One node's page.  Read on the node is the right, as the node-access page grants it.
		α Read( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, const UA_ReadRawModifiedDetails& details, UA_TimestampsToReturn timestamps,
			const UA_HistoryReadValueId& node, UA_HistoryData& data, UA_ByteString& continuation )ι->UA_StatusCode;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_publishing->Mutex) α PublishArchive()ι->void;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_publishing->Mutex) α ScheduleMidnight()ι->void;

		up<Hist::Historian> _historian;
		//Set by Load, before the server runs, and only read after:  the callbacks take no lock of the host's.
		sp<Hist::Group> _group;
		absl::flat_hash_map<NodeId,Hist::NodeIndex,NodeHash,NodeEqual> _indexes;
		absl::flat_hash_set<NodeId,NodeHash,NodeEqual> _typeStepped;//until Load:  open62541 adds a node under its service lock.
		vector<NodeId> _archiveStarts;//each HA Configuration's StartOfArchive and StartOfOnlineArchive.
		Timer _reads, _collections;
		//Held by the midnight timer's callback too, which the clock can start while Stop cancels it:  Stop nulls Ua under
		//Mutex, and a callback that finds it null touches nothing of this.  Mutex goes before open62541's service lock,
		//which no callback takes it under.
		struct Publishing final{
			absl::Mutex Mutex;
			UA_Server* Ua ABSL_GUARDED_BY(Mutex){};
			Hist::IClock::TimerId Midnight ABSL_GUARDED_BY(Mutex){};
		};
		const sp<Publishing> _publishing{ ms<Publishing>() };
	};
}