#pragma once
#include <span>
#include <open62541/server.h>
#include <absl/container/flat_hash_set.h>
#include <absl/functional/function_ref.h>
#include <absl/synchronization/mutex.h>
#include "IHistoryHost.h"

namespace Jde::Opc::Server{
	struct UASettings;
	//A UA server's Part 11 backend (historian spec, *OpcServer* and *UA backend*):  the history of the host's variables in
	//the host's groups, which it asks IHistoryHost for - the OpcServer's one `server` group of what its nodesets mark
	//Historizing, collected through open62541's setValue, the gateway's one per served node, stored by its collector -
	//served through HistoryRead, raw, modified, at times and aggregated, and edited through HistoryUpdate.  open62541
	//calls the backend inside its service lock, on the server's one thread, so collecting only enqueues and a node's read
	//ends at maxReturnDataValues values or one day, with a continuation point.  That point is the library's stateless
	//continuation, so the server holds nothing between calls and has none to release.  An edit is acknowledged once its
	//records are durable, so its callback waits for the flush that writes them, holding the lock for the write and its
	//fsync, up to UASettings::EditTimeout.
	struct UAHistory final : noncopyable{
		//Over the host's historian, read here:  with none, or a null host, the server keeps no history, as when another
		//process holds the lock on its path:  no backend is installed and no node is changed.
		UAHistory( const UASettings& settings, sp<IHistoryHost> host )ε;
		//After the server is gone:  lets go of its shares of the host and its historian.  The historian writes what is
		//buffered at its end, which is the host owner's to choose.
		~UAHistory();
		α Enabled()Ι->bool{ return _historian && _historian->Enabled(); }
		//UAConfig's:  the UA_HistoryDatabase, with its context set, and maxReturnDataValues, hist.readLimit.
		α Database()ι->UA_HistoryDatabase;
		α ReadLimit()Ι->UA_UInt32;
		//UAConfig's too, the server's node lifecycle:  while the nodesets load it notes each HA Configuration that
		//open62541 gives the type's Stepped, false, for want of one its nodeset declares, which the host's load makes the
		//default, true.
		Ω Lifecycle()ι->UA_GlobalNodeLifecycle*;
		α Instantiated( const UA_NodeId& configuration )ι->void;
		α TypeStepped( const UA_NodeId& configuration )Ι->bool;//whether the HA Configuration's Stepped is the type's, until Start.
		//After the host's nodes are historized and before Run, while Enabled:  writes StartOfArchive to the host's archive
		//starts, now and after each midnight, and lists the aggregates served in HistoryServerCapabilities'
		//AggregateFunctions folder, Median among them.
		α Start( UA_Server& ua )ε->void;
		α Stop()ι->void;//before the server is deleted:  the midnight timer writes to it.
		//A node historized between Start and Stop - the gateway's, joining a group while the endpoint runs:  its archive
		//starts written now, as Start wrote the rest's.  Nothing outside that span, which Start's own write covers.
		α Publish( std::span<const IHistoryHost::ArchiveStart> starts )ι->void;
		//The aggregate an AggregateFunction node asks for (spec *Reads*):  Part 13's nine in namespace 0, and Median,
		//which Part 13 doesn't define, so it is an AggregateFunctionType object of the server's own namespace, ns=1;s=_Median,
		//named so no gateway connection folder, ns=1;s=<slug>, can take its id.
		//None for a function that isn't served.
		Ω Aggregate( const UA_NodeId& function )ι->optional<Hist::EAggregate>;
		Ω Median()ι->NodeId;

		//How long open62541's callbacks ran, which is how long each held the service lock.
		struct Timing final{ uint Count{}; steady_clock::duration Total{}; steady_clock::duration Longest{}; };
		α Reads()Ι->Timing{ return _reads.Get(); }
		α Returned()Ι->uint{ return _returned.load(); }//the values the reads returned - for tests and diagnostics.
		α Collections()Ι->Timing{ return _collections.Get(); }
		α Edits()Ι->Timing{ return _edits.Get(); }

		//open62541's callbacks, which a TimestampsToReturn of Neither never reaches:  the service answers it with
		//Bad_TimestampsToReturnInvalid and no results, as Part 4 §5.11.3 says.
		α Collect( UA_Server& ua, const UA_NodeId& node, const UA_DataValue& value )ι->void;
		//The raw values, or with UA_HistoryModifiedData the modified values with their ModificationInfo (spec *Reads*), over
		//the range a raw read takes.
		Ŧ ReadRawModified( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, const UA_ReadRawModifiedDetails& details, UA_TimestampsToReturn timestamps, bool release,
			std::span<const UA_HistoryReadValueId> nodes, UA_HistoryReadResult* results, T* const* data )ι->void;
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
		//of the variable would refuse for its type or dimensions answers Bad_TypeMismatch, and one holding a namespace index
		//the host can't map back answers Bad_NodeIdInvalid (IHistoryHost::Unmap).  A
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
		struct EditTimer;//an edit's time under the service lock, into _edits and the log, however it returns.
		using Node = IHistoryHost::Node;
		//What every mode asks of a node first:  Read on the node for the session's user, the right as the node-access page
		//grants it, and a history, which a node has when it is historized:  its group and its index there, from the host.
		//None with the status that says why.  range is the node's index range, parsed, whose dimensions the caller frees.
		α Admit( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, const UA_HistoryReadValueId& node, UA_NumericRange& range, UA_StatusCode& status )Ι->optional<Node>;
		//Every node's page, each read by read, the mode's own, once Admit lets it, and put into its result by fill:  the
		//node's position, its page and the part of an array its index range names.  what names the mode in the log.
		α Serve( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, bool release,
			std::span<const UA_HistoryReadValueId> nodes, UA_HistoryReadResult* results, sv what,
			absl::FunctionRef<UA_StatusCode( uint, const Node&, const UA_HistoryReadValueId&, UA_ByteString&, Hist::ReadResult& )> read,
			absl::FunctionRef<UA_StatusCode( uint, const Hist::ReadResult&, const UA_NumericRange* )> fill )ι->void;
		//One node's page and continuation point in each mode, Good_NoData for a read none of whose pages held a value.
		α Read( const Node& node, const UA_ReadRawModifiedDetails& details, const UA_HistoryReadValueId& id, UA_ByteString& continuation, Hist::ReadResult& page )ι->UA_StatusCode;
		α AtTime( const Node& node, const UA_ReadAtTimeDetails& details, const UA_HistoryReadValueId& id, UA_ByteString& continuation, Hist::ReadResult& page )ι->UA_StatusCode;
		α Processed( const Node& node, const UA_ReadProcessedDetails& details, const UA_NodeId& function, const UA_HistoryReadValueId& id, UA_ByteString& continuation, Hist::ReadResult& page )ι->UA_StatusCode;
		//One entry's edit, once the session's user may write the node's history:  make builds the library's entry from the
		//node's index, or none to leave result as it set it.  The edit runs in the group's next flush, which this waits for,
		//up to _editTimeout:  then Bad_Timeout, the edit still queued.  None when result says why:  in its status, or, for
		//an UpdateData whose every value make refused, in its operationResults, the status staying Good.
		α Edit( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, const UA_NodeId& node, UA_HistoryUpdateResult& result,
			absl::FunctionRef<optional<Hist::EditDetails>( Hist::NodeIndex )> make )ι->optional<Hist::EditResult>;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_publishing->Mutex) α PublishArchive()ι->void;//every archive start the host has.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_publishing->Mutex) α Write( std::span<const IHistoryHost::ArchiveStart> starts )ι->void;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_publishing->Mutex) α ScheduleMidnight()ι->void;

		//The host answers the callbacks under the service lock, so it takes no lock a UA_Server_* call is made under (spec
		//*Threading*):  the OpcServer's host sets its map before the server runs and only reads it after.
		sp<IHistoryHost> _host;
		sp<Hist::Historian> _historian;//the host's, read once:  null keeps no history.
		Duration _editTimeout;
		bool _started{};
		absl::flat_hash_set<NodeId,NodeHash,NodeEqual> _typeStepped;//until Start:  open62541 adds a node under its service lock.
		Timer _reads, _collections, _edits;
		std::atomic<uint> _returned{};
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