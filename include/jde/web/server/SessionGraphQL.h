#pragma once
#include <jde/ql/QLHook.h>

namespace Jde::Access{ struct Authorize; }
namespace Jde::App{ struct IApp; }
namespace Jde::QL{ struct TableQL; }
namespace Jde::Web::Server{
	//session(id:…)/sessions{…} and purgeSession over this process's Sessions map.  Installed once a process (Install):  a binary
	//hosting two roles - the hub, the suites that embed an OpcServer - has one map, so a second hook would answer the same query
	//again and Hook::Fold would hand the pair back as a list.
	struct SessionGraphQL : QL::IQLHook{
		SessionGraphQL( sp<App::IApp> _appClient, sp<Access::Authorize> authorizer )ι: _appClient{move(_appClient)}, _authorizer{move(authorizer)}{}
		Ω Install( sp<App::IApp> appClient, sp<Access::Authorize> authorizer )ι->void;
		α Select( const QL::TableQL& query, UserPK userPK, SRCE )ι->HookResult override;
		β PurgeBefore( const QL::MutationQL&, UserPK, SRCE )ι->HookResult override;
	private:
		sp<App::IApp> _appClient;
		sp<Access::Authorize> _authorizer;
	};
}