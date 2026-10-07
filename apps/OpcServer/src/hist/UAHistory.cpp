#include "UAHistory.h"
#include <open62541/plugin/nodestore.h>
#include <jde/fwk/process/process.h>
#include <jde/opc/uatypes/DateTime.h>
#include "Historized.h"
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
		_historian = mu<Hist::Historian>( Hist::Settings{*hist, Process::AppDataFolder()/"logs"/"hist"/"opc-server"}, Hist::SystemClock() );
	}
	UAHistory::~UAHistory(){
		Stop();
		let said = []( sv what, const Timing& t ){
			if( t.Count )
				INFO( "History {}:  {} callbacks held the service lock {} µs in all, the longest {} µs.", what, t.Count, duration_cast<microseconds>(t.Total).count(), duration_cast<microseconds>(t.Longest).count() );
		};
		said( "reads", Reads() );
		said( "collection", Collections() );
		_group = nullptr;
		_historian = nullptr;
	}

	α UAHistory::Database()ι->UA_HistoryDatabase{
		UA_HistoryDatabase y{};
		y.context = this;
		y.setValue = setValue;
		y.readRaw = readRaw;
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
		auto p = _indexes.find( node );
		return p==_indexes.end() ? optional<Hist::NodeIndex>{} : p->second;
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
			_indexes.emplace( move(node.Id), *index );
		}
		_group = move( group );
		ul _{ _publishing->Mutex };
		_publishing->Ua = &ua;
		PublishArchive();
		ScheduleMidnight();
		INFO( "Keeping the history of {} nodes under '{}'.", _indexes.size(), _historian->Config().Path.string() );
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
		let start = steady_clock::now();
		uint values{};
		for( uint i=0; i<nodes.size(); ++i ){
			//A continuation point is the read's own state, so releasing one frees nothing.
			results[i].statusCode = release ? UA_STATUSCODE_GOOD : Read( ua, sessionId, sessionContext, details, timestamps, nodes[i], *data[i], results[i].continuationPoint );
			values += data[i]->dataValuesSize;
		}
		let elapsed = steady_clock::now()-start;
		_reads.Add( elapsed );
		LOG( elapsed>=SlowRead ? ELogLevel::Warning : ELogLevel::Trace, _tags, "A history read of {} nodes held the service lock for {} µs, returning {} values.", nodes.size(), duration_cast<microseconds>(elapsed).count(), values );
	}

	α UAHistory::Read( UA_Server& ua, const UA_NodeId* sessionId, void* sessionContext, const UA_ReadRawModifiedDetails& details, UA_TimestampsToReturn timestamps,
		const UA_HistoryReadValueId& node, UA_HistoryData& data, UA_ByteString& continuation )ι->UA_StatusCode{
		if( timestamps!=UA_TIMESTAMPSTORETURN_SOURCE && timestamps!=UA_TIMESTAMPSTORETURN_SERVER && timestamps!=UA_TIMESTAMPSTORETURN_BOTH )
			return UA_STATUSCODE_BADINVALIDTIMESTAMPARGUMENT;
		if( !(UAAccess::GetUserAccessLevel(&ua, nullptr, sessionId, sessionContext, &node.nodeId, nullptr) & UA_ACCESSLEVELMASK_HISTORYREAD) )
			return UA_STATUSCODE_BADUSERACCESSDENIED;
		let index = Find( node.nodeId );
		if( !index || !_group ){
			UA_NodeClass nodeClass;
			return UA_Server_readNodeClass( &ua, node.nodeId, &nodeClass ) ? UA_STATUSCODE_BADNODEIDUNKNOWN : UA_STATUSCODE_BADHISTORYOPERATIONUNSUPPORTED;
		}
		//DateTime's MinValue, 0 on the wire, is a time that isn't given, and two of the three bound a read (Part 11 §6.5.3).
		let hasStart = details.startTime>0, hasEnd = details.endTime>0;
		if( hasStart+hasEnd+(details.numValuesPerNode!=0)<2 )
			return UA_STATUSCODE_BADHISTORYOPERATIONINVALID;
		Hist::ReadRequest request{ .Nodes={*index}, .Bounds=details.returnBounds, .Limit=details.numValuesPerNode, .OneDay=true };
		if( hasStart )
			request.Start = details.startTime;
		if( hasEnd ){
			//Part 11's range holds its start and not its end, whichever way it flows, where the historian's holds both:  a
			//value at the end is only ever its bound.
			let open = hasStart && !details.returnBounds && details.startTime!=details.endTime;
			request.End = details.endTime+( !open ? 0 : details.startTime<details.endTime ? -1 : 1 );
		}
		if( node.continuationPoint.length )
			request.Continuation.assign( (const char*)node.continuationPoint.data, node.continuationPoint.length );
		IndexRange range;
		if( node.indexRange.length ){
			if( let sc = UA_NumericRange_parse(&range.Value, node.indexRange) )
				return sc;
		}
		try{
			let page = _group->Read( request );
			if( let size = page.Values.size() ){
				data.dataValues = (UA_DataValue*)UA_Array_new( size, &UA_TYPES[UA_TYPES_DATAVALUE] );
				if( !data.dataValues )
					return UA_STATUSCODE_BADOUTOFMEMORY;
				data.dataValuesSize = size;
				for( uint i=0; i<size; ++i )
					data.dataValues[i] = toUA( page.Values[i].Value, timestamps, node.indexRange.length ? &range.Value : nullptr );
			}
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
		catch( const std::exception& ){//a file that can't be read, said where it was found.
			return UA_STATUSCODE_BADINTERNALERROR;
		}
	}
}