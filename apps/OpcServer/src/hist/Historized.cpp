#include "Historized.h"
#include "OpcServerHistoryHost.h"
#include <open62541/plugin/nodestore.h>
#include <jde/opc/uatypes/opcHelpers.h>
#include <jde/opc/server/HAConfiguration.h>
#include <jde/opc/server/UAServer.h>

#define let const auto
namespace Jde::Opc::Server{
	constexpr ELogTags _tags = ( ELogTags )EOpcLogTags::Opc;

	using HAConfiguration::Child;
	using HAConfiguration::Publish;
	//The property's number, a Boolean as 0 or 1:  none when it isn't there, or holds no value.
	Ω number( UA_Server& ua, const UA_NodeId& parent, sv browseName, str owner )ε->optional<double>{
		let property = Child( ua, parent, browseName );
		if( !property )
			return nullopt;
		auto value = ReadValue( ua, *property );
		if( !value.hasValue || value.IsEmpty() )
			return nullopt;
		let kind = value.value.type->typeKind;
		let isNumber = UA_DataType_isNumeric( value.value.type ) || kind==UA_DATATYPEKIND_BOOLEAN || kind==UA_DATATYPEKIND_ENUM;
		THROW_IF( !value.IsScalar() || !isNumber, "'{}' has a {} that is a '{}', not a number.", owner, browseName, value.value.type->typeName );
		return kind==UA_DATATYPEKIND_ENUM ? (double)value.Get<UA_Int32>( 0 ) : value.AsNumber<double>();
	}
	Ω expanded( UA_Server& ua, const UA_NodeId& id )ε->ExNodeId{
		UA_ExpandedNodeId y; UA_ExpandedNodeId_init( &y );
		UAε( UA_Server_getNamespaceByIndex(&ua, id.namespaceIndex, &y.namespaceUri) );
		UA_NodeId_copy( &id, &y.nodeId );
		return ExNodeId{ move(y) };
	}
	//The variable's DataType, null for an abstract one or one the server has no type for.
	Ω dataType( UA_Server& ua, const UA_NodeId& variable )ε->const UA_DataType*{
		UA_NodeId dataType{};
		UAε( UA_Server_readDataType(&ua, variable, &dataType) );
		let type = UA_Server_findDataType( &ua, &dataType );
		UA_NodeId_clear( &dataType );
		return type;
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

		auto found = Child( ua, id, HAConfiguration::BrowseName );
		let made = !found;
		const NodeId configuration{ found ? move(*found) : HAConfiguration::Add(ua, id) };
		let setting = [&]( sv browseName ){ return number( ua, configuration, browseName, label ); };
		let boolean = [&]( sv browseName, UA_Boolean value ){ return Publish( ua, configuration, browseName, UA_TYPES[UA_TYPES_BOOLEAN], &value ); };

		Hist::Thresholds thresholds;
		if( let stepped = made || typeStepped(configuration) ? nullopt : setting("Stepped") )
			thresholds.Stepped = *stepped!=0;
		else
			boolean( "Stepped", true );
		let interval = [&]( sv browseName )ε->Duration{
			let ms = setting( browseName );
			if( !ms ){
				const UA_Duration off{};
				Publish( ua, configuration, browseName, UA_TYPES[UA_TYPES_DURATION], &off );
				return {};
			}
			let duration = Hist::ToDuration( *ms );
			THROW_IF( !duration, "'{}' has a {} of {} ms, not a time interval.", label, browseName, *ms );
			return *duration;
		};
		thresholds.MinTimeInterval = interval( "MinTimeInterval" );
		thresholds.MaxTimeInterval = interval( "MaxTimeInterval" );
		let deviation = setting( "ExceptionDeviation" );
		let type = dataType( ua, id );
		let enumeration = deviation && type && type->typeKind==UA_DATATYPEKIND_ENUM;
		if( enumeration )
			INFO( "'{}' is an enumeration, whose every change is stored:  its ExceptionDeviation isn't used.", label );
		if( deviation && !enumeration ){
			thresholds.ExceptionDeviation = *deviation;
			if( let format = setting("ExceptionDeviationFormat") ){
				THROW_IF( !(*format>=0 && *format<=(double)UA_EXCEPTIONDEVIATIONFORMAT_UNKNOWN), "'{}' has an ExceptionDeviationFormat of {}.", label, *format );
				thresholds.DeviationFormat = (UA_ExceptionDeviationFormat)(UA_Int32)*format;
			}
			else
				Publish( ua, configuration, "ExceptionDeviationFormat", UA_TYPES[UA_TYPES_EXCEPTIONDEVIATIONFORMAT], &thresholds.DeviationFormat );
			let ranged = thresholds.DeviationFormat==UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFRANGE ? "InstrumentRange"sv
				: thresholds.DeviationFormat==UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFEURANGE ? "EURange"sv : sv{};
			if( let property = ranged.empty() ? nullopt : Child(ua, id, ranged) ){
				if( let value = ReadValue(ua, *property); value.hasValue && UA_Variant_hasScalarType(&value.value, &UA_TYPES[UA_TYPES_RANGE]) )
					thresholds.Range = Hist::Range{ value.Get<UA_Range>(0).low, value.Get<UA_Range>(0).high };
			}
		}
		boolean( "ServerTimestampSupported", true );//both timestamps are stored.
		HAConfiguration::PublishAggregates( ua, configuration );
		const UA_UtcTime none{};//until UAHistory finds the archive's first day.
		auto startOfArchive = Publish( ua, configuration, "StartOfArchive", UA_TYPES[UA_TYPES_UTCTIME], &none );
		auto startOfOnlineArchive = Publish( ua, configuration, "StartOfOnlineArchive", UA_TYPES[UA_TYPES_UTCTIME], &none );
		Hist::Member member{ expanded(ua, id), move(thresholds), {}, type };
		return { move(id), move(member), move(startOfArchive), move(startOfOnlineArchive) };
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
namespace Jde::Opc{
	α Server::StartHistory( UAServer& ua, OpcServerHistoryHost& host )ε->void{
		auto& history = ua.History();
		if( !history.Enabled() )
			return;
		auto& historian = *host.Historian();//history's, so set while it is Enabled.
		auto nodes = Historized::Load( ua, [&history]( const UA_NodeId& configuration ){ return history.TypeStepped( configuration ); } );
		vector<Hist::Member> members;
		members.reserve( nodes.size() );
		for( let& node : nodes )
			members.push_back( node.Member );
		auto group = historian.AddGroup( {.Name="server", .Indexes=Hist::EIndexes::Issued}, move(members) );
		OpcServerHistoryHost::Indexes indexes;
		vector<NodeId> archiveStarts;
		for( auto& node : nodes ){
			let index = group->Find( node.Member.Node );
			THROW_IF( !index, "'{}' is not in the group made with it.", node.Id.ToString() );
			//There is no subscription to break, so the stop was the break, and the node's value now is its first after it.
			//Stamped as open62541 stamps a write that carries no source time.
			auto value = ReadValue( ua, node.Id, UA_TIMESTAMPSTORETURN_BOTH );
			if( value.hasValue || !UA_StatusCode_isBad(value.status) ){
				if( !value.hasSourceTimestamp ){
					value.sourceTimestamp = UA_DateTime_now();
					value.hasSourceTimestamp = true;
				}
				group->Enqueue( *index, value );
			}
			archiveStarts.push_back( move(node.StartOfArchive) );
			archiveStarts.push_back( move(node.StartOfOnlineArchive) );
			indexes.emplace_back( move(node.Id), *index );
		}
		let count = indexes.size();
		host.Serve( move(group), move(indexes), move(archiveStarts) );
		history.Start( ua );
		INFO( "Keeping the history of {} nodes under '{}'.", count, historian.Config().Path.string() );
	}
}