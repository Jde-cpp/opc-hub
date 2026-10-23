#pragma once
namespace Jde::DB{ struct AppSchema; }

struct UA_DataTypeArray;
namespace Jde::App::Client{ struct IAppClient; }
namespace Jde::Opc::Server {
	struct UAServer; struct UASettings; struct PubSubReader;

	α Initialize( sp<DB::AppSchema> schema )ε->void;//the schema's Authorizer is the node rights the server asks:  Startup's OpcAuthorize.
	//The UA server's settings, from /opcServer, /opc and /access/trustedCertDirs.  Throws without /opcServer/ssl.
	α ReadUASettings()ε->UASettings;
	α ResourceSuffix()ι->string;//`/opcServer/resource`, "" when unset:  the suffix of the access schema this server's nodes are resources of.
	α InstanceSchema()ι->string;//that schema, Access::InstanceSchemaName's:  `opc.<suffix>`, or `opc` with none.
	α AppClient()ι->sp<App::Client::IAppClient>;
	α GetSchema()ι->DB::AppSchema&;
	α GetSchemaPtr()ι->sp<DB::AppSchema>;
	α GetUAServer()ι->UAServer&;
	α FindUAServer()ι->UAServer*;//null before Initialize and after shutdown - Access::Client::Configure installs the acl listener before either.
	α CustomTypes()ι->vector<UA_DataTypeArray>&;//PublishDataTypes' mirrors, kept beside the server, whose config reads them until it is deleted.
	//the Part 14 subscriber on the current UAServer (/opcServer/pubsub); replaces any earlier one.
	α StartPubSub( const jobject& settings, SRCE )ε->void;
	α PubSub()ι->PubSubReader*;//null until StartPubSub.
}