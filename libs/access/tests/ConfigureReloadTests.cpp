//app-review3 M3: reconnect restored delivery but not state.  Subscriptions::Replay re-issues the event subscriptions, but the
//snapshot loaders ran once at startup and AccessListener only ever applies deltas - so anything that changed while the socket was
//down stayed invisible.  ConfigureAwait's reload mode re-runs the loaders, and the two things that make that safe are what is
//pinned here: it must fill the authorizer from the server, and it must NOT subscribe (Replay already did, and a second
//subscription delivers every event twice).  Run against a throwaway Authorize and listener so the suite's own state is untouched;
//a reload reads only - ResourceSyncAwait, the one step that writes, is skipped.
#include <gtest/gtest.h>
#include <jde/access/AccessListener.h>
#include <jde/access/Authorize.h>
#include <jde/access/awaits/ConfigureAwait.h>
#include <jde/ql/LocalSubscriptions.h>
#include "globals.h"
#include "../src/awaits/ResourceLoadAwait.h"

#define let const auto

namespace Jde::Access::Tests{

	Ω reload( sp<Access::Authorize> authorizer, sp<Access::AccessListener> listener )ε->void{
		BlockVoidAwait( ConfigureAwait{QLPtr(), Schemas(), authorizer, UserPK{UserPK::System}, listener, {}, true} );
	}

	TEST( ConfigureReloadTests, ReloadLoadsTheSnapshotWithoutSubscribing ){
		let root = GetRoot();
		let missing = std::to_string( root.Value );//UserName falls back to the pk as a string when Users has no such row.
		auto authorizer = ms<Access::Authorize>( "access" );
		auto listener = ms<Access::AccessListener>( QLPtr() );
		ASSERT_EQ( authorizer->UserName(root), missing ) << "the throwaway authorizer started populated, so this proves nothing";

		reload( authorizer, listener );

		EXPECT_NE( authorizer->UserName(root), missing ) << "the reload did not re-read the identity snapshot";
		//StopListen returns what it removed; nothing to remove means the reload stopped at Acl instead of falling through to Subscribe.
		EXPECT_TRUE( QL::Subscriptions::StopListen(listener).empty() ) << "a reload subscribed a second time - every event would now arrive twice";
	}

	//access-refactor A1:  a reload is one swap.  The chain it replaced swapped Users for fresh, rightless User objects, then ran
	//five more queries before SetUserPermissions - and a live service refused every request in between.  Each loader query here
	//first asks the authorizer for a grant the old snapshot holds.
	struct ProbingQL final : ForwardingQL{
		ProbingQL( sp<QL::IQL> inner, function<void()> probe )ι:ForwardingQL{ move(inner) }, _probe{ move(probe) }{}
		α QueryArray( string query, jobject vars, UserPK executer, bool returnRaw, SL sl )ε->up<TAwait<jarray>> override{
			_probe();
			return ForwardingQL::QueryArray( move(query), move(vars), executer, returnRaw, sl );
		}
		function<void()> _probe;
	};
	TEST( ConfigureReloadTests, ReloadKeepsTheOldSnapshotUntilItSwaps ){
		let root = GetRoot();
		const string slug{ "providerTypes" };//a synced table nothing here grants on but root.
		const UserPK user{ GetId(GetUser("reload-window-grantee", root)) };
		let resource = SelectResource( slug, root, true );
		ASSERT_FALSE( resource.empty() );
		const ResourcePK resourcePK{ GetId(resource) };
		let wasDeleted = !resource.at("deleted").is_null();
		if( wasDeleted )
			Restore( "resources", resourcePK, root );//enforced, so the grant decides.
		let grant = CreateAcl( user, ERights::Read, ERights::None, slug, root );

		auto authorizer = ms<Access::Authorize>( "access" );
		auto listener = ms<Access::AccessListener>( QLPtr() );
		reload( authorizer, listener );
		ASSERT_EQ( authorizer->Rights("access", slug, user), ERights::Read );

		vector<ERights> seen;
		auto probing = ms<ProbingQL>( QLPtr(), [&]{ seen.push_back( authorizer->Rights("access", slug, user) ); } );
		BlockVoidAwait( ConfigureAwait{probing, Schemas(), authorizer, UserPK{UserPK::System}, listener, {}, true} );
		EXPECT_GE( seen.size(), 5u ) << "the probe did not see the loaders' queries";
		EXPECT_TRUE( std::ranges::all_of(seen, [](ERights r){ return r==ERights::Read; }) ) << "a query ran while the authorizer held a half-loaded snapshot";
		EXPECT_EQ( authorizer->Rights("access", slug, user), ERights::Read );

		QL::Subscriptions::StopListen( listener );
		PurgeAcl( user, grant, root );
		if( wasDeleted )
			Delete( "resources", resourcePK, root );
	}

	//The half of the reload that is not just "run the loaders again":  Loader::Resources used to `emplace` into maps it never cleared,
	//which keeps the incumbent - so a resource that changed, or was deleted, while the socket was down would have kept its stale entry
	//however many times the loaders ran.
	TEST( ConfigureReloadTests, ReloadReplacesTheResourceMapsRatherThanMergingIntoThem ){
		auto authorizer = ms<Access::Authorize>( "access" );
		auto listener = ms<Access::AccessListener>( QLPtr() );
		reload( authorizer, listener );

		authorizer->AddResource( ResourcePK{0xFFFF}, "reload-test", "stale", "" );//stands in for a resource the server no longer returns.
		ASSERT_TRUE( authorizer->FindActiveResourcePK("reload-test", "stale", "") );

		reload( authorizer, listener );
		EXPECT_FALSE( authorizer->FindActiveResourcePK("reload-test", "stale", "") ) << "the reload merged into the resource maps, so a stale entry survived it";
		QL::Subscriptions::StopListen( listener );
	}

	//access-enforce-toggle #1:  the rights load selected the nested resource by id alone, and a select naming no `deleted` column is
	//"active rows only" - so every grant on an unenforced resource was dropped at load.  Harmless until the Enforced toggle restored
	//the row live:  enforcement then ran against a map that never held it, and every identity was denied until a restart.  The
	//regression the review asked for, without OPC:  a soft-deleted resource with a direct grant, loaded, restored through the
	//listener's path, still resolves the grant.
	struct RestoringAuthorize final : Access::Authorize{ using Authorize::Authorize; using Authorize::UpdateResourceDeleted; };//the listener's protected entry, driven by the test.
	TEST( ConfigureReloadTests, LoadsRightsOnAnUnenforcedResource ){
		let root = GetRoot();
		const UserPK system{ UserPK::System };
		const string slug{ "providerTypes" };//a synced table nothing here grants on but root.
		const UserPK user{ GetId(GetUser("unenforced-grantee", root)) };
		let resource = SelectResource( slug, root, true );
		ASSERT_FALSE( resource.empty() );
		const ResourcePK resourcePK{ GetId(resource) };
		let wasActive = resource.at("deleted").is_null();
		let grant = CreateAcl( user, ERights::Read, ERights::None, slug, root );
		if( wasActive )
			Delete( "resources", resourcePK, root );//the shipped state:  every row unenforced.

		let loaded = BlockAwait<ResourceLoadAwait,ResourcePermissions>( ResourceLoadAwait{QLPtr(), Schemas(), {}, system} );
		EXPECT_TRUE( std::ranges::any_of(loaded.Permissions, [&](let& kv){ return kv.second.ResourcePK==resourcePK; }) ) << "the load dropped the rights on the unenforced resource";

		auto authorizer = ms<RestoringAuthorize>( "access" );
		auto listener = ms<Access::AccessListener>( QLPtr() );
		reload( authorizer, listener );
		authorizer->UpdateResourceDeleted( resourcePK, "access", {}, true );//what AccessListener applies on restoreResource - the Enforced toggle.
		EXPECT_EQ( authorizer->Rights("access", slug, user), ERights::Read ) << "enforced live against rights that were never loaded";
		EXPECT_NO_THROW( authorizer->Test("access", slug, ERights::Read, user) );

		QL::Subscriptions::StopListen( listener );
		PurgeAcl( user, grant, root );
		if( wasActive )
			Restore( "resources", resourcePK, root );
	}
}
#undef let
