#pragma once
#include "../uatypes/NodeId.h"

struct UA_Server;

namespace Jde::Opc::PubSub{
	//One DataSet field: `Path` under Objects (see BrowsePath), resolved to `Node` on whichever server publishes or
	//receives it - a writer samples the node, a reader writes into it.
	struct Field{
		string Name;
		const UA_DataType* Type{};
		string Path;
		NodeId Node;
	};
	//The Part 14 DataSet contract both ends build from the same jsonnet (apps/OpcServer/config/pubsub/*.libsonnet).  A
	//writer and a reader agree on the wire format only through this: field order and types, and the publisher /
	//writerGroup / dataSetWriter ids the reader filters on.
	struct Config{
		Config( const jobject& o, SRCE )ε;
		//BrowsePath::Resolve every field on `server`; `DataSetName` aliases `Namespace`'s runtime index ("pumps~pump1").
		α Resolve( UA_Server& server, SRCE )ε->void;
		α MetaData( SRCE )Ε->UA_DataSetMetaDataType;//owned - UA_DataSetMetaDataType_clear when done.
		α ToString()Ι->string;

		string TransportProfile, Url, NetworkInterface, DataSetName, Namespace;
		UA_UInt16 PublisherId{}, WriterGroupId{}, DataSetWriterId{};
		Duration PublishingInterval{ 1s };
		vector<Field> Fields;
	};
	//Publisher: PublishedDataSet(Fields) -> WriterGroup(UADP, PublishingInterval) -> DataSetWriter on a connection
	//with Config.PublisherId.  Fields must be resolved on `server` - their values are sampled from those nodes.
	//Enables every PubSub component on the server; the components die with UA_Server_delete.
	struct Writer final : noncopyable{
		Writer( UA_Server& server, const Config& config, SRCE )ε;
		UA_NodeId Connection{}, DataSet{}, WriterGroup{}, DataSetWriter{};
	};
	//Subscriber: ReaderGroup -> DataSetReader keyed on the writer's ids; received fields land in Fields' nodes
	//(TargetVariables), through the server-internal write - no session, no access control.
	struct Reader final : noncopyable{
		Reader( UA_Server& server, const Config& config, SRCE )ε;
		UA_NodeId Connection{}, ReaderGroup{}, DataSetReader{};
	};
}
