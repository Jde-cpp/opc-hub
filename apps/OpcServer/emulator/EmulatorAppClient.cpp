#include "EmulatorAppClient.h"
#include <jde/app/IApp.h>
#include <jde/app/log/LogSettingsAwait.h>
#include <jde/ql/IQL.h>

namespace Jde::Opc::Emulator{
	static sp<App::Client::IAppClient> _appClient = ms<EmulatorAppClient>();
	α AppClient()ι->sp<App::Client::IAppClient>{ return _appClient; }

	//The AppServer pushes log-level changes as an `updateLogSetting` mutation when its instance_tag_levels rows change
	//(InstanceTagLevelAwait::pushRuntime -> QueryClient), and reads them back as a `logSetting` table.  The emulator
	//registers as an instance, so the log-settings page offers it those knobs;  without these two routes the push came
	//back "Only queries are supported" and the read "Table logSetting not supported", and the levels never moved - the
	//lesson OpcServerAppClient.cpp documents.  There is no LocalQL here, so the client awaitables ARE the route.
	//One table/mutation is the shape the AppServer sends;  anything else falls through to EmulatorQL's error, which now
	//names what arrived rather than claiming mutations are unsupported.
	Ω isLogSettingsPush( const QL::RequestQL& q )ι->bool{
		return q.IsMutation() && q.Mutations().size()==1 && App::LogSettingsMAwait::IsApplicable( q.Mutations().front() );
	}
	Ω isLogSettingsRead( const QL::RequestQL& q )ι->bool{
		if( !q.IsQueries() || q.Queries().size()!=1 )
			return false;
		const auto& name = q.Queries().front().JsonName;
		return name=="logSetting" || name=="logSettings";//the two spellings TablesAwait routes on.
	}
	α EmulatorAppClient::ClientQuery( QL::RequestQL&& q, Jde::UserPK executer, SL sl )ε->up<TAwait<jvalue>>{
		//No authenticated-executer guard here, deliberately - unlike OpcQL::CustomMutation, which needs one because the
		//OpcServer serves /graphql to the world and an anonymous push there rewrote its live levels (opcserver-review3 #9).
		//This process has no listener at all: ClientQuery is reachable only over the app-client socket, from the AppServer
		//that already gated updateInstanceTagLevel.  Re-gating on `executer` here would refuse whatever that server chose
		//to accept - which it did: the push carries the caller's UserPK, 0 for an anonymous one, and the levels then never
		//moved, which is the very defect this route exists to fix.
		if( isLogSettingsPush(q) )
			return mu<App::LogSettingsMAwait>( move(q.Mutations().front()), AppClient(), executer, sl );
		if( isLogSettingsRead(q) )
			return App::LogSettingsAwait( move(q.Queries().front()), sl );
		return IAppClient::ClientQuery( move(q), executer, sl );
	}
}
