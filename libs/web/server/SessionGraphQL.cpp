#include <jde/web/server/SessionGraphQL.h>
#include <jde/access/Authorize.h>
#include <jde/fwk/io/protobuf.h>
#include <jde/fwk/io/json.h>
#include <jde/ql/ql.h>
#include <jde/ql/types/TableQL.h>
#include <jde/web/server/Sessions.h>
#include <jde/app/IApp.h>
#include "ServerImpl.h"

#define let const auto

namespace Jde::Web::Server{
	constexpr ELogTags _tags{ ELogTags::Sessions };
	struct SessionGraphQLAwait final: TAwait<jvalue>{
		SessionGraphQLAwait( const QL::TableQL& query, UserPK userPK, sp<App::IApp> appClient, SRCE )ι:
			TAwait<jvalue>{ sl }, _appClient{ move(appClient) }, Query{ query }, UserPK{ userPK }{}
		α Suspend()ι->void override{ Select(); }
		α Select()ι->TAwait<jvalue>::Task;
		sp<App::IApp> _appClient;
		QL::TableQL Query;
		Jde::UserPK UserPK;
	};

	α SessionGraphQLAwait::Select()ι->TAwait<jvalue>::Task{
		try{
			let sessionString = Json::FindString( Query.Args, "id" );
			let sessionId = sessionString ? Str::TryTo<SessionPK>( *sessionString, 16 ) : nullopt;
			if( sessionString && !sessionId )
				co_return ResumeExp( Exception(_sl, {_tags}, "Could not parse sessionid: '{}'", *sessionString) );
			vector<sp<Server::SessionInfo>> sessions;
			if( !sessionString )
				sessions = Sessions::Get();
			else if( auto p = Sessions::Find(*sessionId); p )
				sessions.push_back( p );
			if( sessions.empty() )
				co_return Resume( jobject{} ); //(Json::Parse( "{\"data\": null}"))
			flat_map<Jde::UserPK, tuple<string,string>> userDomainLoginNames;
			jobject users;
			if( Query.FindColumn("domain") || Query.FindColumn("loginName") ){
				string inClause; inClause.reserve( sessions.size()*5 );
				for( let& session : sessions )
					inClause += std::to_string( session->UserPK.Value ) + ",";
				auto q = "query{ users(id:["+inClause.substr(0, inClause.size()-1)+"]){id loginName provider{id name}} }";
				users = Json::AsObject( co_await (*_appClient->Query<jvalue>(move(q), {})) );
				TRACET( _tags | ELogTags::Pedantic, "users={}"sv, serialize(users) );
				for( let& vuser : Json::AsArrayPath(users, "data/users") ){
					let& user = Json::AsObject(vuser);
					userDomainLoginNames[{Json::AsNumber<Jde::UserPK::Type>(user,"id")}] = make_tuple( Json::AsSVPath(user, "provider/name"), Json::AsString(user, "loginName") );
				}
			}
			auto array = Query.IsPlural() ? jarray{} : optional<jarray>{};
			for( let& session : sessions ){
				jobject j;
				if( auto pUser = userDomainLoginNames.find(session->UserPK); pUser!=userDomainLoginNames.end() ){
					if( Query.FindColumn("domain") )
						j["domain"] = std::get<0>(pUser->second);
					if( Query.FindColumn("loginName") )
						j["loginName"] = std::get<1>(pUser->second);
				}
#ifndef NDEBUG
				if( Query.FindColumn("id") )
					j["id"] = Ƒ( "{:x}", session->SessionId );
#endif
				if( Query.FindColumn("endpoint") )
					j["endpoint"] = session->UserEndpoint;
				if( Query.FindColumn("lastUpdate") )
					j["lastUpdate"] = ToIsoString( session->LastServerUpdate );
				if( Query.FindColumn("expiration") )
					j["expiration"] = ToIsoString( session->Expiration );
				if( array )
					array->emplace_back( move(j) );
				else{
					Resume( move(j) );
					break;
				}
			}
			if( array )
				Resume( move(*array) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	struct PurgeSessionAwait final: TAwait<jvalue>{
		PurgeSessionAwait( const QL::MutationQL& m, UserPK executer, sp<Access::Authorize> authorizer, SRCE )ι:
			TAwait<jvalue>{ sl }, _mutation{ m }, _executer{ executer }, _authorizer{ move(authorizer) }{}
		α await_resume()ε->jvalue override;
		α await_ready()ι->bool override;
	private:
		QL::MutationQL _mutation;
		Jde::UserPK _executer;
		sp<Access::Authorize> _authorizer;
		jobject _result{ {"complete", true} };
		up<Exception> _exception;
	};
	α PurgeSessionAwait::await_ready()ι->bool{
		uint rows = 0;
		try{
			//the acl is the only gate available here - ownership can't be one.  OpcGateway logs a user out by purging *their*
			//session as its own service identity (HttpRequestAwait::Logout), so a legitimate purge is routinely cross-user and
			//"you may only purge your own" would break logout.  admin over "sessions" is what separates that from an attacker.
			THROW_IF( !_authorizer, "No authorizer - refusing to purge session." );
			_authorizer->TestAdmin( "sessions", _executer, _sl );
			if( auto sessionId = _mutation.FindPtr("id"); sessionId )
				rows = Sessions::Remove( Str::TryTo<SessionPK>(Json::AsString(*sessionId), 16).value_or(0) ) ? 1 : 0;
			_result["rowCount"] =	rows;
		}
		catch( Exception& e ){
			_exception = e.Move();
		}
		return true;
	}
	α PurgeSessionAwait::await_resume()ε->jvalue{
		if( _exception )
			_exception->Throw();
		return _result;
	}

	α SessionGraphQL::Select( const QL::TableQL& query, UserPK userPK, SL sl )ι->up<TAwait<jvalue>>{
		return query.JsonName.starts_with( "session" ) ? mu<SessionGraphQLAwait>( query, userPK, _appClient, sl ) : nullptr;
	}

	α SessionGraphQL::PurgeBefore( const QL::MutationQL& m, UserPK executer, SL sl )ι->HookResult{
		return m.TableName()=="sessions" ? mu<PurgeSessionAwait>( m, executer, _authorizer, sl ) : nullptr;
	}
}