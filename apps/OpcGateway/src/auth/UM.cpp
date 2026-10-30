#include "UM.h"
#include <jde/ql/IQL.h>
#include <jde/app/client/AppClientSocketSession.h>
#include <jde/app/client/IAppClient.h>
#include "../GatewayAppClient.h"
#include "../UAClient.h"

#define let const auto

namespace Jde::Opc::Gateway{
	α ProviderAwait::Execute()ι->TAwait<jobject>::Task{
		try{
			constexpr auto q = "provider(name:$opcSlug){ id }";
			jobject vars{ {"opcSlug", _opcId} };
			let j = co_await *AppClient()->Query( q, move(vars) );
			let providerId = Json::FindNumber<Access::ProviderPK>( j, "id" ).value_or(0);
			ResumeScaler( providerId );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	//An existing row is reused, not re-inserted:  the seed ships one for the bundled OpcServer (release-opcServer.mutation -
	//applied inside the schema sync, before the gateway's login, which ServerCnnctnMAwait waits for), and (type, slug) is the
	//natural key, so a second createProvider would fail and take the connection insert down with it.
	α ProviderMAwait::Execute()ι->VoidTask{
		try{
			string slug;
			if( _opcKey.IsPK() ){
				ServerCnnctnAwait select{ _opcKey, true };
				auto servers = co_await Any( select );
				THROW_IF( servers.empty(), "[{}]Could not find OpcServer", _opcKey.PK() );
				slug = move( servers.front().Slug );
			}
			else
				slug = _opcKey.NK();
			ProviderAwait find{ slug };
			auto providerPK = co_await Any( find );
			auto appClient = AppClient();
			if( _insert && !providerPK ){
				let j = co_await Any( *appClient->QLServer()->QueryObject(Ƒ("createProvider( slug:\"{}\", providerType:\"OpcServer\" ){{id}}", slug), {}, appClient->UserPK()) );
				providerPK = QL::AsId<Access::ProviderPK>( j );
			}
			else if( !_insert && providerPK )
				co_await Any( *appClient->QLServer()->Query("purgeProvider( id:$id )", jobject{{"id", providerPK}}, appClient->UserPK()) );
			ResumeScaler( providerPK );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}