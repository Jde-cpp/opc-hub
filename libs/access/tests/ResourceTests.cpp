#include "gtest/gtest.h"
#include "globals.h"
#include <jde/ql/QLAwait.h>
#include <jde/db/meta/Table.h>
#include <jde/access/types/Resource.h>
#include <jde/access/Authorize.h>
#include <jde/ql/IQL.h>
#include <jde/ql/LocalQL.h>
#include "../src/awaits/ResourceLoadAwait.h"

#define let const auto
namespace Jde::Access::Tests{
//	using namespace Json;
//	using namespace Tests;
	class ResourceTests : public ::testing::Test{
	};

	α selectResources( sv slug, string filter, bool includeDeleted=false )->jarray{
		let slugFilter = slug.size() ? Ƒ( ", slug:\"{}\"", slug ) : "";
		if( filter.size() )
			filter = Ƒ( ", criteria:\"{}\"", filter );
		let ql = Ƒ( "resources( schemaName:\"access\"{}{} ){{ id schemaName allowed name attributes created {} updated slug description }}", slugFilter, filter, includeDeleted ? "deleted" : "" );
		return QL().QuerySync<jarray>( ql, {}, GetRoot() );
	}

	//access-review3 #24:  ResourceSync creates each resource active and disables it in a second, untransacted call.  A failure in
	//between left the table denying every non-System user, and the next sync skipped any slug that had a row, so it never healed.
	//This is the real LocalQL with the second call refused - the failure the finding describes - and the sync's own next pass.
	struct DeleteRefusingQL final : QL::IQL{
		DeleteRefusingQL( sp<QL::IQL> inner )ι:_inner{ move(inner) }{}
		α Authorizer()ε->Access::Authorize& override{ return _inner->Authorizer(); }
		α AuthorizerPtr()ε->sp<Access::Authorize> override{ return _inner->AuthorizerPtr(); }
		α CustomQuery( QL::TableQL& ql, QL::Creds executer, SL sl )ι->up<TAwait<jvalue>> override{ return _inner->CustomQuery( ql, executer, sl ); }
		α CustomMutation( QL::MutationQL& ql, QL::Creds executer, SL sl )ι->up<TAwait<jvalue>> override{ return _inner->CustomMutation( ql, executer, sl ); }
		α LogQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->up<TAwait<jvalue>> override{ return _inner->LogQuery( move(ql), executer, sl ); }
		α LogSettingsQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->up<TAwait<jvalue>> override{ return _inner->LogSettingsQuery( move(ql), executer, sl ); }
		α StatusQuery( QL::TableQL&& ql, QL::Creds executer, SL sl )ε->jobject override{ return _inner->StatusQuery( move(ql), executer, sl ); }
		α Query( string query, jobject vars, UserPK executer, bool returnRaw, SL sl )ε->up<TAwait<jvalue>> override{
			THROW_IF( query.starts_with("deleteResource"), "refused, as the finding's failure: {}", query );
			return _inner->Query( move(query), move(vars), executer, returnRaw, sl );
		}
		α QueryObject( string query, jobject vars, UserPK executer, bool returnRaw, SL sl )ε->up<TAwait<jobject>> override{ return _inner->QueryObject( move(query), move(vars), executer, returnRaw, sl ); }
		α QueryArray( string query, jobject vars, UserPK executer, bool returnRaw, SL sl )ε->up<TAwait<jarray>> override{ return _inner->QueryArray( move(query), move(vars), executer, returnRaw, sl ); }
		α Upsert( string query, jobject vars, UserPK executer )ε->jarray override{ return _inner->Upsert( move(query), move(vars), executer ); }
		α Schemas()Ι->const vector<sp<DB::AppSchema>>& override{ return _inner->Schemas(); }
		α Subscribe( string&& query, jobject vars, sp<QL::IListener> listener, UserPK executer, SL sl )ε->up<TAwait<vector<QL::SubscriptionId>>> override{ return _inner->Subscribe( move(query), move(vars), listener, executer, sl ); }
		sp<QL::IQL> _inner;
	};
	//AclTests.cpp
	α CreateAcl( IdentityPK identityPK, ERights allowed, ERights denied, string resource, UserPK executer )ε->PermissionRightsPK;
	α SelectAcl( IdentityPK identityPK, string resourceSlug )ε->jobject;
	α PurgeAcl( IdentityPK identityPK, PermissionRightsPK permissionPK, UserPK executer )ε->void;
	//reviews/m3-closing.md #16:  what the Resources page now sends for a filter on its Enforced column - the server applies it
	//like any other, "null" as `is null` in the bare-array form and "not null" as `ne`.  The page used to drop the arg.
	TEST_F( ResourceTests, TheEnforcedFilterIsApplied ){
		let root = GetRoot();
		let all = QL().QuerySync<jarray>( R"(resources( schemaName:"access", criteria:null ){ id deleted })", {}, root );
		uint enforced{}, unenforced{};
		for( let& r : all )
			++( Json::AsObject(r).at("deleted").is_null() ? enforced : unenforced );
		ASSERT_TRUE( enforced && unenforced ) << "the fixture needs both:  " << serialize( all );
		let isNull = QL().QuerySync<jarray>( R"(resources( schemaName:"access", criteria:null, deleted:$deleted ){ id deleted })", {{"deleted", jarray{nullptr}}}, root );
		EXPECT_EQ( isNull.size(), enforced );
		EXPECT_TRUE( std::ranges::all_of(isNull, [](let& r){ return Json::AsObject(r).at("deleted").is_null(); }) );
		let notNull = QL().QuerySync<jarray>( R"(resources( schemaName:"access", criteria:null, deleted:{"ne":null} ){ id deleted })", {}, root );
		EXPECT_EQ( notNull.size(), unenforced );
	}

	TEST_F( ResourceTests, SyncHealsARowLeftActiveByAnInterruptedInstall ){
		let root = GetRoot();
		const UserPK system{ UserPK::System };
		const string slug{ "providerTypes" }; //a synced table nothing here grants on - except root, whom GetRoot grants everything once.
		let rootGrant = SelectAcl( root, slug );
		ASSERT_FALSE( rootGrant.empty() );
		PurgeAcl( root, GetId(rootGrant), system ); //or the resource row cannot be purged.
		auto row = SelectResource( slug, root, true );
		ASSERT_FALSE( row.empty() );
		Purge( "resource", GetId(row), root ); //un-sync it - the next sync has to create it.

		//what the next start would enforce:  the loader (Loader::Resources) puts every active row into SchemaResources.  CreateResource
		//registers an active row the same way now (appserver-review3 #13) - it used to fill only Resources, so the lockout was a restart
		//away - but this looks through the loader rather than at Test(), as the finding is about what a start enforces.
		auto loadsActive = [&]()->optional<bool>{
			let loaded = BlockAwait<ResourceLoadAwait,ResourcePermissions>( ResourceLoadAwait{QLPtr(), Schemas(), {}, system} );
			auto p = find_if( loaded.Resources, [&](let& kv){ return kv.second.Slug==slug; } );
			return p==loaded.Resources.end() ? optional<bool>{} : optional<bool>{ !p->second.IsDeleted };
		};
		EXPECT_THROW( BlockVoidAwait( ResourceSyncAwait{ms<DeleteRefusingQL>(QLPtr()), Schemas(), {}, system} ), Exception ); //the create lands, the disable is refused.
		row = SelectResource( slug, root, true );
		ASSERT_FALSE( row.empty() );
		ASSERT_TRUE( row.at("deleted").is_null() ) << "left active - the interrupted install";
		EXPECT_EQ( loadsActive(), optional<bool>{true} ) << "and a restart would enforce it:  deny-all, nobody holds a right on it";

		BlockVoidAwait( ResourceSyncAwait{QLPtr(), Schemas(), {}, system} ); //the next start's sync.
		row = SelectResource( slug, root, true );
		EXPECT_FALSE( row.at("deleted").is_null() ) << "re-disabled by the sync";
		EXPECT_EQ( loadsActive(), optional<bool>{false} ) << "and loads disabled - fail-open, as the installation meant";
		CreateAcl( root, ERights::All, ERights::None, slug, system ); //as GetRoot left it.
	}

	//install-issues #25 (the no-checkboxes half):  a resource that exists only because a role referenced it - the hub's role seed
	//names opc.install nodeIds before the OpcServer connects and declares it - carries no `allowed`, so the permission table shows
	//no checkboxes.  The OpcServer's next resource sync fills the ops from the meta, and does not re-enable the unenforced row.
	TEST_F( ResourceTests, SyncFillsOpsOnAResourceCreatedBareByARoleReference ){
		let root = GetRoot();
		const UserPK system{ UserPK::System };
		const string slug{ "providerTypes" }; //a declared table; un-synced here so a role reference re-creates it bare.
		let bareAllowed = []( const jobject& o ){ auto p = o.if_contains("allowed"); return !p || p->is_null() || (p->is_array() && p->get_array().empty()); };
		if( auto grant = SelectAcl(root, slug); !grant.empty() )
			PurgeAcl( root, GetId(grant), system );
		if( auto row = SelectResource(slug, root, true); !row.empty() )
			Purge( "resource", GetId(row), root );

		//a role references it -> access_role_add creates it, and (criteria-null, install-issues #25) bare and unenforced.
		const RolePK rolePK{ (RolePK)GetId(Get("role", "roleSyncOpsFill", root)) };
		QL().QuerySync<jvalue>( Ƒ(R"(addRole( id:{}, permissionRight:{{ allowed:2, denied:0, resource:{{ schemaName:"access", slug:"{}" }} }} ))", rolePK, slug), {}, root );
		auto row = SelectResource( slug, root, true );
		ASSERT_FALSE( row.empty() );
		EXPECT_TRUE( bareAllowed(row) ) << "created bare - the permission table would show no checkboxes";
		EXPECT_FALSE( row.at("deleted").is_null() ) << "and unenforced";

		BlockVoidAwait( ResourceSyncAwait{QLPtr(), Schemas(), {}, system} );//the OpcServer's next start.
		row = SelectResource( slug, root, true );
		EXPECT_FALSE( bareAllowed(row) ) << "the sync filled the ops from the meta - the checkboxes are there now";
		EXPECT_FALSE( row.at("deleted").is_null() ) << "and left it unenforced - updateResource cannot touch deleted";

		//restore the shipped state:  drop the test role and resource, let the sync recreate it, and give root its grant back.
		Purge( "role", rolePK, root );
		if( auto r = SelectResource(slug, root, true); !r.empty() )
			Purge( "resource", GetId(r), root );
		BlockVoidAwait( ResourceSyncAwait{QLPtr(), Schemas(), {}, system} );
		CreateAcl( root, ERights::All, ERights::None, slug, system );
	}

	TEST_F( ResourceTests, CheckDefaults ){
		let ql = "resources( schemaName:\"access\", criteria:null ){ id allowed name attributes created deleted updated slug description }";
		let& resources = QL().QuerySync<jarray>( ql, {}, GetRoot() );
		ASSERT_EQ( resources.size(), 6 ); //"users", "members", "roles", "resources", "provider_types", "acl" (access-review3 #21)
		constexpr ERights base = ERights::Create | ERights::Read | ERights::Update | ERights::Delete | ERights::Purge | ERights::Administer;
		TRACET( ELogTags::Test, "base={:x}", underlying(base) );
		for( let& v : resources ){
			let& o = Json::AsObject( v );
			let slug = Json::AsSV( o, "slug" );
			auto allowed = ToRights( Json::AsArray(o, "allowed") );
			auto expected = base;
			//Subscribe on users/roles/resources/acl since 71e94e8a - the tables the SPA subscribes to; groups and providerTypes keep the defaults.
			if( slug=="users" )
				expected = base | ERights::Execute | ERights::Subscribe;
			else if( slug=="roles" )
				expected = base | ERights::Subscribe;
			else if( slug=="resources" )
				expected = ERights::Delete | ERights::Subscribe;
			else if( slug=="acl" )
				expected = ERights::Read | ERights::Administer | ERights::Subscribe;
			ASSERT_EQ( expected, allowed ) << "slug=" << slug;
		}
	}

	TEST_F( ResourceTests, Crud ){
		constexpr sv slug = "members";
		let userPK = GetId( GetUser("resourceTester", GetRoot()) );
		let filter = Ƒ( "userId:{{ eq: {} }}", userPK );
		auto resources = selectResources( slug, filter );
		if( !resources.size() ){
			let& userTable = *GetTable( "users" );
			let create = Ƒ( "mutation createResource( schemaName:\"access\", name:\"creator\", slug:\"{}\", criteria:\"{}\", rights:{} )", slug, filter, underlying(userTable.Operations) );
			let createJson = QL().QuerySync<jvalue>( create, {}, GetRoot() );
			resources = selectResources( slug, filter );
			ASSERT_EQ( resources.size(), 1 );
		}
		let id = GetId( Json::AsObject(resources[0]) );

		let update = Ƒ( "mutation updateResource( \"id\":{}, \"allowed\": [\"Read\"] )", id );
		let updateJson = QL().QuerySync<jvalue>( update, {}, GetRoot() );
		let rights = selectResources(slug, filter)[0].at("allowed").as_array();
		ASSERT_TRUE( rights.size()==1 );
		ASSERT_EQ( Json::AsSV(rights[0]), "Read" );

 		let del = Ƒ( "mutation deleteResource(\"id\":{})", id );
 		let deleteJson = QL().QuerySync<jvalue>( del, {}, GetRoot() );
		ASSERT_TRUE( selectResources(slug, filter).empty() );
		ASSERT_TRUE( !selectResources(slug, filter, true).empty() );

 		let restore = Ƒ( "mutation restoreResource(\"id\":{})", id );
 		let restoreJson = QL().QuerySync<jvalue>( restore, {}, GetRoot() );
		ASSERT_TRUE( !selectResources(slug, filter).empty() );

		//access-review3 #18:  the purge used to be built and never run, and the closing assertion was about GroupTests' group -
		//true in every ordering - so purgeResource had no coverage and the criteria-scoped `members` row leaked into the shared db.
		QL().QuerySync<jvalue>( Ƒ("mutation purgeResource( id:{} )", id), {}, GetRoot() );
		ASSERT_TRUE( selectResources(slug, filter, true).empty() ); //gone, deleted rows included.
	}

	//ql-review3 #48: the insert-side twin.  getEnumValue had its own flags loop that `continue`d past a non-string element, so
	//an all-numeric array - which no name lookup could ever match - wrote flags==0 with no error, and the resource was silently
	//unusable (allowed=0 hides it from the permission table).  Both paths share one parser now.
	TEST_F( ResourceTests, NumericFlagsAreAnErrorOnInsertToo ){
		constexpr sv schema{ "qlFlagTests" };
		constexpr sv slug{ "flagNumeric" };
		let select = Ƒ( R"(resources( schemaName:"{}", slug:"{}" ){{ id allowed }})", schema, slug );
		EXPECT_THROW( QL().QuerySync<jvalue>(Ƒ(R"(mutation createResource( schemaName:"{0}", name:"{1}", slug:"{1}", allowed:[1,2] ))", schema, slug), {}, GetRoot()), Exception );
		EXPECT_TRUE( QL().QuerySync<jarray>(select, {}, GetRoot()).empty() ) << "the row was created with allowed=0";

		//and the same array through the update path, which has always refused it - the two now refuse it in the same words.
		QL().QuerySync<jvalue>( Ƒ(R"(mutation createResource( schemaName:"{0}", name:"{1}", slug:"{1}", allowed:["Read"] ))", schema, slug), {}, GetRoot() );
		auto rows = QL().QuerySync<jarray>( select, {}, GetRoot() );
		ASSERT_EQ( rows.size(), 1u );
		let id = GetId( Json::AsObject(rows[0]) );
		EXPECT_THROW( QL().QuerySync<jvalue>(Ƒ(R"(mutation updateResource( id:{}, allowed:[1,2] ))", id), {}, GetRoot()), Exception );
		EXPECT_EQ( serialize(Json::AsObject(QL().QuerySync<jarray>(select, {}, GetRoot())[0]).at("allowed")), R"(["Read"])" );
		Purge( "resource", id, GetRoot() );
	}
	//the control:  a flags array of names still inserts, which is the shape everything in tree sends.
	TEST_F( ResourceTests, NamedFlagsStillInsert ){
		constexpr sv schema{ "qlFlagTests" };
		constexpr sv slug{ "flagNamed" };
		QL().QuerySync<jvalue>( Ƒ(R"(mutation createResource( schemaName:"{0}", name:"{1}", slug:"{1}", allowed:["Read","Update"] ))", schema, slug), {}, GetRoot() );
		let rows = QL().QuerySync<jarray>( Ƒ(R"(resources( schemaName:"{}", slug:"{}" ){{ id allowed }})", schema, slug), {}, GetRoot() );
		ASSERT_EQ( rows.size(), 1u );
		EXPECT_EQ( Json::AsArray(Json::AsObject(rows[0]), "allowed").size(), 2u );
		Purge( "resource", GetId(Json::AsObject(rows[0])), GetRoot() );
	}
	TEST_F( ResourceTests, UnknownFlagNameIsAnErrorNotAWipe ){
		constexpr sv schema{ "qlFlagTests" };
		constexpr sv slug{ "flagTypo" };
		let select = Ƒ( R"(resources( schemaName:"{}", slug:"{}" ){{ id allowed }})", schema, slug );
		if( QL().QuerySync<jarray>(select, {}, GetRoot()).empty() )
			QL().QuerySync<jvalue>( Ƒ(R"(mutation createResource( schemaName:"{0}", name:"{1}", slug:"{1}", allowed:["Read"] ))", schema, slug), {}, GetRoot() );
		auto rows = QL().QuerySync<jarray>( select, {}, GetRoot() );
		ASSERT_EQ( rows.size(), 1u );
		let id = GetId( Json::AsObject(rows[0]) );
		let before = serialize( Json::AsObject(rows[0]).at("allowed") );

		EXPECT_THROW( QL().QuerySync<jvalue>(Ƒ(R"(mutation updateResource( "id":{}, "allowed":["Reed"] ))", id), {}, GetRoot()), Exception );
		let after = QL().QuerySync<jarray>( select, {}, GetRoot() );
		ASSERT_EQ( after.size(), 1u );
		EXPECT_EQ( serialize(Json::AsObject(after[0]).at("allowed")), before ); //unchanged - the update never ran, rather than running with 0.
	}
}
