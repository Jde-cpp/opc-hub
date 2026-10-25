#include <jde/opc/server/HAConfiguration.h>
#include <jde/opc/uatypes/opcHelpers.h>

#define let const auto
namespace Jde::Opc::Server{
	//A standard browse name:  namespace 0.
	Ω name( sv text )ι->UA_QualifiedName{ return { 0, {text.size(), (UA_Byte*)text.data()} }; }

	α HAConfiguration::Child( UA_Server& ua, const UA_NodeId& parent, sv browseName )ι->optional<NodeId>{
		let qualified = name( browseName );
		auto found = UA_Server_browseSimplifiedBrowsePath( &ua, parent, 1, &qualified );
		optional<NodeId> y;
		if( found.statusCode==UA_STATUSCODE_GOOD && found.targetsSize )
			y.emplace( found.targets[0].targetId.nodeId );
		UA_BrowsePathResult_clear( &found );
		return y;
	}
	α HAConfiguration::Publish( UA_Server& ua, const UA_NodeId& parent, sv browseName, const UA_DataType& type, const void* scalar )ε->NodeId{
		UA_Variant value; UA_Variant_setScalar( &value, const_cast<void*>(scalar), &type );
		if( auto existing = Child(ua, parent, browseName) ){
			UAε( UA_Server_writeValue(&ua, *existing, value) );
			return move( *existing );
		}
		UA_VariableAttributes attributes = UA_VariableAttributes_default;
		attributes.displayName = UA_LocalizedText{ {}, {browseName.size(), (UA_Byte*)browseName.data()} };
		attributes.dataType = type.typeId;
		attributes.valueRank = UA_VALUERANK_SCALAR;
		attributes.value = value;
		UA_NodeId id{};
		UAε( UA_Server_addVariableNode(&ua, UA_NODEID_NUMERIC(OwnNamespace, 0), parent, UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY), name(browseName), UA_NODEID_NUMERIC(0, UA_NS0ID_PROPERTYTYPE), attributes, nullptr, &id) );
		return NodeId{ move(id) };
	}
	α HAConfiguration::Publish( UA_Server& ua, const UA_NodeId& configuration, const Hist::Thresholds& thresholds )ε->void{
		const UA_Boolean stepped{ thresholds.Stepped }, timestamps{ true };
		Publish( ua, configuration, "Stepped", UA_TYPES[UA_TYPES_BOOLEAN], &stepped );
		let ms = []( Duration d )->UA_Duration{ return std::chrono::duration<double,std::milli>{ d }.count(); };
		const UA_Duration min{ ms(thresholds.MinTimeInterval) }, max{ ms(thresholds.MaxTimeInterval) };
		Publish( ua, configuration, "MinTimeInterval", UA_TYPES[UA_TYPES_DURATION], &min );
		Publish( ua, configuration, "MaxTimeInterval", UA_TYPES[UA_TYPES_DURATION], &max );
		const UA_Double deviation{ thresholds.ExceptionDeviation.value_or(0) };
		Publish( ua, configuration, "ExceptionDeviation", UA_TYPES[UA_TYPES_DOUBLE], &deviation );
		Publish( ua, configuration, "ExceptionDeviationFormat", UA_TYPES[UA_TYPES_EXCEPTIONDEVIATIONFORMAT], &thresholds.DeviationFormat );
		Publish( ua, configuration, "ServerTimestampSupported", UA_TYPES[UA_TYPES_BOOLEAN], &timestamps );
	}
	α HAConfiguration::PublishAggregates( UA_Server& ua, const UA_NodeId& configuration )ε->void{
		let aggregate = Child( ua, configuration, "AggregateConfiguration" );
		if( !aggregate )
			return;
		const Hist::AggregateConfiguration defaults;
		const UA_Boolean uncertainAsBad{ defaults.TreatUncertainAsBad }, sloped{ defaults.UseSlopedExtrapolation };
		const UA_Byte bad{ defaults.PercentDataBad }, good{ defaults.PercentDataGood };
		Publish( ua, *aggregate, "TreatUncertainAsBad", UA_TYPES[UA_TYPES_BOOLEAN], &uncertainAsBad );
		Publish( ua, *aggregate, "PercentDataBad", UA_TYPES[UA_TYPES_BYTE], &bad );
		Publish( ua, *aggregate, "PercentDataGood", UA_TYPES[UA_TYPES_BYTE], &good );
		Publish( ua, *aggregate, "UseSlopedExtrapolation", UA_TYPES[UA_TYPES_BOOLEAN], &sloped );
	}
	α HAConfiguration::PublishArchiveStarts( UA_Server& ua, const UA_NodeId& configuration )ε->vector<NodeId>{
		const UA_UtcTime none{};
		vector<NodeId> y;
		for( let property : {"StartOfArchive"sv, "StartOfOnlineArchive"sv} )
			y.push_back( Publish(ua, configuration, property, UA_TYPES[UA_TYPES_UTCTIME], &none) );
		return y;
	}
	α HAConfiguration::Add( UA_Server& ua, const UA_NodeId& variable )ε->NodeId{
		UA_ObjectAttributes attributes = UA_ObjectAttributes_default;
		attributes.displayName = UA_LocalizedText{ {}, {BrowseName.size(), (UA_Byte*)BrowseName.data()} };
		UA_NodeId id{};
		UAε( UA_Server_addObjectNode(&ua, UA_NODEID_NUMERIC(OwnNamespace, 0), variable, UA_NODEID_NUMERIC(0, UA_NS0ID_HASHISTORICALCONFIGURATION), name(BrowseName), UA_NODEID_NUMERIC(0, UA_NS0ID_HISTORICALDATACONFIGURATIONTYPE), attributes, nullptr, &id) );
		return NodeId{ move(id) };
	}
}