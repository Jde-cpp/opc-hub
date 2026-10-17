#include "OpcServerAppClient.h"
#include <jde/app/IApp.h>
#include <jde/app/client/appClient.h>
#include <jde/app/log/LogSettingsAwait.h>
#include <jde/ql/IQL.h>
#include <jde/ql/QLAwait.h>
#include "ql/OpcQL.h"
#include "globals.h"
#include "access/OpcAuthorize.h"
#include <jde/access/AccessException.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/web/server/SessionGraphQL.h>
#define let const auto

namespace Jde::Opc::Server{
	struct OpcServerQL : TAwait<jvalue>{
		using base = TAwait<jvalue>;
		OpcServerQL( QL::RequestQL&& q, Jde::UserPK executer, SL sl )ι:
			base{ sl },
			_q{ move(q) },
			_executer{ executer }
		{}
		α await_ready()ι->bool override{ return true; }
		α await_resume()ε->jvalue override;
	private:
		α Suspend()ι->void override{ ASSERT(false); }
		QL::RequestQL _q;
		Jde::UserPK _executer;
	};
	//The app server pushes log-level changes as updateLogSetting mutations, and reads the live levels with a logSetting query
	//for instanceTagLevel's `running`; OpcServerQL below answers `status` and the AppServer's admin check and nothing else, so
	//without this the push came back "Only queries are supported" and the read "Table logSetting not supported".  Only log
	//settings get through to the ql - everything else stays as restricted as it was.
	Ω isLogSettings( const QL::RequestQL& q )ι->bool{
		if( q.IsQueries() )
			return std::ranges::all_of( q.Queries(), [](const auto& t){return t.JsonName=="logSetting";} );
		return q.IsMutation() && std::ranges::all_of( q.Mutations(), [](const auto& m){return App::LogSettingsMAwait::IsApplicable(m);} );
	}
	//The page's Web Sessions tab:  InstanceSessionsAwait gates the caller on the AppServer, then asks raw for the bare list.  This
	//socket is the only way in - OpcQL routes no sessions query, since opcAuthorize holds no `sessions` resource and SessionGraphQL's
	//gate could never refuse a caller of this server's own /graphql.
	Ω isSessions( const QL::RequestQL& q )ι->bool{
		return q.IsQueries() && q.Queries().size()==1 && q.Queries().front().JsonName=="sessions" && q.Queries().front().ReturnRaw;
	}
	α OpcServerAppClient::ClientQuery( QL::RequestQL&& q, Jde::UserPK executer, SL sl )ε->up<TAwait<jvalue>>{
		if( isSessions(q) )
			return Web::Server::SessionGraphQL{ AppClient(), Acl() }.Select( q.Queries().front(), executer, sl );
		return isLogSettings( q )
			? mu<QL::QLAwait<>>( move(q), QL::Creds{executer}, Server::QLPtr(), sl )
			: up<TAwait<jvalue>>{ mu<OpcServerQL>(move(q), executer, sl) };
	}

	//The AppServer's delegated admin check (ServerSocketSession::TestAdminAwait, appserver-review3 #13):  who may grant on a node
	//is whoever administers the resource governing it, which only this server can resolve (OpcAuthorize::TestAdminNode).  The
	//shape is the AppServer's:  adminCheck( user:$user ){ isAdmin resource( resource:$slug, criteria:$criteria ) } - a
	//registered system table (Startup), since QL::Parse resolves the client's queries against no schema.
	//A denial is the answer;  anything else - not ready, an undecodable criteria - fails the query, which the AppServer also
	//takes as a denial.
	Ω adminCheck( const QL::TableQL& table, SL sl )ε->jvalue{
		const Jde::UserPK user{ Json::AsNumber<Jde::UserPK::Type>(table.ExtrapolateVariables(), "user") };//Args holds the $variable names; Extrapolate binds them.
		let resource = table.FindTable( "resource" );
		THROW_IFSL( !resource, "adminCheck needs a resource( resource, criteria ) sub-table." );
		let args = resource->ExtrapolateVariables();
		let slug = Json::AsString( args, "resource" );
		let criteria = Json::FindString( args, "criteria" ).value_or( string{} );
		auto& auth = static_cast<OpcAuthorize&>( *GetSchema().Authorizer );//installed by Startup, as UAAccess reads it.
		bool isAdmin{ true };
		try{
			auth.TestAdminNode( slug, criteria, user, sl );
		}
		catch( const Access::AccessException& e ){
			isAdmin = false;
			DBGT( ELogTags::Access, "[{}]{}.{}: {}", user.Value, slug, criteria, e.what() );
		}
		return jobject{ {"adminCheck", jobject{{"isAdmin", isAdmin}}} };
	}
	α OpcServerQL::await_resume()ε->jvalue{
		THROW_IF( !_q.IsQueries(), "Only queries are supported." );
		jvalue y;
		for( const auto& table : _q.Queries() ){
			if( table.JsonName=="status" )
				y = App::IApp::Status();
			else if( table.JsonName=="adminCheck" )
				y = adminCheck( table, Source() );
			else
				THROW( "Table {} not supported.", table.JsonName );
		}
		return y;
	}
}