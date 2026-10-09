//review3 #9:  nothing on the subscribe path authorized anything.  Subscriptions::Listen took no user, SubscribeQueryAwait's
//_executer had no reader, and the websocket path called Listen directly - so a subscription delivered rows (mutation args,
//trimmed to the subscriber's fields) the same client's query would have been refused.  Listen now has a gated overload that
//authorizes every subscribed table, and LocalQL::Subscribe routes through it.  The acl here is a stub: Jde.QL.Tests links no
//authorizer, and IAcl is a pure-virtual header, so the schema carries this instead.
#include <gtest/gtest.h>
#include <jde/access/IAcl.h>
#include <jde/ql/LocalQL.h>
#include <jde/ql/LocalSubscriptions.h>
#include "NullQL.h"
#include "RecordingListener.h"
#include "UnitSchema.h"

#define let const auto

namespace Jde::QL::Tests{
	//refuses user 0 the way Authorize::Test does for an unknown user against an active resource; anyone else passes.
	struct AclStub final : Access::IAcl{
		α Test( str, str resourceName, Access::ERights rights, UserPK executer, SL sl )ε->void override{
			Tested.emplace_back( string{resourceName}, rights );
			if( !executer.Value )
				throw Exception{ sl, {ELogTags::Access}, "[{}]User not found.", executer.Value };
		}
		α TestSystem( str, str resourceName, Access::ERights rights, UserPK executer, SL sl )ε->void override{//as Authorize::TestSystem: the system alone.
			Tested.emplace_back( string{resourceName}, rights );
			if( executer.Value!=UserPK::System )
				throw Exception{ sl, {ELogTags::Access}, "[{}]User does not have '{}' access to '{}'.", executer.Value, Access::ToString(rights), resourceName };
		}
		α Rights( str, str, UserPK )ι->Access::ERights override{ return Access::ERights::All; }
		α UserName( UserPK )ι->string override{ return {}; }
		vector<std::pair<string,Access::ERights>> Tested;
	};
	//LocalQL leaves the custom/log/status hooks pure; nothing here calls them.
	struct TestQL final : LocalQL{
		TestQL( vector<sp<DB::AppSchema>> schemas )ι:LocalQL{ move(schemas), {} }{}
		α CustomQuery( TableQL&, Creds, SL )ι->up<TAwait<jvalue>> override{ return nullptr; }
		α CustomMutation( MutationQL&, Creds, SL )ι->up<TAwait<jvalue>> override{ return nullptr; }
		α LogQuery( TableQL&&, Creds, SL )ε->up<TAwait<jvalue>> override{ return nullptr; }
		α LogSettingsQuery( TableQL&&, Creds, SL )ε->up<TAwait<jvalue>> override{ return nullptr; }
		α StatusQuery( TableQL&&, Creds, SL )ε->jobject override{ return {}; }
	};

	struct SubscribeAuthorizeTests : ::testing::Test{
		α SetUp()->void override{
			Acl = ms<AclStub>();
			auto schema = schemas();
			schema[0]->Authorizer = Acl;
			Ql = ms<TestQL>( schema );
			Listener = ms<RecordingListener>( "SubscribeAuthorizeTests" );
		}
		α TearDown()->void override{ Subscriptions::StopListen( Listener ); }
		α subscribe( sv text, UserPK executer )ε->vector<SubscriptionId>{
			auto await = Ql->Subscribe( string{text}, {}, Listener, executer );
			return BlockTAwait<vector<SubscriptionId>>( move(*await) );
		}
		sp<AclStub> Acl;
		sp<TestQL> Ql;
		sp<RecordingListener> Listener;
	};

	//the finding's shape:  the un-logged-in socket's `userCreated` equivalent.
	TEST_F( SubscribeAuthorizeTests, AnonymousSubscribeIsRefused ){
		try{
			subscribe( "subscription ProviderCreated{ providerCreated(subscriptionId:1){ id name } }", UserPK{} );
			FAIL() << "expected a throw";
		}
		catch( const Exception& e ){
			EXPECT_NE( string{e.what()}.find("User not found"), string::npos ) << e.what();
		}
		ASSERT_EQ( Acl->Tested.size(), 1u );
		EXPECT_EQ( Acl->Tested[0].first, "providers" );
		EXPECT_EQ( Acl->Tested[0].second, Access::ERights::Read ); //the same right the query would have taken.
		EXPECT_TRUE( Subscriptions::StopListen(Listener).empty() ); //nothing was registered.
	}
	TEST_F( SubscribeAuthorizeTests, AuthenticatedSubscribeIsRegistered ){
		let ids = subscribe( "subscription ProviderCreated{ providerCreated(subscriptionId:1){ id name } }", UserPK{7} );
		ASSERT_EQ( ids.size(), 1u );
		EXPECT_EQ( Subscriptions::StopListen(Listener).size(), 1u ); //registered, and only now removed.
	}
	//a sub-table is a read of its own table, so it is authorized too - the fan-out delivers those columns as well.
	TEST_F( SubscribeAuthorizeTests, NestedTableIsAuthorized ){
		subscribe( "subscription ProviderCreated{ providerCreated(subscriptionId:1){ id providerTypes{ id name } } }", UserPK{7} );
		ASSERT_EQ( Acl->Tested.size(), 2u );
		EXPECT_EQ( Acl->Tested[0].first, "providers" );
		EXPECT_EQ( Acl->Tested[1].first, "providerTypes" ); //Table::Authorize names the resource in json - see #10 for which name that ought to be.
	}
	//review3 #30: IQL::Unsubscribe built `unsubscribe( id:[…] )` and sent it round the QL text path - a spelling
	//Parser::LoadUnsubscriptions rejects outright, and which QLAwait answers "Unsubscribe is not supported in this context."
	//even in the brace form.  It is virtual and direct now, so the registration it is meant to drop actually goes.
	TEST_F( SubscribeAuthorizeTests, UnsubscribeDropsTheRegistration ){
		let ids = subscribe( "subscription ProviderCreated{ providerCreated(subscriptionId:1){ id name } }", UserPK{7} );
		ASSERT_EQ( ids.size(), 1u );
		Ql->Unsubscribe( Listener, ids );
		EXPECT_TRUE( Subscriptions::StopListen(Listener).empty() ) << "the subscription was still registered";
	}
	//and an id the listener does not hold leaves its other subscriptions alone.
	TEST_F( SubscribeAuthorizeTests, UnsubscribeOfAnUnknownIdKeepsTheRest ){
		let ids = subscribe( "subscription ProviderCreated{ providerCreated(subscriptionId:1){ id name } }", UserPK{7} );
		ASSERT_EQ( ids.size(), 1u );
		Ql->Unsubscribe( Listener, {SubscriptionId{999999}} );
		EXPECT_EQ( Subscriptions::StopListen(Listener).size(), 1u );
	}

	//the ungated overload stays for in-process registration (the AccessListener, System) - it must not have grown a check.
	TEST_F( SubscribeAuthorizeTests, UngatedListenStillRegistersWithoutAUser ){
		auto subs = QL::ParseSubscriptions( "subscription ProviderCreated{ providerCreated(subscriptionId:1){ id name } }", {}, Ql->Schemas() );
		Subscriptions::Listen( Listener, move(subs) );
		EXPECT_TRUE( Acl->Tested.empty() );
		EXPECT_EQ( Subscriptions::StopListen(Listener).size(), 1u );
	}
}
