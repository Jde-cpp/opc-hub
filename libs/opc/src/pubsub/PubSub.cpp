#include <jde/opc/pubsub/PubSub.h>
#include <open62541/server_pubsub.h>
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/BrowsePath.h>

#define let const auto
namespace Jde::Opc::PubSub{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::PubSub };
	Ω uv( sv s )ι->UA_String{ return UA_String{ s.size(), (UA_Byte*)s.data() }; }//a view is enough: open62541 deep-copies every config it is handed.
	Ω check( UA_StatusCode sc, string what, SL sl )ε->void{ THROW_IFX( sc, UAException(sc, move(what), {.Tags=EOpcLogTags::PubSub}, sl) ); }
	Ω checkResolved( const Config& c, SL sl )ε->void{
		for( let& f : c.Fields )
			THROW_IFSL( UA_NodeId_isNull(&f.Node), "pubsub field '{}' ('{}') is unresolved - Config::Resolve first.", f.Name, f.Path );
	}
	Ω dataType( sv name, SL sl )ε->const UA_DataType*{
		constexpr array<std::pair<sv,uint16>,12> types{{
			{"Boolean",UA_TYPES_BOOLEAN}, {"Byte",UA_TYPES_BYTE}, {"Int16",UA_TYPES_INT16}, {"UInt16",UA_TYPES_UINT16}, {"Int32",UA_TYPES_INT32}, {"UInt32",UA_TYPES_UINT32},
			{"Int64",UA_TYPES_INT64}, {"UInt64",UA_TYPES_UINT64}, {"Float",UA_TYPES_FLOAT}, {"Double",UA_TYPES_DOUBLE}, {"String",UA_TYPES_STRING}, {"DateTime",UA_TYPES_DATETIME} }};
		let p = find_if( types, [name](let& t){ return t.first==name; } );
		THROW_IFSL( p==types.end(), "Unsupported pubsub field type '{}'.", name );
		return &UA_TYPES[p->second];
	}
	//UADP connection shared by both ends: the writer's carries the contract's PublisherId, a reader's any id (it filters on the writer's).
	Ω addConnection( UA_Server& server, const Config& c, UA_PublisherId publisherId, str name, SL sl )ε->UA_NodeId{
		UA_PubSubConnectionConfig cc{};
		cc.name = uv( name );
		cc.transportProfileUri = uv( c.TransportProfile );
		UA_NetworkAddressUrlDataType address{ uv(c.NetworkInterface), uv(c.Url) };
		UA_Variant_setScalar( &cc.address, &address, &UA_TYPES[UA_TYPES_NETWORKADDRESSURLDATATYPE] );
		cc.publisherId = publisherId;
		UA_NodeId id{};
		check( UA_Server_addPubSubConnection(&server, &cc, &id), Ƒ("addPubSubConnection '{}'", c.Url), sl );
		return id;
	}

	Config::Config( const jobject& o, SL sl )ε{
		TransportProfile = Json::FindString( o, "transportProfile" ).value_or( "http://opcfoundation.org/UA-Profile/Transport/pubsub-udp-uadp" );
		Url = Json::AsString( o, "url", sl );
		NetworkInterface = Json::FindString( o, "networkInterface" ).value_or( "" );
		PublisherId = Json::AsNumber<UA_UInt16>( o, "publisherId", sl );
		WriterGroupId = Json::AsNumber<UA_UInt16>( o, "writerGroupId", sl );
		DataSetWriterId = Json::AsNumber<UA_UInt16>( o, "dataSetWriterId", sl );
		PublishingInterval = Json::FindDuration( o, "publishingInterval" ).value_or( 1s );
		let& dataSet = Json::AsObject( o, "dataSet", sl );
		DataSetName = Json::AsString( dataSet, "name", sl );
		Namespace = Json::FindString( dataSet, "namespace" ).value_or( "" );
		for( let& f : Json::AsArray(dataSet, "fields", sl) ){
			let& field = Json::AsObject( f, sl );
			Fields.push_back( Field{ Json::AsString(field, "name", sl), dataType(Json::AsSV(field, "type", sl), sl), Json::AsString(field, "path", sl), {} } );
		}
		THROW_IFSL( Fields.empty(), "pubsub dataSet '{}' has no fields.", DataSetName );
	}

	α Config::Resolve( UA_Server& server, SL sl )ε->void{
		flat_map<string,NsIndex> aliases;
		if( Namespace.size() )
			aliases.emplace( DataSetName, NamespaceIndex(server, Namespace, sl) );
		for( auto& f : Fields )
			f.Node = BrowsePath::Resolve( server, f.Path, 0, aliases, sl );
	}

	α Config::MetaData( SL sl )Ε->UA_DataSetMetaDataType{
		checkResolved( *this, sl );
		UA_DataSetMetaDataType y;
		UA_DataSetMetaDataType_init( &y );
		y.name = AllocUAString( DataSetName );
		y.fieldsSize = Fields.size();
		y.fields = (UA_FieldMetaData*)UA_Array_new( Fields.size(), &UA_TYPES[UA_TYPES_FIELDMETADATA] );
		for( size_t i=0; i<Fields.size(); ++i ){
			auto& f = y.fields[i];
			UA_FieldMetaData_init( &f );
			f.name = AllocUAString( Fields[i].Name );
			UA_NodeId_copy( &Fields[i].Type->typeId, &f.dataType );
			f.builtInType = (UA_Byte)Fields[i].Type->typeId.identifier.numeric;//ns0 numeric id of the built-in type, as the vendor's subscriber tutorial fills it.
			f.valueRank = UA_VALUERANK_SCALAR;
		}
		return y;
	}

	α Config::ToString()Ι->string{
		return Ƒ( "dataSet '{}' @ {} publisherId={} writerGroupId={} dataSetWriterId={} interval={} fields={}", DataSetName, Url, PublisherId, WriterGroupId, DataSetWriterId, Chrono::ToString(PublishingInterval), Fields.size() );
	}

	Writer::Writer( UA_Server& server, const Config& c, SL sl )ε{
		checkResolved( c, sl );
		UA_PublisherId publisherId{};
		publisherId.idType = UA_PUBLISHERIDTYPE_UINT16;
		publisherId.id.uint16 = c.PublisherId;
		Connection = addConnection( server, c, publisherId, Ƒ("{} publisher", c.DataSetName), sl );

		UA_PublishedDataSetConfig pds{};
		pds.publishedDataSetType = UA_PUBSUB_DATASET_PUBLISHEDITEMS;
		pds.name = uv( c.DataSetName );
		check( UA_Server_addPublishedDataSet(&server, &pds, &DataSet).addResult, "addPublishedDataSet", sl );
		for( let& f : c.Fields ){
			UA_DataSetFieldConfig fc{};
			fc.dataSetFieldType = UA_PUBSUB_DATASETFIELD_VARIABLE;
			fc.field.variable.fieldNameAlias = uv( f.Name );
			fc.field.variable.promotedField = false;
			fc.field.variable.publishParameters.publishedVariable = f.Node;//shallow - the server deep-copies the config.
			fc.field.variable.publishParameters.attributeId = UA_ATTRIBUTEID_VALUE;
			UA_NodeId fieldId{};
			check( UA_Server_addDataSetField(&server, DataSet, &fc, &fieldId).result, Ƒ("addDataSetField '{}'", f.Name), sl );
		}
		let writerGroupName = Ƒ( "{} writers", c.DataSetName );
		UA_WriterGroupConfig wg{};
		wg.name = uv( writerGroupName );
		wg.publishingInterval = (UA_Duration)std::chrono::duration<double,std::milli>( c.PublishingInterval ).count();
		wg.writerGroupId = c.WriterGroupId;
		wg.encodingMimeType = UA_PUBSUB_ENCODING_UADP;
		//PublisherId + group header + writerGroupId + payload header (dataSetWriterId): what a reader keys on.
		UA_UadpWriterGroupMessageDataType message;
		UA_UadpWriterGroupMessageDataType_init( &message );
		message.networkMessageContentMask = (UA_UadpNetworkMessageContentMask)( UA_UADPNETWORKMESSAGECONTENTMASK_PUBLISHERID | UA_UADPNETWORKMESSAGECONTENTMASK_GROUPHEADER | UA_UADPNETWORKMESSAGECONTENTMASK_WRITERGROUPID | UA_UADPNETWORKMESSAGECONTENTMASK_PAYLOADHEADER );
		UA_ExtensionObject_setValueNoDelete( &wg.messageSettings, &message, &UA_TYPES[UA_TYPES_UADPWRITERGROUPMESSAGEDATATYPE] );//stack-owned; copied by addWriterGroup.
		check( UA_Server_addWriterGroup(&server, Connection, &wg, &WriterGroup), "addWriterGroup", sl );

		let writerName = Ƒ( "{} writer", c.DataSetName );
		UA_DataSetWriterConfig dsw{};
		dsw.name = uv( writerName );
		dsw.dataSetWriterId = c.DataSetWriterId;
		dsw.keyFrameCount = 0;//every message a keyframe: open62541's reader discards anything else ("Only keyframes are supported", ua_pubsub_reader.c), so any other value silently drops samples.  0 - not 1 - is what suppresses delta frames: the writer emits a delta while `deltaFrameCounter>0 && deltaFrameCounter<=keyFrameCount` (ua_pubsub_writer.c), so 1 alternates key/delta and 10 sent nine deltas per keyframe.
		//A reading travels with its quality (OPC 10000-4 7.38).  The writer samples each field's whole DataValue and then
		//strips the status unless this bit is set (ua_pubsub_writer.c); with it the fields go out DataValue-encoded instead
		//of as bare variants.  The reader needs nothing in return - the encoding is in the message header, and it writes
		//the received DataValue, status included, into the target variable (ua_pubsub_reader.c `writeVal.value = *field`).
		//It does skip a field that carries no value, so a publisher keeps the value on a Bad reading.
		dsw.dataSetFieldContentMask = UA_DATASETFIELDCONTENTMASK_STATUSCODE;
		check( UA_Server_addDataSetWriter(&server, WriterGroup, DataSet, &dsw, &DataSetWriter), "addDataSetWriter", sl );
		check( UA_Server_enableAllPubSubComponents(&server), "enableAllPubSubComponents", sl );
		INFO( "PubSub writer: {}", c.ToString() );
	}

	Reader::Reader( UA_Server& server, const Config& c, SL sl )ε{
		checkResolved( c, sl );
		UA_PublisherId publisherId{};
		publisherId.idType = UA_PUBLISHERIDTYPE_UINT32;
		publisherId.id.uint32 = UA_UInt32_random();
		Connection = addConnection( server, c, publisherId, Ƒ("{} subscriber", c.DataSetName), sl );

		let readerGroupName = Ƒ( "{} readers", c.DataSetName );
		UA_ReaderGroupConfig rg{};
		rg.name = uv( readerGroupName );
		check( UA_Server_addReaderGroup(&server, Connection, &rg, &ReaderGroup), "addReaderGroup", sl );

		let readerName = Ƒ( "{} reader", c.DataSetName );
		UA_DataSetReaderConfig rc{};
		rc.name = uv( readerName );
		rc.publisherId.idType = UA_PUBLISHERIDTYPE_UINT16;
		rc.publisherId.id.uint16 = c.PublisherId;
		rc.writerGroupId = c.WriterGroupId;
		rc.dataSetWriterId = c.DataSetWriterId;
		rc.dataSetMetaData = c.MetaData( sl );
		let sc = UA_Server_addDataSetReader( &server, ReaderGroup, &rc, &DataSetReader );
		UA_DataSetMetaDataType_clear( &rc.dataSetMetaData );//deep-copied by addDataSetReader.
		check( sc, "addDataSetReader", sl );

		let size = c.Fields.size();
		auto targets = (UA_FieldTargetDataType*)UA_Array_new( size, &UA_TYPES[UA_TYPES_FIELDTARGETDATATYPE] );
		for( size_t i=0; i<size; ++i ){
			targets[i].attributeId = UA_ATTRIBUTEID_VALUE;
			UA_NodeId_copy( &c.Fields[i].Node, &targets[i].targetNodeId );
		}
		let targetsStatus = UA_Server_setDataSetReaderTargetVariables( &server, DataSetReader, size, targets );
		UA_Array_delete( targets, size, &UA_TYPES[UA_TYPES_FIELDTARGETDATATYPE] );
		check( targetsStatus, "setDataSetReaderTargetVariables", sl );
		check( UA_Server_enableAllPubSubComponents(&server), "enableAllPubSubComponents", sl );
		INFO( "PubSub reader: {}", c.ToString() );
		//A reader is an unauthenticated write path into the served address space: TargetVariables are written
		//server-internally - no session, no OpcAuthorize - and the only filter is the three ids, which are published in a
		//tracked config file.  Whoever reads a deployment's log has to be told, hence WARN rather than INFO.
		WARN( "PubSub reader on '{}' is UNAUTHENTICATED: any UADP publisher that reaches this url with publisherId={} writerGroupId={} dataSetWriterId={} writes {} node(s) into the address space, bypassing access control.  Remove /opcServer/pubsub (it lives in the Opc.Server.Emulator overlays, not the stock config) to disable.", c.Url, c.PublisherId, c.WriterGroupId, c.DataSetWriterId, c.Fields.size() );
	}
}
