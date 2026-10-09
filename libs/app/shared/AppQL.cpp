#include <jde/app/AppQL.h>
#include <jde/app/log/LogQLAwait.h>
#include <jde/app/IApp.h>
#include <jde/app/log/LogSettingsAwait.h>
#include <jde/access/AccessException.h>

namespace Jde::App{
	α AppQL::RequireAuthenticated( QL::Creds& executer, sv what, SL sl )ε->void{
		if( !executer.UserPK().Value )
			throw Exception{ sl, {ELogTags::App | ELogTags::Exception, 0, EHttpStatus::Unauthorized}, "[{}]An authenticated user is required.", what };
	}
	α AppQL::LogQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->up<TAwait<jvalue>>{
		RequireAuthenticated( executer, "logs", sl ); //every process log line - query texts, user names, cert DNs, hostnames, session ids.
		return mu<LogQLAwait>( move(ql), sl );
	}
	α AppQL::LogSettingsQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->up<TAwait<jvalue>>{
		RequireAuthenticated( executer, "logSettings", sl ); //the live tag/level configuration.
		return LogSettingsAwait( move(ql), sl );
	}
	α AppQL::LogSettingsMutation( QL::MutationQL& m, QL::Creds& creds, sp<IApp> app, SL sl )ι->up<TAwait<jvalue>>{
		if( !LogSettingsMAwait::IsApplicable(m) )
			return nullptr;
		//The AppServer's push arrives this way carrying the admin who ran updateInstanceTagLevel, so requiring a user costs it nothing.
		//Anonymous over a web listener it rewrote the live levels and, `persist` defaulting on, had the AppServer store them under
		//the instance's own identity (opcserver-review3 #9).  Unauthorized, not Forbidden:  it is about who is asking.
		if( !creds.UserPK().Value )
			return mu<RefusedMutation>( Ƒ("[{}]An authenticated user is required.", m.CommandName), creds.UserPK(), EHttpStatus::Unauthorized, sl );
		return mu<LogSettingsMAwait>( move(m), move(app), creds.UserPK(), sl );
	}
	α RefusedMutation::await_resume()ε->jvalue{
		throw Access::AccessException{ Source(), _executer, _status, "{}", _reason };
	}
	α AppQL::StatusQuery( QL::TableQL&&, QL::Creds executer, SL sl )ε->jobject{
		RequireAuthenticated( executer, "status", sl );
		return App::IApp::Status();
	}
}