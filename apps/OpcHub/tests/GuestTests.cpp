#include <absl/cleanup/cleanup.h>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/tests/AccessHelpers.h>
#include <jde/tests/HttpTest.h>
#include <jde/web/server/Sessions.h>
#include "../../AppServer/src/LocalClient.h"
#include "../../OpcGateway/src/GatewayAppClient.h"
#include "../../OpcGateway/tests/utils/GatewayClientSocket.h"
#include "../../OpcGateway/tests/utils/helpers.h"
#include "helpers.h"
#define let const auto

//#483, the guest in the hub:  /access/guest, `hubGuest` here, a user the test creates and purges.  A session minted for a caller with
//no Authorization is the guest's, and /opc binds to it as to any session:  with gateway/serverConnections enforced, a subscribe
//takes the guest's Read, as every web session's does (ConnectAwait), and is refused without it.
namespace Jde::Opc::Hub::Tests{
	using Gateway::Tests::GatewayClientSocket; using Gateway::Tests::GatewayPort; using Gateway::Tests::OpcServerSlug; using Gateway::Tests::Intensity;
	namespace http = boost::beast::http;
	struct GuestTests : HttpTest{
		struct Listener final : Gateway::Tests::IListener{ α OnData( string, NodeId, const Opc::Proto::Value& )ι->void override{} };
	};

	TEST_F( GuestTests, OpcAsTheGuest ){
		let slug = Settings::FindString( "/access/guest" ).value_or( "" );
		ASSERT_FALSE( slug.empty() ) << "the suite's config names the guest";
		auto app = App::Server::AppClient();
		auto purge = [&]{
			for( let& r : app->QuerySync<jarray>("users( slug:$slug ){ id }", {{"slug", slug}}) )
				app->QuerySync<jvalue>( "purgeUser( id:$id )", {{"id", Json::AsNumber<uint>(r.as_object(), "id")}} );
		};
		purge();
		const Jde::UserPK guest{ Json::AsNumber<Jde::UserPK::Type>( app->QuerySync<jobject>("createUser( slug:$slug, name:$slug ){ id }", {{"slug", slug}}), "id" ) };
		absl::Cleanup purged{ [&]{ purge(); EXPECT_FALSE( Web::Server::Sessions::Guest() ) << "the purge cleared the guest"; } };
		ASSERT_EQ( Web::Server::Sessions::Guest().Value, guest.Value ) << "the listener's userCreated sets the guest";

		let res = Get( AppPort(), "/opcGateways" );
		let id = Str::TryTo<SessionPK,16>( string{res.Headers()[http::field::authorization]} );
		ASSERT_TRUE( id ) << "a caller with no Authorization was told no session";
		absl::Cleanup remove{ [&]{ Web::Server::Sessions::Remove( *id ); } };
		ASSERT_TRUE( Web::Server::Sessions::Find(*id) );
		EXPECT_EQ( Web::Server::Sessions::Find(*id)->UserPK.Value, guest.Value );

		Gateway::Tests::GetConnection( OpcServerSlug );//the embedded server's row, as the gateway's suites make it.
		Gateway::Tests::EnforceResource( "gateway", "serverConnections", true );
		absl::Cleanup open{ []{ Gateway::Tests::EnforceResource( "gateway", "serverConnections", false ); } };
		auto socket = GatewayClientSocket::Open( string{Host}, GatewayPort(), *id );
		auto listener = ms<Listener>();
		auto subscribe = [&]{ return BlockAwait<Web::Client::ClientSocketAwait<Gateway::FromServer::SubscriptionAck>,Gateway::FromServer::SubscriptionAck>( socket->Subscribe(OpcServerSlug, {Intensity}, listener) ); };
		try{
			subscribe();
			ADD_FAILURE() << "a guest with no grant opened the connection";
		}
		catch( const Gateway::Tests::GatewayErrorResponse& e ){
			EXPECT_NE( string{e.what()}.find("'Read' access"), string::npos ) << e.what();
		}
		GrantRole( *Gateway::AppClient(), "hubGuestViewer", "gateway", "serverConnections", {}, Access::ERights::Read, guest );//the demo's visitor role (spec *Guests*)
		let ack = subscribe();
		EXPECT_EQ( ack.results_size(), 1 ) << "with Read the guest's subscribe reaches the server";
	}
}