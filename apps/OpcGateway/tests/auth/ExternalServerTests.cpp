#include <jde/fwk/io/json.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/opc/uatypes/opcHelpers.h>
#include "../utils/helpers.h"
#include "../../src/UAClient.h"
#include "../../src/GatewayAppClient.h"
#include "../../src/auth/OpcServerSession.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };
	//A third-party server's cells of reviews/security-matrix.md, on demand:  /testing/external names the server and the fixture
	//is skipped without one, so the suite runs in CI against the embedded server alone.  What each cell answers is the server's
	//configuration as much as the gateway's - which token policies it puts on its None endpoint, whether it trusts our
	//certificate yet - so a test asserts only what /testing/external/expect declares for its cell and otherwise records the
	//outcome, MATRIX-prefixed, for the doc.  Five cells:  {None, Basic256Sha256} x {anonymous, username}, and a connection with no
	//certificateUri at a url that has no unsecured endpoint.  A None connection still presents its credential encrypted
	//(UAClient::Configuration), so noneUsername succeeds wherever the server's None endpoint takes usernames under Basic256Sha256.
	//	testing:{ external:{
	//		url: "opc.tcp://192.168.84.130:49320", //Kepware publishes its None endpoint under its hostname/addresses only, not 127.0.0.1
	//		nameUrl: "opc.tcp://JDE-CPP:49320", //the same server by a name whose first addresses are dead - IPv6 link-local, where Kepware listens on IPv4 alone (security-matrix #10); absent: that cell is skipped
	//		certificateUri: "urn:JDE-CPP:Kepware.KEPServerEX.V6:UA Server", //the server's applicationUri, raw - the Basic256Sha256 rows' certificateUri; absent: those cells are skipped
	//		user: "user1", password: "…", //absent: the username cells are skipped
	//		secureOnlyUrl: "opc.tcp://127.0.0.1:49320", //a url of the same server with no None endpoint - Kepware's loopback; absent: that cell is skipped
	//		expect: { noneUsername: "ok", noneAnonymous: "BadIdentityTokenRejected" } //optional, per cell:  "ok", or text the failure must contain
	//	} }
	//The secured cells take the two-way trust the doc describes:  the server's certificate under /gateway/trustedCertDirs, and
	//the gateway's issued certificate (<product>/ssl/certs/<instance>.opcTestsExternalSecure.pem) trusted by the server.
	str ExternalNoneSlug{ "opcTestsExternalNone" };
	str ExternalSecureSlug{ "opcTestsExternalSecure" };
	str ExternalSecureOnlySlug{ "opcTestsExternalSecureOnly" };
	str ExternalByNameSlug{ "opcTestsExternalByName" };
	class ExternalServerTests : public ::testing::Test{
	protected:
		α SetUp()ε->void override{
			let external = Settings::FindObject( "/testing/external" );
			if( !external )
				GTEST_SKIP() << "/testing/external names no server.";
			_url = Json::FindDefaultSV( *external, "url" );
			_certificateUri = Json::FindDefaultSV( *external, "certificateUri" );
			_user = Json::FindDefaultSV( *external, "user" );
			_password = Json::FindDefaultSV( *external, "password" );
			_secureOnlyUrl = Json::FindDefaultSV( *external, "secureOnlyUrl" );
			_nameUrl = Json::FindDefaultSV( *external, "nameUrl" );
			_expect = Json::FindDefaultObject( *external, "expect" );
		}
		α TearDown()ι->void override{
			if( _client )
				UAClient::RemoveClient( move(_client) );
		}
		Ω TearDownTestSuite()->void{
			for( let& slug : {ExternalNoneSlug, ExternalSecureSlug, ExternalSecureOnlySlug, ExternalByNameSlug} ){
				if( auto row = SelectServerCnnctn(slug); row )
					PurgeServerCnnctn( row->Id );
			}
		}
		α Connect( str slug, Credential cred )ι->ConnectAwait::Task;
		α Cell( sv name, str slug, str certificateUri, Credential cred, optional<string> cellUrl={} )ε->void;
		string _url, _certificateUri, _user, _password, _secureOnlyUrl, _nameUrl;
		jobject _expect;
		up<Exception> _exception;
		sp<UAClient> _client;
		atomic_flag _done;
	};

	α ExternalServerTests::Connect( str slug, Credential cred )ι->ConnectAwait::Task{
		try{
			_client = co_await UAClient::GetClient( slug, move(cred) );
		}
		catch( Exception& e ){
			_exception = e.Move();
		}
		_done.test_and_set();
		_done.notify_all();
	}
	α ExternalServerTests::Cell( sv name, str slug, str certificateUri, Credential cred, optional<string> cellUrl )ε->void{
		let& url = cellUrl ? *cellUrl : _url;
		GetConnection( slug, url, certificateUri );//select or create - the rows outlive the test and go with the suite.
		let type = TokenTypeName( cred.Type() );
		_exception = nullptr; _done.clear();
		Connect( slug, move(cred) );
		_done.wait( false );
		let outcome = _exception ? string{_exception->what()} : Negotiated( _client );
		INFO( "MATRIX|{}|{}|{}|{}|{}", name, certificateUri.empty() ? "None" : "Basic256Sha256", type, url, outcome );
		if( let expect = Json::FindString(_expect, name); expect ){
			if( *expect=="ok" )
				EXPECT_FALSE( _exception ) << outcome;
			else
				EXPECT_TRUE( _exception && outcome.contains(*expect) ) << outcome;
		}
	}

	TEST_F( ExternalServerTests, NoneAnonymous ){
		Cell( "noneAnonymous", ExternalNoneSlug, "", Credential{} );
	}
	TEST_F( ExternalServerTests, NoneUsername ){
		if( _user.empty() )
			GTEST_SKIP() << "/testing/external/user names no login.";
		Cell( "noneUsername", ExternalNoneSlug, "", Credential{User{_user, _password}} );
	}
	TEST_F( ExternalServerTests, NoneAtAUrlWithNoUnsecuredEndpoint ){
		if( _secureOnlyUrl.empty() || _user.empty() )
			GTEST_SKIP() << "/testing/external needs secureOnlyUrl and user.";
		Cell( "noneAtSecureOnlyUrl", ExternalSecureOnlySlug, "", Credential{User{_user, _password}}, _secureOnlyUrl );
	}
	TEST_F( ExternalServerTests, NoneUsernameByName ){//no certificateUri:  the unsecured channel needs no trust on the server's side, so the cell is about the name alone.
		if( _nameUrl.empty() || _user.empty() )
			GTEST_SKIP() << "/testing/external needs nameUrl and user.";
		Cell( "noneUsernameByName", ExternalByNameSlug, "", Credential{User{_user, _password}}, _nameUrl );
	}
	TEST_F( ExternalServerTests, SecureAnonymous ){
		if( _certificateUri.empty() )
			GTEST_SKIP() << "/testing/external/certificateUri names no server uri.";
		Cell( "secureAnonymous", ExternalSecureSlug, _certificateUri, Credential{} );
	}
	TEST_F( ExternalServerTests, SecureUsername ){
		if( _certificateUri.empty() || _user.empty() )
			GTEST_SKIP() << "/testing/external needs certificateUri and user.";
		Cell( "secureUsername", ExternalSecureSlug, _certificateUri, Credential{User{_user, _password}} );
	}
}
