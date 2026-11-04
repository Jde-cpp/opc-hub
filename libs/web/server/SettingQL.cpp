#include <jde/web/server/SettingQL.h>
#include <jde/fwk/co/Await.h>
#include <jde/app/IApp.h>
#include <jde/ql/types/TableQL.h>
#include <jde/web/server/Sessions.h>
#define let const auto

namespace Jde::Web::Server{
	α ServerSetting( sv target, const App::IApp& appClient )ε->jvalue{
		jvalue y;
		if( target=="restSessionTimeout" )
			y = Chrono::ToString<steady_clock::duration>( Sessions::RestSessionTimeout() );
		else if( target=="serverConnection" )
			y = appClient.ConnectionPK();
		else if( let value = Settings::FindString(Ƒ("/http/clientSettings/{}", target)); value )
			y = *value;
		return y;
	}

	α SettingQL( QL::TableQL query, sp<App::IApp> appClient, SL sl )ι->up<TAwait<jvalue>>{
		return mu<CompletedAwait<jvalue>>( [query=move(query), appClient=move(appClient), sl]()->jvalue{
			auto y = query.DefaultResult();
			let targets = query.As<jvalue>( "target", sl );
			Json::Visit( targets, [&]( const sv& target ){
				jobject setting;
				if( query.FindColumn("target") )
					setting["target"] = target;
				if( query.FindColumn("value") )
					setting["value"] = ServerSetting( target, *appClient );

				if( y.is_array() )//DefaultResult: [] for a plural query, {} for one.
					y.get_array().push_back( move(setting) );
				else
					y = move( setting );
			});
			return y;
		}, sl );
	}
}