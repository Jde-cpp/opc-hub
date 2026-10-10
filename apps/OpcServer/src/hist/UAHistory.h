#pragma once
#include <span>
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/functional/function_ref.h>
#include <absl/synchronization/mutex.h>
#include <jde/historian/Historian.h>
#include <jde/opc/uatypes/NodeId.h>

namespace Jde::Opc::Server{
	//OpcServer's historian (historian spec, *OpcServer* and *UA backend*):  the history of the variables its nodesets mark
	//Historizing, in one group, `server`, collected through open62541's setValue, served through its HistoryRead, raw,
	//modified, at times and aggregated, and edited through its HistoryUpdate.  open62541 calls the backend inside its
	//service lock, on the server's one thread, so collecting only enqueues and a node's read ends at maxReturnDataValues
	//values or one day, with a continuation point.  That point is the library's stateless
	//continuation, so the server holds nothing between calls and has none to release.  An edit is acknowledged once its
	//records are durable, so its callback waits for the flush that writes them, holding the lock for the write and its
	//fsync, up to hist.editTimeout.
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
		//current value as its first after the stop, writes StartOfArchive, now and after each midnight, and lists the
		//aggregates served in HistoryServerCapabilities' AggregateFunctions folder, Median among them.
		α Load( UA_Server& ua )ε->void;
		α Stop()ι->void;//before the server is deleted:  the midnight timer writes to it.
		α Group()Ι->sp<Hist::Group>{ return _group; }
		α Find( const UA_NodeId& node )Ι->optional<Hist::NodeIndex>;
		//The aggregate an AggregateFunction node asks for (spec *Reads*):  Part 13's nine in namespace 0, and Median,
		//which Part 13 doesn't define, so it is an AggregateFunctionType object of the server's own namespace, ns=1;s=Median.
		//None for a function that isn't served.
		Ω Aggregate( const UA_NodeId& function )ι->optional<Hist::EAggregate>;
		Ω Median()ι->NodeId;

		//How long open62541's callbacks ran, which is how long each held the service lock.
		struct Timing final{ uint Count{}; steady_clock::duration Total{}; steady_clock::duration Longest{}; };
		α Reads()Ι->Timing{ return _reads.Get(); }
		α Collections()Ι->Timing{ return _collections.Get(); }
		α Edits()Ι->Timing{ return _edits.Get(); }

		//open62541's callbacks, which a TimestampsToReturn of Neither never reaches:  the service answers it with
		//Bad_TimestampsToReturnInvalid and no results, as Part 4 §5.11.3 says.
		α Collect( UA_Server& ua, const UA_NodeId& node, const UA_DataValue& value )ι->void;
		α ReadRaw( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, const UA_ReadRawModifiedDetails& details, UA_TimestampsToReturn timestamps, bool release,
			std::span<const UA_HistoryReadValueId> nodes, UA_HistoryReadResult* results, UA_HistoryData* const* data )ι->void;
		//The modified values with their ModificationInfo (spec *Reads*), over the range a raw read takes.
		α ReadModified( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, const UA_ReadRawModifiedDetails& details, UA_TimestampsToReturn timestamps, bool release,
			std::span<const UA_HistoryReadValueId> nodes, UA_HistoryReadResult* results, UA_HistoryModifiedData* const* data )ι->void;
		//Part 11's ReadAtTime (spec *OpcServer*):  each node's value at each requested time, in the request's order.
		α ReadAtTime( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, const UA_ReadAtTimeDetails& details, UA_TimestampsToReturn timestamps, bool release,
			std::span<const UA_HistoryReadValueId> nodes, UA_HistoryReadResult* results, UA_HistoryData* const* data )ι->void;
		//Part 11's ReadProcessed (spec *OpcServer*):  each node's aggregate over the range's intervals, the one its
		//aggregateType entry names, by position.
		α ReadProcessed( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, const UA_ReadProcessedDetails& details, UA_TimestampsToReturn timestamps, bool release,
			std::span<const UA_HistoryReadValueId> nodes, UA_HistoryReadResult* results, UA_HistoryData* const* data )ι->void;
		//Part 11's HistoryUpdate on one node (spec *Edits*), by the session's user, who needs Update, or Delete, on the
		//node granted on a resource that is enforced (spec *Authorization*):  the entry is refused with Bad_UserAccessDenied
		//where none governs the node, and where the node's own AccessLevel lacks HistoryWrite.  Each value of an UpdateData
		//is asked for on its own, as the default plugin asks, and a refused one answers that in its place, as one a Write
		//of the variable would refuse for its type or dimensions answers Bad_TypeMismatch.  A
		//DeleteRawModified's range holds its start and not its end, as a read's does, startTime equal to endTime is that
		//instant, and a time of 0 is one not given, which a delete needs both of; deleting the modified values is refused,
		//since they are the audit trail.
		α UpdateData( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, const UA_UpdateDataDetails& details, UA_HistoryUpdateResult& result )ι->void;
		α DeleteRawModified( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, const UA_DeleteRawModifiedDetails& details, UA_HistoryUpdateResult& result )ι->void;
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
		//What every mode asks of a node first:  Read on the node for the session's user, the right as the node-access page
		//grants it, and a history, which a node has when it is historized:  its index in the group.  None with the status
		//that says why.  range is the node's index range, parsed, whose dimensions the caller frees.
		α Admit( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, const UA_HistoryReadValueId& node, UA_NumericRange& range, UA_StatusCode& status )Ι->optional<Hist::NodeIndex>;
		//Every node's page, each read by read, the mode's own, once Admit lets it, and put into its result by fill:  the
		//node's position, its page and the part of an array its index range names.  what names the mode in the log.
		α Serve( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, bool release,
			std::span<const UA_HistoryReadValueId> nodes, UA_HistoryReadResult* results, sv what,
			absl::FunctionRef<UA_StatusCode( uint, Hist::NodeIndex, const UA_HistoryReadValueId&, UA_ByteString&, Hist::ReadResult& )> read,
			absl::FunctionRef<UA_StatusCode( uint, const Hist::ReadResult&, const UA_NumericRange* )> fill )ι->void;
		//One node's page and continuation point in each mode, Good_NoData for a read none of whose pages held a value.
		α Read( Hist::NodeIndex index, const UA_ReadRawModifiedDetails& details, const UA_HistoryReadValueId& id, UA_ByteString& continuation, Hist::ReadResult& page )ι->UA_StatusCode;
		α AtTime( Hist::NodeIndex index, const UA_ReadAtTimeDetails& details, const UA_HistoryReadValueId& id, UA_ByteString& continuation, Hist::ReadResult& page )ι->UA_StatusCode;
		α Processed( Hist::NodeIndex index, const UA_ReadProcessedDetails& details, const UA_NodeId& function, const UA_HistoryReadValueId& id, UA_ByteString& continuation, Hist::ReadResult& page )ι->UA_StatusCode;
		//One entry's edit, once the session's user may write the node's history:  make builds the library's entry from the
		//node's index, or none to leave result as it set it.  The edit runs in the group's next flush, which this waits for,
		//up to _editTimeout:  then Bad_Timeout, the edit still queued.  None when result says why:  in its status, or, for
		//an UpdateData whose every value make refused, in its operationResults, the status staying Good.
		α Edit( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, const UA_NodeId& node, UA_HistoryUpdateResult& result,
			absl::FunctionRef<optional<Hist::EditDetails>( Hist::NodeIndex )> make )ι->optional<Hist::EditResult>;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_publishing->Mutex) α PublishArchive()ι->void;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_publishing->Mutex) α ScheduleMidnight()ι->void;

		up<Hist::Historian> _historian;
		//hist.editTimeout, an ISO 8601 duration:  the default is open62541's client timeout, past which the client has given up.
		Duration _editTimeout{ 5s };
		//Set by Load, before the server runs, and only read after:  the callbacks take no lock of the host's.
		sp<Hist::Group> _group;
		absl::flat_hash_map<NodeId,Hist::NodeIndex,NodeHash,NodeEqual> _indexes;
		absl::flat_hash_set<NodeId,NodeHash,NodeEqual> _typeStepped;//until Load:  open62541 adds a node under its service lock.
		vector<NodeId> _archiveStarts;//each HA Configuration's StartOfArchive and StartOfOnlineArchive.
		Timer _reads, _collections, _edits;
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