#include "UAHistory.h"
#include <absl/synchronization/notification.h>
#include <open62541/plugin/nodestore.h>
#include <jde/fwk/process/process.h>
#include <jde/opc/uatypes/DateTime.h>
#include "Historized.h"
#include "../globals.h"
#include "../access/UAAccess.h"

#define let const auto
namespace Jde::Opc::Server{
	constexpr ELogTags _tags = ( ELogTags )EOpcLogTags::Opc;

	//A callback this long is said:  every client waits on the service lock meanwhile.
	constexpr steady_clock::duration SlowRead{ 100ms };

	Ω setValue( UA_Server* server, void* context, const UA_NodeId* /*sessionId*/, void* /*sessionContext*/, const UA_NodeId* nodeId, UA_Boolean historizing, const UA_DataValue* value )ι->void{
		if( historizing && nodeId && value )
			static_cast<UAHistory*>( context )->Collect( *server, *nodeId, *value );
	}
	Ω readRaw( UA_Server* server, void* context, const UA_NodeId* sessionId, void* sessionContext, const UA_RequestHeader* /*header*/, const UA_ReadRawModifiedDetails* details,
		UA_TimestampsToReturn timestamps, UA_Boolean release, size_t count, const UA_HistoryReadValueId* nodes, UA_HistoryReadResponse* response, UA_HistoryData* const* const data )ι->void{
		static_cast<UAHistory*>( context )->ReadRaw( *server, sessionId, sessionContext, *details, timestamps, release, {nodes, count}, response->results, data );
	}
	Ω readModified( UA_Server* server, void* context, const UA_NodeId* sessionId, void* sessionContext, const UA_RequestHeader* /*header*/, const UA_ReadRawModifiedDetails* details,
		UA_TimestampsToReturn timestamps, UA_Boolean release, size_t count, const UA_HistoryReadValueId* nodes, UA_HistoryReadResponse* response, UA_HistoryModifiedData* const* const data )ι->void{
		static_cast<UAHistory*>( context )->ReadModified( *server, sessionId, sessionContext, *details, timestamps, release, {nodes, count}, response->results, data );
	}
	Ω readAtTime( UA_Server* server, void* context, const UA_NodeId* sessionId, void* sessionContext, const UA_RequestHeader* /*header*/, const UA_ReadAtTimeDetails* details,
		UA_TimestampsToReturn timestamps, UA_Boolean release, size_t count, const UA_HistoryReadValueId* nodes, UA_HistoryReadResponse* response, UA_HistoryData* const* const data )ι->void{
		static_cast<UAHistory*>( context )->ReadAtTime( *server, sessionId, sessionContext, *details, timestamps, release, {nodes, count}, response->results, data );
	}
	Ω readProcessed( UA_Server* server, void* context, const UA_NodeId* sessionId, void* sessionContext, const UA_RequestHeader* /*header*/, const UA_ReadProcessedDetails* details,
		UA_TimestampsToReturn timestamps, UA_Boolean release, size_t count, const UA_HistoryReadValueId* nodes, UA_HistoryReadResponse* response, UA_HistoryData* const* const data )ι->void{
		static_cast<UAHistory*>( context )->ReadProcessed( *server, sessionId, sessionContext, *details, timestamps, release, {nodes, count}, response->results, data );
	}
	Ω updateData( UA_Server* server, void* context, const UA_NodeId* sessionId, void* sessionContext, const UA_RequestHeader* /*header*/, const UA_UpdateDataDetails* details, UA_HistoryUpdateResult* result )ι->void{
		static_cast<UAHistory*>( context )->UpdateData( *server, sessionId, sessionContext, *details, *result );
	}
	Ω deleteRawModified( UA_Server* server, void* context, const UA_NodeId* sessionId, void* sessionContext, const UA_RequestHeader* /*header*/, const UA_DeleteRawModifiedDetails* details, UA_HistoryUpdateResult* result )ι->void{
		static_cast<UAHistory*>( context )->DeleteRawModified( *server, sessionId, sessionContext, *details, *result );
	}

	Ω instantiated( UA_Server* server, const UA_NodeId* /*sessionId*/, void* /*sessionContext*/, const UA_NodeId* source, const UA_NodeId* parent, const UA_NodeId* /*referenceType*/, UA_NodeId* /*target*/ )ι->UA_StatusCode{
		const UA_NodeId stepped = UA_NODEID_NUMERIC( 0, UA_NS0ID_HISTORICALDATACONFIGURATIONTYPE_STEPPED );
		if( source && parent && UA_NodeId_equal(source, &stepped) )
			static_cast<UAHistory*>( UA_Server_getConfig(server)->historyDatabase.context )->Instantiated( *parent );
		return UA_STATUSCODE_GOOD;//the id is the server's to give.
	}
	//A record as HistoryRead returns it:  the timestamps asked for, and the part of an array the index range names.
	Ω toUA( const Hist::Proto::DataValue& stored, UA_TimestampsToReturn timestamps, const UA_NumericRange* range )ι->UA_DataValue{
		Value value{ (StatusCode)UA_STATUSCODE_BADDECODINGERROR };
		try{
			value = Hist::ToUA( stored );
		}
		catch( const Exception& ){//a value this build can't decode, with the times it was stored by.
			if( (value.hasSourceTimestamp = stored.has_source_ts()) )
				value.sourceTimestamp = stored.source_ts();
			if( (value.hasServerTimestamp = stored.has_server_ts()) )
				value.serverTimestamp = stored.server_ts();
		}
		if( timestamps==UA_TIMESTAMPSTORETURN_SOURCE ){
			value.hasServerTimestamp = value.hasServerPicoseconds = false;
			value.serverTimestamp = value.serverPicoseconds = 0;
		}
		else if( timestamps==UA_TIMESTAMPSTORETURN_SERVER ){
			value.hasSourceTimestamp = value.hasSourcePicoseconds = false;
			value.sourceTimestamp = value.sourcePicoseconds = 0;
		}
		if( range && value.hasValue ){
			UA_Variant part; UA_Variant_init( &part );
			let sc = UA_Variant_copyRange( &value.value, &part, *range );
			UA_Variant_clear( &value.value );
			value.value = part;
			if( sc ){
				value.hasValue = false;
				value.status = sc;
				value.hasStatus = true;
			}
		}
		UA_DataValue y = value;
		UA_DataValue_init( &value );
		return y;
	}

	namespace{
		struct IndexRange final{
			~IndexRange(){ UA_free( Value.dimensions ); }
			UA_NumericRange Value{};
		};
		//A page's values into HistoryRead's result, as toUA has them:  the dataValues a raw and a modified read both have.
		Ω dataValues( UA_DataValue*& values, size_t& valuesSize, const Hist::ReadResult& page, UA_TimestampsToReturn timestamps, const UA_NumericRange* range )ι->UA_StatusCode{
			let size = page.Values.size();
			if( !size )
				return UA_STATUSCODE_GOOD;
			values = (UA_DataValue*)UA_Array_new( size, &UA_TYPES[UA_TYPES_DATAVALUE] );
			if( !values )
				return UA_STATUSCODE_BADOUTOFMEMORY;
			valuesSize = size;
			for( uint i=0; i<size; ++i )
				values[i] = toUA( page.Values[i].Value, timestamps, range );
			return UA_STATUSCODE_GOOD;
		}
		Ω fill( UA_HistoryData& data, const Hist::ReadResult& page, UA_TimestampsToReturn timestamps, const UA_NumericRange* range )ι->UA_StatusCode{
			return dataValues( data.dataValues, data.dataValuesSize, page, timestamps, range );
		}
		//A modified read's, each value with its ModificationInfo:  the record's type is Part 11's HistoryUpdateType.
		static_assert( (int)Hist::Proto::UPDATE_TYPE_INSERT==UA_HISTORYUPDATETYPE_INSERT && (int)Hist::Proto::UPDATE_TYPE_REPLACE==UA_HISTORYUPDATETYPE_REPLACE
			&& (int)Hist::Proto::UPDATE_TYPE_UPDATE==UA_HISTORYUPDATETYPE_UPDATE && (int)Hist::Proto::UPDATE_TYPE_DELETE==UA_HISTORYUPDATETYPE_DELETE );
		Ω fill( UA_HistoryModifiedData& data, const Hist::ReadResult& page, UA_TimestampsToReturn timestamps, const UA_NumericRange* range )ι->UA_StatusCode{
			if( let sc = dataValues(data.dataValues, data.dataValuesSize, page, timestamps, range); sc || !data.dataValuesSize )
				return sc;
			let size = page.Values.size();
			data.modificationInfos = (UA_ModificationInfo*)UA_Array_new( size, &UA_TYPES[UA_TYPES_MODIFICATIONINFO] );
			if( !data.modificationInfos )
				return UA_STATUSCODE_BADOUTOFMEMORY;
			data.modificationInfosSize = size;
			for( uint i=0; i<size; ++i ){
				let& v = page.Values[i];
				ASSERT( v.Modification );//a modified read's values each carry one.
				if( !v.Modification )
					continue;
				auto& info = data.modificationInfos[i];
				info.modificationTime = v.Modification->Time;
				info.updateType = (UA_HistoryUpdateType)v.Modification->Type;
				info.userName = UA_String_fromChars( v.Modification->UserName.c_str() );
			}
			return UA_STATUSCODE_GOOD;
		}
		//Whether a value may be a variable's, as open62541 lets a Write of it:  of the DataType's built-in kind, which takes
		//a UtcTime's DateTime, or an enumeration's Int32 for it, any enumeration for the abstract Enumeration, and with the
		//dimensions the ValueRank allows.  An abstract or structured DataType's values are left as they come, open62541's
		//subtype check being internal.
		Ω typed( const UA_DataValue& value, const UA_DataType* type, UA_Int32 rank )ι->bool{
			let& v = value.value;
			if( !value.hasValue || UA_Variant_isEmpty(&v) )
				return true;
			if( type && type!=&UA_TYPES[UA_TYPES_VARIANT] && type!=&UA_TYPES[UA_TYPES_EXTENSIONOBJECT] ){
				let ofKind = type->typeKind==UA_DATATYPEKIND_ENUM
					? v.type==type || v.type==&UA_TYPES[UA_TYPES_INT32] || (type==&UA_TYPES[UA_TYPES_ENUMERATION] && v.type->typeKind==UA_DATATYPEKIND_ENUM)
					: type->typeKind>UA_DATATYPEKIND_DIAGNOSTICINFO || v.type->typeKind==type->typeKind;
				if( !ofKind )
					return false;
			}
			if( !v.data || rank<UA_VALUERANK_SCALAR_OR_ONE_DIMENSION || rank==UA_VALUERANK_ANY )
				return true;
			let dimensions = UA_Variant_isScalar( &v ) ? 0 : std::max<size_t>( v.arrayDimensionsSize, 1 );
			return rank==UA_VALUERANK_SCALAR_OR_ONE_DIMENSION ? dimensions<=1
				: rank==UA_VALUERANK_SCALAR ? dimensions==0
				: rank==UA_VALUERANK_ONE_OR_MORE_DIMENSIONS ? dimensions>=1
				: dimensions==(size_t)rank;
		}
		//A node's page in any mode, from the library, its continuation into the result's:  the status the read answers,
		//the library's own for a request it refuses, and Bad_InternalError for a file that can't be read, said where it
		//was found.
		Ω paged( absl::FunctionRef<Hist::ReadResult()> read, UA_ByteString& continuation, Hist::ReadResult& page )ι->UA_StatusCode{
			try{
				page = read();
				if( page.Continuation.size() ){
					if( let sc = UA_ByteString_allocBuffer(&continuation, page.Continuation.size()) )
						return sc;
					memcpy( continuation.data, page.Continuation.data(), page.Continuation.size() );
				}
				return page.NoData ? UA_STATUSCODE_GOODNODATA : UA_STATUSCODE_GOOD;
			}
			catch( const UAException& e ){
				return (UA_StatusCode)e.Code();
			}
			catch( const std::exception& ){
				return UA_STATUSCODE_BADINTERNALERROR;
			}
		}
		Ω continuationOf( const UA_HistoryReadValueId& id )ι->string{ return { (const char*)id.continuationPoint.data, id.continuationPoint.length }; }
		//Part 13's AggregateFunction objects, namespace 0's, for the aggregates served from it.
		constexpr std::array<std::pair<UA_UInt32,Hist::EAggregate>,9> Part13Aggregates{{
			{UA_NS0ID_AGGREGATEFUNCTION_INTERPOLATIVE, Hist::EAggregate::Interpolative}, {UA_NS0ID_AGGREGATEFUNCTION_AVERAGE, Hist::EAggregate::Average},
			{UA_NS0ID_AGGREGATEFUNCTION_TIMEAVERAGE, Hist::EAggregate::TimeAverage}, {UA_NS0ID_AGGREGATEFUNCTION_COUNT, Hist::EAggregate::Count},
			{UA_NS0ID_AGGREGATEFUNCTION_MINIMUM, Hist::EAggregate::Minimum}, {UA_NS0ID_AGGREGATEFUNCTION_MAXIMUM, Hist::EAggregate::Maximum},
			{UA_NS0ID_AGGREGATEFUNCTION_START, Hist::EAggregate::Start}, {UA_NS0ID_AGGREGATEFUNCTION_END, Hist::EAggregate::End},
			{UA_NS0ID_AGGREGATEFUNCTION_STANDARDDEVIATIONSAMPLE, Hist::EAggregate::StandardDeviationSample} }};
		//HistoryServerCapabilities' AggregateFunctions folder organizes what is served:  Part 13's objects, which
		//namespace 0 holds unreferenced, and Median, added as an AggregateFunctionType object of the server's own namespace.
		Ω publishAggregates( UA_Server& ua )ε->void{
			const UA_NodeId folder = UA_NODEID_NUMERIC( 0, UA_NS0ID_HISTORYSERVERCAPABILITIES_AGGREGATEFUNCTIONS ), organizes = UA_NODEID_NUMERIC( 0, UA_NS0ID_ORGANIZES );
			for( let& [function, _] : Part13Aggregates )
				UAε( UA_Server_addReference(&ua, folder, organizes, UA_EXPANDEDNODEID_NUMERIC(0, function), true) );
			UA_ObjectAttributes attributes = UA_ObjectAttributes_default;
			attributes.displayName = UA_LOCALIZEDTEXT( (char*)"", (char*)"Median" );
			attributes.description = UA_LOCALIZEDTEXT( (char*)"en", (char*)"The median of the Good values in each interval, which Part 13 doesn't define:  Uncertain where a value that isn't Good was left out." );
			UAε( UA_Server_addObjectNode(&ua, UAHistory::Median(), folder, organizes, UA_QUALIFIEDNAME(1, (char*)"Median"), UA_NODEID_NUMERIC(0, UA_NS0ID_AGGREGATEFUNCTIONTYPE), attributes, nullptr, nullptr) );
		}
		//An edit's answer, shared with the coroutine that awaits it, which a wait that gave up leaves behind.
		struct Edited final{
			absl::Notification Done;
			optional<Hist::EditResult> Result;//none when the edit failed.
		};
		Ω edit( sp<Hist::Group> group, Hist::EditDetails details, Hist::Writer by, sp<Edited> edited )ι->VoidTask{
			try{
				auto results = co_await group->Edit( {move(details)}, move(by) );
				edited->Result = move( results.at(0) );//one per entry.
			}
			catch( const std::exception& )//the group removed or stopped, or the historian ended before the edit was written:  said where it was found.
			{}
			edited->Done.Notify();
		}
	}

	α UAHistory::Timer::Add( steady_clock::duration elapsed )ι->void{
		_count.fetch_add( 1, std::memory_order_relaxed );
		_total.fetch_add( elapsed.count(), std::memory_order_relaxed );
		for( auto longest = _longest.load(std::memory_order_relaxed); elapsed.count()>longest && !_longest.compare_exchange_weak(longest, elapsed.count(), std::memory_order_relaxed); )
		{}
	}
	α UAHistory::Timer::Get()Ι->Timing{ return { _count.load(), steady_clock::duration{_total.load()}, steady_clock::duration{_longest.load()} }; }

	UAHistory::UAHistory()ε{
		let hist = Settings::FindObject( "/opcServer/hist" );
		if( !hist ){
			INFO( "No '/opcServer/hist':  this server keeps no history." );
			return;
		}
		if( let timeout = Settings::FindDuration("/opcServer/hist/editTimeout"); timeout ){
			THROW_IF( *timeout<=Duration::zero(), "hist.editTimeout must be positive, not {} ms.", duration_cast<milliseconds>(*timeout).count() );
			_editTimeout = *timeout;
		}
		_historian = mu<Hist::Historian>( Hist::Settings{*hist, Process::AppDataFolder()/"logs"/"hist"/"opc-server"}, Hist::SystemClock() );
	}
	UAHistory::~UAHistory(){
		Stop();
		let said = []( sv what, const Timing& t ){
			if( t.Count )
				INFO( "History {}:  {} callbacks held the service lock {} µs in all, the longest {} µs.", what, t.Count, duration_cast<microseconds>(t.Total).count(), duration_cast<microseconds>(t.Longest).count() );
		};
		said( "reads", Reads() );
		said( "edits", Edits() );
		said( "collection", Collections() );
		_group = nullptr;
		_historian = nullptr;
	}

	α UAHistory::Database()ι->UA_HistoryDatabase{
		UA_HistoryDatabase y{};
		y.context = this;
		y.setValue = setValue;
		y.readRaw = readRaw;
		y.readModified = readModified;
		y.readAtTime = readAtTime;
		y.readProcessed = readProcessed;
		y.updateData = updateData;
		y.deleteRawModified = deleteRawModified;
		return y;
	}
	α UAHistory::Lifecycle()ι->UA_GlobalNodeLifecycle*{
		static UA_GlobalNodeLifecycle lifecycle{ .generateChildNodeId=instantiated };
		return &lifecycle;
	}
	α UAHistory::Instantiated( const UA_NodeId& configuration )ι->void{
		if( !_group )
			_typeStepped.emplace( configuration );
	}
	α UAHistory::ReadLimit()Ι->UA_UInt32{
		return _historian ? (UA_UInt32)std::min<uint>( _historian->Config().ReadLimit, std::numeric_limits<UA_UInt32>::max() ) : 0;
	}
	α UAHistory::Find( const UA_NodeId& node )Ι->optional<Hist::NodeIndex>{
		auto p = _nodes.find( node );
		return p==_nodes.end() ? optional<Hist::NodeIndex>{} : p->second.Index;
	}
	α UAHistory::Median()ι->NodeId{
		static const NodeId median{ UA_NODEID_STRING_ALLOC(1, "Median") };
		return median;
	}
	α UAHistory::Aggregate( const UA_NodeId& function )ι->optional<Hist::EAggregate>{
		if( function.namespaceIndex==0 && function.identifierType==UA_NODEIDTYPE_NUMERIC ){
			for( let& [id, aggregate] : Part13Aggregates ){
				if( id==function.identifier.numeric )
					return aggregate;
			}
		}
		let median = Median();
		return UA_NodeId_equal( &function, &median ) ? optional<Hist::EAggregate>{ Hist::EAggregate::Median } : optional<Hist::EAggregate>{};
	}

	α UAHistory::Load( UA_Server& ua )ε->void{
		if( !Enabled() )
			return;
		auto nodes = Historized::Load( ua, [this]( const UA_NodeId& configuration ){ return _typeStepped.contains( configuration ); } );
		_typeStepped.clear();
		vector<Hist::Member> members;
		members.reserve( nodes.size() );
		for( let& node : nodes )
			members.push_back( node.Member );
		auto group = _historian->AddGroup( {.Name="server", .Indexes=Hist::EIndexes::Issued}, move(members) );
		for( auto& node : nodes ){
			let index = group->Find( node.Member.Node );
			THROW_IF( !index, "'{}' is not in the group made with it.", node.Id.ToString() );
			//There is no subscription to break, so the stop was the break, and the node's value now is its first after it.
			//Stamped as open62541 stamps a write that carries no source time.
			auto value = Historized::Read( ua, node.Id, UA_TIMESTAMPSTORETURN_BOTH );
			if( value.hasValue || !UA_StatusCode_isBad(value.status) ){
				if( !value.hasSourceTimestamp ){
					value.sourceTimestamp = UA_DateTime_now();
					value.hasSourceTimestamp = true;
				}
				group->Enqueue( *index, value );
			}
			_archiveStarts.push_back( move(node.StartOfArchive) );
			_archiveStarts.push_back( move(node.StartOfOnlineArchive) );
			_nodes.emplace( move(node.Id), Node{*index, node.Aggregates} );
		}
		_group = move( group );
		publishAggregates( ua );
		ul _{ _publishing->Mutex };
		_publishing->Ua = &ua;
		PublishArchive();
		ScheduleMidnight();
		INFO( "Keeping the history of {} nodes under '{}'.", _nodes.size(), _historian->Config().Path.string() );
	}
	α UAHistory::Stop()ι->void{
		Hist::IClock::TimerId timer;
		{
			ul _{ _publishing->Mutex };
			_publishing->Ua = nullptr;
			timer = std::exchange( _publishing->Midnight, 0 );
		}
		if( timer )
			_historian->Time().Cancel( timer );
	}

	//Each historized node's StartOfArchive and StartOfOnlineArchive, the same since every file is online:  the start of
	//the earliest day that holds a file, so a client can tell a purged range from one with no data.  Before the first
	//flush there is no file, and nothing is older than today.
	α UAHistory::PublishArchive()ι->void{
		let& tz = *_historian->Config().TimeZone;
		optional<TimePoint> earliest;
		try{
			earliest = _group->Earliest();
		}
		catch( const Exception& )
		{}
		const UA_UtcTime start{ UADateTime{earliest.value_or(Hist::DayStart(Hist::DayOf(_historian->Time().Now(), tz), tz))}.UA() };
		UA_Variant value; UA_Variant_setScalar( &value, const_cast<UA_UtcTime*>(&start), &UA_TYPES[UA_TYPES_UTCTIME] );
		for( let& node : _archiveStarts ){
			if( let sc = UA_Server_writeValue(_publishing->Ua, node, value) )
				WARN( "Could not write the start of the archive to '{}':  {}", node.ToString(), UAException::Message(sc) );
		}
	}
	α UAHistory::ScheduleMidnight()ι->void{
		let due = Hist::NextDayStart( _historian->Time().Now(), *_historian->Config().TimeZone )+_historian->Config().Delay;//with the group's rewrite of the day.
		_publishing->Midnight = _historian->Time().Schedule( due, [this, publishing=_publishing]{
			ul _{ publishing->Mutex };
			if( !publishing->Ua )
				return;//stopped:  this may be gone.
			_publishing->Mutex.AssertHeld();//the same mutex, which the analysis can't tell.
			PublishArchive();
			ScheduleMidnight();
		});
	}

	α UAHistory::Collect( UA_Server& ua, const UA_NodeId& node, const UA_DataValue& value )ι->void{
		let start = steady_clock::now();
		if( let index = Find(node); index && _group ){//the group stamps a missing server time:  this is the server.
			//A write with an IndexRange passes only the part it wrote, which open62541 has already written into the node,
			//in place:  the node holds the whole value.  A data source's value is behind its read callback.
			auto nodestore = UA_Server_getConfig( &ua )->nodestore;
			let stored = nodestore->getNode( nodestore, &node, UA_NODEATTRIBUTESMASK_VALUE, UA_REFERENCETYPESET_NONE, UA_BROWSEDIRECTION_INVALID );
			UA_DataValue whole = value;
			if( stored && stored->head.nodeClass==UA_NODECLASS_VARIABLE && value.hasValue ){
				let& variable = stored->variableNode;
				const UA_DataValue* current = variable.valueSourceType==UA_VALUESOURCETYPE_INTERNAL ? &variable.valueSource.internal.value
					: variable.valueSourceType==UA_VALUESOURCETYPE_EXTERNAL && variable.valueSource.external.value ? *variable.valueSource.external.value : nullptr;
				if( current && current->hasValue )
					whole.value = current->value;
			}
			_group->Enqueue( *index, whole );
			if( stored )
				nodestore->releaseNode( nodestore, stored );
		}
		_collections.Add( steady_clock::now()-start );
	}

	α UAHistory::ReadRaw( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, const UA_ReadRawModifiedDetails& details, UA_TimestampsToReturn timestamps, bool release,
		std::span<const UA_HistoryReadValueId> nodes, UA_HistoryReadResult* results, UA_HistoryData* const* data )ι->void{
		Serve( ua, sessionId, sessionContext, timestamps, true, release, nodes, results, "raw",
			[&]( uint, const Node& node, const UA_HistoryReadValueId& id, UA_ByteString& continuation, Hist::ReadResult& page ){ return Read( node, details, id, continuation, page ); },
			[&]( uint i, const Hist::ReadResult& page, const UA_NumericRange* range ){ return fill( *data[i], page, timestamps, range ); } );
	}
	α UAHistory::ReadModified( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, const UA_ReadRawModifiedDetails& details, UA_TimestampsToReturn timestamps, bool release,
		std::span<const UA_HistoryReadValueId> nodes, UA_HistoryReadResult* results, UA_HistoryModifiedData* const* data )ι->void{
		Serve( ua, sessionId, sessionContext, timestamps, true, release, nodes, results, "modified",
			[&]( uint, const Node& node, const UA_HistoryReadValueId& id, UA_ByteString& continuation, Hist::ReadResult& page ){ return Read( node, details, id, continuation, page ); },
			[&]( uint i, const Hist::ReadResult& page, const UA_NumericRange* range ){ return fill( *data[i], page, timestamps, range ); } );
	}
	α UAHistory::ReadAtTime( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, const UA_ReadAtTimeDetails& details, UA_TimestampsToReturn timestamps, bool release,
		std::span<const UA_HistoryReadValueId> nodes, UA_HistoryReadResult* results, UA_HistoryData* const* data )ι->void{
		Serve( ua, sessionId, sessionContext, timestamps, false, release, nodes, results, "at-time",
			[&]( uint, const Node& node, const UA_HistoryReadValueId& id, UA_ByteString& continuation, Hist::ReadResult& page ){ return AtTime( node, details, id, continuation, page ); },
			[&]( uint i, const Hist::ReadResult& page, const UA_NumericRange* range ){ return fill( *data[i], page, timestamps, range ); } );
	}
	α UAHistory::ReadProcessed( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, const UA_ReadProcessedDetails& details, UA_TimestampsToReturn timestamps, bool release,
		std::span<const UA_HistoryReadValueId> nodes, UA_HistoryReadResult* results, UA_HistoryData* const* data )ι->void{
		if( details.aggregateTypeSize!=nodes.size() && !release ){//one aggregate per node, a node read with two named twice (Part 11 §6.5.4.2).
			for( uint i=0; i<nodes.size(); ++i )
				results[i].statusCode = UA_STATUSCODE_BADAGGREGATELISTMISMATCH;
			return;
		}
		Serve( ua, sessionId, sessionContext, timestamps, false, release, nodes, results, "processed",
			[&]( uint i, const Node& node, const UA_HistoryReadValueId& id, UA_ByteString& continuation, Hist::ReadResult& page ){ return Processed( node, details, details.aggregateType[i], id, continuation, page ); },
			[&]( uint i, const Hist::ReadResult& page, const UA_NumericRange* range ){ return fill( *data[i], page, timestamps, range ); } );
	}
	α UAHistory::Serve( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, UA_TimestampsToReturn timestamps, bool serverTimestamps, bool release,
		std::span<const UA_HistoryReadValueId> nodes, UA_HistoryReadResult* results, sv what,
		absl::FunctionRef<UA_StatusCode( uint, const Node&, const UA_HistoryReadValueId&, UA_ByteString&, Hist::ReadResult& )> read,
		absl::FunctionRef<UA_StatusCode( uint, const Hist::ReadResult&, const UA_NumericRange* )> fill )ι->void{
		let start = steady_clock::now();
		uint values{};
		for( uint i=0; i<nodes.size(); ++i ){
			auto& result = results[i];
			if( release ){//A continuation point is the read's own state, so releasing one frees nothing.
				result.statusCode = UA_STATUSCODE_GOOD;
				continue;
			}
			IndexRange range;
			let node = Admit( ua, sessionId, sessionContext, timestamps, serverTimestamps, nodes[i], range.Value, result.statusCode );
			if( !node )
				continue;
			Hist::ReadResult page;
			result.statusCode = read( i, *node, nodes[i], result.continuationPoint, page );
			if( UA_StatusCode_isBad(result.statusCode) )
				continue;
			if( let sc = fill(i, page, nodes[i].indexRange.length ? &range.Value : nullptr) )
				result.statusCode = sc;
			values += page.Values.size();
		}
		let elapsed = steady_clock::now()-start;
		_reads.Add( elapsed );
		LOG( elapsed>=SlowRead ? ELogLevel::Warning : ELogLevel::Trace, _tags, "A history read of {} nodes held the service lock for {} µs, returning {} {} values.", nodes.size(), duration_cast<microseconds>(elapsed).count(), values, what );
	}
	α UAHistory::Admit( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, UA_TimestampsToReturn timestamps, bool serverTimestamps, const UA_HistoryReadValueId& node,
		UA_NumericRange& range, UA_StatusCode& status )Ι->const Node*{
		status = UA_STATUSCODE_GOOD;
		if( timestamps!=UA_TIMESTAMPSTORETURN_SOURCE && timestamps!=UA_TIMESTAMPSTORETURN_SERVER && timestamps!=UA_TIMESTAMPSTORETURN_BOTH )
			status = UA_STATUSCODE_BADINVALIDTIMESTAMPARGUMENT;
		else if( !serverTimestamps && timestamps==UA_TIMESTAMPSTORETURN_SERVER )//a computed value has none (Part 13 §5.4.3.1).
			status = UA_STATUSCODE_BADTIMESTAMPSTORETURNINVALID;
		else if( !(UAAccess::GetUserAccessLevel(&ua, nullptr, sessionId, sessionContext, &node.nodeId, nullptr) & UA_ACCESSLEVELMASK_HISTORYREAD) )
			status = UA_STATUSCODE_BADUSERACCESSDENIED;
		else if( node.indexRange.length )
			status = UA_NumericRange_parse( &range, node.indexRange );
		if( status )
			return nullptr;
		let p = _nodes.find( node.nodeId );
		if( p==_nodes.end() || !_group ){
			UA_NodeClass nodeClass;
			status = UA_Server_readNodeClass( &ua, node.nodeId, &nodeClass ) ? UA_STATUSCODE_BADNODEIDUNKNOWN : UA_STATUSCODE_BADHISTORYOPERATIONUNSUPPORTED;
			return nullptr;
		}
		return &p->second;
	}

	α UAHistory::Read( const Node& node, const UA_ReadRawModifiedDetails& details, const UA_HistoryReadValueId& id, UA_ByteString& continuation, Hist::ReadResult& page )ι->UA_StatusCode{
		//DateTime's MinValue, 0 on the wire, is a time that isn't given, and two of the three bound a read (Part 11 §6.5.3).
		let hasStart = details.startTime>0, hasEnd = details.endTime>0;
		if( hasStart+hasEnd+(details.numValuesPerNode!=0)<2 )
			return UA_STATUSCODE_BADHISTORYOPERATIONINVALID;
		Hist::ReadRequest request{ .Nodes={node.Index}, .Bounds=details.returnBounds, .Modified=details.isReadModified, .Limit=details.numValuesPerNode, .Continuation=continuationOf(id), .OneDay=true };
		if( hasStart )
			request.Start = details.startTime;
		if( hasEnd ){
			//Part 11's range holds its start and not its end, whichever way it flows, where the historian's holds both:  a
			//value at the end is only ever its bound.
			let open = hasStart && !details.returnBounds && details.startTime!=details.endTime;
			request.End = details.endTime+( !open ? 0 : details.startTime<details.endTime ? -1 : 1 );
		}
		return paged( [&]{ return _group->Read( request ); }, continuation, page );
	}
	α UAHistory::AtTime( const Node& node, const UA_ReadAtTimeDetails& details, const UA_HistoryReadValueId& id, UA_ByteString& continuation, Hist::ReadResult& page )ι->UA_StatusCode{
		if( !details.reqTimesSize )
			return UA_STATUSCODE_BADHISTORYOPERATIONINVALID;
		const Hist::AtTimeRequest request{ .Nodes={node.Index}, .Times={details.reqTimes, details.reqTimes+details.reqTimesSize}, .SimpleBounds=details.useSimpleBounds!=0, .Configuration=node.Aggregates, .Continuation=continuationOf(id) };
		return paged( [&]{ return _group->ReadAtTime( request ); }, continuation, page );
	}
	α UAHistory::Processed( const Node& node, const UA_ReadProcessedDetails& details, const UA_NodeId& function, const UA_HistoryReadValueId& id, UA_ByteString& continuation, Hist::ReadResult& page )ι->UA_StatusCode{
		let aggregate = Aggregate( function );
		if( !aggregate )
			return UA_STATUSCODE_BADAGGREGATENOTSUPPORTED;
		if( details.startTime<=0 || details.endTime<=0 )//all three shall be specified (Part 11 §6.5.4.2), a time of 0 isn't.
			return UA_STATUSCODE_BADHISTORYOPERATIONINVALID;
		constexpr double longest{ 9e12 };//what a Duration's nanoseconds hold, in ms.
		if( !(details.processingInterval>=0 && details.processingInterval<longest) )
			return UA_STATUSCODE_BADINVALIDARGUMENT;
		let& c = details.aggregateConfiguration;
		const Hist::ProcessedRequest request{ .Nodes={node.Index}, .Start=details.startTime, .End=details.endTime, .Interval=duration_cast<Duration>( std::chrono::duration<double,std::milli>{details.processingInterval} ),
			.Aggregate=*aggregate, .Configuration=c.useServerCapabilitiesDefaults ? node.Aggregates : Hist::AggregateConfiguration{ c.treatUncertainAsBad!=0, c.percentDataBad, c.percentDataGood, c.useSlopedExtrapolation!=0 },
			.Continuation=continuationOf(id) };
		return paged( [&]{ return _group->ReadProcessed( request ); }, continuation, page );
	}

	α UAHistory::Edit( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, const UA_NodeId& node, UA_HistoryUpdateResult& result, absl::FunctionRef<optional<Hist::EditDetails>( Hist::NodeIndex )> make )ι->optional<Hist::EditResult>{
		//HistoryWrite is the edit rights' bit (OpcAuthorize::UserRights):  none for an unprotected node, or no session.
		if( !(UAAccess::GetUserAccessLevel(&ua, nullptr, sessionId, sessionContext, &node, nullptr) & UA_ACCESSLEVELMASK_HISTORYWRITE) ){
			result.statusCode = UA_STATUSCODE_BADUSERACCESSDENIED;
			return nullopt;
		}
		let index = Find( node );
		if( !index || !_group ){
			UA_NodeClass nodeClass;
			result.statusCode = UA_Server_readNodeClass( &ua, node, &nodeClass ) ? UA_STATUSCODE_BADNODEIDUNKNOWN : UA_STATUSCODE_BADHISTORYOPERATIONUNSUPPORTED;
			return nullopt;
		}
		//And the node's own, which a nodeset that gave it HistoryRead alone left out (Historized::load):  the default
		//plugin masks the user's level by it.
		UA_Byte accessLevel{};
		if( UA_Server_readAccessLevel(&ua, node, &accessLevel) || !(accessLevel & UA_ACCESSLEVELMASK_HISTORYWRITE) ){
			result.statusCode = UA_STATUSCODE_BADUSERACCESSDENIED;
			return nullopt;
		}
		auto details = make( *index );
		if( !details )
			return nullopt;
		//The session's user, by the lookup UAAccess made at activation, and the name OpcAuthorize holds for it:  no round trip.
		let& ctx = *static_cast<UAAccess::SessionContext*>( sessionContext );//the access check above refused a session without one.
		auto edited = ms<Edited>();
		edit( _group, move(*details), Hist::Writer{ctx.UserPK, GetSchema().Authorizer->UserName(ctx.UserPK)}, edited );
		//Runs in the group's next flush and answers once its records are durable:  the server's thread waits, holding the
		//service lock, since nothing else may answer the client before then.  The flush's writes resume on the executor,
		//where the midnight StartOfArchive write and a re-mapping of node rights wait on that lock, so with every executor
		//thread waiting the flush never would:  the wait ends at editTimeout, and the edit, still queued, may yet be written.
		if( !edited->Done.WaitForNotificationWithTimeout(absl::FromChrono(_editTimeout)) ){
			ERR( "A history edit wasn't written within {} ms:  answered Bad_Timeout, it may still be written.", duration_cast<milliseconds>(_editTimeout).count() );
			result.statusCode = UA_STATUSCODE_BADTIMEOUT;
			return nullopt;
		}
		if( !edited->Result )
			result.statusCode = UA_STATUSCODE_BADINTERNALERROR;
		return move( edited->Result );
	}
	α UAHistory::UpdateData( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, const UA_UpdateDataDetails& details, UA_HistoryUpdateResult& result )ι->void{
		let start = steady_clock::now();
		const std::span<const UA_DataValue> values{ details.updateValues, details.updateValuesSize };
		vector<uint> allowed;//each value the session's user may write, by its position.
		let make = [&]( Hist::NodeIndex index )->optional<Hist::EditDetails> {
			result.operationResults = (UA_StatusCode*)UA_Array_new( values.size(), &UA_TYPES[UA_TYPES_STATUSCODE] );
			if( !result.operationResults ){
				result.statusCode = UA_STATUSCODE_BADOUTOFMEMORY;
				return nullopt;
			}
			result.operationResultsSize = values.size();
			auto& ac = UA_Server_getConfig( &ua )->accessControl;
			NodeId dataType;
			UA_Int32 rank{ UA_VALUERANK_ANY };
			let type = UA_Server_readDataType( &ua, details.nodeId, &dataType ) ? nullptr : UA_Server_findDataType( &ua, &dataType );
			UA_Server_readValueRank( &ua, details.nodeId, &rank );
			Hist::UpdateData update{ index, details.performInsertReplace, {} };
			for( uint i=0; i<values.size(); ++i ){
				if( ac.allowHistoryUpdateUpdateData && !ac.allowHistoryUpdateUpdateData(&ua, &ac, sessionId, sessionContext, &details.nodeId, details.performInsertReplace, &values[i]) ){
					result.operationResults[i] = UA_STATUSCODE_BADUSERACCESSDENIED;
					continue;
				}
				if( !typed(values[i], type, rank) ){
					result.operationResults[i] = UA_STATUSCODE_BADTYPEMISMATCH;
					continue;
				}
				allowed.push_back( i );
				update.Values.emplace_back( values[i] );
			}
			return allowed.empty() ? nullopt : optional<Hist::EditDetails>{ move(update) };
		};
		if( let r = Edit(ua, sessionId, sessionContext, details.nodeId, result, make) ){
			result.statusCode = r->Status;
			for( uint i=0; i<allowed.size() && i<r->Results.size(); ++i )
				result.operationResults[allowed[i]] = r->Results[i];
		}
		//An entry refused whole wrote none of its values, and each says so, for a client that reads only theirs.
		if( UA_StatusCode_isBad(result.statusCode) ){
			for( let i : allowed )
				result.operationResults[i] = result.statusCode;
		}
		let elapsed = steady_clock::now()-start;
		_edits.Add( elapsed );
		DBG( "A history update of {} values held the service lock for {} µs:  {}.", values.size(), duration_cast<microseconds>(elapsed).count(), UAException::Message(result.statusCode) );
	}
	α UAHistory::DeleteRawModified( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, const UA_DeleteRawModifiedDetails& details, UA_HistoryUpdateResult& result )ι->void{
		let start = steady_clock::now();
		if( details.isDeleteModified ){//the modifications are the audit trail.
			result.statusCode = UA_STATUSCODE_BADHISTORYOPERATIONUNSUPPORTED;
			return;
		}
		let make = [&]( Hist::NodeIndex index )->optional<Hist::EditDetails> {
			//DateTime's MinValue, 0 on the wire, is a time that isn't given, as a read takes it, and a delete needs both:  the
			//library would take 0 as 1601.
			if( details.startTime<=0 || details.endTime<=0 ){
				result.statusCode = UA_STATUSCODE_BADHISTORYOPERATIONINVALID;
				return nullopt;
			}
			auto& ac = UA_Server_getConfig( &ua )->accessControl;
			if( ac.allowHistoryUpdateDeleteRawModified && !ac.allowHistoryUpdateDeleteRawModified(&ua, &ac, sessionId, sessionContext, &details.nodeId, details.startTime, details.endTime, details.isDeleteModified) ){
				result.statusCode = UA_STATUSCODE_BADUSERACCESSDENIED;
				return nullopt;
			}
			//Part 11's range holds its start and not its end, as a read's does, where the historian's holds both.
			return Hist::DeleteRaw{ index, details.startTime, details.endTime-( details.startTime<details.endTime ? 1 : 0 ) };
		};
		if( let r = Edit(ua, sessionId, sessionContext, details.nodeId, result, make) )
			result.statusCode = r->Status;
		let elapsed = steady_clock::now()-start;
		_edits.Add( elapsed );
		DBG( "A history delete held the service lock for {} µs:  {}.", duration_cast<microseconds>(elapsed).count(), UAException::Message(result.statusCode) );
	}
}