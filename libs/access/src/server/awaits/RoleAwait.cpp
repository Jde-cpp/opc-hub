#include <jde/access/server/awaits/RoleAwait.h>
#include <jde/db/IDataSource.h>
#include <jde/db/names.h>
#include <jde/db/generators/InsertClause.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Table.h>
#include <jde/db/awaits/ExecuteAwait.h>
#include <jde/db/awaits/SelectAwait.h>
#include <jde/db/Row.h>
#include <jde/fwk/chrono.h>
#include <jde/fwk/co/AnyAwait.h>
#include <jde/ql/ql.h>
#include <jde/ql/IQL.h>
#include <jde/ql/LocalSubscriptions.h>
#include <jde/ql/QLAwait.h>
#include <jde/ql/types/TableQL.h>
#include <jde/access/Authorize.h>
#include "../serverInternal.h"
#include "../../accessInternal.h"

#define let const auto
namespace Jde::Access::Server{

	//{ mutation addRole(id:42, allowed:255, denied:0, resource:{slug:"users"}) }
	//{ mutation addRole(id:11, role:{id:13}) }
	//{ mutation addRole(slug:"sa", role:{slug:"viewer"}) } - the seed's spelling, release.roles.
	//Suspend is noexcept, so nothing here may throw:  the arg shapes are checked, not asserted - a non-object `role` or
	//`permissionRight` from a client used to reach Json::AsObject and take the process down (access-review3 #7).  The args live
	//on the await rather than this frame:  the coroutines take them by reference, and AddPermission reads `rights` again after
	//its admin check, which suspends when the schema's authorizer is remote.
	α RoleMAwait::Start()ι->void{
		_args = _mutation.ExtrapolateVariables();
		if( auto id = _mutation.FindId<RolePK>(); id )
			Dispatch( *id );
		else if( auto slug = _mutation.FindPtr<jstring>("slug"); slug )
			Resolve( string{*slug} );
		else
			ResumeExp( Exception{"Invalid mutation, expecting the role's id or slug."} );
	}
	Ω roleBySlug( const DB::Table& roles )ι->string{ return Ƒ( "select {} from {} where slug=?", roles.GetPK()->Name, roles.DBName ); }
	α RoleMAwait::Resolve( string slug )ι->DB::ScalerAwaitOpt<RolePK>::Task{
		try{
			auto pk = co_await DS().ScalerOpt<RolePK>( {roleBySlug(GetTable("roles")), {DB::Value{slug}}} );
			THROW_IFX( !pk, Exception(_sl, ELogLevel::Debug, "Role '{}' not found.", slug) );
			Dispatch( *pk );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	//The notification the listener updates the cache from reads ids (AccessListener::RoleChanged), so a slug-keyed mutation is
	//rewritten to the pks it resolved to before it is published - here for the role, in ResolveChildren for its members.
	α RoleMAwait::Dispatch( RolePK rolePK )ι->void{
		_mutation.Args["id"] = rolePK;
		_mutation.Args.erase( "slug" );
		if( _mutation.Type==QL::EMutationQL::Remove )
			Remove( rolePK );
		else
			Add( rolePK );
	}
	α RoleMAwait::Add( RolePK rolePK )ι->void{
		if( auto role = _args.if_contains("role"); role && role->is_object() )
			AddRole( rolePK, role->get_object() );
		else if( auto rights = _args.if_contains("permissionRight"); rights && rights->is_object() )
			AddPermission( rolePK, rights->get_object() );
		else
			ResumeExp( Exception{"Invalid mutation, expecting object 'role' or 'permissionRight'."} );
	}
	//`role:{id:N}`, `{id:[…]}`, `{slug:"viewer"}` or `{slug:[…]}` - ids go straight to the members step, slugs through a lookup each.
	Ω childSlugs( const jobject& childRole )ε->vector<string>{
		vector<string> y;
		if( auto slug = childRole.if_contains("slug"); slug )
			y = slug->is_array() ? Json::ToVector<string>( *slug ) : vector<string>{ string{Json::AsString(childRole, "slug")} };
		return y;
	}
	α RoleMAwait::AddRole( RolePK parentRolePK, const jobject& childRole )ι->void{
		try{
			Authorizer().TestAdmin( "roles", _userPK, _sl );//TestAddRoleMember below is only the cycle check - the executer gate is here, as in AclQLAwait::InsertRole.
			_children = childRole.contains( "id" ) ? Json::ToVector<RolePK>( childRole.at("id") ) : vector<RolePK>{};
			if( auto slugs = childSlugs(childRole); slugs.size() )
				ResolveChildren( parentRolePK, move(slugs) );
			else if( _children.empty() )
				ResumeExp( Exception{"Invalid mutation, expecting 'id' or 'slug' in role."} );
			else
				AddMembers( parentRolePK );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α RoleMAwait::ResolveChildren( RolePK parentRolePK, vector<string> slugs )ι->DB::ScalerAwaitOpt<RolePK>::Task{
		try{
			let sql = roleBySlug( GetTable("roles") );
			for( let& slug : slugs ){
				auto pk = co_await DS().ScalerOpt<RolePK>( {sql, {DB::Value{slug}}} );
				THROW_IFX( !pk, Exception(_sl, ELogLevel::Debug, "Role '{}' not found.", slug) );
				_children.push_back( *pk );
			}
			jarray ids; for( let child : _children ) ids.push_back( child );
			_mutation.Args["role"] = jobject{ {"id", move(ids)} };//as above - the listener reads role/id.
			if( _mutation.Type==QL::EMutationQL::Remove )
				RemoveMembers( parentRolePK );
			else
				AddMembers( parentRolePK );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	//A member the role already holds is left alone rather than inserted twice:  the seed reruns on every -sync start, and a
	//duplicate key there would fail the whole file.  The cache's view - loaded at start, kept by the listener.
	α RoleMAwait::AddMembers( RolePK parentRolePK )ι->DB::ExecuteAwait::Task{
		try{
			let& table = GetTable( "role_members" );
			uint rowCount{};
			for( auto childRolePK : _children ){
				if( Authorizer().IsRoleMember(parentRolePK, childRolePK) )
					continue;
				Authorizer().TestAddRoleMember( parentRolePK, childRolePK );
				DB::InsertClause insert;
				insert.Add( table.GetColumnPtr("role_id"), parentRolePK );
				insert.Add( table.GetColumnPtr("member_id"), childRolePK );
				rowCount += co_await table.Schema->DS()->Execute( insert.Move() );
			}
			QL::Subscriptions::OnMutation( _mutation, jvalue{} );
			Resume( rowCount );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α RoleMAwait::AddPermission( RolePK rolePK, const jobject& rights )ι->TAwait<PermissionRightsPK>::Task{
		try{
			//addRole( id:1, permissionRight:{allowed:1, denied:0, resource:{schema:\"opc.default\", slug:\"nodeIds\", criteria:null}} )","variables":{}}
			auto& resource = Json::AsObject( rights, "resource" );
			auto schema = Json::FindString( resource, "schemaName" );
			auto criteria = Json::FindString( resource, "criteria" );
			if( criteria && criteria->empty() )
				criteria = nullopt;
			auto resourceName = Json::FindString( resource, "name" );
			auto& auth = Authorizer();
			auto resourceKey = Json::AsKey( resource );
			if( resourceKey.IsPK() ){
				if( auto existing = auth.FindResource( Resource{(ResourcePK)resourceKey.PK(), {}} ); existing ){
					resourceKey = DB::Key{ existing->Slug };
					schema = existing->Schema;
					if( !criteria && !existing->Criteria.empty() )//the row's own criteria, else a PK naming a criteria-scoped resource would grant on the slug's root instead.
						criteria = existing->Criteria;
				}else
					THROW( "Resource with PK '{}' not found.", resourceKey.PK() );
			}
			if( !schema )
				schema = auth.GetSchema( resourceKey.NK(), _sl );

			auto adminCheck = auth.TestAdmin( *schema, resourceKey.NK(), criteria.value_or(""), _userPK );//the schema's OpcServer answers when one is registered - it knows which resource governs the node; else the flat rule, where a criteria without a row falls back to the slug's root (appserver-review3 #13).
			co_await *adminCheck;
			let& table = GetTable( "roles" );
			if( _mutation.AddIfMissing ){//the seed's add (LocalQL::Upsert):  a role that already holds a grant on this resource keeps it as it is.  access_role_add would have rewritten it with the seed's numbers at every -sync start - Delete back on a hardened Engineer, a deny on Viewer cleared (reviews/m3-closing.md #12, ruled 09-21).
				let byCriteria = criteria ? "r.criteria=?" : "r.criteria is null";
				vector<DB::Value> params{ {rolePK}, {resourceKey.NK()}, {*schema} };
				if( criteria )
					params.emplace_back( *criteria );
				let held = co_await Any( table.Schema->DS()->SelectAsync({ Ƒ("select m.member_id from {} m join {} p on p.permission_id=m.member_id join {} r on r.resource_id=p.resource_id where m.role_id=? and r.slug=? and r.schema_name=? and {}",
					GetTable("role_members").DBName, GetTable("permission_rights").DBName, GetTable("resources").DBName, byCriteria), move(params) }) );
				if( held.size() ){
					DBGT( ELogTags::Access, "[{}]Seed grant on '{}.{}' skipped - the role holds one.", rolePK, *schema, resourceKey.NK() );
					Resume( jobject{{"permissionRight", jobject{{"id", held.front().Get<PermissionRightsPK>(0)}}}} );
					co_return;
				}
			}
			DB::InsertClause insert{ DB::Names::ToSingular(table.DBName)+"_add" };
			insert.Add( rolePK );
			insert.Add( Json::FindNumber<uint>(rights, "allowed").value_or(0) );
			insert.Add( Json::FindNumber<uint>(rights, "denied").value_or(0) );
			insert.Add( resourceKey.NK() );
			insert.Add( *schema );
			insert.AddOpt( move(resourceName) );
			insert.AddOpt( criteria );
			auto ds = table.Schema->DS();
			let permissionPK = co_await ds->InsertSeq<PermissionRightsPK>( move(insert) );
			jobject y;
			auto& permissionRight = y["permissionRight"].emplace_object();
			//if( criteria ){
				auto resourcePK = auth.FindActiveResourcePK( *schema, resourceKey.NK(), criteria.value_or(string{}) );
				optional<string> resourceDeleted;
				vector<DB::Value> params{ {*schema}, {resourceKey.NK()} };
				if( !resourcePK ){
					string dbCriteria;
					if( criteria.has_value() ){
						dbCriteria = "criteria=?";
						params.emplace_back( *criteria );
					}
					else
						dbCriteria = "criteria is null";
					//Deleted rows too:  access_role_add creates a missing root resource unenforced (f8b191cd), and `deleted is null` missed
					//the very row it had just made - the grant went out with no resource id, the authorizer dropped it, and until a restart
					//a grant on it by id was refused, enforcing it threw in the listener, and Effective rights read a lockout
					//(reviews/m3-closing.md #11 - the hub's seed-created `opc.install nodeIds`).  Its id and `deleted` go out with the grant,
					//so the listener caches the row as it is;  only a live row joins the enforced set here.
					let rows = co_await Any( ds->SelectAsync({ Ƒ("select resource_id, deleted from {} where schema_name=? and slug=? and {}", GetTable("resources").DBName, dbCriteria), move(params) }) );
					if( rows.size() ){
						resourcePK = rows.front().Get<ResourcePK>( 0 );
						if( let deleted = rows.front().GetOpt<DB::DBTimePoint>(1); deleted )
							resourceDeleted = ToIsoString( *deleted );//a string:  Resource(jobject) reads it with Json::FindTimePoint, which takes nothing else
						else
							auth.AddResource( *resourcePK, *schema, resourceKey.NK(), criteria.value_or(string{}) );
					}
				}
				if( resourcePK ){
					auto& jResource = permissionRight["resource"].emplace_object();
					jResource["id"] = *resourcePK;
					if( resourceDeleted )
						jResource["deleted"] = *resourceDeleted;
				}
			//}
			permissionRight["id"] = permissionPK;
			QL::Subscriptions::OnMutation(
				_mutation,
				y,
				[&]( QL::TableQL& subscription )->bool {
					let permissionRights = subscription.FindTable("permissionRights");
					if( !permissionRights )
						return true;
					let resource = permissionRights->FindTable("resource");
					if( !resource )
						return true;
					let subscriptionSchema = resource->FindPtr<jvalue>("schemaName");
					if( !subscriptionSchema )
						return true;
					if( subscriptionSchema->is_string() )
						return *schema==subscriptionSchema->as_string();
					else if( subscriptionSchema->is_array() ){
						for( let& v : subscriptionSchema->as_array() ){
							if( v.is_string() && *schema==v.get_string() )
								return true;
						}
						return false;
					}
					DBGT( ELogTags::Access, "Unexpected schemaName type in subscription: {}", serialize(*subscriptionSchema) );
					return false;
				}
			);
			Resume( y );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	//{ mutation removeRole(id:42, permissionRight:{id:420}) }
	//{ mutation removeRole(id:11, role:{id:13}) }
	α RoleMAwait::Remove( RolePK rolePK )ι->void{
		if( auto role = _args.if_contains("role"); role && role->is_object() )
			RemoveRole( rolePK, role->get_object() );
		else if( _args.contains("permissionRight") )
			RemovePermission( rolePK ); //reads the id inside its own try.
		else
			ResumeExp( Exception{"Invalid mutation, expecting object 'role' or 'permissionRight'."} );
	}
	α RoleMAwait::RemoveRole( RolePK parentRolePK, const jobject& childRole )ι->void{
		try{
			Authorizer().TestAdmin( "roles", _userPK, _sl );
			_children = childRole.contains( "id" ) ? Json::ToVector<RolePK>( childRole.at("id") ) : vector<RolePK>{};
			if( auto slugs = childSlugs(childRole); slugs.size() )
				ResolveChildren( parentRolePK, move(slugs) );
			else if( _children.empty() )
				ResumeExp( Exception{"Invalid mutation, expecting 'id' or 'slug' in role."} );
			else
				RemoveMembers( parentRolePK );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α RoleMAwait::RemoveMembers( RolePK parentRolePK )ι->DB::ExecuteAwait::Task{
		try{
			let& table = GetTable( "role_members" );
			//the membership row only - access_role_remove would also drop the child's access_permissions row, ie purge the role itself.
			let sql = Ƒ( "delete from {} where {}=? and {}=?", table.DBName, table.GetColumnPtr("role_id")->Name, table.GetColumnPtr("member_id")->Name );
			uint rowCount{};
			for( let childRolePK : _children )
				rowCount += co_await table.Schema->DS()->Execute( DB::Sql{sql, {DB::Value{parentRolePK}, DB::Value{childRolePK}}} );
			QL::Subscriptions::OnMutation( _mutation, jvalue{} );
			Resume( rowCount );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α RoleMAwait::RemovePermission( RolePK parentRolePK )ι->DB::ExecuteAwait::Task{
		try{
			let permissionPK = _mutation.AsPathNumber<PermissionPK>( "permissionRight/id" );
			Authorizer().TestAdminPermission( permissionPK, _userPK, _sl );//admin of the permission's resource, the same right AddPermission requires to grant it.
			let& table = GetTable( "roles" );
			DB::InsertClause remove{ DB::Names::ToSingular(table.DBName)+"_remove" };
			remove.Add( parentRolePK );
			remove.Add( permissionPK );
			let y = co_await table.Schema->DS()->Execute( remove.Move() );
			QL::Subscriptions::OnMutation( _mutation, jvalue{} );
			ResumeScaler( y );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	RoleAwait::RoleAwait( const QL::TableQL& q, Jde::UserPK userPK, SL sl )ε:
		TAwait<jvalue>{ sl },
		MemberTable{ GetTablePtr("role_members") },
		Query{ q },
		UserPK{ userPK }
	{}

	α RoleAwait::RoleStatement( QL::TableQL& roleQL )ε->DB::Statement{ //role(id:11){role(id:13){id slug deleted}}
		auto statement = QL::SelectStatement( roleQL, true );
		let& roleTable = GetTable( "roles" );
		statement.From = { {MemberTable->GetColumnPtr("member_id"), roleTable.GetPK(), true} };
		let memberRoleIdCol = MemberTable->GetColumnPtr( "role_id" );
		if( auto roleKey = Query.FindKey(); roleKey ){
			if( roleKey->IsPK() )
				statement.Where.Add( memberRoleIdCol, DB::Value::FromKey(*roleKey) );//role_members.role_id=?
			else{
				const string alias = "parent";
				statement.Where.Add( Ƒ("{}.slug=?", alias) );
				statement.Where.Params().push_back( DB::Value::FromKey(*roleKey) );
				statement.From+={ MemberTable->GetSK0(), {}, roleTable.GetPK(), alias, true };
			}
		}
		statement.Select+=memberRoleIdCol;
		roleQL.Columns.push_back( QL::ColumnQL{"parentRoleId", memberRoleIdCol} );
		return statement;
	}

	α RoleAwait::PermissionsStatement( QL::TableQL& permissionQL )ε->DB::Statement{
		auto permissionStatement = QL::SelectStatement( permissionQL, true );
		let& permissionsTable = GetTable( "permission_rights" );
		permissionStatement.From = { {MemberTable->GetColumnPtr("member_id"), permissionsTable.GetPK(), true} };
		permissionStatement.From +={ permissionsTable.GetColumnPtr("resource_id"), GetTable("resources").GetPK(), true };
		let rolePKCol = MemberTable->GetColumnPtr( "role_id" );
		if( auto roleKey = Query.FindKey(); roleKey ){
			if( roleKey->IsPK() )
				permissionStatement.Where.Add( rolePKCol, DB::Value::FromKey(*roleKey) );
			else{
				auto rolesTable = GetTable( "roles" );
				permissionStatement.Where.Add( rolesTable.GetColumnPtr("slug"), DB::Value::FromKey(*roleKey) );
				permissionStatement.From += { MemberTable->GetSK0(), rolesTable.GetPK() };
			}
		}

		if( !permissionQL.FindColumn("id") ){
			permissionStatement.Select+=permissionsTable.GetPK();
			permissionQL.Columns.push_back( QL::ColumnQL{"id", permissionsTable.GetPK()} );
		}
		permissionStatement.Select+=rolePKCol;
		permissionQL.Columns.push_back( QL::ColumnQL{"parentRoleId", rolePKCol} );
		return permissionStatement;
	}

	//query{ role(id:42){permissionRights{id allowed denied resource(slug:"users",criteria:null)}} }
	α RoleAwait::Select()ι->QL::QLAwait<>::Task{
		try{
			optional<jvalue> permissions;
			optional<jvalue> roleMembers;
			string permissionsKey = "permissionRights";
			bool permissionsPlural = true;
			// QL: role( id:16 ){ permissionRight{id allowed denied resource(slug:"users",criteria:null)} }
			if( auto permissionQL = Query.ExtractTable(permissionsKey); permissionQL ){
				if( permissionsPlural = permissionQL->IsPlural(); !permissionsPlural ){
					permissionQL->JsonName = permissionsKey;//bring back array could be single right for multiple roles.
					permissionsKey = "permissionsRight";
				}
				auto statement = PermissionsStatement( *permissionQL );
				auto rights = co_await QL::QLAwait<jvalue>{ move(*permissionQL), move(statement), UserPK };
				permissions = move( rights.get_array() );
			}
			string roleKey = "roles";
			bool rolePlural = true;
			if( auto roleQL = Query.ExtractTable("roles"); roleQL ){
				auto statement = RoleStatement( *roleQL );
				rolePlural = roleQL->IsPlural();
				auto dbMembers = co_await QL::QLAwait( move(*roleQL), move(statement), UserPK );
				if( dbMembers.is_array() )
					roleMembers = dbMembers.get_array().empty() ? jarray{} : move( dbMembers.get_array() );
				else if( dbMembers.is_object() ){
					roleKey = "role";
					roleMembers = dbMembers.get_object().empty() ? jobject{} : move( dbMembers.get_object() );
				}
			}

			flat_map<RolePK,jobject> roles;
			auto createRolesFromMembers = [&roles]( jvalue& roleMembers, str memberName, bool plural ){
				auto addRoleMember = [&]( jobject& member ){
					let parentRolePK = Json::AsNumber<RolePK>( member, "parentRoleId" );
					member.erase( "parentRoleId" );
					auto role = roles.try_emplace( parentRolePK, jobject{{"id", parentRolePK}} );
					auto& jmember = role.first->second;
					if( role.second || !jmember.contains(memberName) ){ //first row
						if( plural )
							jmember[memberName] = jarray{ move(member) };
						else
							jmember[DB::Names::ToSingular( memberName )] = move( member );
					}
					else if( plural )
						jmember[memberName].get_array().emplace_back( move(member) );
				};
				Json::Visit( roleMembers, addRoleMember );
			};

			if( permissions )
				createRolesFromMembers( *permissions, "permissionRights", permissionsPlural );
			if( roleMembers )
				createRolesFromMembers( *roleMembers, "roles", rolePlural );
			let& roleTable = GetTable( "roles" );
			if( !Query.FindColumn("id") )
				Query.Columns.push_back( QL::ColumnQL{"id", roleTable.GetPK()} );
			let returnArray = Query.IsPlural();
			Query.ReturnRaw = true;
			auto qlRoles = co_await QL::QLAwait( move(Query), UserPK );
			auto addRole = [&]( jobject&& roleProperties ){
				let rolePK = Json::AsNumber<RolePK>( roleProperties, "id" );
				auto existing = roles.find( rolePK );
				if( existing!=roles.end() ){
					for( auto&& [key,value] : roleProperties )
						existing->second[key] = move( value );
				}else
					existing = roles.emplace( rolePK, move(roleProperties) ).first;
				auto& role = existing->second;
				if( permissions && !role.contains(permissionsKey) )
					role[permissionsKey] = permissionsPlural ? ( jvalue )jarray{} : ( jvalue )jobject{};
				if( roleMembers && !role.contains(roleKey) )
					role[roleKey] = rolePlural ? ( jvalue )jarray{} : ( jvalue )jobject{};
			};
			Json::Visit( move(qlRoles), addRole );
			jvalue y;
			if( returnArray ){
				jarray jRoles;
				for( auto&& [_,value] : roles )
					jRoles.emplace_back( move(value) );
				y = jRoles;
			}
			else if( roles.size() )
				y = move( roles.begin()->second );
			Resume( move(y) );
		}
		catch( boost::system::system_error& e ){
			ResumeExp( CodeException{e.code(), ELogTags::Access, ELogLevel::Debug} );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}