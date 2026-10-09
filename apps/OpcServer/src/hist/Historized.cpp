#include "Historized.h"
#include <open62541/plugin/nodestore.h>

#define let const auto
namespace Jde::Opc::Server{
	constexpr ELogTags _tags = ( ELogTags )EOpcLogTags::Opc;

	constexpr sv Configuration{ "HA Configuration" };
	//A standard browse name:  namespace 0.
	Ω name( sv text )ι->UA_QualifiedName{ return { 0, {text.size(), (UA_Byte*)text.data()} }; }
	Ω child( UA_Server& ua, const UA_NodeId& parent, sv browseName )ι->optional<NodeId>{
		let qualified = name( browseName );
		auto found = UA_Server_browseSimplifiedBrowsePath( &ua, parent, 1, &qualified );
		optional<NodeId> y;
		if( found.statusCode==UA_STATUSCODE_GOOD && found.targetsSize )
			y.emplace( found.targets[0].targetId.nodeId );
		UA_BrowsePathResult_clear( &found );
		return y;
	}
	α Historized::Read( UA_Server& ua, const UA_NodeId& node, UA_TimestampsToReturn timestamps )ι->Value{
		UA_ReadValueId id; UA_ReadValueId_init( &id );
		id.nodeId = node;
		id.attributeId = UA_ATTRIBUTEID_VALUE;
		return Value{ UA_Server_read(&ua, &id, timestamps) };
	}
	//The property's number, a Boolean as 0 or 1:  none when it isn't there, or holds no value.
	Ω number( UA_Server& ua, const UA_NodeId& parent, sv browseName, str owner )ε->optional<double>{
		let property = child( ua, parent, browseName );
		if( !property )
			return nullopt;
		auto value = Historized::Read( ua, *property );
		if( !value.hasValue || value.IsEmpty() )
			return nullopt;
		let kind = value.value.type->typeKind;
		let isNumber = UA_DataType_isNumeric( value.value.type ) || kind==UA_DATATYPEKIND_BOOLEAN || kind==UA_DATATYPEKIND_ENUM;
		THROW_IF( !value.IsScalar() || !isNumber, "'{}' has a {} that is a '{}', not a number.", owner, browseName, value.value.type->typeName );
		return kind==UA_DATATYPEKIND_ENUM ? (double)value.Get<UA_Int32>( 0 ) : value.AsNumber<double>();
	}
	//Writes the property, adding it in the server's own namespace when its nodeset has none.
	Ω publish( UA_Server& ua, const UA_NodeId& parent, sv browseName, const UA_DataType& type, const void* scalar )ε->NodeId{
		UA_Variant value; UA_Variant_setScalar( &value, const_cast<void*>(scalar), &type );
		if( auto existing = child(ua, parent, browseName) ){
			UAε( UA_Server_writeValue(&ua, *existing, value) );
			return move( *existing );
		}
		UA_VariableAttributes attributes = UA_VariableAttributes_default;
		attributes.displayName = UA_LocalizedText{ {}, {browseName.size(), (UA_Byte*)browseName.data()} };
		attributes.dataType = type.typeId;
		attributes.valueRank = UA_VALUERANK_SCALAR;
		attributes.value = value;
		UA_NodeId id{};
		UAε( UA_Server_addVariableNode(&ua, UA_NODEID_NUMERIC(1, 0), parent, UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY), name(browseName), UA_NODEID_NUMERIC(0, UA_NS0ID_PROPERTYTYPE), attributes, nullptr, &id) );
		return NodeId{ move(id) };
	}
	Ω addConfiguration( UA_Server& ua, const UA_NodeId& variable )ε->NodeId{
		UA_ObjectAttributes attributes = UA_ObjectAttributes_default;
		attributes.displayName = UA_LocalizedText{ {}, {Configuration.size(), (UA_Byte*)Configuration.data()} };
		UA_NodeId id{};
		UAε( UA_Server_addObjectNode(&ua, UA_NODEID_NUMERIC(1, 0), variable, UA_NODEID_NUMERIC(0, UA_NS0ID_HASHISTORICALCONFIGURATION), name(Configuration), UA_NODEID_NUMERIC(0, UA_NS0ID_HISTORICALDATACONFIGURATIONTYPE), attributes, nullptr, &id) );
		return NodeId{ move(id) };
	}
	Ω expanded( UA_Server& ua, const UA_NodeId& id )ε->ExNodeId{
		UA_ExpandedNodeId y; UA_ExpandedNodeId_init( &y );
		UAε( UA_Server_getNamespaceByIndex(&ua, id.namespaceIndex, &y.namespaceUri) );
		UA_NodeId_copy( &id, &y.nodeId );
		return ExNodeId{ move(y) };
	}
	Ω isEnumeration( UA_Server& ua, const UA_NodeId& variable )ε->bool{
		UA_NodeId dataType{};
		UAε( UA_Server_readDataType(&ua, variable, &dataType) );
		let type = UA_Server_findDataType( &ua, &dataType );
		UA_NodeId_clear( &dataType );
		return type && type->typeKind==UA_DATATYPEKIND_ENUM;
	}

	//An instance declaration, a type's member, is a template nothing writes, which a companion nodeset may still mark
	//Historizing:  it has an ObjectType or VariableType above it.  HasModellingRule doesn't tell, since open62541 keeps it
	//on the instances it makes (modellingRulesOnInstances).
	Ω inType( UA_Server& ua, const UA_NodeId& variable )ι->bool{
		vector<NodeId> next{ NodeId{variable} };
		flat_set<NodeId> seen;
		while( next.size() ){
			const NodeId node{ move(next.back()) };
			next.pop_back();
			if( !seen.insert(node).second )
				continue;
			UA_BrowseDescription parents; UA_BrowseDescription_init( &parents );
			parents.nodeId = node;
			parents.browseDirection = UA_BROWSEDIRECTION_INVERSE;
			parents.referenceTypeId = UA_NODEID_NUMERIC( 0, UA_NS0ID_HIERARCHICALREFERENCES );
			parents.includeSubtypes = true;
			parents.nodeClassMask = UA_NODECLASS_OBJECT | UA_NODECLASS_VARIABLE | UA_NODECLASS_OBJECTTYPE | UA_NODECLASS_VARIABLETYPE;
			parents.resultMask = UA_BROWSERESULTMASK_NODECLASS;
			auto found = UA_Server_browse( &ua, 0, &parents );
			bool type{};
			for( uint i=0; i<found.referencesSize && !type; ++i ){
				let& parent = found.references[i];
				type = parent.nodeClass==UA_NODECLASS_OBJECTTYPE || parent.nodeClass==UA_NODECLASS_VARIABLETYPE;
				if( !parent.nodeId.serverIndex )
					next.emplace_back( parent.nodeId.nodeId );
			}
			UA_BrowseResult_clear( &found );
			if( type )
				return true;
		}
		return false;
	}

	Ω load( UA_Server& ua, NodeId id, absl::FunctionRef<bool( const UA_NodeId& )> typeStepped )ε->Historized{
		let label = id.ToString();
		//UserAccessLevel is the node's AccessLevel masked by the user's rights, and a nodeset's often lacks the history
		//bits, without which a client never offers the node's history:  HistoryRead, and HistoryWrite since Insert,
		//Replace, Update and DeleteRaw are served.  A nodeset that set HistoryRead set the history bits it meant, and with
		//HistoryRead alone, as companion nodesets' 5 has it, made the history read-only, which it stays.
		UA_Byte accessLevel{};
		UAε( UA_Server_readAccessLevel(&ua, id, &accessLevel) );
		if( !(accessLevel & UA_ACCESSLEVELMASK_HISTORYREAD) )
			UAε( UA_Server_writeAccessLevel(&ua, id, accessLevel | UA_ACCESSLEVELMASK_HISTORYREAD | UA_ACCESSLEVELMASK_HISTORYWRITE) );

		auto found = child( ua, id, Configuration );
		let made = !found;
		const NodeId configuration{ found ? move(*found) : addConfiguration(ua, id) };
		let setting = [&]( sv browseName ){ return number( ua, configuration, browseName, label ); };
		let boolean = [&]( sv browseName, UA_Boolean value ){ return publish( ua, configuration, browseName, UA_TYPES[UA_TYPES_BOOLEAN], &value ); };

		Hist::Thresholds thresholds;
		if( let stepped = made || typeStepped(configuration) ? nullopt : setting("Stepped") )
			thresholds.Stepped = *stepped!=0;
		else
			boolean( "Stepped", true );
		let interval = [&]( sv browseName )ε->Duration{
			let ms = setting( browseName );
			if( !ms ){
				const UA_Duration off{};
				publish( ua, configuration, browseName, UA_TYPES[UA_TYPES_DURATION], &off );
				return {};
			}
			constexpr double longest{ 9e12 };//what a Duration's nanoseconds hold, in ms.
			THROW_IF( !( *ms>=0 && *ms<longest ), "'{}' has a {} of {} ms, not a time interval.", label, browseName, *ms );
			return std::chrono::duration_cast<Duration>( std::chrono::duration<double,std::milli>{*ms} );
		};
		thresholds.MinTimeInterval = interval( "MinTimeInterval" );
		thresholds.MaxTimeInterval = interval( "MaxTimeInterval" );
		let deviation = setting( "ExceptionDeviation" );
		let enumeration = deviation && isEnumeration( ua, id );
		if( enumeration )
			INFO( "'{}' is an enumeration, whose every change is stored:  its ExceptionDeviation isn't used.", label );
		if( deviation && !enumeration ){
			thresholds.ExceptionDeviation = *deviation;
			if( let format = setting("ExceptionDeviationFormat") ){
				THROW_IF( !(*format>=0 && *format<=(double)UA_EXCEPTIONDEVIATIONFORMAT_UNKNOWN), "'{}' has an ExceptionDeviationFormat of {}.", label, *format );
				thresholds.DeviationFormat = (UA_ExceptionDeviationFormat)(UA_Int32)*format;
			}
			else
				publish( ua, configuration, "ExceptionDeviationFormat", UA_TYPES[UA_TYPES_EXCEPTIONDEVIATIONFORMAT], &thresholds.DeviationFormat );
			let ranged = thresholds.DeviationFormat==UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFRANGE ? "InstrumentRange"sv
				: thresholds.DeviationFormat==UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFEURANGE ? "EURange"sv : sv{};
			if( let property = ranged.empty() ? nullopt : child(ua, id, ranged) ){
				if( let value = Historized::Read(ua, *property); value.hasValue && UA_Variant_hasScalarType(&value.value, &UA_TYPES[UA_TYPES_RANGE]) )
					thresholds.Range = Hist::Range{ value.Get<UA_Range>(0).low, value.Get<UA_Range>(0).high };
			}
		}
		boolean( "ServerTimestampSupported", true );//both timestamps are stored.
		//Part 13's defaults, which the library's are, for a ReadProcessed with useServerCapabilitiesDefaults and an at-time
		//read, over whatever is there:  open62541 fills a property the nodeset leaves out with false or 0, which can't be
		//told from the nodeset's own.
		const Hist::AggregateConfiguration aggregates;
		if( let aggregate = child(ua, configuration, "AggregateConfiguration") ){
			const UA_Boolean uncertainAsBad{ aggregates.TreatUncertainAsBad }, sloped{ aggregates.UseSlopedExtrapolation };
			const UA_Byte bad{ aggregates.PercentDataBad }, good{ aggregates.PercentDataGood };
			publish( ua, *aggregate, "TreatUncertainAsBad", UA_TYPES[UA_TYPES_BOOLEAN], &uncertainAsBad );
			publish( ua, *aggregate, "PercentDataBad", UA_TYPES[UA_TYPES_BYTE], &bad );
			publish( ua, *aggregate, "PercentDataGood", UA_TYPES[UA_TYPES_BYTE], &good );
			publish( ua, *aggregate, "UseSlopedExtrapolation", UA_TYPES[UA_TYPES_BOOLEAN], &sloped );
		}
		const UA_UtcTime none{};//until UAHistory finds the archive's first day.
		auto startOfArchive = publish( ua, configuration, "StartOfArchive", UA_TYPES[UA_TYPES_UTCTIME], &none );
		auto startOfOnlineArchive = publish( ua, configuration, "StartOfOnlineArchive", UA_TYPES[UA_TYPES_UTCTIME], &none );
		Hist::Member member{ expanded(ua, id), move(thresholds) };
		return { move(id), move(member), aggregates, move(startOfArchive), move(startOfOnlineArchive) };
	}

	α Historized::Load( UA_Server& ua, absl::FunctionRef<bool( const UA_NodeId& )> typeStepped )ε->vector<Historized>{
		vector<NodeId> ids;
		let nodestore = UA_Server_getConfig( &ua )->nodestore;
		nodestore->iterate( nodestore, []( void* context, const UA_Node* node ){
			if( node->head.nodeClass==UA_NODECLASS_VARIABLE && node->variableNode.historizing )
				static_cast<vector<NodeId>*>( context )->emplace_back( node->head.nodeId );
		}, &ids );
		if( let members = std::erase_if(ids, [&ua]( let& id ){ return inType(ua, id); }) )
			DBG( "{} variables marked Historizing are a type's members, which aren't historized.", members );
		std::sort( ids.begin(), ids.end() );//so a new file issues its indexes the same way every run.
		vector<Historized> y;
		y.reserve( ids.size() );
		for( auto& id : ids )
			y.push_back( load(ua, move(id), typeStepped) );
		return y;
	}
}