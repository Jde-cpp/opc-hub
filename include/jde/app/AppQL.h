#pragma once
#include <jde/ql/LocalQL.h>
#include <jde/app/usings.h>

namespace Jde::App{
	struct IApp;
	//logSchema:  the schema of this process's `logs` resource - `app` for the AppServer, `opc.<instance>` for an OpcServer - or
	//empty for a process that declares none (a standalone gateway), whose log routes stay authentication-only.
	//hiddenLogInstances:  the instances whose forwarded lines a user may not read - only an AppServer's log holds any.
	struct AppQL : QL::LocalQL{
		using HiddenLogInstancesΛ = function<flat_set<ProgInstPK>(UserPK)>;
		AppQL( vector<sp<DB::AppSchema>> schemas, sp<Access::Authorize> authorizer, string logSchema, HiddenLogInstancesΛ hiddenLogInstances={} )ι:
			QL::LocalQL{ move(schemas), authorizer }, _logSchema{ move(logSchema) }, _hiddenLogInstances{ move(hiddenLogInstances) }{};
		α LogQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->up<TAwait<jvalue>> override;
		α LogSettingsQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->up<TAwait<jvalue>> override;
		α StatusQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->jobject override;
	protected:
		//The log/logSettings/status routes bypass SelectAwait's Authorize, so these are the only gates they have.  status is
		//authentication only;  TestLogs is `rights` on <logSchema>/logs - open to anyone, anonymous too, while the resource is
		//unenforced - or authentication only without a logSchema.
		Ω RequireAuthenticated( QL::Creds& executer, sv what, SL sl )ε->void;
		α TestLogs( QL::Creds& executer, Access::ERights rights, sv what, SL sl )ε->void;
		//updateLogSetting(s), which every app's CustomMutation asks first:  null for any other mutation, a refusal when TestLogs
		//refuses Update, else the update on `app`.
		α LogSettingsMutation( QL::MutationQL& m, QL::Creds& creds, sp<IApp> app, SL sl )ι->up<TAwait<jvalue>>;
	private:
		string _logSchema;
		HiddenLogInstancesΛ _hiddenLogInstances;
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
