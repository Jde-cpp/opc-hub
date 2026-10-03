#pragma once
#include <jde/db/awaits/ExecuteAwait.h>
#include <jde/db/awaits/ScalerAwait.h>
#include <jde/access/access.h>
#include <jde/ql/QLAwait.h>

namespace Jde::QL{ struct MutationQL; struct TableQL; }
namespace Jde::Access::Server{
	struct AclQLAwait final : TAwait<jvalue>, noncopyable{
		AclQLAwait( QL::MutationQL m, UserPK executer, SRCE )ι:
			TAwait<jvalue>{ sl },
			_mutation{ move(m) },
			_executer{ executer }
		{}
		α Suspend()ι->void override;
		α InsertAcl()ι->void;
	private:
		QL::MutationQL _mutation;
		Jde::UserPK _executer;

		α Table()ε->const DB::Table&;
		α InsertPermission( const jobject& permission )ι->TAwait<optional<ResourcePK>>::Task;
		α InsertPermission( ERights allowed, ERights denied, ResourcePK resourcePK )ι->DB::ScalerAwait<PermissionPK>::Task;
		α InsertRole()ι->DB::ExecuteAwait::Task;
		α PurgeAcl()ι->DB::ScalerAwaitOpt<uint>::Task;
		α PurgeAcl( IdentityPK::Type identityPK, PermissionPK permissionPK, bool isRole )ι->DB::ExecuteAwait::Task;
	};

	struct AclQLSelectAwait final : TAwait<jvalue>, noncopyable{
		AclQLSelectAwait( const QL::TableQL& ql, UserPK executer, SRCE )ι:
			TAwait<jvalue>{ sl },
			Query{ ql },
			_executer{ executer }
		{}
		α Suspend()ι->void;
		struct Nest final{ Nest( string table, vector<string> path )ε; string Table; vector<string> Path; string IdKey; };//where a joined table's columns land in each row.
	private:
		α GetStatement( const QL::TableQL& childTable, sp<DB::Column> joinColumn )ε->DB::Statement;
		α Load( DB::Statement statement, vector<Nest> nesting, const QL::TableQL* identitiesQL={} )ι->DB::SelectAwait::Task;
		QL::TableQL Query;
		Jde::UserPK _executer;
	};
}