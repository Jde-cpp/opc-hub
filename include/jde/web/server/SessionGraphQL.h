#pragma once
#include <jde/ql/types/TableQL.h>

namespace Jde::Access{ struct Authorize; }
namespace Jde::App{ struct IApp; }
namespace Jde::QL{ struct MutationQL; struct RequestQL; }
namespace Jde::Web::Server{
	//session(id:…)/sessions{…} and purgeSession over this process's Sessions map.  No QL hook:  each application-server role routes
	//them from its own CustomQuery/CustomMutation (AppQLAwait, AppServerQL, HubQL), so a binary hosting two roles - the hub, the
	//suites that embed an AppServer - answers once, with the authorizer of the role whose QL was asked.  The gate is the app schema's
	//`sessions` resource (app-meta.jsonnet), by (schema, slug) - the hub also holds the gateway's `sessions`:  Read lists, Administer
	//purges another's (Purge).  An instance - an OpcServer, a stand-alone gateway - answers only the AppServer's socket (Forwarded).
	struct SessionGraphQL{
		SessionGraphQL( sp<App::IApp> appClient, sp<Access::Authorize> authorizer, string schema )ι: _appClient{move(appClient)}, _authorizer{move(authorizer)}, _schema{move(schema)}{}
		//An instance's answer to the AppServer's ask, ungated:  InstanceSessionsAwait gated the caller there, and an instance's acl
		//holds no app schema.  It purges nothing.
		Ω Forwarded( sp<App::IApp> appClient )ι->SessionGraphQL{ SessionGraphQL y{ move(appClient), nullptr, {} }; y._forwarded = true; return y; }
		Ω IsApplicable( const QL::TableQL& q )ι->bool;
		Ω IsForwarded( const QL::RequestQL& q )ι->bool;//the AppServer's ask, as InstanceSessionsAwait sends it:  one raw sessions{…}.
		Ω IsPurge( const QL::MutationQL& m )ι->bool;
		α Select( const QL::TableQL& query, UserPK userPK, SRCE )ι->up<TAwait<jvalue>>;
		α Purge( const QL::MutationQL& m, UserPK executer, SRCE )ι->up<TAwait<jvalue>>;
	private:
		sp<App::IApp> _appClient;
		sp<Access::Authorize> _authorizer;
		string _schema;
		bool _forwarded{};
	};
}