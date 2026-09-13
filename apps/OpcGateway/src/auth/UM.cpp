#include "UM.h"
#include <jde/ql/IQL.h>
#include <jde/app/client/AppClientSocketSession.h>
//#include <jde/app/client/appClient.h>
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

	α ProviderMAwait::Execute( ServerCnnctnPK opcPK )ι->TAwait<vector<ServerCnnctn>>::Task{
		try{
			auto server = co_await ServerCnnctnAwait{ opcPK, true };
			THROW_IF( server.empty(), "[{}]Could not find OpcServer", opcPK );
			if( _insert )
				Check( move(server.front().Slug) );
			else
				Purge( move(server.front().Slug) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	//An existing row is reused, not re-inserted:  the seed ships one for the bundled OpcServer (release-opcServer.mutation -
	//applied inside the schema sync, before this hook is registered), and (type, slug) is the natural key, so a second
	//createProvider would fail and take the connection insert down with it.
	α ProviderMAwait::Check( string slug )ι->ProviderAwait::Task{
		try{
			let existing = co_await ProviderAwait{ slug };
			if( existing )
				ResumeScaler( existing );
			else
				Insert( slug );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α ProviderMAwait::Insert( str slug )ι->TAwait<jobject>::Task{
		let q = Ƒ( "createProvider( slug:\"{}\", providerType:\"OpcServer\" ){{id}}", slug );
		try{
			auto appClient = AppClient();
			let j = co_await *appClient->QLServer()->QueryObject( q, {}, appClient->UserPK() );
			let newPK = QL::AsId<Access::ProviderPK>( j );
			ResumeScaler( newPK );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	α ProviderMAwait::Purge( str slug )ι->ProviderAwait::Task{
		try{
			let providerPK = co_await ProviderAwait{ slug };
			if( !providerPK )
				ResumeScaler( providerPK );
			else
				Purge( providerPK );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α ProviderMAwait::Purge( Access::ProviderPK providerPK )ι->TAwait<jvalue>::Task{
		ASSERT( providerPK );
		jobject vars{ {"id", providerPK} };
		constexpr auto q = "purgeProvider( id:$id )";
		try{
			auto appClient = AppClient();
			co_await *appClient->QLServer()->Query( q, move(vars), appClient->UserPK() );
			ResumeScaler( providerPK );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α ProviderMAwait::Suspend()ι->void{
		if( _opcKey.IsPK() )
			Execute( _opcKey.PK() );
		else if( _insert )
			Check( _opcKey.NK() );
		else
			Purge( _opcKey.NK() );
	}
}