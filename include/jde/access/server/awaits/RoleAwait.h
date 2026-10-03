#pragma once
#include <jde/db/awaits/ExecuteAwait.h>
#include <jde/db/awaits/ScalerAwait.h>
#include <jde/ql/QLAwait.h>

namespace Jde::DB{ struct Statement; }
namespace Jde::QL{ struct MutationQL; struct TableQL; }
namespace Jde::Access::Server{
	struct RoleAwait final : TAwait<jvalue>, noncopyable{
		RoleAwait( const QL::TableQL& q, UserPK userPK, SRCE )ε;
		α Suspend()ι->void override{ Select(); }
		sp<DB::Table> MemberTable;
		QL::TableQL Query;
		Jde::UserPK UserPK;
	private:
		α PermissionsStatement( QL::TableQL& permissionQL )ε->DB::Statement;
		α RoleStatement( QL::TableQL& roleQL )ε->DB::Statement;
		α Select()ι->QL::QLAwait<>::Task;
	};

	//addRole/removeRole.  The role by id, or by slug - a seed file (libs/access/config/release.roles) names the roles it created
	//a line earlier and cannot know their pks - and a member role likewise:  role:{id:N} or role:{slug:"viewer"}.  Each slug
	//is a lookup, so the chain is Start → [Resolve] → Dispatch → Members → [ResolveChildren] → AddMembers/RemoveMembers, state on the
	//members between hand-offs.
	struct RoleMAwait final : TAwait<jvalue>{
		RoleMAwait( const QL::MutationQL& m, UserPK userPK, SRCE )ι:TAwait<jvalue>{ sl }, _mutation{m}, _userPK{userPK}{}
		α Suspend()ι->void override{ Start(); }
	private:
		α Start()ι->void;
		α Resolve( string slug )ι->DB::ScalerAwaitOpt<RolePK>::Task;
		α Dispatch( RolePK rolePK )ι->void;
		α Members( RolePK parentRolePK, const jobject& childRole )ι->void;
		α ResolveChildren( RolePK parentRolePK, vector<string> slugs )ι->DB::ScalerAwaitOpt<RolePK>::Task;
		α AddMembers( RolePK parentRolePK )ι->DB::ExecuteAwait::Task;
		α AddPermission( RolePK parentRolePK, const jobject& permissionRights )ι->TAwait<PermissionRightsPK>::Task;
		α RemoveMembers( RolePK parentRolePK )ι->DB::ExecuteAwait::Task;
		α RemovePermission( RolePK parentRolePK )ι->DB::ExecuteAwait::Task;

		QL::MutationQL _mutation;
		jobject _args; //ExtrapolateVariables() - AddPermission references into it across its suspensions.
		vector<RolePK> _children; //the member roles of an add/remove, once resolved.
		UserPK _userPK;
	};
}