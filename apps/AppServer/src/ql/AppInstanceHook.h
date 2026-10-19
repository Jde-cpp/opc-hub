#pragma once
#include <jde/ql/QLHook.h>

namespace Jde::App{ struct IApp; }
namespace Jde::App::Server{
	struct AppInstanceHook final : QL::IQLHook{
		AppInstanceHook( sp<IApp> appClient )ι: _appClient{move(appClient)}{}
		α Start( const QL::MutationQL& m, UserPK userPK, SRCE )ι->HookResult override;//a start is not implemented:  a refusal.
		α Stop( const QL::MutationQL& m, UserPK userPK, SRCE )ι->HookResult override;
	private:
		sp<IApp> _appClient;
	};
}