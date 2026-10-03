#include <jde/access/server/awaits/AclAwait.h>
#include <jde/db/IDataSource.h>
#include <jde/db/generators/InsertClause.h>
#include <jde/db/awaits/SelectAwait.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Table.h>
#include <jde/db/names.h>
#include <jde/ql/ql.h>
#include <jde/ql/IQL.h>
#include <jde/ql/LocalSubscriptions.h>
#include <jde/ql/QLAwait.h>
#include <jde/access/types/Resource.h>
#include <jde/access/Authorize.h>
#include "../serverInternal.h"
#include "../../accessInternal.h"

#define let const auto
namespace Jde::Access::Server{
	α AclQLAwait::Table()ε->const DB::Table&{ return GetTable("acl"); }

	α AclQLAwait::Suspend()ι->void{
		if( _mutation.Type==QL::EMutationQL::Purge )
			PurgeAcl();
		else if( _mutation.Type==QL::EMutationQL::Create )
			InsertAcl();
	}
	//{ mutation purgeAcl( identity:{id:7}, permissionRight:{id:42} ) } - or role:{id:42}.  A direct grant and a role assignment are
	//both access_acl rows in one pk space, so the pk is resolved first and the gate comes from what it *is*
	//(access_permissions.is_role), not from which key the client chose:  keyed on the spelling, a roles admin could revoke any
	//identity's direct grant on any resource by sending its pk as role:{id} (access-review3 #11).  Either spelling is accepted.
	α AclQLAwait::PurgeAcl()ι->DB::ScalerAwaitOpt<uint>::Task{
		try{
			let args = _mutation.ExtrapolateVariables();
			let identityPK = Json::AsNumber<IdentityPK::Type>( args, "identity/id" );
			auto permissionPK = Json::FindNumberPath<PermissionPK::Type>( args, "permissionRight/id" );
			if( !permissionPK )
				permissionPK = Json::FindNumberPath<PermissionPK::Type>( args, "role/id" );
			THROW_IF( !permissionPK, "Could not find permissionRight or role id in '{}'", serialize(args) );
			let isRole = co_await DS().ScalerOpt<uint>( DB::Sql{Ƒ("select is_role from {} where permission_id=?", GetTable("permissions").DBName), vector<DB::Value>{{*permissionPK}}} );
			THROW_IF( !isRole, "[{}]Permission not found.", *permissionPK );
			if( *isRole )
				Authorizer().TestAdminSlug( "roles", _executer, _sl );
			else
				Authorizer().TestAdminPermission( PermissionPK{*permissionPK}, _executer, _sl );
			//the listener - and any subscriber - branches on the key, so the notification has to carry the one the pk is.
			const sv key = *isRole ? "role" : "permissionRight", other = *isRole ? "permissionRight" : "role";
			_mutation.Args.erase( other );
			_mutation.Args[key] = jobject{ {"id", *permissionPK} };
			PurgeAcl( identityPK, PermissionPK{*permissionPK}, *isRole!=0 );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α AclQLAwait::PurgeAcl( IdentityPK::Type identityPK, PermissionPK permissionPK, bool isRole )ι->DB::ExecuteAwait::Task{
		try{
			let ds = Table().Schema->DS();
			let aclCount = co_await ds->Execute(
				DB::Sql{ Ƒ("delete from {} where identity_id=? and permission_id=?", Table().DBName), vector<DB::Value>{{identityPK}, {permissionPK.Value}} }, _sl );
			if( aclCount && !isRole ){ //a direct grant's rights row goes with its last acl link;  a role has no rights row, and the acl row was the whole assignment.
				co_await ds->Execute(
					DB::Sql{ Ƒ("delete from {} where permission_id=? and not exists( select 1 from {} where permission_id=? )", GetTable("permission_rights").DBName, Table().DBName), vector<DB::Value>{{permissionPK.Value}, {permissionPK.Value}} }, _sl );
			}
			jobject y;
			y["rowCount"] = aclCount;
			QL::Subscriptions::OnMutation( _mutation, y );
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α AclQLAwait::InsertAcl()ι->void{
		auto input = _mutation.ExtrapolateVariables();
		if( auto p = input.find("permissionRight"); p!=input.end() && p->value().is_object() ) //identity{ id:x }, permission:{ allowed:x, denied:x, resource:{id:x} }
			InsertPermission( p->value().get_object() );
		else if( auto r = input.find("role"); r!=input.end() ) //identity{ id:x }, role:{ id:x }
			InsertRole();
		else
			ResumeExp( Exception{"Invalid ACL mutation"} );
	}
	α AclQLAwait::InsertRole()ι->DB::ExecuteAwait::Task{
		jobject y;
		try{
			Authorizer().TestAdminSlug( "roles", _executer, _sl );
			DB::InsertClause insert{ Table().InsertProcName()+"_role" };
			let args = _mutation.ExtrapolateVariables();
			let identityPK = Json::AsNumber<IdentityPK::Type>( args, "identity/id" );
			insert.Add( identityPK );
			let rolePK = Json::AsNumber<RolePK::Type>( args, "role/id" );
			insert.Add( rolePK );
			y["rowCount"] = co_await DS().Execute( insert.Move() );
			QL::Subscriptions::OnMutation( _mutation, y );
			Resume( jvalue{y} );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α AclQLAwait::InsertPermission( const jobject& permission )ι->TAwait<optional<ResourcePK::Type>>::Task{
		let allowed = ( ERights )Json::FindNumber<uint8>( permission, "allowed" ).value_or( 0 );
		let denied = ( ERights )Json::FindNumber<uint8>( permission, "denied" ).value_or( 0 );
		try{
			auto& resource = permission.at("resource").as_object();
			auto key = Json::AsKey( resource );
			if( !key.IsPK() ){
				auto criteria = Json::FindString( resource, "criteria" );
				auto dbCriteria = criteria ? DB::Value{move(*criteria)} : DB::Value{nullptr};
				auto resPK = co_await DS().ScalerOpt<ResourcePK::Type>({
					Ƒ( "select resource_id from {} where schema_name=? and slug=? and coalesce(criteria, '')=coalesce(?, '')", GetTable("resources").DBName ),
					{ DB::Value{Json::AsString(resource, "schemaName")}, DB::Value::FromKey(key.NK()), dbCriteria }
				});
				if( resPK ){
					key = *resPK;
					_mutation.Args.at("permissionRight").at("resource").as_object()["id"] = key.PK(); //for subscriptions
				}else
					THROW( "Resource not found for slug '{}' schema '{}'", key.NK(), Json::AsString(resource, "schemaName") );//TODO implement TestAdmin for this
			}
			InsertPermission( allowed, denied, ResourcePK{static_cast<ResourcePK::Type>(key.PK())} );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α AclQLAwait::InsertPermission( ERights allowed, ERights denied, ResourcePK resourcePK )ι->DB::ScalerAwait<PermissionPK::Type>::Task{
		try{
			Authorizer().TestAdminResource( resourcePK, _executer, _sl );
			DB::InsertClause insert{ Table().UpsertProcName()+"_permission" };
			let identityPK = _mutation.AsPathNumber<IdentityPK::Type>( "identity/id" );
			insert.Add( identityPK );

			insert.Add( underlying(allowed) );
			insert.Add( underlying(denied) );
			insert.Add( resourcePK.Value );
			let permissionPK = co_await DS().InsertSeq<PermissionPK::Type>( move(insert) );
			jobject y;
			y["permissionRight"].emplace_object()["id"] = permissionPK;
			QL::Subscriptions::OnMutation( _mutation, y );
			Resume( y );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	//the identities an acl entry names, joined the other way round.
	Ω identitiesStatement( const QL::TableQL& aclQL, const QL::TableQL& identitiesQL )ε->DB::Statement{
		auto statement = QL::SelectStatement( aclQL );
		statement.Select += move( QL::SelectStatement(identitiesQL).Select );
		statement.From = DB::Join{ GetTable("acl").GetColumnPtr("identity_id"), GetTable("identities").GetColumnPtr("identity_id"), true };
		return statement;
	}
	α AclQLSelectAwait::Suspend()ι->void{
		try{
			GetTable( "acl" ).Authorize( Access::ERights::Read, _executer, _sl );
			const Nest identity{ "identities", {"identity"} };
			if( auto rights = Query.FindTable("permissionRights"); rights )
				Load( GetStatement(*rights, GetTable("permission_rights").GetColumnPtr("permission_id")), {{"permission_rights", {"permissionRight"}}, {"resources", {"permissionRight", "resource"}}, identity} );
			else if( auto roles = Query.FindTable("roles"); roles )
				Load( GetStatement(*roles, GetTable("roles").GetColumnPtr("role_id")), {{"roles", {"role"}}, identity} );
			else if( auto permissions = Query.FindTable("permissions"); permissions )
				Load( GetStatement(*permissions, GetTable("permissions").GetColumnPtr("permission_id")), {} );
			else if( auto identities = Query.FindTable("identities"); identities )
				Load( identitiesStatement(Query, *identities), {}, identities );
			else
				ResumeExp( Exception{"query not implemented"} );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α AclQLSelectAwait::GetStatement( const QL::TableQL& childTable, sp<DB::Column> joinColumn )ε->DB::Statement{
		let& table = GetTable( "acl" );
		auto statement = QL::SelectStatement( childTable );
		auto aclStatement = QL::SelectStatement( Query );
		statement.Select += move( aclStatement.Select );
		statement.Where += aclStatement.Where;
		statement.From += { joinColumn, table.GetColumnPtr("permission_id"), true };
		if( auto identities = Query.FindTable("identities"); identities ){
			if( !(identities->Columns.size()==1 && identities->Columns.front().JsonName=="id") )
				statement.From += { table.GetColumnPtr("identity_id"), GetTable("identities").GetColumnPtr("identity_id"), true };
		}
		return statement;
	}
	AclQLSelectAwait::Nest::Nest( string table, vector<string> path )ε:
		Table{ move(table) }, Path{ move(path) }, IdKey{ DB::Names::ToJson(GetTable(Table).GetPK()->Name) }
	{}
	//A column lands under its table's entry, else the last entry - acl's own columns go with the identity - and the entry
	//table's pk is `id`.  No nesting = the query's own shape.
	Ω shapeRow( DB::Row& row, const vector<DB::Object>& columns, const vector<AclQLSelectAwait::Nest>& nesting, const QL::TableQL& ql )ε->jobject{
		jobject y;
		for( uint i=0; i<row.Size() && i<columns.size(); ++i ){
			let& column = get<DB::AliasCol>( columns[i] ).Column;
			if( nesting.empty() ){
				ql.SetResult( y, column, move(row[i]) );
				continue;
			}
			let entry = std::ranges::find( nesting, column->Table->Name, &AclQLSelectAwait::Nest::Table );
			let& nest = entry!=nesting.end() ? *entry : nesting.back();
			jobject* target = &y;
			for( let& key : nest.Path ){
				auto& v = ( *target )[key];
				target = v.is_object() ? &v.get_object() : &v.emplace_object();
			}
			let key = DB::Names::ToJson( column->Name );
			( *target )[key==nest.IdKey ? "id" : key] = row[i].Move();
		}
		return y;
	}
	α AclQLSelectAwait::Load( DB::Statement statement, vector<Nest> nesting, const QL::TableQL* identitiesQL )ι->DB::SelectAwait::Task{
		try{
			auto rows = co_await DS().SelectAsync( statement.Move() );
			jarray y;
			for( auto& row : rows )
				y.emplace_back( shapeRow(row, statement.Select.Columns, nesting, identitiesQL ? *identitiesQL : Query) );
			if( identitiesQL ){//one object holding the rows, where every other child is a row per acl entry.
				jobject o{ {"identities", move(y)} };
				if( Query.IsPlural() )
					Resume( jarray{o} );
				else
					Resume( move(o) );
			}
			else
				Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}
