#pragma once
#include <absl/synchronization/mutex.h>
#include <jde/access/IAcl.h>
#include "types/Resource.h"
#include "types/Group.h"
#include "types/Role.h"
#include "types/User.h"

struct UA_Server;
namespace Jde::Access{
	namespace Server{ struct AuthenticateAwait; struct LoginAwait; }
	struct Identities; struct Listener; struct Permission; struct ResourcePermissions;

	struct Authorize /*final*/ : IAcl, std::enable_shared_from_this<Authorize>{
		Authorize( string app )ι:_app{move(app)}{}
		virtual ~Authorize()=default;

		α Test( str schemaName, str resourceName, ERights rights, UserPK userPK, SRCE )ε->void override;
		α TestSystem( str schemaName, str resourceName, ERights rights, UserPK executer, SRCE )ε->void override;
		α TestUser( UserPK executer, SRCE )Ε->void override;
		α Rights( str schemaName, str resourceName, UserPK executer )ι->ERights override;
		α UserName( UserPK userPK )ι->string override;

		α AddResource( ResourcePK resourcePK, string schema, string resourceSlug, string criteria )ι->void;
		//By value:  the protected overload's pointer aliases Resources, a flat_map any concurrent insert reallocates, so nothing may
		//carry it past the lock (access-review3 #19).  A shared lock, as this reads only.
		α FindResource( const Resource& resource )Ι->optional<Resource>{ rl _{Mutex}; auto p = FindResourceLocked( resource ); return p ? optional<Resource>{*p} : optional<Resource>{}; }
		α FindResource( ResourcePK pk )Ι->optional<Resource>{ rl _{Mutex}; auto p = Resources.find( pk ); return p!=Resources.end() ? optional<Resource>{p->second} : optional<Resource>{}; }
		α FindActiveResourcePK( string schema, str resourceName, str criteria )ι->optional<ResourcePK>{ rl _{Mutex}; return FindActiveResourcePKLocked(schema, resourceName, criteria); }
		α GetSchema( str resourceSlug, SL sl )ε->string;

		α TestAdminSlug( str slug, UserPK userPK, SRCE )ε->void;//an app schema's slug:  opc schemas reuse slugs across instances, so they are not searched.
		//The gate on a role/acl grant for (schema, slug, criteria).  Remote - the schema's registered IAdminAcl, the OpcServer,
		//which alone knows which resource governs a node - when one is registered and its registrant still passes TestSchemaAdmin;
		//else TestAdminLocal, pre-completed (appserver-review3 #13).
		α TestAdminGrant( str schema, str resource, str criteria, UserPK userPK, SRCE )ι->up<AnyVoidAwait>;
		//The flat rule, on this cache alone:  the active (schema,slug,criteria) row when there is one - a mapped criteria is its
		//own resource, root does not inherit down - else the slug's criteria-less root row, which an unmapped criteria inherits
		//as an unmapped node inherits it in OpcAuthorize::UserRights.  Neither active = not enabled, passes as Test does.
		α TestAdminLocal( str schema, str resource, str criteria, UserPK userPK, SRCE )ε->void;
		α TestAdminResource( ResourcePK resourcePK, UserPK userPK, SRCE )ε->void;
		α TestAdminPermission( PermissionPK permissionPK, UserPK userPK, SRCE )ε->void;
		//May userPK stand in for the schema and answer its admin checks (AddAdminAuthorizer)?  Administer on every active
		//criteria-less resource of the schema.  A schema with no active root passes:  unknown, or every root deleted, is a
		//no-op rather than a denial, so an instance may register before its resources exist (appserver-review3 #4, rejected -
		//the denial was tried and removed, do not reinstate it without changing that call).
		α TestSchemaAdmin( str schema, UserPK userPK, SRCE )ε->void;
		struct AdminAuthorizer{ sp<IAdminAcl> Acl; UserPK User; };//User: the registrant, re-tested with TestSchemaAdmin at each check so a registrant whose rights went falls back to the local rule.
		α AddAdminAuthorizer( str schemaName, sp<IAdminAcl> authorizer, UserPK registrant )ι->void;
		α RemoveAdminAuthorizer( const sp<IAdminAcl>& authorizer )ι->void;//deregister on disconnect - registrations are otherwise permanent and go stale.

		α TestAddGroupMember( GroupPK groupPK, flat_set<IdentityPK::Type>&& memberPKs, SRCE )ε->void;
		α TestAddRoleMember( RolePK parent, RolePK child, SRCE )ε->void;

		//One user's rights source by source - the walk SetUserPermissions makes for everyone, run for one user and kept apart
		//instead of OR'd into User::Rights (the SPA's Effective rights tab, UserRightsAwait).  Paths run top-down from the acl
		//identity:  Groups from the granted group to the one holding the user (empty = granted to the user), Roles from the
		//assigned role to the one holding Permission (empty = a bare acl grant).
		struct RightsSource final{ PermissionPK Permission; ERights Allowed; ERights Denied; vector<GroupPK> Groups; vector<RolePK> Roles; };
		struct ResourceRights final{
			Access::ResourcePK PK{};
			optional<Access::Resource> Cached;//the Resources row, copied under the lock; nullopt for a pk the cache never saw.
			AllowedDisallowed Rights{};//the OR over Sources - the same OR User::operator+= makes, so Effective() is what Test/Rights answer.
			vector<RightsSource> Sources;
		};
		α UserRights( UserPK userPK )Ι->vector<ResourceRights>;//empty for an unknown user; by PK.
		α IsRoleMember( RolePK parent, RolePK child )Ι->bool;//a direct member, as the cache holds it - RoleMAwait::AddRole's no-op check for a re-add (the seed reruns on every -sync start).
	protected:
		α Load( Identities&& identities, ResourcePermissions&& resources, flat_map<RolePK,Role>&& roles, flat_multimap<IdentityPK,PermissionRole>&& acl )ι->void;
		ABSL_SHARED_LOCKS_REQUIRED(Mutex) α FindResourceLocked( const Resource& resource )Ι->const Resource*;
		ABSL_SHARED_LOCKS_REQUIRED(Mutex) α FindActiveResourcePKLocked( str schemaName, str resourceName, str criteria )Ι->optional<ResourcePK>;
		ABSL_SHARED_LOCKS_REQUIRED(Mutex) α FindUserLocked( UserPK executer )Ι->std::expected<const User*,EHttpStatus>;//null for the System; Unauthorized = an unknown user, Forbidden = a deleted one.
		ABSL_SHARED_LOCKS_REQUIRED(Mutex) α ConfiguredRightsLocked( UserPK executer, ResourcePK resourcePK )Ι->std::expected<AllowedDisallowed,EHttpStatus>;//System is granted everything; errors as FindUserLocked.
		ABSL_SHARED_LOCKS_REQUIRED(Mutex) α TestRights( ResourcePK resourcePK, sv resourceName, ERights rights, UserPK executer, SL sl )Ε->void;

		string _app;
		mutable absl::Mutex Mutex;
		/// Active only <schemaName, <resourceJsonName,<criteria, resourcePK>>>
		flat_map<string, flat_map<string,flat_map<string,Access::ResourcePK>>> SchemaResources ABSL_GUARDED_BY(Mutex);
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(Mutex) α Index( const Resource& resource )ι->void;//a deleted row is a no-op.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(Mutex) α Index( str schema, str slug, str criteria, ResourcePK pk )ι->void;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(Mutex) α Unindex( const Resource& resource )ι->void;//only while the entry is still this row's.
		flat_map<UserPK,User> Users ABSL_GUARDED_BY(Mutex);
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(Mutex) α SetUserPermissions( const flat_set<UserPK>& users )ι->void;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(Mutex) α RecalcGroupMembers( GroupPK groupPK, bool remove=false )ι->void;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(Mutex) α Recalc()ι->void;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(Mutex) α RecursiveUsers( GroupPK groupPK )ι->flat_set<UserPK>;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(Mutex) α RecursiveUsers( GroupPK groupPK, flat_set<GroupPK>& visited )ι->flat_set<UserPK>;
		α FindAdminAuthorizer( str schemaName )ι->optional<AdminAuthorizer>;

		ABSL_EXCLUSIVE_LOCKS_REQUIRED(Mutex) α AddAclEntry( IdentityPK identityPK, PermissionRole permissionRole )ι->void;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(Mutex) α PurgeIdentity( IdentityPK identityPK )ι->void;
		α AddAcl( IdentityPK::Type userGroupPK, const Permission& permission )ι->void;
		α AddAcl( IdentityPK::Type userGroupPK, RolePK rolePK )ι->void;
		α RemoveAcl( IdentityPK::Type userGroupPK, PermissionRole rolePK )ι->void;

		α AddToGroup( GroupPK groupPK, flat_set<IdentityPK::Type> members )ι->void;
		α DeleteGroup( GroupPK identityPK )ι->void;
		α RestoreGroup( GroupPK groupPK )ι->void;
		α RemoveFromGroup( GroupPK groupPK, flat_set<IdentityPK::Type> members )ι->void;
		α PurgeGroup( GroupPK groupPK )ι->void;

		ABSL_EXCLUSIVE_LOCKS_REQUIRED(Mutex) α AddPermission( IdentityPK identityPK, PermissionRole permissionRole, const flat_set<UserPK>& users )ι->void;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(Mutex) α AddPermission( IdentityPK identityPK, PermissionRole permissionRole, const flat_set<UserPK>& users, flat_set<GroupPK>& visitedGroups )ι->void;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(Mutex) α AddUserPermissions( User& user, PermissionRole permissionRole, flat_set<RolePK>& visitedRoles )ι->void;
		α UpdatePermission( PermissionPK permissionPK, optional<ERights> allowed, optional<ERights> denied )ε->void;

		β CreateResource( Resource&& resource )ε->void;
		β UpdateResourceDeleted( ResourcePK pk, sv schemaName, const jobject& args, bool restored )ε->void;

		α DeleteRestoreRole( RolePK rolePK, bool deleted )ι->void;
		α PurgeRole( RolePK rolePK )ι->void;
		α AddRolePermission( RolePK rolePK, const Permission& permission, const jobject& resource )ι->void;
		α AddRoleChild( RolePK parentRolePK, vector<RolePK>&& childRolePK )ι->void;
		α RemoveRoleChildren( RolePK rolePK, const flat_set<PermissionRole>& toRemove )ι->void;

		α CreateUser( UserPK userPK, string name )ι->void;
		α RenameUser( UserPK userPK, string name )ι->void;
		α DeleteUser( UserPK identityPK )ι->void;
		α RestoreUser( UserPK identityPK )ι->void;
		α PurgeUser( UserPK identityPK )ι->void;

		ABSL_SHARED_LOCKS_REQUIRED(Mutex) α TestAdmin( const Resource& resource, UserPK userPK, SL sl )ε->void;
		ABSL_SHARED_LOCKS_REQUIRED(Mutex) α ToIdentityPK( IdentityPK::Type userGroupPK )Ι->IdentityPK;

		/// Includes inactive resources.
		flat_map<ResourcePK,Resource> Resources ABSL_GUARDED_BY(Mutex);

		flat_map<PermissionPK,Permission> Permissions ABSL_GUARDED_BY(Mutex);
		flat_map<GroupPK,Group> Groups ABSL_GUARDED_BY(Mutex);
		flat_map<RolePK,Role> Roles ABSL_GUARDED_BY(Mutex);
		flat_multimap<IdentityPK,PermissionRole> Acl ABSL_GUARDED_BY(Mutex);
	private:
		concurrent_flat_map<string,AdminAuthorizer> _adminAuthorizers;
		friend struct AccessListener; friend struct ConfigureAwait; friend struct Server::AuthenticateAwait; friend struct Server::LoginAwait;
	};

	Ξ Authorize::FindResourceLocked( const Resource& resource )Ι->const Resource*{
		auto pk = resource.PK;
		if( !pk && resource.Schema.size() && resource.Slug.size() )
			pk = FindActiveResourcePKLocked( resource.Schema, resource.Slug, resource.Criteria ).value_or( ResourcePK{} );
		if( auto p = pk ? Resources.find(pk) : Resources.end(); p!=Resources.end() )
			return &p->second;
		//Only a criteria-less request may fall back to the criteria-less row.  Without that guard a *criteria-scoped* lookup
		//that missed - its row not cached yet, which is the ordinary case for a resource created by the same mutation that
		//grants on it - resolved to the slug's root row instead, and AddRolePermission then wrote the node's rights over
		//the root's.  A node-scoped grant silently rewriting the root grant is the opposite of what it says (opcserver-review3
		//L30, found fixing that test).  Missing now returns null, and AddRolePermission's `new resource` branch caches the
		//row from the payload, which carries its pk.
		if( resource.Slug.size() && resource.Criteria.empty() ){
			for( const auto& [existingPK,existing] : Resources ){
				if( (resource.Schema.empty() || existing.Schema==resource.Schema) && existing.Slug==resource.Slug && existing.Criteria.empty() )
					return &existing;
			}
		}
		return nullptr;
	}
	Ξ Authorize::FindActiveResourcePKLocked( str schemaName, str resourceSlug, str criteria )Ι->optional<ResourcePK>{
		if( auto schemaResources = SchemaResources.find(schemaName); schemaResources!=SchemaResources.end() ){
			if( auto slugResources = schemaResources->second.find(resourceSlug); slugResources!=schemaResources->second.end() ){
				auto& criteras = slugResources->second;
				if( criteras.contains({}) )// if permisions are enabled
					return Find(criteras, criteria);
			}
		}
		return {};
	}
}