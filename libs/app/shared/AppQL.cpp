#include <jde/app/AppQL.h>
#include <jde/app/log/LogQLAwait.h>
#include <jde/app/IApp.h>
#include <jde/app/log/LogSettingsAwait.h>
#include <jde/access/AccessException.h>
#include <jde/access/Authorize.h>

namespace Jde::App{
	α AppQL::RequireAuthenticated( QL::Creds& executer, sv what, SL sl )ε->void{
		if( !executer.UserPK().Value )
			throw Exception{ sl, {ELogTags::App | ELogTags::Exception, 0, EHttpStatus::Unauthorized}, "[{}]An authenticated user is required.", what };
	}
	α AppQL::TestLogs( QL::Creds& executer, Access::ERights rights, sv what, SL sl )ε->void{
		if( _logSchema.empty() )//no resource to enforce, so nothing could ever close it to anonymous callers.
			RequireAuthenticated( executer, what, sl );
		else
			Authorizer().Test( _logSchema, "logs", rights, executer.UserPK(), sl );//an anonymous caller passes an unenforced resource, gets 401 from an enforced one.
	}
	α AppQL::LogQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->up<TAwait<jvalue>>{
		TestLogs( executer, Access::ERights::Read, "logs", sl ); //every process log line - query texts, user names, cert DNs, hostnames, session ids.
		return mu<LogQLAwait>( move(ql), _hiddenLogInstances ? _hiddenLogInstances(executer.UserPK()) : flat_set<ProgInstPK>{}, sl );
	}
	α AppQL::LogSettingsQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->up<TAwait<jvalue>>{
		TestLogs( executer, Access::ERights::Read, "logSettings", sl ); //the live tag/level configuration.
		return LogSettingsAwait( move(ql), sl );
	}
	α AppQL::LogSettingsMutation( QL::MutationQL& m, QL::Creds& creds, sp<IApp> app, SL sl )ι->up<TAwait<jvalue>>{
		if( !LogSettingsMAwait::IsApplicable(m) )
			return nullptr;
		//The AppServer's push arrives this way carrying the admin who ran updateInstanceTagLevel.  An anonymous caller rewrites the
		//live levels and, `persist` defaulting on, has the AppServer store them under the instance's own identity
		//(opcserver-review3 #9) - only while the logs resource is unenforced, when updateInstanceTagLevel is open to it anyway.
		try{
			TestLogs( creds, Access::ERights::Update, m.CommandName, sl );
		}
		catch( const Exception& e ){
			return mu<RefusedMutation>( e.what(), creds.UserPK(), e.HttpStatus(), sl );
		}
		catch( const runtime_error& e ){
			return mu<RefusedMutation>( e.what(), creds.UserPK(), EHttpStatus::Forbidden, sl );
		}
		return mu<LogSettingsMAwait>( move(m), move(app), creds.UserPK(), sl );
	}
	α RefusedMutation::await_resume()ε->jvalue{
		throw Access::AccessException{ Source(), _executer, ELogLevel::Debug, _status, "{}", _reason };
	}
	α AppQL::StatusQuery( QL::TableQL&&, QL::Creds executer, SL sl )ε->jobject{
		RequireAuthenticated( executer, "status", sl );
		return App::IApp::Status();
	}
}