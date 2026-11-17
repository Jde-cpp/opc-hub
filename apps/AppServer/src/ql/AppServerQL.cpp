#include "AppServerQL.h"
#include <jde/db/meta/AppSchema.h>
#include <jde/access/Authorize.h>
#include <jde/access/server/accessServer.h>
#include <jde/app/log/LogQLAwait.h>
#include <jde/app/IApp.h>
#include <jde/calendar/server/CalendarMAwait.h>
#include <jde/calendar/server/OccurrencesQLAwait.h>
#include <jde/web/server/SessionGraphQL.h>
#include <jde/web/server/SettingQL.h>
#include "../LocalClient.h"
#include "../appStartup.h"
#include "AppInstanceMAwait.h"
#include "ConnectionQLAwait.h"
#include "InstanceSessionsAwait.h"
#include "InstanceTagLevelAwait.h"

#define let const auto
namespace Jde::App{
	sp<QL::LocalQL> _ql;
	α Server::QLPtr()ι->sp<QL::LocalQL>{ return App::_ql; }
	α Server::QL()ι->QL::LocalQL&{ return *QLPtr(); }
	α Server::ConfigureQL( vector<sp<DB::AppSchema>> schemas, sp<Access::Authorize> authorizer )ι->void{
		QL::Configure( schemas );
		_ql = ms<AppServerQL>( move(schemas), move(authorizer) );
	}
	α Server::SetQL( sp<QL::LocalQL> ql )ι->void{ _ql = move(ql); }

	Ω sessionGraphQL()ι->Web::Server::SessionGraphQL{ return { Server::AppClient(), Server::Authorizer(), Server::AppSchema()->Name }; }
	//The registry, `connections`, declares no right, so any known user reads it, and where the install has a guest that is every
	//visitor (docs/demo/spec.md *Guests*).  What a route finds its instance by stays so, as /opcGateways answers discovery
	//(types/rest/json.h):  the row's id, the instance's and its names, and `deleted:null`, the live rows.  The rest is the
	//instances' own:  the host, the pid, the start, a run's end - an instance's restarts - and the live status, a filter or an
	//order by anything else, and the hosts' rows, queried or introspected as an enum.
	Ω instancesOwn( const QL::TableQL& q )ι->bool{
		let open = []( sv name ){ return name=="id" || name=="instanceId" || name=="instanceName" || name=="programName"; };
		let openArg = [&]( const jobject::value_type& arg ){
			let key = arg.key();
			return open( key ) || (key=="deleted" && arg.value().is_null()) || key=="limit" || key=="offset" || key=="skip";
		};
		if( let table = q.DBTable(); table && table->Name=="hosts" && table->Schema==Server::AppSchema() )
			return q.JsonName!="__type" || q.FindTable( "enumValues" );
		return q.JsonName.starts_with( "connection" ) && (
			q.Tables.size()
			|| std::ranges::any_of( q.Columns, [&](let& c){ return !open(c.JsonName); } )
			|| !std::ranges::all_of( q.Args, openArg ) );
	}
	α Server::CustomQuery( QL::TableQL& q, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>>{
		if( instancesOwn(q) ){//Read on app/instances, where that is enforced.
			if( let read = Authorizer()->Check(AppSchema()->Name, "instances", {}, Access::ERights::Read, creds.UserPK(), sl); !read )
				return mu<RefusedAwait>( read.error().what(), creds.UserPK(), read.error().HttpStatus(), sl );
		}
		if( auto await = Access::Server::CustomQuery(q, creds, sl); await )
			return await;
		if( Calendar::Server::OccurrencesQLAwait::IsApplicable(q) )//calendarOccurrences (docs/calendar/spec.md *Reads*);  QLPtr() is the HubQL in the hub.
			return mu<Calendar::Server::OccurrencesQLAwait>( move(q), move(creds), QLPtr(), sl );
		if( ConnectionQLAwait::IsApplicable(q) )
			return mu<ConnectionQLAwait>( move(q), move(creds), sl );
		if( q.JsonName.starts_with("setting") )
			return Web::Server::SettingQL( move(q), AppClient(), sl );
		if( InstanceTagLevelAwait::IsApplicable(q) )
			return mu<InstanceTagLevelAwait>( move(q), creds.UserPK(), sl );
		if( InstanceSessionsAwait::IsApplicable(q) )
			return mu<InstanceSessionsAwait>( move(q), creds.UserPK(), sl );
		if( Web::Server::SessionGraphQL::IsApplicable(q) )//this process's own map - the hub's too, which asks only this.
			return sessionGraphQL().Select( q, creds.UserPK(), sl );
		return nullptr;
	}
	α Server::CustomMutation( QL::MutationQL& m, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>>{
		if( auto await = Access::Server::CustomMutation(m, creds, sl); await )
			return await;
		if( Calendar::Server::CalendarMAwait::IsApplicable(m) )//the calendar tables' edits, validated (docs/calendar/spec.md *Validation*);  QLPtr() is the HubQL in the hub.
			return mu<Calendar::Server::CalendarMAwait>( move(m), move(creds), QLPtr(), sl );
		if( auto await = AppInstanceMutation(m, sl); await )//start/stopApplicationInstance.
			return await;
		if( InstanceTagLevelMAwait::IsApplicable(m) )
			return mu<InstanceTagLevelMAwait>( move(m), creds.UserPK(), sl );
		if( Web::Server::SessionGraphQL::IsPurge(m) )//a stand-alone gateway's logout, as its own account.
			return sessionGraphQL().Purge( m, creds.UserPK(), sl );
		return nullptr;
	}
}

namespace Jde::App::Server{
	AppServerQL::AppServerQL( vector<sp<DB::AppSchema>>&& schemas, sp<Access::Authorize>&& authorizer )ι:
		AppQL{ move(schemas), move(authorizer), App::Server::AppSchema()->Name, App::HiddenLogInstances }//as logGate and InstanceLogSchema:  the app schema's own name.
	{}

	α AppServerQL::CustomQuery( QL::TableQL& q, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>>{
		return Server::CustomQuery( q, move(creds), sl );
	}
	α AppServerQL::CustomMutation( QL::MutationQL& m, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>>{
		if( auto await = LogSettingsMutation(m, creds, AppClient(), sl); await )
			return await;
		return Server::CustomMutation( m, move(creds), sl );
	}
}