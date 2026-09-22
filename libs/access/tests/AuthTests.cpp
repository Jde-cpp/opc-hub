#include "globals.h"
#include <jde/ql/QLAwait.h>
#include <jde/access/server/awaits/AuthenticateAwait.h>

#define let const auto
namespace Jde::Access::Tests{
	class AuthTests : public ::testing::Test{
	protected:
		Ω SetUpTestCase()->void;

		constexpr static sv OpcServer{"AuthTests::OpcServer1"};
		static ProviderPK OpcProviderId;
	};
	ProviderPK AuthTests::OpcProviderId{};

	α AuthTests::SetUpTestCase()ε->void{
		if( auto o = QL().QuerySync(Ƒ("provider(name:\"{}\"){{id}}", OpcServer), {}, GetRoot()); !o.empty() )
			OpcProviderId = GetId( o );
		else{
			let createQL = Ƒ( "createProvider( slug:\"{}\", providerType:{} ){{id}}", OpcServer, underlying(EProviderType::OpcServer) );
			OpcProviderId = GetId( QL().QuerySync(createQL, {}, GetRoot()) );
		}
	}

	α login( str loginName, ProviderPK providerId, string opcServer )ε->UserPK{
		return BlockTAwait<UserPK>( Server::AuthenticateAwait{loginName, providerId, opcServer} );
	}

	TEST_F( AuthTests, Login_Existing ){
		const string user{ "Login_Existing-google" };
		let provider = Access::EProviderType::Google;
		let fetchedUserPK{ GetId( GetUser(user, GetRoot(), true, (ProviderPK)provider) ) };
		let loginUserPK = login( user, underlying(provider), {} );
		ASSERT_EQ( fetchedUserPK, loginUserPK.Value );
		PurgeUser( {fetchedUserPK}, GetRoot() );
	}

	//reviews/m3-closing.md #15:  a user an admin adds ahead of their first sign-in is the one that sign-in finds - but only
	//when the create carries what the lookup keys on.  The SPA's create is now this string (web/access/.../user.spec.ts pins
	//it); it used to be `createUser( slug, name )` alone, and the sign-in made a second identity.
	TEST_F( AuthTests, APreCreatedUserIsTheOneASignInFinds ){
		const string login{ "precreated@plant.com" };
		let google = underlying( Access::EProviderType::Google );
		let q = Ƒ( R"(mutation createUser( slug:"precreated", name:"Pre Created", providerId:{}, loginName:"{}", email:"{}" ){{id}})", google, login, login );
		const UserPK created{ GetId(QL().QuerySync(q, {}, GetRoot())) };
		let row = QL().QuerySync( Ƒ(R"(user( id:{} ){{ email loginName provider }})", created.Value), {}, GetRoot() );
		EXPECT_EQ( Json::FindSV(row, "email").value_or(""), login ) << serialize( row );
		EXPECT_EQ( Tests::login(login, google, {}), created ) << "the first sign-in bound to the pre-created identity";
		PurgeUser( created, GetRoot() );

		const string oldLogin{ "precreated-old@plant.com" };//the shape the SPA used to send
		const UserPK bare{ GetId(QL().QuerySync(R"(mutation createUser( slug:"precreatedOld", name:"Pre Created Old" ){id})", {}, GetRoot())) };
		let second = Tests::login( oldLogin, google, {} );
		EXPECT_NE( second, bare ) << "without provider and login name the sign-in cannot find it - it makes a second identity";
		PurgeUser( second, GetRoot() );
		PurgeUser( bare, GetRoot() );
	}

	TEST_F( AuthTests, Login_New ){
		const string user{ "Login_New" };
		let userId = login( user, underlying(Access::EProviderType::Google), {} );
		PurgeUser( {userId}, GetRoot() );
	}

	TEST_F( AuthTests, Login_Existing_Opc ){
		const string user{ "Login_Existing_Opc" };
		let fetchedUserPK = GetId( GetUser(user, GetRoot(), true, OpcProviderId) );
		let loginUserPK = login( user, OpcProviderId, string{OpcServer} );
		ASSERT_EQ( fetchedUserPK, loginUserPK.Value );
		PurgeUser( {fetchedUserPK}, GetRoot() );
	}

	TEST_F( AuthTests, Login_New_Opc ){
		const string user{ "Login_New_Opc" };
		let userId = login( user, OpcProviderId, string{OpcServer} );
		PurgeUser( {userId}, GetRoot() );
	}
}