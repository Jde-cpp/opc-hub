#pragma once
#include <jde/ql/types/TableQL.h>

namespace Jde::Access{ struct Authorize; }
namespace Jde::App{ struct IApp; }
namespace Jde::QL{ struct MutationQL; }
namespace Jde::Web::Server{
	//session(id:…)/sessions{…} and purgeSession over this process's Sessions map.  No QL hook:  each role routes them from its own
	//CustomQuery/CustomMutation (AppQLAwait, AppServerQL, HubQL, GatewayQL), so a binary hosting two roles - the hub, the suites
	//that embed an AppServer - answers once, with the authorizer of the role whose QL was asked.  The OpcServer answers only the
	//AppServer's socket (OpcServerAppClient::ClientQuery).
	struct SessionGraphQL{
		SessionGraphQL( sp<App::IApp> appClient, sp<Access::Authorize> authorizer )ι: _appClient{move(appClient)}, _authorizer{move(authorizer)}{}
		Ω IsApplicable( const QL::TableQL& q )ι->bool;
		Ω IsPurge( const QL::MutationQL& m )ι->bool;
		α Select( const QL::TableQL& query, UserPK userPK, SRCE )ι->up<TAwait<jvalue>>;
		α Purge( const QL::MutationQL& m, UserPK executer, SRCE )ι->up<TAwait<jvalue>>;
	private:
		sp<App::IApp> _appClient;
		sp<Access::Authorize> _authorizer;
	};
}