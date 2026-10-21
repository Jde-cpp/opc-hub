#pragma once
namespace Jde::DB{ struct AppSchema; }

namespace Jde::App::Client{ struct IAppClient; }
namespace Jde::Opc::Server {
	struct UAServer; struct PubSubReader;

	α Initialize( sp<DB::AppSchema> schema )ε->void;
	α ResourceSuffix()ι->string;//`/opcServer/resource`, "" when unset:  the suffix of the access schema this server's nodes are resources of.
	α InstanceSchema()ι->string;//that schema, Access::InstanceSchemaName's:  `opc.<suffix>`, or `opc` with none.
	α AppClient()ι->sp<App::Client::IAppClient>;
	α GetSchema()ι->DB::AppSchema&;
	α GetSchemaPtr()ι->sp<DB::AppSchema>;
	α GetUAServer()ι->UAServer&;
	α FindUAServer()ι->UAServer*;//null before Initialize and after shutdown - Access::Client::Configure installs the acl listener before either.
	//the Part 14 subscriber on the current UAServer (/opcServer/pubsub); replaces any earlier one.
	α StartPubSub( const jobject& settings, SRCE )ε->void;
	α PubSub()ι->PubSubReader*;//null until StartPubSub.
}