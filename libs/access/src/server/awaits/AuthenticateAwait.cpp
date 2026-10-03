#include <jde/access/server/awaits/AuthenticateAwait.h>
#include <jde/db/IDataSource.h>
#include <jde/db/Value.h>
#include <jde/db/generators/InsertClause.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/access/Authorize.h>
#include "../serverInternal.h"

#define let const auto
namespace Jde::Access::Server{
	α AuthenticateAwait::InsertUser( str prefix, vector<DB::Value>&& params )->TAwait<UserPK::Type>::Task{
		try{
			let userPK = UserPK{ co_await DS().InsertSeq<UserPK::Type>(DB::InsertClause{Ƒ("{}user_insert_login", prefix), move(params)}) };
			Authorizer().CreateUser( userPK, _loginName );//the login procs name the identity after its login name.
			PublishUserCreated( userPK, _loginName );//the clients' caches - CreateUser is the server's own
			ResumeScaler( userPK );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	α AuthenticateAwait::Execute()ι->DB::ScalerAwaitOpt<UserPK::Type>::Task{
		let& identities = GetTable( "identities" );
		let identityPK = identities.GetColumnPtr( "identity_id" );
		let providerFK = identities.GetColumnPtr( "provider_id" );
		DB::SelectClause select{ identityPK };
		DB::FromClause from;
		let& usersTable = GetTable( "users" );
		from.TryAdd( {identityPK, usersTable.GetPK(), true} );
		let& providers = GetTable( "providers" );
		from.TryAdd( {providerFK, providers.GetPK(), true} );
		DB::WhereClause where;
		where.Add( usersTable.GetColumnPtr("login_name"), _loginName );
		where.Add( providerFK, _providerId );
		auto slugColumn = providers.GetColumnPtr("slug");
		if( _opcServer.size() )
			where.Add( slugColumn, _opcServer );
		else
			where.Add( slugColumn, nullptr );
		auto sql = DB::Statement{ move(select), move(from), move(where) }.Move();
		try{
			auto params = sql.Params;
			let userPK = co_await DS().ScalerOpt<UserPK::Type>( move(sql) );
			if( !userPK ){
				if( _opcServer.empty() )
					params.emplace_back( nullptr );
				InsertUser( usersTable.Schema->Prefix, move(params) );
				co_return;
			}
			ResumeScaler( {*userPK} );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}