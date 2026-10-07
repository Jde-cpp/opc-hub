#pragma once
#include <jde/ql/QLHook.h>

namespace Jde::Access{ struct Authorize; }
namespace Jde::App{ struct IApp; }
namespace Jde::QL{ struct TableQL; }
namespace Jde::Web::Server{
	struct SessionGraphQL : QL::IQLHook{
		SessionGraphQL( sp<App::IApp> _appClient, sp<Access::Authorize> authorizer )ι: _appClient{move(_appClient)}, _authorizer{move(authorizer)}{}
		α Select( const QL::TableQL& query, UserPK userPK, SRCE )ι->HookResult override;
		β PurgeBefore( const QL::MutationQL&, UserPK, SRCE )ι->HookResult override;
	private:
		sp<App::IApp> _appClient;
		sp<Access::Authorize> _authorizer;
	};
}