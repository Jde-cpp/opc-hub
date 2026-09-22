#include <jde/access/Authorize.h>
#include <jde/fwk/str.h>
#include <jde/db/usings.h>
#include <jde/access/types/Group.h>
#include <jde/access/types/User.h>
#include <jde/access/AccessException.h>
#include <jde/fwk/utils/collections.h>

#define let const auto
namespace Jde::Access{
	constexpr ELogTags _tags{ ELogTags::Access };
	constexpr ELogTags _ptags{ ELogTags::Access | ELogTags::Pedantic };

	α Authorize::AddAdminAuthorizer( str schemaName, sp<IAdminAcl> authorizer, UserPK registrant )ι->void{
		_adminAuthorizers.insert_or_assign( schemaName, AdminAuthorizer{move(authorizer), registrant} );//not emplace: a restarted app must replace its stale registration, not be silently discarded behind the closed one.
	}
	α Authorize::RemoveAdminAuthorizer( const sp<IAdminAcl>& authorizer )ι->void{
		_adminAuthorizers.erase_if( [&](let& pair){ return pair.second.Acl==authorizer; } );//drop every schema this session authorized, so TestAdmin falls back to the local rule instead of a dead stream.
	}
	α Authorize::FindAdminAuthorizer( str schemaName )ι->optional<AdminAuthorizer>{
		optional<AdminAuthorizer> y;
		_adminAuthorizers.cvisit( schemaName, [&](let& pair){ y = pair.second; } );
		return y;
	}
	α Authorize::AddResource( ResourcePK resourcePK, string schema, string resourceSlug, string criteria )ι->void{
		ul _{ Mutex };
		SchemaResources[schema][resourceSlug][criteria] = resourcePK;
	}

	//A bare slug names a schema only when one schema carries it.  An app schema wins outright; an opc schema ("opc.<server>")
	//answers only when it is the sole live one with the slug, since every OpcServer registers the same `nodeIds`.  Callers that
	//can be specific (the acl/role mutations, which have the resource's PK) should be - this is the fallback for those that cannot.
	α Authorize::GetSchema( str resourceSlug, SL sl )ε->string{
		Jde::sl _{ Mutex };
		optional<string> qualified;
		bool ambiguous{};
		for( let& [_,resource] : Resources ){
			if( resource.Slug!=resourceSlug )
				continue;
			if( !resource.Schema.contains('.') ) //an app schema's slug is unique by construction
				return resource.Schema;
			if( resource.IsDeleted )
				continue;
			if( qualified && *qualified!=resource.Schema )
				ambiguous = true;
			else
				qualified = resource.Schema;
		}
		THROW_IFSL( ambiguous, "Resource slug '{}' exists in more than one schema; send the resource 'id' or a 'schemaName'.", resourceSlug );
		if( qualified )
			return *qualified;
		THROWSL( "Schema not found for resource slug '{}'.", resourceSlug );
	}
	α Authorize::Test( str schemaName, str resourceName, ERights rights, UserPK executer, SL sl )ε->void{
		Jde::sl l{ Mutex };
		auto resourcePK = FindActiveResourcePK( schemaName, resourceName, {}, l );
		if( !resourcePK )//not enabled
			return;

		//AccessException, as TestAdmin below throws:  a plain Exception carries no http status, so ServerImpl fell through to
		//its InternalServerError branch and a denial reached the client as 500 "Query failed." - indistinguishable from a
		//broken query.  Forbidden for what the executer may do, Unauthorized only for who they are (access-review3 #17).
		//The executer is the exception's own field, not a "[{}]" prefix:  ServerImpl formats it through UserName().
		if( auto user = Users.find(executer); user!=Users.end() ){
			THROW_IFX( user->second.IsDeleted, Access::AccessException(sl, executer, "User is deleted.") );
			let configured = user->second.ResourceRights( *resourcePK );
			THROW_IFX( !empty(configured.Denied & rights), Access::AccessException(sl, executer, "User denied '{}' access to '{}'.", ToString(rights), resourceName) );
			THROW_IFX( empty(configured.Allowed & rights), Access::AccessException(sl, executer, "User does not have '{}' access to '{}'.", ToString(rights), resourceName) );
		}
		else if( executer.Value!=UserPK::System )
			throw Access::AccessException{ sl, executer, EHttpStatus::Unauthorized, "User not found." };//not a known user - anonymous or stale - the one case the client's 401 policy is for.
	}
	α Authorize::TestAdmin( ResourcePK resourcePK, UserPK executer, SL sl )ε->void{
		Jde::sl _{ Mutex };
		auto resource=Resources.find( resourcePK );
		if( resource!=Resources.end() && !resource->second.IsDeleted )
			TestAdmin( resource->second, executer, sl );
	}

	α Authorize::TestAdmin( str resourceSlug, UserPK executer, SL sl )ε->void{
		Jde::sl l{ Mutex };
		auto resource = find_if( Resources, [&](let& r){return r.second.Slug==resourceSlug && !r.second.Schema.contains('.');} );//exclude opc schemas which can have same slug
		if( resource!=Resources.end() && !resource->second.IsDeleted )
			TestAdmin( resource->second, executer, sl );
	}
	α Authorize::TestAdmin( str schema, str resource, str criteria, UserPK userPK, SL sl )ι->up<AnyVoidAwait>{
		if( auto remote = FindAdminAuthorizer(schema); remote ){
			try{
				TestSchemaAdmin( schema, remote->User, sl );//still entitled to answer for the schema - rights revoked since it registered fall through to the local rule.
				return remote->Acl->TestAdmin( resource, criteria, userPK, sl );
			}
			catch( const runtime_error& e ){
				DBGT( _tags, "[{}]registrant {} no longer administers the schema ({}) - local rule.", schema, remote->User.Value, e.what() );
			}
		}
		up<Exception> error;
		try{
			TestAdminLocal( schema, resource, criteria, userPK, sl );
		}
		catch( Exception& e ){ error = e.Move(); }
		catch( runtime_error& e ){ error = mu<Exception>( move(e) ); }
		return mu<AnyCompletedAwait>( move(error), sl );
	}
	α Authorize::TestAdminLocal( str schema, str resource, str criteria, UserPK executer, SL sl )ε->void{
		Jde::sl l{ Mutex };
		auto active = [&]( str c )->const Resource*{ //SchemaResources keeps a deleted criteria row (UpdateResourceDeleted maintains it for criteria-less rows only) - the row decides.
			let pk = FindActiveResourcePK( schema, resource, c, l );
			auto p = pk ? Resources.find( *pk ) : Resources.end();
			return p!=Resources.end() && !p->second.IsDeleted ? &p->second : nullptr;
		};
		auto p = active( criteria );
		if( !p && criteria.size() )
			p = active( {} );//an unmapped criteria inherits the slug's root, as OpcAuthorize::UserRights does for an unmapped node.
		if( p )//else permissions are not enabled for the slug - passes, as Test does.
			TestAdmin( *p, executer, sl );
	}
	α Authorize::TestSchemaAdmin( str schema, UserPK executer, SL sl )ε->void{
		Jde::sl l{ Mutex };
		if( auto slugs = SchemaResources.find(schema); slugs!=SchemaResources.end() ){
			for( let& [_, criterias] : slugs->second ){
				auto root = criterias.find( string{} );
				auto p = root!=criterias.end() ? Resources.find( root->second ) : Resources.end();
				if( p==Resources.end() || p->second.IsDeleted )
					continue;
				TestAdmin( p->second, executer, sl );
			}
		}
	}
	α Authorize::TestAdmin( const Resource& resource, UserPK executer, SL sl )ε->void{
		if( executer==UserPK{UserPK::System} )
			return;
		auto user = Users.find( executer );
		THROW_IFX( user==Users.end(), Access::AccessException(sl, executer, EHttpStatus::Unauthorized, "User not found.") ); //not a known user - anonymous or stale - the one case the client's 401 policy is for.
		THROW_IFX( user->second.IsDeleted, Access::AccessException(sl, executer, "User is deleted.") );
		let configured = user->second.ResourceRights( resource.PK );
		THROW_IFX( !empty(configured.Denied & ERights::Administer), Access::AccessException(sl, executer, "User denied admin access to '{}'.", resource.Slug) );
		THROW_IFX( empty(configured.Allowed & ERights::Administer), Access::AccessException(sl, executer, "User does not have admin access to '{}'.", resource.Slug) );
	}
	α Authorize::TestAdminPermission( PermissionPK permissionPK, UserPK userPK, SL sl )ε->void{
		Jde::sl l{ Mutex };
		auto permission = Permissions.find( permissionPK );
		THROW_IF( permission==Permissions.end(), "[{}]Permission not found.", permissionPK );
		let resourcePK = permission->second.ResourcePK;
		l.unlock();
		TestAdmin( resourcePK, userPK, sl );
	}

	α Authorize::Rights( str schemaName, str resourceName, UserPK executer )ι->ERights{
		Jde::sl _{ Mutex };
		auto resourcePK = FindActiveResourcePK( schemaName, resourceName, {}, _ );
		if( !resourcePK )//not enabled
			return ERights::All;

		auto user = Users.find( executer );
		if( user==Users.end() )
			return executer.Value==UserPK::System ? ERights::All : ERights::None;//System early-passes in Test/TestAdmin - stay consistent.
		if( user->second.IsDeleted )
			return ERights::None;

		auto rights = user->second.ResourceRights( *resourcePK );
		return rights.Allowed & ~rights.Denied;
	}
	α Authorize::UserName( UserPK userPK )ι->string{
		Jde::sl _{ Mutex };
		if( auto user = Users.find(userPK); user!=Users.end() )
			return user->second.Name;
		else
			return std::to_string( userPK.Value );
	}

	α Authorize::RecursiveUsers( GroupPK groupPK, const ul& l, bool clear )ι->flat_set<UserPK>{
		flat_set<GroupPK> visited;
		return RecursiveUsers( groupPK, l, clear, visited );
	}
	α Authorize::RecursiveUsers( GroupPK groupPK, const ul& l, bool clear, flat_set<GroupPK>& visited )ι->flat_set<UserPK>{
		flat_set<UserPK> users;
		auto group = visited.emplace( groupPK ).second ? Groups.find( groupPK ) : Groups.end();//visited guards cycles in existing data.
		if( group==Groups.end() || group->second.IsDeleted )
			return users;

		for( auto member : group->second.Members ){
			if( member.IsUser() ){
				users.emplace( member.UserPK() );
				if( auto user = clear ? Users.find(member.UserPK()) : Users.end(); user!=Users.end() )
					user->second.Clear();
			}
			else{
				let groupUsers = RecursiveUsers( member.GroupPK(), l, clear, visited );
				users.insert( groupUsers.begin(), groupUsers.end() );
			}
		}
		return users;
	}

	α Authorize::AddToGroup( GroupPK groupPK, flat_set<IdentityPK::Type> members )ι->void{
		ul l{ Mutex };
		flat_set<UserPK> users;
		auto& existing = Groups.try_emplace( groupPK, Group{groupPK, false} ).first->second;
		for( let& member : members ){
			if( auto pkUser = Users.find(UserPK{member}); pkUser!=Users.end() ){
				existing.Members.emplace( pkUser->first );
				pkUser->second.Clear();
				users.emplace( pkUser->first );
			}
			else{
				GroupPK childGroup{ member };
				existing.Members.emplace( childGroup );
				TRACET( _ptags, "[{}+{}]AddToGroup", groupPK.Value, childGroup.Value );
				let groupUsers = RecursiveUsers( childGroup, l, true );
				for( let user : groupUsers )
					users.emplace( user );
			}
		}
		if( users.size() )
			SetUserPermissions( move(users), l );
	}
	α Authorize::RemoveFromGroup( GroupPK groupPK, flat_set<IdentityPK::Type> members )ι->void{
		ul l{ Mutex };
		flat_set<UserPK> users;
		auto group = Groups.find( groupPK );
		if( group==Groups.end() )
			return;
		for( let& member : members ){
			auto existing = group->second.Members.find( UserPK{member} );//compare on UserPK.
			if( existing==group->second.Members.end() )
				continue;
			if( auto pkUser = existing->IsUser() ? Users.find(existing->UserPK()) : Users.end(); pkUser!=Users.end() ){
				pkUser->second.Clear();
				users.emplace( pkUser->first );
			}
			else if( !existing->IsUser() ){
				for( let user : RecursiveUsers(existing->GroupPK(), l, true) )
					users.emplace( user );
			}
			group->second.Members.erase( existing );
		}
		if( users.size() )
			SetUserPermissions( move(users), l );
	}


	α Authorize::RestoreGroup( GroupPK groupPK )ι->void{
		ul l{ Mutex };
		if( auto p = Groups.find(groupPK); p!=Groups.end() ){
			p->second.IsDeleted = false;
			RecalcGroupMembers( groupPK, l );
		}
	}

	α Authorize::RecalcGroupMembers( GroupPK groupPK, const ul& l, bool remove )ι->void{
		auto users = RecursiveUsers( groupPK, l, true );
		if( remove )
			Groups.erase( groupPK );
		if( users.size() )
			SetUserPermissions( move(users), l );
	}
	α Authorize::AddAclEntry( IdentityPK identityPK, PermissionRole permissionRole, const ul& )ι->void{
		let range = Acl.equal_range( identityPK );
		for( auto p=range.first; p!=range.second; ++p ){
			if( p->second==permissionRole )
				return;//multimap - a duplicate would survive RemoveAcl, which erases only the first match.
		}
		Acl.emplace( identityPK, permissionRole );
	}
	α Authorize::AddAcl( IdentityPK::Type userGroupPK, PermissionPK permissionPK, ERights allowed, ERights denied, ResourcePK resourcePK )ι->void{
		ul l{ Mutex };
		const PermissionRole permissionRole{ std::in_place_index<0>, permissionPK };
		ASSERT( Resources.find(resourcePK)!=Resources.end() );
		//access_ac_upsert_permission is an upsert on (identity, resource) - a re-grant returns the same pk carrying new rights.
		auto existing = Permissions.find( permissionPK );
		let changed = existing!=Permissions.end() && (existing->second.Allowed!=allowed || existing->second.Denied!=denied || existing->second.ResourcePK!=resourcePK);
		Permissions.insert_or_assign( permissionPK, Permission{permissionPK, resourcePK, allowed, denied} );
		auto user = Users.find( {userGroupPK} );
		let identityPK = user!=Users.end() ? IdentityPK{ user->first } : IdentityPK{ GroupPK{userGroupPK} };
		AddAclEntry( identityPK, permissionRole, l );
		if( changed )
			Recalc( l );//rebuild everything - the pk may be cached on identities other than this one.
		else if( user!=Users.end() ){
			user->second.Clear();//rebuild, operator+= can only raise rights.
			SetUserPermissions( {user->first}, l );
		}
		else
			RecalcGroupMembers( identityPK.GroupPK(), l );
	}

	α Authorize::AddAcl( IdentityPK::Type userGroupPK, RolePK rolePK )ι->void{
		ul l{ Mutex };
		auto user = Users.find( {userGroupPK} );
		let identityPK = user!=Users.end() ? IdentityPK{ user->first } : IdentityPK{ GroupPK{userGroupPK} };
		AddAclEntry( identityPK, PermissionRole{std::in_place_index<1>, rolePK}, l );
		if( user!=Users.end() )
			AddPermission( identityPK, PermissionRole{std::in_place_index<1>, rolePK}, {user->first}, l );
		else
			RecalcGroupMembers( identityPK.GroupPK(), l );
	}
	α Authorize::RemoveAcl( IdentityPK::Type userGroupPK, PermissionRole rolePK )ι->void{
		ul l{ Mutex };
		let identityPK = ToIdentityPK( userGroupPK, l );
		auto permissionRoles = Acl.equal_range( ToIdentityPK(userGroupPK, l) );
		for( auto p=permissionRoles.first; p!=permissionRoles.second; ++p ){
			if( p->second==rolePK ){
				Acl.erase( p );
				break;
			}
		}
		if( identityPK.IsUser() ){
			if( auto user = Users.find(identityPK.UserPK()); user!=Users.end() )
				user->second.Clear();
			SetUserPermissions( {identityPK.UserPK()}, l );
		}
		else
			RecalcGroupMembers( identityPK.GroupPK(), l );
	}

	α Authorize::ToIdentityPK( IdentityPK::Type userGroupPK, const ul& )Ι->IdentityPK{
		auto user = Users.find( {userGroupPK} );
		return user!=Users.end() ? IdentityPK{ user->first } : IdentityPK{ GroupPK{userGroupPK} };
	}

	α Authorize::CreateResource( Resource&& resource )ε->void{
		ul _{ Mutex };
		if( !resource.IsDeleted )//as Loader::Resources registers every active row:  a row created active is enforced - and found by TestSchemaAdmin - now, not at the next start (appserver-review3 #13).
			SchemaResources[resource.Schema][resource.Slug][resource.Criteria] = resource.PK;
		Resources[resource.PK] = move( resource );//assignment, not emplace:  sqlite reuses a purged pk, and resource purges are not subscribed, so the entry may be a stale deleted row.
	}
	//A pk names one row.  No pk - the fan-out could not pick one, because a by-slug delete hit several - names every row of that
	//schema+slug, and the db changed all of them, so the cache does too (access-review3 #22).
	α Authorize::UpdateResourceDeleted( ResourcePK pk, sv schemaName, const jobject& args, bool restored )ε->void{
		ul _{ Mutex };
		if( !pk )
			pk = Json::FindNumber<ResourcePK>( args, "id" ).value_or( 0 );
		let slug = Json::FindSV( args, "slug" );
		uint applied{};
		for( auto&& [resourcePK, resource] : Resources ){ //&&: flat_map iterates a proxy pair.
			if( pk ? pk!=resource.PK : !((schemaName.empty() || resource.Schema==schemaName) && slug && *slug==resource.Slug) ) //no schema in the mutation means the db matched the slug in every schema.
				continue;
			++applied;
			resource.IsDeleted = restored ? optional<DB::DBTimePoint>{} : DB::DBClock::now();
			if( resource.Criteria.empty() ){
				if( auto resources = resource.IsDeleted ? SchemaResources.find(resource.Schema) : SchemaResources.end(); resources!=SchemaResources.end() ){
					resources->second.erase( resource.Slug );
					DBGT( _ptags, "[{}.{}.{}]Deleted from schema resource.", resource.Schema, resource.Slug, resource.PK );
				}
				else if( !resource.IsDeleted ){
					auto& slugResources = SchemaResources.try_emplace( string{resource.Schema} ).first->second;
					auto& criteras = slugResources.try_emplace( resource.Slug ).first->second;
					criteras.try_emplace( {}, resource.PK );
					DBGT( _ptags, "[{}.{}.{}]Restored from schema resource.", resource.Schema, resource.Slug, resource.PK );
				}
			}
		}
		// ie Testing schema where testing app isn't started.
		THROW_IFX( !applied, Exception(SRCE_CUR, ELogLevel::Debug, "Resource not found pk: {}, schema:'{}', args:'{}'", pk, schemaName, serialize(args)) );
	}


	α Authorize::CreateUser( UserPK userPK )ι->void{
		ul _{ Mutex };
		Users.emplace( userPK, User{userPK, "", false} );
	}
	α Authorize::DeleteUser( UserPK identityPK )ι->void{
		ul _{ Mutex };
		if( auto p = Users.find(identityPK); p!=Users.end() )
			p->second.IsDeleted = true;
	}
	//a purged identity's acl rows and memberships are inert (lookups skip what isn't in Users/Groups) but would leak for the process lifetime.
	α Authorize::PurgeIdentity( IdentityPK identityPK, const ul& )ι->void{
		Acl.erase( identityPK );
		for( auto group=Groups.begin(); group!=Groups.end(); ++group )
			group->second.Members.erase( identityPK );//IdentityPK orders on Underlying(), so this matches a user or group member.
	}
	α Authorize::PurgeUser( UserPK identityPK )ι->void{
		ul l{ Mutex };
		Users.erase( identityPK );
		PurgeIdentity( identityPK, l );
	}
	α Authorize::RestoreUser( UserPK identityPK )ι->void{
		ul _{ Mutex };
		if( auto p = Users.find(identityPK); p!=Users.end() )
			p->second.IsDeleted = false;
	}
	α Authorize::DeleteGroup( GroupPK groupPK )ι->void{
		ul l{ Mutex };
		auto p = Groups.find( groupPK );
		if( p==Groups.end() || p->second.IsDeleted )
			return;
		auto users = RecursiveUsers( groupPK, l, true );//collect+clear while still active, RecursiveUsers early-outs on a deleted group.
		p->second.IsDeleted = true;//soft delete, symmetric with DeleteUser - RestoreGroup needs the row.
		if( users.size() )
			SetUserPermissions( move(users), l );
	}
	//TODO test on deleted members.
	α Authorize::TestAddGroupMember( GroupPK parentGroupPK/*groupD*/, flat_set<IdentityPK::Type>&& memberPKs, SL sl )ε->void{
		std::shared_lock _{ Mutex };
		for( let memberPK : memberPKs ){
			if( Users.contains({memberPK}) )
				continue;
			GroupPK childGroup{ memberPK };/*GroupA*/
			THROW_IFX( childGroup==parentGroupPK, Exception(sl, ELogLevel::Debug, "Group cannot be a member of itself.") );
			if( IsChild(Groups, childGroup, parentGroupPK) )
				throw Exception{ sl, ELogLevel::Debug, "Group '{}' cannot be a member of '{}' because it is a ancester.", childGroup.Value, parentGroupPK.Value };
		}
	}
	α Authorize::PurgeGroup( GroupPK groupPK )ι->void{
		ul l{ Mutex };
		auto p = Groups.find( groupPK );
		if( p==Groups.end() )
			return;
		if( p->second.IsDeleted )
			Groups.erase( p );//members were cleared+recalculated when it was deleted.
		else
			RecalcGroupMembers( groupPK, l, true );//collect+clear the members before erasing, RecursiveUsers can't find them after.
		PurgeIdentity( groupPK, l );//after the recalc - the group is already out of Groups, so its acl rows are inert either way.
	}

	α Authorize::TestAddRoleMember( RolePK parent, RolePK child, SL sl )ε->void{
		THROW_IFX( parent==child, Exception(sl, ELogLevel::Debug, "Role cannot be a member of itself.") );
		flat_set<RolePK> visited;
		function<bool( RolePK,RolePK )> isChild = [&]( RolePK parent, RolePK child )->bool {
			auto children = visited.emplace( parent ).second ? Roles.find( parent ) : Roles.end();//visited guards cycles in existing data.
			if( children==Roles.end() )
				return false;
			for( PermissionRole member : children->second.Members ){
				if( member.index()==1 && (get<1>(member)==child || isChild(get<1>(member), child)) )
					return true;
			}
			return false;
		};
		std::shared_lock _{ Mutex };
		THROW_IFX( isChild(child, parent), Exception(sl, ELogLevel::Debug, "Role '{}' cannot be a member of '{}' because it is a ancester.", child, parent) );
	}
	α Authorize::IsRoleMember( RolePK parent, RolePK child )Ι->bool{
		Jde::sl _{ Mutex };
		auto p = Roles.find( parent );
		return p!=Roles.end() && p->second.Members.contains( PermissionRole{std::in_place_index<1>, child} );
	}
	α Authorize::AddRolePermission( RolePK rolePK, PermissionPK member, ERights allowed, ERights denied, const jobject& jResource )ι->void{
		ul l{ Mutex };
		Resource resource{ jResource };
		optional<ResourcePK> resourcePK;
		auto found = FindResource( resource, l );
		//The grant names its row - pk, schema and slug - and the entry cached under that pk is another resource:  sqlite reuses a
		//purged pk and resource purges are not subscribed (CreateResource), so it is a stale row.  Taken as this one, the grant -
		//and every later grant on the new row by id - landed on the old row's slug (reviews/m3-closing.md #11).  Replace it.
		let names = [&]( const Resource& cached )->bool{//what the payload says, where it says it - a grant by slug alone names no schema
			return cached.Slug!=resource.Slug || (resource.Schema.size() && cached.Schema!=resource.Schema) || (jResource.contains("criteria") && cached.Criteria!=resource.Criteria);
		};
		if( found && found->PK==resource.PK && resource.Slug.size() && names(*found) ){
			if( auto schema = SchemaResources.find(found->Schema); schema!=SchemaResources.end() ){
				if( auto slug = schema->second.find(found->Slug); slug!=schema->second.end() && Find(slug->second, found->Criteria)==found->PK )
					slug->second.erase( found->Criteria );
			}
			found = nullptr;
		}
		if( found )
			resourcePK = found->PK;
		else if( resource.PK ){ //new resource, or a reused pk's
			auto& saved = Resources.insert_or_assign( resource.PK, move(resource) ).first->second;
			ASSERT( saved.Schema.size() && saved.Slug.size() );
			if( !saved.IsDeleted )//as CreateResource:  a row a role grant created unenforced is cached - so a grant by id finds it, and enforcing it is the toggle's to do - but not enforced here (reviews/m3-closing.md #11)
				SchemaResources[saved.Schema][saved.Slug][saved.Criteria] = saved.PK;
			resourcePK = saved.PK;
		}
		if( auto permission = Permissions.find(member); permission!=Permissions.end() ){ //a re-grant, or a purged pk the db reused (sqlite) - the entry outlives PurgeAcl/RemoveRoleChildren, so take the resource from the payload like AddAcl does, not from the stale entry.
			permission->second.Allowed = allowed;
			permission->second.Denied = denied;
			if( resourcePK )
				permission->second.ResourcePK = *resourcePK;
		}
		else if( resourcePK )
			Permissions.emplace( member, Permission{member, *resourcePK, allowed, denied} );
		else
			DBGT( _ptags, "[{}]Role permission grants on '{}', a resource neither cached nor named by id in the grant - it applies once the resource loads.", member, resource.Slug );//RoleMAwait now names every row it finds, deleted ones included (reviews/m3-closing.md #11), so this is a grant on a row the db does not have
		auto role = Roles.try_emplace( rolePK, rolePK, false );
		role.first->second.Members.emplace( PermissionRole{std::in_place_index<0>, member} );
		Recalc( l );
		TRACET( _ptags, "[{}+{}]Added role permission.", rolePK, member );
	}
	α Authorize::AddRoleChild( RolePK parentRolePK, vector<RolePK>&& childRolePKs )ι->void{
		ul l{ Mutex };
		auto role = Roles.try_emplace( parentRolePK, parentRolePK, false );
		for( let childRolePK : childRolePKs )
			role.first->second.Members.emplace( PermissionRole{std::in_place_index<1>,childRolePK} );

		Recalc( l );
		TRACET( _ptags, "[{}+{}]Added role child.", parentRolePK, Str::Join(childRolePKs) );
	}

	α Authorize::RemoveRoleChildren( 	RolePK rolePK, flat_set<PermissionRightsPK> toRemove )ι->void{
		if( !toRemove.size() )
			return;
		ul l{ Mutex };
		auto role = Roles.find( rolePK );
		ASSERT( role!=Roles.end() );
		if( role==Roles.end() )
			return;
		for( let& member : toRemove ){
			auto& members = role->second.Members;
			for( auto p = members.begin(); p!=members.end(); ++p ){
				if( member==std::visit([](auto id)->PermissionRightsPK{return id;}, *p) ) {
					members.erase( p );
					break;
				}
			}
		}
		Recalc( l );
	}

	α	Authorize::DeleteRestoreRole( RolePK rolePK, bool deleted )ι->void{
		ul l{ Mutex };
		if( auto p = Roles.find(rolePK); p!=Roles.end() )
			p->second.IsDeleted = deleted;
		Recalc( l );//not sure a better way than recalc all users.
	}
	α Authorize::PurgeRole( RolePK rolePK )ι->void{
		ul l{ Mutex };
		auto p = Roles.find( rolePK );
		if( p==Roles.end() )
			return;
		let deleted = p->second.IsDeleted;
		Roles.erase( p );
		const PermissionRole member{ std::in_place_index<1>, rolePK };
		for( auto acl=Acl.begin(); acl!=Acl.end(); )//Acl is keyed by identity, so a role has to be swept by value.
			acl = acl->second==member ? Acl.erase( acl ) : std::next( acl );
		for( auto role=Roles.begin(); role!=Roles.end(); ++role )
			role->second.Members.erase( member );
		if( !deleted )
			Recalc( l );
	}

	α Authorize::AddPermission( IdentityPK identityPK, PermissionRole permissionRole, const flat_set<UserPK>& users, const ul& l )ι->void{
		flat_set<GroupPK> visitedGroups;
		AddPermission( identityPK, permissionRole, users, visitedGroups, l );
	}
	α Authorize::AddPermission( IdentityPK identityPK, PermissionRole permissionRole, const flat_set<UserPK>& users, flat_set<GroupPK>& visitedGroups, const ul& l )ι->void{
		if( auto pkUser = identityPK.IsUser() ? Users.find(identityPK.UserPK()) : Users.end(); pkUser!=Users.end() ){
			if( !users.empty() && !users.contains(pkUser->first) )
				return;
			flat_set<RolePK> visitedRoles;
			AddUserPermissions( pkUser->second, permissionRole, visitedRoles );
		}
		else if( auto group = identityPK.IsUser() ? Groups.end() : Groups.find(identityPK.GroupPK()); group!=Groups.end() && !group->second.IsDeleted && visitedGroups.emplace(group->first).second ){//deleted groups don't propagate, mirrors RecursiveUsers.
			for( auto member : group->second.Members )
				AddPermission( member, permissionRole, users, visitedGroups, l );//user
		}
	}
	α Authorize::AddUserPermissions( User& user, PermissionRole permissionRole, flat_set<RolePK>& visitedRoles )ι->void{
		if( auto p = permissionRole.index()==0 ? Permissions.find(get<0>(permissionRole)) : Permissions.end(); p!=Permissions.end() )
			user += p->second;
		else if( auto rolePermissions = permissionRole.index()==1 ? Roles.find(get<1>(permissionRole)) : Roles.end(); rolePermissions!=Roles.end() && !rolePermissions->second.IsDeleted && visitedRoles.emplace(rolePermissions->first).second ){
			for( let& rolePermission : rolePermissions->second.Members )
				AddUserPermissions( user, rolePermission, visitedRoles );
		}
	}
	α Authorize::UpdatePermission( PermissionPK permissionPK, optional<ERights> allowed, optional<ERights> denied )ε->void{
		ul l{ Mutex };
		auto p = Permissions.find( permissionPK ); THROW_IF( p==Permissions.end(), "[{}]Permission not found", permissionPK );
		p->second.Update( allowed, denied );
		for( let& user : Users )
			user.second.UpdatePermission( permissionPK, allowed, denied );
	}
	α Authorize::Recalc( const ul& l )ι->void{
		for( let& user : Users )
			user.second.Clear();
		SetUserPermissions( {}, l );
	}
	α Authorize::SetUserPermissions( flat_set<UserPK>&& users, const ul& l )ι->void{
		for( let& [identityPK,permissionRole] : Acl )
			AddPermission( identityPK, permissionRole, users, l );
	}

	//The guards are on the path, not visited sets:  a group or role reached two ways is two sources here, where enforcement
	//expands it once - the OR'd bits are the same either way.  Deleted groups and roles stop the walk as AddPermission and
	//AddUserPermissions do;  a deleted resource stays in the result - unenforced is what the reader is trying to see.
	α Authorize::UserRights( UserPK userPK )Ι->vector<ResourceRights>{
		Jde::sl _{ Mutex };
		if( !Users.contains(userPK) )
			return {};
		flat_map<ResourcePK,ResourceRights> byResource;
		vector<GroupPK> groups; vector<RolePK> roles;//the path so far
		auto expand = [&]( this auto&& self, PermissionRole permissionRole )ι->void {
			if( permissionRole.index()==0 ){
				auto p = Permissions.find( get<0>(permissionRole) );
				if( p==Permissions.end() )
					return;
				auto& resource = byResource.try_emplace( p->second.ResourcePK, ResourceRights{p->second.ResourcePK} ).first->second;
				resource.Rights.Allowed |= p->second.Allowed;
				resource.Rights.Denied |= p->second.Denied;
				resource.Sources.emplace_back( RightsSource{p->first, p->second.Allowed, p->second.Denied, groups, roles} );
			}
			else{
				let rolePK = get<1>( permissionRole );
				auto role = Roles.find( rolePK );
				if( role==Roles.end() || role->second.IsDeleted || std::ranges::find(roles, rolePK)!=roles.end() )
					return;
				roles.push_back( rolePK );
				for( let& member : role->second.Members )
					self( member );
				roles.pop_back();
			}
		};
		//does the acl identity reach the user - itself, or a live group whose members do - expanding the grant along each way in.
		auto reach = [&]( this auto&& self, IdentityPK identity, PermissionRole permissionRole )ι->void {
			if( identity.IsUser() ){
				if( identity.UserPK()==userPK )
					expand( permissionRole );
				return;
			}
			let groupPK = identity.GroupPK();
			auto group = Groups.find( groupPK );
			if( group==Groups.end() || group->second.IsDeleted || std::ranges::find(groups, groupPK)!=groups.end() )
				return;
			groups.push_back( groupPK );
			for( let& member : group->second.Members )
				self( member, permissionRole );
			groups.pop_back();
		};
		for( let& [identity, permissionRole] : Acl )
			reach( identity, permissionRole );
		vector<ResourceRights> y; y.reserve( byResource.size() );
		for( auto&& [pk, resource] : byResource ){ //&&: flat_map iterates a proxy pair.
			if( auto p = Resources.find(pk); p!=Resources.end() )
				resource.Cached = p->second;
			y.push_back( move(resource) );
		}
		return y;
	}
}