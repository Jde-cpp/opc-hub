#include <jde/access/server/awaits/AdminWriteAwait.h>
#include <jde/access/Authorize.h>
#include <jde/ql/LocalQL.h>
#include <jde/ql/ops/MutationAwait.h>
#include "../serverInternal.h"

#define let const auto
namespace Jde::Access::Server{
	//The original mutation, variables and result request included, through the stock path:  as the system it skips
	//CustomMutation's admin branches and passes Table::Authorize.
	Ω asSystem( QL::MutationQL&& m, SL sl )ι->QL::MutationAwait{
		return QL::MutationAwait{ move(m), QL::Creds{UserPK{UserPK::System}}, LocalQL().shared_from_this(), sl };
	}

	α PermissionRightMAwait::Execute()ι->TAwait<jvalue>::Task{
		try{
			let args = _mutation.ExtrapolateVariables();
			Authorizer().TestAdminPermission( PermissionPK{Json::AsNumber<PermissionPK::Type>(args, "id")}, _executer, _sl );
			auto result = co_await asSystem( move(_mutation), _sl );
			Resume( move(result) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	α ResourceMAwait::Execute()ι->TAwait<jvalue>::Task{
		try{
			auto& auth = Authorizer();
			auth.TestUser( _executer, _sl );
			let args = _mutation.ExtrapolateVariables();
			flat_set<string> schemas;
			if( let schema = Json::FindString(args, "schemaName"); schema )
				schemas.emplace( *schema );
			if( _mutation.Type==QL::EMutationQL::Update ){
				let id = Json::FindNumber<ResourcePK::Type>( args, "id" );
				THROW_IFX( !id, Exception(_sl, ExceptionArgs{EHttpStatus::BadRequest}, "updateResource needs the row's id - a slug is not unique across schemas.") );
				let row = co_await *LocalQL().Query( Ƒ("resource( id:{} ){{ schemaName deleted }}", *id), {}, UserPK{UserPK::System}, true, _sl );//deleted:  selecting it is what includes a deleted row.
				let found = row.is_object() ? Json::FindString( row.get_object(), "schemaName" ) : optional<string>{};
				THROW_IFX( !found, Exception(_sl, ExceptionArgs{EHttpStatus::NotFound}, "Resource '{}' not found.", *id) );
				schemas.emplace( *found );
			}
			else
				THROW_IFX( schemas.empty(), Exception(_sl, ExceptionArgs{EHttpStatus::BadRequest}, "createResource needs a schemaName.") );
			for( let& schema : schemas )
				auth.TestSchemaAdmin( schema, _executer, _sl );
			auto result = co_await asSystem( move(_mutation), _sl );
			Resume( move(result) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}
