#include <jde/access/Authorize.h>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/ql/ql.h>
#include <jde/ql/IQL.h>
#include <jde/web/server/Sessions.h>
#include "../../AppServer/src/LocalClient.h"
#include "../../OpcGateway/src/GatewayAppClient.h"
#include "helpers.h"
#define let const auto

//The in-process app client (src/HubAppClient.h): every socket round trip the gateway makes to an AppServer answered here.
namespace Jde::Opc::Hub::Tests{
	using Web::FromServer::SessionInfo;

	struct HubClientTests : ::testing::Test{
		//`slug`'s provider, created anew:  one an earlier run left behind is purged first.
		Ω FreshProvider( str slug )ε->Access::ProviderPK{
			if( let existing = FindProvider(slug); existing )
				PurgeProvider( existing );
			let j = Gateway::AppClient()->QuerySync<jobject>( Ƒ("createProvider( slug:\"{}\", providerType:\"OpcServer\" ){{id}}", slug), {} );
			return QL::AsId<Access::ProviderPK>( j );
		}
		Ω FindProvider( str slug )ε->Access::ProviderPK{
			let j = Gateway::AppClient()->QuerySync<jobject>( "provider(name:$opcSlug){ id }", {{"opcSlug", slug}} );
			return Json::FindNumber<Access::ProviderPK>( j, "id" ).value_or( 0 );
		}
		Ω PurgeProvider( Access::ProviderPK pk )ε->void{
			Gateway::AppClient()->QuerySync<jvalue>( "purgeProvider( id:$id )", {{"id", pk}} );
		}
	};

	//the seam: the gateway's client is local, shares the AppServer's QL, Authorize, registration pks and signing key.
	TEST_F( HubClientTests, Seam ){
		auto client = Gateway::AppClient();
		auto appServer = App::Server::AppClient();
		EXPECT_TRUE( client->IsLocal() );
		EXPECT_TRUE( client->Connected() );
		EXPECT_TRUE( client->UserPK().Value==Jde::UserPK::System );//==, not EXPECT_EQ: gtest takes its operands by reference, which odr-uses the constexpr member.
		EXPECT_EQ( client->QLServer().get(), static_cast<QL::IQL*>(App::Server::QLPtr().get()) );
		EXPECT_EQ( client->Acl().get(), App::Server::Authorizer().get() );
		EXPECT_NE( client->InstancePK(), 0u );
		EXPECT_EQ( client->InstancePK(), appServer->InstancePK() );
		EXPECT_EQ( client->ConnectionPK(), appServer->ConnectionPK() );
		EXPECT_TRUE( client->PublicKey()==appServer->PublicKey() );
	}

	//The hub's own certificate is a user, found as an OPC server's certificate login finds one (UAAccess):  the collector presents it.
	TEST_F( HubClientTests, OwnCertificateIsAUser ){
		auto client = Gateway::AppClient();
		ASSERT_TRUE( client->SslSettings );
		let key = Crypto::ExtractPublicKey( Crypto::ReadCertificate(client->SslSettings->Certificate.Path) );
		let user = client->QuerySync( Ƒ("user( modulus: \"{}\", exponent: {}, deleted: null ){{id}}", key.ModulusHex(), key.ExponentInt()), {} );
		ASSERT_FALSE( user.empty() );
		EXPECT_TRUE( QL::AsId<Jde::UserPK::Type>(user)!=Jde::UserPK::System );
	}

	//GraphQL to the app db (the provider lookups the OPC login path makes) through the local QL, as System.
	TEST_F( HubClientTests, ProviderRoundTrip ){
		let slug = "hubTestsProvider";
		let pk = FreshProvider( slug );
		ASSERT_NE( pk, 0u );
		EXPECT_EQ( FindProvider(slug), pk );
		PurgeProvider( pk );
		EXPECT_EQ( FindProvider(slug), 0u );
	}

	//OPC user/password login's AddSession: authenticate + mint a web session in-process (the kAddSession round trip).
	TEST_F( HubClientTests, AddSession ){
		let slug = "hubTestsAddSession";
		let providerPK = FreshProvider( slug );
		ASSERT_NE( providerPK, 0u );
		auto await = Gateway::AppClient()->AddSession( slug, "hubTestsUser", providerPK, string{Host}, false );
		let info = BlockAwait<TAwait<SessionInfo>,SessionInfo>( move(*await) );
		EXPECT_NE( info.session_id(), 0u );
		EXPECT_NE( info.user_pk(), 0u );
		auto session = Web::Server::Sessions::Find( info.session_id() );
		ASSERT_TRUE( session );
		EXPECT_EQ( session->UserPK.Value, info.user_pk() );
		EXPECT_EQ( session->UserEndpoint, Host );
		Web::Server::Sessions::Remove( info.session_id() );
		PurgeProvider( providerPK );
	}
}