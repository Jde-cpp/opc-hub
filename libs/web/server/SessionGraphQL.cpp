#include <jde/web/server/SessionGraphQL.h>
#include <jde/access/Authorize.h>
#include <jde/fwk/io/protobuf.h>
#include <jde/fwk/io/json.h>
#include <jde/ql/ql.h>
#include <jde/ql/types/MutationQL.h>
#include <jde/ql/types/TableQL.h>
#include <jde/web/server/Sessions.h>
#include <jde/app/IApp.h>
#include "ServerImpl.h"

#define let const auto

namespace Jde::Web::Server{
	constexpr ELogTags _tags{ ELogTags::Sessions };
	struct SessionGraphQLAwait final: TAwait<jvalue>{
		SessionGraphQLAwait( const QL::TableQL& query, UserPK userPK, sp<App::IApp> appClient, sp<Access::Authorize> authorizer, SRCE )ι:
			TAwait<jvalue>{ sl }, _appClient{ move(appClient) }, _authorizer{ move(authorizer) }, Query{ query }, UserPK{ userPK }{}
		α Suspend()ι->void override{ Select(); }
		α Select()ι->TAwait<jvalue>::Task;
		sp<App::IApp> _appClient;
		sp<Access::Authorize> _authorizer;
		QL::TableQL Query;
		Jde::UserPK UserPK;
	};

	α SessionGraphQLAwait::Select()ι->TAwait<jvalue>::Task{
		try{
			if( Query.Args.contains("instanceId") )//only the AppServer's sessions( instanceId ) forwards (InstanceSessionsAwait);  this map is the process's own.
				co_return ResumeExp( Exception(_sl, {_tags, 0, EHttpStatus::BadRequest}, "{}( instanceId ) - only the application server's sessions( instanceId ) lists another instance's.", Query.JsonName) );
			let sessionString = Json::FindString( Query.Args, "id" );
			let sessionId = sessionString ? Str::TryTo<SessionPK,16>( *sessionString ) : nullopt;
			if( sessionString && !sessionId )
				co_return ResumeExp( Exception(_sl, {_tags}, "Could not parse sessionid: '{}'", *sessionString) );
			vector<sp<Server::SessionInfo>> sessions;
			if( sessionId ){
				if( auto p = Sessions::Find(*sessionId); p )
					sessions.push_back( p );
			}
			//The listing, and another's session, are gated as the purge is (PurgeBefore):  admin over `sessions`.  A signed-in
			//caller's own session is theirs to read - the spa confirms its sign-in with session(id:<its own>) (app-service.ts
			//validateSessionId), which can not wait on a grant.  An id that is nobody's, or not found, takes the gate too:  a
			//refusal is the same answer either way, so a probe learns nothing.
			let own = sessionString && UserPK.Value && sessions.size()==1 && sessions.front()->UserPK.Value==UserPK.Value;
			if( !own ){
				THROW_IF( !_authorizer, "No authorizer - refusing to list sessions." );
				_authorizer->TestAdminSlug( "sessions", UserPK, _sl );
			}
			if( !sessionString )
				sessions = Sessions::Get();//after the gate:  a refused caller copies no map.
			if( sessions.empty() )
				co_return Resume( Query.DefaultResult() );//[] for the listing, {} for one - the shape the caller indexes.
			flat_map<Jde::UserPK, tuple<string,string>> userDomainLoginNames;
			let named = Query.FindColumn("domain") || Query.FindColumn("loginName");
			flat_set<Jde::UserPK::Type> ids;//once each - a browser holds a session per Bearer request - and no anonymous 0.
			for( let& session : sessions ){
				if( named && session->UserPK )
					ids.emplace( session->UserPK.Value );
			}
			if( ids.size() ){
				try{
					auto q = Ƒ( "query{{ users(id:[{}]){{id loginName name provider}} }}", Str::Join(ids) );//provider as the scalar:  the enum's name (Google, Key);  the nested provider{{name}} form answers nothing.
					auto users = co_await (*_appClient->Query<jvalue>(move(q), {}, true, _sl));//raw - the bare rows, as OpcSessionsQLAwait reads the same query (query{…} is raw whatever is asked).
					TRACET( _tags | ELogTags::Pedantic, "users={}"sv, serialize(users) );
					for( let& vuser : Json::AsArray(users) ){
						let& user = Json::AsObject( vuser );
						let domain = Json::FindString( user, "provider" ).value_or( string{} );//null for a user without one.
						let loginName = Json::FindString( user, "loginName" ).value_or( Json::FindString(user, "name").value_or(string{}) );//the name, for a user with no login of their own.
						userDomainLoginNames[{Json::AsNumber<Jde::UserPK::Type>(user,"id")}] = make_tuple( domain, loginName );
					}
				}
				catch( runtime_error& e ){//the names are the decoration:  a refused or timed-out lookup still lists the sessions.
					WARN( "The users lookup failed, so the sessions are listed without names:  {}", e.what() );
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
				else if( named && session->UserPK && Query.FindColumn("loginName") )
					j["loginName"] = nullptr;//signed in, but not named (a failed lookup, a user it did not return):  null - absent is anonymous.
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

	α SessionGraphQL::IsApplicable( const QL::TableQL& q )ι->bool{ return q.JsonName=="session" || q.JsonName=="sessions"; }
	α SessionGraphQL::IsPurge( const QL::MutationQL& m )ι->bool{ return m.Type==QL::EMutationQL::Purge && m.JsonTableName=="session"; }//purgeSession(id:…).  Not TableName():  no view, so no DBTable to name it.

	α SessionGraphQL::Select( const QL::TableQL& query, UserPK userPK, SL sl )ι->up<TAwait<jvalue>>{
		return mu<SessionGraphQLAwait>( query, userPK, _appClient, _authorizer, sl );
	}

	α SessionGraphQL::Purge( const QL::MutationQL& m, UserPK executer, SL sl )ι->up<TAwait<jvalue>>{
		return mu<CompletedAwait<jvalue>>( [m, executer, authorizer=_authorizer, sl]()->jvalue{
			//the acl is the only gate available here - ownership can't be one.  OpcGateway logs a user out by purging *their*
			//session as its own service identity (HttpRequestAwait::Logout), so a legitimate purge is routinely cross-user and
			//"you may only purge your own" would break logout.  admin over "sessions" is what separates that from an attacker.
			THROW_IF( !authorizer, "No authorizer - refusing to purge session." );
			authorizer->TestAdminSlug( "sessions", executer, sl );
			uint rows{};
			if( auto sessionId = m.FindPtr("id"); sessionId )
				rows = Sessions::Remove( Str::TryTo<SessionPK,16>(Json::AsString(*sessionId)).value_or(0) ) ? 1 : 0;
			return jobject{ {"complete", true}, {"rowCount", rows} };
		}, sl );
	}
}