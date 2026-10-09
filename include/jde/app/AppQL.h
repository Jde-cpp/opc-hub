#pragma once
#include <jde/ql/LocalQL.h>

namespace Jde::App{
	struct IApp;
	struct AppQL : QL::LocalQL{
		AppQL(vector<sp<DB::AppSchema>> schemas, sp<Access::Authorize> authorizer)ι:QL::LocalQL{ move(schemas), authorizer }{};
		α LogQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->up<TAwait<jvalue>> override;
		α LogSettingsQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->up<TAwait<jvalue>> override;
		α StatusQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->jobject override;
	protected:
		//The log/logSettings/status routes bypass SelectAwait's Authorize, so this is the only gate they have.  Authentication,
		//not authorization:  no acl resource exists for the log archive or the status document, and all three were anonymous.
		Ω RequireAuthenticated( QL::Creds& executer, sv what, SL sl )ε->void;
		//updateLogSetting(s), which every app's CustomMutation asks first:  null for any other mutation, a refusal for an anonymous
		//caller, else the update on `app`.
		Ω LogSettingsMutation( QL::MutationQL& m, QL::Creds& creds, sp<IApp> app, SL sl )ι->up<TAwait<jvalue>>;
	};

	//CustomMutation is ι, so a refusal has to be an await that throws on resume, not a throw.
	struct RefusedMutation final : TAwait<jvalue>{
		RefusedMutation( string reason, UserPK executer, EHttpStatus status, SL sl )ι:TAwait<jvalue>{sl}, _reason{move(reason)}, _executer{executer}, _status{status}{}
		α await_ready()ι->bool override{ return true; }
		α await_resume()ε->jvalue override;
	private:
		α Suspend()ι->void override{ ASSERT(false); }
		string _reason;
		UserPK _executer;
		EHttpStatus _status;
	};
}
