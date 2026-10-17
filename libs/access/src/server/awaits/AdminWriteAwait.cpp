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

	α AdminWriteAwait::Execute()ι->TAwait<jvalue>::Task{
		try{
			Test();
			auto result = co_await asSystem( move(_mutation), _sl );
			Resume( move(result) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	α PermissionRightMAwait::Test()ε->void{
		auto& auth = Authorizer();
		auth.TestUser( _executer, _sl );//TestAdminResource passes anyone on an unenforced resource.
		let args = _mutation.ExtrapolateVariables();
		for( let& kv : args )//the admin check covers the grant's current resource only - a resourceId would move it past that.
			THROW_IFX( kv.key()!="id" && kv.key()!="allowed" && kv.key()!="denied", Exception(_sl, ExceptionArgs{EHttpStatus::BadRequest}, "updatePermissionRight takes only id, allowed and denied, not '{}'.", string{kv.key()}) );
		auth.TestAdminPermission( PermissionPK{Json::AsNumber<PermissionPK::Type>(args, "id")}, _executer, _sl );
	}

	α ResourceMAwait::Test()ε->void{
		Authorizer().TestUser( _executer, _sl );
	}
	α ResourceMAwait::Execute()ι->TAwait<jvalue>::Task{
		try{
			Test();
			auto& auth = Authorizer();
			let args = _mutation.ExtrapolateVariables();
			flat_set<string> schemas;
			if( let schema = Json::FindString(args, "schemaName"); schema )
				schemas.emplace( *schema );
			using enum QL::EMutationQL;
			if( _mutation.Type==Update ){
				let id = Json::FindNumber<ResourcePK::Type>( args, "id" );
				THROW_IFX( !id, Exception(_sl, ExceptionArgs{EHttpStatus::BadRequest}, "updateResource needs the row's id - a slug is not unique across schemas.") );
				let row = co_await *LocalQL().Query( Ƒ("resource( id:{} ){{ schemaName }}", *id), {}, UserPK{UserPK::System}, true, _sl );
				let found = row.is_object() ? Json::FindString( row.get_object(), "schemaName" ) : optional<string>{};
				THROW_IFX( !found, Exception(_sl, ExceptionArgs{EHttpStatus::NotFound}, "Resource '{}' not found.", *id) );
				schemas.emplace( *found );
				auth.TestAdminResource( ResourcePK{*id}, _executer, _sl );//TestSchemaAdmin tests the roots only - not an enforced criteria row.
			}
			else
				THROW_IFX( schemas.empty(), Exception(_sl, ExceptionArgs{EHttpStatus::BadRequest}, "{} needs a schemaName.", _mutation.CommandName) );
			for( let& schema : schemas )
				auth.TestSchemaAdmin( schema, _executer, _sl );
			if( _mutation.Type==Purge ){
				//By its key, a criteria row's:  a root's purge takes every grant on its slug, and stays the system's, by id (review #4).  Each
				//admin check comes before the lookup, so a caller who fails one learns nothing of its rows.  Under an active root the schema's
				//administrator is enough, as an update's;  under an open one a criteria row can still govern - an OPC branch (OpcAuthorize) - so
				//the purge, which takes its grants (access_resource_purge), asks what revoking one does (review #1).
				let slug = Json::FindString( args, "slug" );
				let criteria = Json::FindString( args, "criteria" );
				THROW_IFX( !slug || !criteria || criteria->empty(), Exception(_sl, ExceptionArgs{EHttpStatus::BadRequest}, "purgeResource takes a criteria row's key:  schemaName, slug and criteria.  A root's purge is the system's, by id.") );
				let& schema = *schemas.begin();
				if( !auth.FindActiveResourcePK(schema, *slug, {}) ){
					auto adminCheck = auth.TestAdminGrant( schema, *slug, *criteria, _executer, _sl );
					co_await *adminCheck;
				}
				let rows = co_await *LocalQL().Query( "resources( schemaName:$schemaName, slug:$slug, criteria:$criteria ){ id }", jobject{ {"schemaName", schema}, {"slug", *slug}, {"criteria", *criteria} }, UserPK{UserPK::System}, true, _sl );
				let& found = Json::AsArray( rows, _sl );
				THROW_IFX( found.empty(), Exception(_sl, ExceptionArgs{EHttpStatus::NotFound}, "Resource ({}, {}, {}) not found.", schema, *slug, *criteria) );
				_mutation.Args = jobject{ {"id", QL::AsId<ResourcePK::Type>(found[0], _sl)} };
			}
			let create = _mutation.Type==Create;
			auto result = co_await asSystem( move(_mutation), _sl );
			if( create ){//unenforced, as the sync leaves it:  an enforced row nobody administers locks its creator out of the schema.
				let& rows = Json::AsArray( result, _sl );
				THROW_IF( rows.empty(), "createResource returned no row." );
				co_await *LocalQL().Query( Ƒ("deleteResource( id:{} )", QL::AsId<ResourcePK::Type>(rows[0], _sl)), {}, UserPK{UserPK::System}, true, _sl );
			}
			Resume( move(result) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	α ProviderMAwait::Test()ε->void{
		Authorizer().TestUser( _executer, _sl );
		TestAccessAdmin( "users", _executer, _sl );
	}
}