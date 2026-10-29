#include <jde/web/server/SessionGraphQL.h>
#include <jde/access/Authorize.h>
#include <jde/fwk/io/protobuf.h>
#include <jde/fwk/io/json.h>
#include <jde/ql/ql.h>
#include <jde/ql/types/MutationQL.h>
#include <jde/ql/types/RequestQL.h>
#include <jde/ql/types/TableQL.h>
#include <jde/web/server/Sessions.h>
#include <jde/app/IApp.h>
#include "ServerImpl.h"

#define let const auto

namespace Jde::Web::Server{
	constexpr ELogTags _tags{ ELogTags::Sessions };
	struct SessionGraphQLAwait final: TAwait<jvalue>{
		SessionGraphQLAwait( const QL::TableQL& query, UserPK userPK, sp<App::IApp> appClient, sp<Access::Authorize> authorizer, string schema, bool forwarded, SRCE )ι:
			TAwait<jvalue>{ sl }, _appClient{ move(appClient) }, _authorizer{ move(authorizer) }, _schema{ move(schema) }, _forwarded{ forwarded }, Query{ query }, UserPK{ userPK }{}
		α Suspend()ι->void override{ Select(); }
		α Select()ι->TAwait<jvalue>::Task;
		sp<App::IApp> _appClient;
		sp<Access::Authorize> _authorizer;
		string _schema;
		bool _forwarded;
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
			//The listing, and another's session, take Read on the app schema's `sessions`.  A signed-in caller's own session is
			//theirs to read - the spa confirms its sign-in with session(id:<its own>) (app-service.ts validateSessionId), which can
			//not wait on a grant.  An id that is nobody's, or not found, takes the gate too:  a refusal is the same answer either
			//way, so a probe learns nothing.
			let own = sessionString && UserPK.Value && sessions.size()==1 && sessions.front()->UserPK.Value==UserPK.Value;
			if( !own && !_forwarded ){
				THROW_IF( !_authorizer, "No authorizer - refusing to list sessions." );
				_authorizer->Test( _schema, "sessions", Access::ERights::Read, UserPK, _sl );
			}
			if( !sessionString ){
				sessions = Sessions::Get();//after the gate:  a refused caller copies no map.
				std::erase_if( sessions, [now=steady_clock::now()]( let& s ){ return s->Expiration<now; } );//dead, but UpdateExpiration trims the map only lazily - at most once a RestSessionTimeout.
			}
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
	α SessionGraphQL::IsForwarded( const QL::RequestQL& q )ι->bool{
		return q.IsQueries() && q.Queries().size()==1 && q.Queries().front().JsonName=="sessions" && q.Queries().front().ReturnRaw;
	}
	α SessionGraphQL::IsPurge( const QL::MutationQL& m )ι->bool{ return m.Type==QL::EMutationQL::Purge && m.JsonTableName=="session"; }//purgeSession(id:…).  Not TableName():  no view, so no DBTable to name it.

	α SessionGraphQL::Select( const QL::TableQL& query, UserPK userPK, SL sl )ι->up<TAwait<jvalue>>{
		return mu<SessionGraphQLAwait>( query, userPK, _appClient, _authorizer, _schema, _forwarded, sl );
	}

	α SessionGraphQL::Purge( const QL::MutationQL& m, UserPK executer, SL sl )ι->up<TAwait<jvalue>>{
		return mu<CompletedAwait<jvalue>>( [m, executer, authorizer=_authorizer, schema=_schema, sl]()->jvalue{
			//A signed-in caller may end their own session, and one they signed its user in to:  a stand-alone gateway logs a user out
			//by purging the session it began for them (AddSession, Login - MintedBy) as its own identity (HttpRequestAwait::Logout),
			//so its sign-out needs no grant (364-sessions-resource review #2).  Anyone else's takes Administer on `sessions`.  Not the
			//id alone:  it is 32 bits, so holding one would let a guesser sign anyone out.  An id not found takes the gate, as the listing's.
			THROW_IF( !authorizer, "No authorizer - refusing to purge session." );
			let id = m.FindPtr( "id" );
			let sessionId = id ? Str::TryTo<SessionPK,16>( Json::AsString(*id) ) : nullopt;
			let session = sessionId ? Sessions::Find( *sessionId ) : sp<SessionInfo>{};
			let theirs = session && executer.Value && (session->UserPK.Value==executer.Value || session->MintedBy.Value==executer.Value);
			if( !theirs )
				authorizer->Test( schema, "sessions", Access::ERights::Administer, executer, sl );
			let rows = session && Sessions::Remove( session->SessionId ) ? 1 : 0;
			return jobject{ {"complete", true}, {"rowCount", rows} };
		}, sl );
	}
}