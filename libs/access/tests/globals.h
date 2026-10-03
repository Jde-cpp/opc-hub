#pragma once
#include <jde/access/access.h>
#include <jde/fwk/io/json.h>
#include <jde/ql/IQL.h>

namespace Jde::DB{ struct AppSchema; struct IDataSource; struct Table; }
namespace Jde::QL{ struct LocalQL; }
namespace Jde::Access{ struct Authorize; }
namespace Jde::Access::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };
	using ResourcePK=uint16;

	α Authorizer()ι->sp<Access::Authorize>;
	α QL()ι->QL::LocalQL&;
	α QLPtr()ι->sp<QL::LocalQL>;
	α SetQL( sp<QL::LocalQL> ql )ι->void;
	α GetTable( str name )ι->sp<DB::Table>;
	α DS()ι->DB::IDataSource&;
	α Schemas()ι->vector<sp<DB::AppSchema>>;

	//AclTests.cpp
	α CreateAcl( IdentityPK identityPK, ERights allowed, ERights denied, string resource, UserPK executer )ε->PermissionRightsPK;
	α CreateAcl( IdentityPK identityPK, RolePK rolePK, UserPK executer )ε->void;
	α PurgeAcl( IdentityPK identityPK, PermissionRightsPK permissionPK, UserPK executer )ε->void;
	α RestoreResource( string name, UserPK executer )ε->void;
	α SelectAcl( IdentityPK identityPK, string resourceSlug )ε->jobject;
	α SelectAcl( IdentityPK identityPK, RolePK rolePK )ε->jobject;

	α Add( const DB::Table& table, uint pk, vector<uint> members, UserPK userPK )ε->void;
	α AddToGroup( GroupPK id, vector<IdentityPK> members, UserPK userPK )ε->void;
	α Remove( const DB::Table& table, uint groupId, vector<uint> members, UserPK userPK )ε->void;
	α RemoveFromGroup( GroupPK id, vector<IdentityPK> members, UserPK userPK )ε->void;

	α Create( str table, sv slug, UserPK userPK, str input={}, SRCE )ε->uint;
	α Delete( str table, uint id, UserPK userPK )ε->jvalue;
	α Restore( str table, uint id, UserPK userPK )ε->jvalue;
	α Get( str table, str slug, UserPK userPK, sv cols={}, bool includeDeleted=false )ε->jobject;
	Ξ GetId( const jobject& j )ε->uint32{ return Json::AsNumber<uint32>( j, "id" ); }
	α GetGroup( str slug, UserPK userPK )ε->jobject;
	α GetRoot()ε->UserPK;
	α GetUser( str slug, UserPK userPK, bool includeDeleted=false, ProviderPK providerId=(ProviderPK)Access::EProviderType::Google )ε->jobject;

	α Purge( str table, uint id, UserPK userPK )ε->jvalue;
	α PurgeGroup( GroupPK id, UserPK userPK )ε->void;
	α PurgeUser( UserPK userId, UserPK userPK, SRCE )ε->void;

	α Select( sv table, uint id, UserPK userPK, sv cols={}, bool includeDeleted=false, SRCE )ε->jobject;
	α Select( sv table, str slug, UserPK userPK, sv cols={}, bool includeDeleted=false, SRCE )ε->jobject;
	α SelectGroup( str slug, UserPK userPK, bool includeDeleted=false )ε->jobject;
	α SelectPermission( ResourcePK resourcePK, UserPK userPK )ε->jobject;
	α SelectResource( str slug, UserPK userPK, bool includeDeleted=true, SRCE )ε->jobject;
	α SelectUser( str slug, UserPK userPK, optional<ProviderPK> providerPK=nullopt, bool includeDeleted=false )->jobject;

	α TestCrud( str table, str slug, UserPK userPK )ε->uint;
	α TestPurge( str table, uint id, UserPK userPK )ε->void;
	α TestAdd( str tableName, uint groupId, vector<uint> members, UserPK userPK )->void;
	α TestRemove( str tableName, uint groupId, vector<uint> members, UserPK userPK )->void;

	α TestUnauthCrud( str table, str slug, UserPK userPK )ε->uint;
	α TestUnauthUpdateName( str table, uint id, UserPK userPK, sv updatedName )ε->void;
	α TestUnauthDeleteRestore( str table, uint id, UserPK userPK )ε->void;
	α TestUnauthAddRemove( str tableName, uint groupId, vector<uint> members, UserPK userPK )->void;
	α TestUnauthPurge( str table, uint id, UserPK userPK )ε->void;

	//Every call straight through to the inner QL - a test overrides the one it intercepts.
	struct ForwardingQL : QL::IQL{
		ForwardingQL( sp<QL::IQL> inner )ι:_inner{ move(inner) }{}
		α Authorizer()ε->Access::Authorize& override{ return _inner->Authorizer(); }
		α AuthorizerPtr()ε->sp<Access::Authorize> override{ return _inner->AuthorizerPtr(); }
		α CustomQuery( QL::TableQL& ql, QL::Creds executer, SL sl )ι->up<TAwait<jvalue>> override{ return _inner->CustomQuery( ql, executer, sl ); }
		α CustomMutation( QL::MutationQL& ql, QL::Creds executer, SL sl )ι->up<TAwait<jvalue>> override{ return _inner->CustomMutation( ql, executer, sl ); }
		α LogQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->up<TAwait<jvalue>> override{ return _inner->LogQuery( move(ql), executer, sl ); }
		α LogSettingsQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->up<TAwait<jvalue>> override{ return _inner->LogSettingsQuery( move(ql), executer, sl ); }
		α StatusQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->jobject override{ return _inner->StatusQuery( move(ql), executer, sl ); }
		α Query( string query, jobject vars, UserPK executer, bool returnRaw, SL sl )ε->up<TAwait<jvalue>> override{ return _inner->Query( move(query), move(vars), executer, returnRaw, sl ); }
		α QueryObject( string query, jobject vars, UserPK executer, bool returnRaw, SL sl )ε->up<TAwait<jobject>> override{ return _inner->QueryObject( move(query), move(vars), executer, returnRaw, sl ); }
		α QueryArray( string query, jobject vars, UserPK executer, bool returnRaw, SL sl )ε->up<TAwait<jarray>> override{ return _inner->QueryArray( move(query), move(vars), executer, returnRaw, sl ); }
		α Upsert( string query, jobject vars, UserPK executer )ε->jarray override{ return _inner->Upsert( move(query), move(vars), executer ); }
		α Schemas()Ι->const vector<sp<DB::AppSchema>>& override{ return _inner->Schemas(); }
		α Subscribe( string&& query, jobject vars, sp<QL::IListener> listener, UserPK executer, SL sl )ε->up<TAwait<vector<QL::SubscriptionId>>> override{ return _inner->Subscribe( move(query), move(vars), listener, executer, sl ); }
		sp<QL::IQL> _inner;
	};
}