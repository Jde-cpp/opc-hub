#include "globals.h"
#include <jde/fwk/str.h>
#include <jde/db/names.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Column.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/QLAwait.h>
#include "../src/awaits/ResourceLoadAwait.h"
#include "jde/fwk/exceptions/Exception.h"
#include <jde/access/Authorize.h>

#define let const auto

namespace Jde::DB{ struct IDataSource; struct AppSchema; }
namespace Jde::Access{
	using namespace DB::Names;
	sp<QL::LocalQL> _localQL;
	sp<Authorize> _authorizer = ms<Authorize>( "Tests" );

	α Tests::Authorizer()ι->sp<Access::Authorize>{ return _authorizer; }
	α Tests::QL()ι->QL::LocalQL&{ return *_localQL; }
	α Tests::QLPtr()ι->sp<QL::LocalQL>{ return _localQL; }
	α Tests::SetQL( sp<QL::LocalQL> ql )ι->void{ _localQL = move(ql); }
	α Tests::DS()ι->DB::IDataSource&{ return _localQL->DS(); }
	α Tests::GetTable( str name )ι->sp<DB::Table>{ return _localQL->GetTablePtr( FromJson(name) ); }
	α Tests::Schemas()ι->vector<sp<DB::AppSchema>>{ return _localQL->Schemas(); }


namespace Tests{
	Ω testUnauthGet( str table, str slug, UserPK executer, sv cols, bool includeDeleted )ε->jobject{
		auto y = Select( table, slug, GetRoot(), cols, includeDeleted );
		if( y.empty() ){
			try{
				Create(table, slug, executer);
				throw std::runtime_error( "Should not be able to create." );
			}
			catch( Exception& e ){
				e.SetLevel(ELogLevel::NoLog);
			}
			Create( table, slug, GetRoot() );
			EXPECT_THROW( Select(table, slug, executer, cols, includeDeleted), Exception );
			y = Select( table, slug, GetRoot(), cols, includeDeleted );
		}
		return y;
	}

	Ω updateNameQL( str table, uint id, sv updatedName )ε->string{
		return Str::Replace( Ƒ("mutation update{}( id:{}, name:'{}' )", Capitalize(table), id, updatedName), '\'', '"' );
	}
	Ω testUpdateName( str table, uint id, UserPK executer, sv updatedName )ε->void{
 		let updateJson = QL().QuerySync<jvalue>( updateNameQL(table, id, updatedName), {}, executer );
		ASSERT_TRUE( Json::AsSV(Select(table,id, GetRoot(), {}, true), "name")==updatedName );
	}

	Ω deleteQL( str table, uint id )ι->string{ return Ƒ( "mutation delete{}( id:{} )", Capitalize(table), id ); }
	Ω restoreQL( str table, uint id )ι->string{ return Ƒ( "mutation restore{}( id:{} )", Capitalize(table), id ); }
	Ω testDeleteRestore( str table, uint id, UserPK executer )ε->void{
		let del = Ƒ( "mutation delete{}(\"id\":{})", Capitalize(table), id );
		let deleteJson = QL().QuerySync<jvalue>( deleteQL(table, id), {}, executer );
		ASSERT_TRUE( Select(table, id, executer).empty() );
		ASSERT_FALSE( Select(table, id, executer, {}, true).empty() );

 		let restoreJson = QL().QuerySync<jvalue>( restoreQL(table, id), {}, executer );
		ASSERT_FALSE( Select(table, id, executer).empty() );
	}
	Ω addRemoveQL( sv op, const DB::Table& table, uint pk, vector<uint> members )ε->string{
		let& map = *table.Map;
		let parentTable = map.Parent->Table;
		let parentTableName = Capitalize( parentTable->JsonName() );
		let memberString = members.size()==1 ? Ƒ( "{}", members[0] ) : '['+Str::Join( members )+']';
		return Ƒ( "mutation {}{}( id:{}, {}:{} )", op, parentTableName, pk, ToJson(map.Child->Name), memberString );
	}
	Ω addRemove( sv op, const DB::Table& table, uint pk, vector<uint> members, UserPK executer )ε->jvalue{
		return QL().QuerySync<jvalue>( addRemoveQL(op, table, pk, members), {}, executer );
	}
}
	α Tests::TestUnauthUpdateName( str table, uint id, UserPK executer, sv updatedName )ε->void{
 		EXPECT_THROW( QL().QuerySync<jvalue>( updateNameQL(table, id, updatedName), {}, executer), Exception );
	}
	α Tests::TestUnauthDeleteRestore( str table, uint id, UserPK executer )ε->void{
		EXPECT_THROW( QL().QuerySync<jvalue>( deleteQL(table,id), {}, executer), Exception );
		EXPECT_THROW( QL().QuerySync<jvalue>( restoreQL(table,id), {}, executer), Exception );
	}

	using namespace Json;

	α Tests::Add( const DB::Table& table, uint groupPK, vector<uint> members, UserPK executer )ε->void{
		addRemove( "add", table, groupPK, members, executer );
	}

	α memberString( vector<IdentityPK> members )ε->string{
		string memberString;
		for( let member : members )
			memberString+= std::to_string(member.Underlying()) + ',';
		memberString.pop_back();
		if( members.size()>1 )
			memberString = "["+memberString+"]";
		return memberString;
	}
	α Tests::AddToGroup( GroupPK id, vector<IdentityPK> members, UserPK executer )ε->void{
		let ql =	Ƒ( "mutation addGroup( \"id\":{}, \"memberId\":{} )", id.Value, memberString(members) );
		let addJson = QL().QuerySync<jvalue>( ql, {}, executer );
	}

	α Tests::Remove( const DB::Table& table, uint groupPK, vector<uint> members, UserPK executer )ε->void{
		addRemove( "remove", table, groupPK, members, executer );
	}
	α Tests::RemoveFromGroup( GroupPK id, vector<IdentityPK> members, UserPK executer )ε->void{
		let ql = Ƒ( "mutation removeGroup( \"id\":{}, \"memberId\":{} )", id.Value, memberString(members) );
		let removeJson = QL().QuerySync<jvalue>( ql, {}, executer );
	}


	α Tests::Create( str table, sv slug, UserPK executer, str input, SL sl )ε->uint{
		let create = Ƒ( "create{0}(  slug:'{1}', name:'{1} - name', description:'{1} - description' {2} ){{id}}", Capitalize(table), slug, input.size() ? ","s+input : "" );
		let createJson = QL().QuerySync( Str::Replace(create, '\'', '"'), {}, executer, true, sl );
		return GetId( createJson );//{"user":{"id":7}}
	}
	using Tests::QL;
	Ω createUser( str slug, ProviderPK providerId, UserPK executer )ε->UserPK{
		jobject vars{ {"slug", slug}, {"provider",providerId}, {"name", slug+" - name "}, {"description", slug+" - desc"} };
		let q = "createUser(  loginName:$slug, slug:$slug, providerId:$provider, name:$name, description:$description ){{id}}";
		let createJson = QL().QuerySync( q, vars, executer );
		return { Tests::GetId(createJson) };//{"user":{"id":7}}}
	}
	α createGroup( str slug, UserPK executer )ε->GroupPK{
		let create = Ƒ( "mutation createGroup(  slug:'{0}', name:'{0} - name', description:'{0} - description' ){{id}}", slug );
		let createJson = QL().QuerySync( Str::Replace(create, '\'', '"'), {}, executer );
		return {AsNumber<GroupPK::Type>( createJson, "id")};
	}
	α columns( sv cols, bool includeDeleted )ε->string{
		return Ƒ( "id name attributes created updated slug description {} {}", cols, includeDeleted ? "deleted" : "" );
	}
	Ω select( sv table, str filter, str cols, UserPK executer, SRCE )ε->jobject{
		let ql = Ƒ( "{}({}){{ {} }}", table, filter, cols );
		return QL().QuerySync( ql, {}, executer, true, sl );
	}

	α Tests::Select( sv table, uint id, UserPK executer, sv cols, bool includeDeleted, SL sl )ε->jobject{
		return select( DB::Names::ToSingular(table), Ƒ("id:{} ", id), columns(cols, includeDeleted), executer, sl );
	}

	α Tests::Select( sv table, str slug, UserPK executer, sv cols, bool includeDeleted, SL sl )ε->jobject{
		return select( DB::Names::ToSingular(table), Ƒ("slug:\"{}\" ", slug), columns(cols, includeDeleted), executer, sl );
	}

	α Tests::SelectGroup( str slug, UserPK executer, bool includeDeleted )ε->jobject{
		let ql = Ƒ( "group(slug:\"{}\"){{ id name attributes created updated slug description {} groupMembers{{id name}} }}", slug, includeDeleted ? "deleted" : "" );
		return QL().QuerySync( ql, {}, executer );
	}
	α Tests::SelectPermission( ResourcePK resourcePK, UserPK executer )ε->jobject{
		let ql = Ƒ( "permission( resourceId:{} ){{ id resourceId allowed denied }}", resourcePK.Value );
		return QL().QuerySync( ql, {}, executer );
	}

	α Tests::SelectResource( str slug, UserPK executer, bool includeDeleted, SL sl )ε->jobject{
		let ql = Ƒ( "resource( schemaName:\"access\", slug:\"{}\", criteria:null ){{ id schemaName allowed name attributes created {} updated slug description }}", slug, includeDeleted ? "deleted" : "" );
		return QL().QuerySync( ql, {}, executer, true, sl );
	}
	α Tests::SelectUser( str slug, UserPK executer, optional<ProviderPK> provider, bool includeDeleted )->jobject{
		jobject vars{ {"slug", slug} };
		string providerQL;
		if( provider ){
			vars["provider"] = *provider;
			providerQL = ", provider_id:$provider";
		}
		auto selectAll = Ƒ( "user(slug:$slug {}){{ id name attributes created updated slug description provider {} }}", move(providerQL), includeDeleted ? "deleted" : "" );
		return QL().QuerySync( move(selectAll), vars, executer );
	}

	α Tests::Get( str table, str slug, UserPK executer, sv cols, bool includeDeleted )ε->jobject{
		auto y = Select( table, slug, executer, cols, includeDeleted );
		if( y.empty() ){
			Create( table, slug, executer );
			y = Select( table, slug, executer, cols, includeDeleted );
		}
		return y;
	}
	optional<UserPK> _root;
	α Tests::GetRoot()ε->UserPK{
		if( _root )
			return *_root;

		auto root = SelectUser( "root", {UserPK::System} );
		if( root.empty() ){
			_root = createUser( "root", (ProviderPK)EProviderType::Google, {UserPK::System} );
			let resourcePermissions = BlockAwait<ResourceLoadAwait,ResourcePermissions>( ResourceLoadAwait(_localQL, {GetTable("acl")->Schema}, {}, {UserPK::System}) );
			for( let& [pk,resource] : resourcePermissions.Resources )
				CreateAcl( *_root, ERights::All, ERights::None, resource.Slug, {UserPK::System} );
		}
		else
			_root = UserPK{ GetId(root) };
		return *_root;
	}

	α Tests::GetUser( str slug, UserPK executer, bool includeDeleted, ProviderPK provider )ε->jobject{
		if( executer==UserPK{0} )
			executer = GetRoot();
		auto user = SelectUser( slug, executer, provider, includeDeleted );
		if( user.empty() ){
			createUser( slug, provider, executer );
			user = SelectUser( slug, executer, provider, includeDeleted );
		}
		return user;
	}
	α Tests::GetGroup( str slug, UserPK executer )ε->jobject{
		auto y = SelectGroup( slug, executer, true );
		TRACE( "{}", serialize(y) );
		if( y.empty() ){
			createGroup( slug, executer );
			y = SelectGroup( slug, executer, false );
		}
		return y;
	}

	α Tests::Purge( str table, uint id, UserPK executer )ε->jvalue{
		let ql = Ƒ( "mutation purge{}(\"id\":{})", Capitalize(table), id );
		let y = QL().QuerySync<jvalue>( ql, {}, executer );
		return y;
	}
	α Tests::PurgeUser( UserPK userId, UserPK executer, SL sl )ε->void{
		let purge = Ƒ( "mutation purgeUser(\"id\":{})", userId.Value );
		let purgeJson = QL().QuerySync<jvalue>( purge, {}, executer, true, sl );
	}
	α Tests::PurgeGroup( GroupPK id, UserPK executer )ε->void{
		let purge = Ƒ( "mutation purgeGroup(\"id\":{})", id.Value );
		let purgeJson = QL().QuerySync<jvalue>( purge, {}, executer );
	}
	α Tests::Delete( str table, uint id, UserPK executer )ε->jvalue{
		let del = Ƒ( "mutation delete{}( id:{} )", Capitalize(table), id );
		return QL().QuerySync<jvalue>( del, {}, executer );
	}
	α Tests::Restore( str table, uint id, UserPK executer )ε->jvalue{
		let ql = Ƒ( "mutation restore{}( id:{} )", Capitalize(table), id );
		return QL().QuerySync<jvalue>( ql, {}, executer );
	}
	α Tests::TestAdd( str tableName, uint groupPK, vector<uint> members, UserPK executer )->void{
		auto getMemberIds = [&]()->flat_set<uint>{
			let o = Select( tableName, groupPK, executer, {"groupMembers{id}"}, true );
			flat_set<uint> memberIds;
			for( let& member : o.empty() ? jarray{} : AsArray(o, "groupMembers") )
				memberIds.emplace( GetId(AsObject(member)) );
			return memberIds;
		};
		auto memberIds = getMemberIds();
		if( memberIds.find(members.front())==memberIds.end() ){
			Add( *GetTable(tableName), groupPK, members, executer );
			memberIds = getMemberIds();
		}
		for( let member : members )
			ASSERT_TRUE( memberIds.contains(member) ) << "member not found: " << member;
	}
	α Tests::TestRemove( str tableName, uint groupPK, vector<uint> members, UserPK executer )->void{
		Remove( *GetTable(tableName), groupPK, members, executer );
		let o = Select( ToSingular(tableName), groupPK, GetRoot(), {"groupMembers{id}"}, true );
		flat_set<uint> memberIds;
		for( let& member : AsArray(o, "groupMembers") )
			memberIds.emplace( AsNumber<uint>(AsObject(member), "id") );
		for( let member : members )
			ASSERT_TRUE( !memberIds.contains(member) );
	}
}

namespace Jde::Access{
	α Tests::TestCrud( str table, str slug, UserPK executer )ε->uint{
		let row = Get( table, slug, executer, {}, true );
		let id = GetId( row );
		testUpdateName( table, id, executer, "newName" );
		testDeleteRestore( table, id, executer );
		return id;
	}
	α Tests::TestPurge( str table, uint id, UserPK executer )ε->void{
		Purge( table, id, executer );
 		ASSERT_TRUE( Select(table, id, executer, {}, true).empty() );
	}

	α Tests::TestUnauthCrud( str table, str slug, UserPK executer )ε->uint{
		let row = testUnauthGet( table, slug, executer, {}, true );
		let id = GetId( row );
		TestUnauthUpdateName( table, id, executer, "newName" );
		TestUnauthDeleteRestore( table, id, executer );
		return id;
	}
	α Tests::TestUnauthAddRemove( str tableName, uint pk, vector<uint> members, UserPK executer )->void{
		let& table = *GetTable( tableName );
		EXPECT_THROW( addRemove("add", table, pk, members, executer), Exception );
		EXPECT_THROW( addRemove("remove", table, pk, members, executer), Exception );
	}
	α Tests::TestUnauthPurge( str table, uint id, UserPK executer )ε->void{
		EXPECT_THROW( Purge(table, id, executer), Exception );
		Purge( table, id, GetRoot() );
	}
}