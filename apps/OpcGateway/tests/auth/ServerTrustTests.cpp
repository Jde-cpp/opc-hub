//Gateway→OPC-server certificate verification (libs/opc ServerTrust.cpp):  the verifier open62541 consults in initSecurityPolicy
//must accept a server certificate that is under a trusted directory, reject one that is not with a reason that names the
//server and the fix, and accept anything when /gateway/verifyServerCertificate is off.  No connection:  the group is
//installed on a bare UA_ClientConfig and its verifyCertificate called directly, on two certificates the harness already
//issued - the gateway's app certificate and the per-slug issued certificate (tests/main.cpp EnsureCertificate).
#include <open62541/client_config_default.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/fwk/io/file.h>
#include <jde/opc/uatypes/Logger.h>
#include <jde/opc/ServerTrust.h>
#include "../../src/UAClient.h"
#include "../../src/GatewayAppClient.h"
#include "../utils/helpers.h"
#include "Auth.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	struct RestoreTrustedCertDirs final{//puts /gateway/trustedCertDirs back however the test leaves - every later connect in the process reads it.
		RestoreTrustedCertDirs(){ for( let& dir : Settings::FindStringArray("/gateway/trustedCertDirs") ) Dirs.emplace_back( dir ); }
		~RestoreTrustedCertDirs(){ try{ Settings::Set("/gateway/trustedCertDirs", Dirs); }catch( const std::exception& ){} }
		jarray Dirs;
	};
	struct ServerTrustTests : ::testing::Test{
		static constexpr Jde::Handle TestHandle{ 0x5e77 };
		Logger _logger{ TestHandle };
		UA_ClientConfig _config{};
		fs::path _trustedDir{ fs::temp_directory_path()/Ƒ("jde-servertrust-{}", Process::ProcessId()) };
		fs::path _trusted, _other;

		α SetUp()->void override{
			_config.logging = &_logger;
			_trusted = AppClient()->SslSettings->Certificate.Path;
			_other = UAClient::CryptoSettings( ServerCnnctnNK{OpcServerSlug} ).Certificate.Path;
			ASSERT_TRUE( fs::exists(_trusted) ) << _trusted;
			ASSERT_TRUE( fs::exists(_other) ) << _other;
			ASSERT_NE( Crypto::ReadCertificate(_trusted), Crypto::ReadCertificate(_other) );
			fs::create_directories( _trustedDir );
			fs::copy_file( _trusted, _trustedDir/"trusted.pem", fs::copy_options::overwrite_existing );//only one of the two is anchored.
		}
		α TearDown()->void override{
			UA_ClientConfig_clear( &_config );//runs the group's clear - frees the context.
			std::error_code ec; fs::remove_all( _trustedDir, ec );
		}
		α Verify( const fs::path& pem )->UA_StatusCode{
			let der = Crypto::ReadCertificate( pem );
			UA_ByteString bs{ der.size(), (UA_Byte*)der.data() };
			return _config.certificateVerification.verifyCertificate( &_config.certificateVerification, &bs );
		}
	};

	TEST_F( ServerTrustTests, TrustedDirAcceptsUntrustedRejects ){
		ServerTrust::Install( _config, true, {_trustedDir}, TestHandle, "opc.tcp://server.under.test:4840" );
		ASSERT_EQ( ServerTrust::AnchorCount(_config), 1u );
		EXPECT_EQ( Verify(_trusted), UA_STATUSCODE_GOOD );
		EXPECT_EQ( ServerTrust::Rejection(_config), "" );

		EXPECT_EQ( Verify(_other), UA_STATUSCODE_BADCERTIFICATEUNTRUSTED );
		let rejection = ServerTrust::Rejection( _config );
		EXPECT_NE( rejection.find("opc.tcp://server.under.test:4840"), string::npos ) << rejection;//names the server...
		EXPECT_NE( rejection.find("/gateway/verifyServerCertificate"), string::npos ) << rejection;//...and the switch.

		EXPECT_EQ( Verify(_trusted), UA_STATUSCODE_GOOD );
		EXPECT_EQ( ServerTrust::Rejection(_config), "" );//a later success clears the last rejection.
	}

	//install-issues #33:  an OPC UA server publishes its instance certificate as DER - Kepware's kepserverex_ua_server.der -
	//and a UA trust list is a directory of .der by convention.  The operator step the Gateways help describes, done with the
	//file the server actually writes, used to change nothing:  the scan took .pem/.crt only, skipped the rest without a word,
	//and the refusal then said "0 trusted certificates loaded", which reads as an empty directory.  The same bytes under
	//either extension must anchor the same server.
	TEST_F( ServerTrustTests, DerIsTrustedLikePem ){
		fs::remove( _trustedDir/"trusted.pem" );//the DER copy is the only anchor.
		let der = Crypto::ReadCertificate( _trusted );
		IO::SaveBinary<const byte>( _trustedDir/"trusted.der", std::span{der} );
		ServerTrust::Install( _config, true, {_trustedDir}, TestHandle, "opc.tcp://server.under.test:4840" );
		EXPECT_EQ( ServerTrust::AnchorCount(_config), 1u );
		EXPECT_EQ( Verify(_trusted), UA_STATUSCODE_GOOD );
		EXPECT_EQ( Verify(_other), UA_STATUSCODE_BADCERTIFICATEUNTRUSTED );
	}

	//...and what is genuinely not a certificate is still passed over - but the rejection now says so, instead of leaving
	//"0 trusted certificates loaded" to be read as "the directory is empty" and sending the operator round again.
	TEST_F( ServerTrustTests, SkippedFilesAreNamedInTheRejection ){
		fs::remove( _trustedDir/"trusted.pem" );
		let readme = string{ "not a certificate" };
		IO::SaveBinary<const char>( _trustedDir/"README.txt", std::span{readme} );
		ServerTrust::Install( _config, true, {_trustedDir}, TestHandle, "opc.tcp://server.under.test:4840" );
		ASSERT_EQ( ServerTrust::AnchorCount(_config), 0u );
		EXPECT_EQ( Verify(_trusted), UA_STATUSCODE_BADCERTIFICATEUNTRUSTED );
		let rejection = ServerTrust::Rejection( _config );
		EXPECT_NE( rejection.find("1 file was skipped"), string::npos ) << rejection;
		EXPECT_NE( rejection.find(".der"), string::npos ) << rejection;//and what it should have been called.
	}

	TEST_F( ServerTrustTests, NoAnchorsRejectsEverything ){
		ServerTrust::Install( _config, true, {_trustedDir/"does-not-exist"}, TestHandle, "opc.tcp://server.under.test:4840" );
		EXPECT_EQ( ServerTrust::AnchorCount(_config), 0u );
		EXPECT_EQ( Verify(_trusted), UA_STATUSCODE_BADCERTIFICATEUNTRUSTED );
		EXPECT_EQ( Verify(_other), UA_STATUSCODE_BADCERTIFICATEUNTRUSTED );
	}

	TEST_F( ServerTrustTests, OffAcceptsAnything ){
		ServerTrust::Install( _config, false, {_trustedDir}, TestHandle, "opc.tcp://server.under.test:4840" );
		EXPECT_EQ( ServerTrust::AnchorCount(_config), 0u );
		EXPECT_EQ( Verify(_trusted), UA_STATUSCODE_GOOD );
		EXPECT_EQ( Verify(_other), UA_STATUSCODE_GOOD );
		EXPECT_EQ( ServerTrust::Rejection(_config), "" );
	}


	//reviews/m2-closing.md #12:  the settings-driven Install took an override of *no* directories for no override at all - it
	//tested the copied list for empty, not the optional for engaged - and fell back to <root>/trustedCertDirs.  "Trust
	//nothing" spelled the obvious way therefore trusted everything production does, without a word.  The setting here holds
	//a directory that anchors _trusted, so whichever list is read shows in the count.
	TEST_F( ServerTrustTests, AnEmptyOverrideTrustsNothing ){
		RestoreTrustedCertDirs restore;
		Settings::Set( "/gateway/trustedCertDirs", jarray{_trustedDir.string()} );
		struct Restore final{ ~Restore(){ ServerTrust::OverrideTrustedCertDirs( nullopt ); } } restoreOverride;//every later client in the process reads it.

		ServerTrust::OverrideTrustedCertDirs( vector<fs::path>{} );
		ServerTrust::Install( _config, "/gateway", TestHandle, "opc.tcp://server.under.test:4840" );
		EXPECT_EQ( ServerTrust::AnchorCount(_config), 0u ) << "an override of no directories fell back to the setting's";
		EXPECT_EQ( Verify(_trusted), UA_STATUSCODE_BADCERTIFICATEUNTRUSTED );

		ServerTrust::OverrideTrustedCertDirs( nullopt );//and with none, the setting is read again.
		ServerTrust::Install( _config, "/gateway", TestHandle, "opc.tcp://server.under.test:4840" );
		EXPECT_EQ( ServerTrust::AnchorCount(_config), 1u );
		EXPECT_EQ( Verify(_trusted), UA_STATUSCODE_GOOD );
	}

	//security-matrix #3:  the verifier reads the app's own list, /gateway/trustedCertDirs, and never /access/trustedCertDirs -
	//in the hub, and in this harness (the AppServer is in-process), those are the enrollment anchors:  every certificate under
	//them may create a user, so an OPC server's certificate has no business there, and a certificate that may enroll is not
	//thereby a server the gateway will talk to.  _trusted sits in the harness's enrollment directory, which makes it the probe.
	TEST_F( ServerTrustTests, TheEnrollmentAnchorsAreNotTheServerTrust ){
		let enrollment = Settings::FindStringArray( "/access/trustedCertDirs" );
		ASSERT_FALSE( enrollment.empty() );
		ASSERT_TRUE( find_if(enrollment, [&](let& dir){ return fs::equivalent(fs::path{dir}, _trusted.parent_path()); })!=enrollment.end() ) << _trusted;
		RestoreTrustedCertDirs restore;
		ServerTrust::OverrideTrustedCertDirs( nullopt );//the setting itself is the subject.

		Settings::Set( "/gateway/trustedCertDirs", jarray{} );
		ServerTrust::Install( _config, "/gateway", TestHandle, "opc.tcp://server.under.test:4840" );
		EXPECT_EQ( ServerTrust::AnchorCount(_config), 0u );//nothing borrowed from the enrollment list.
		EXPECT_EQ( Verify(_trusted), UA_STATUSCODE_BADCERTIFICATEUNTRUSTED );
		let rejection = ServerTrust::Rejection( _config );
		EXPECT_NE( rejection.find("/gateway/trustedCertDirs"), string::npos ) << rejection;//names the list that is read.
		EXPECT_EQ( rejection.find("/access/trustedCertDirs"), string::npos ) << rejection;

		Settings::Set( "/gateway/trustedCertDirs", jarray{_trustedDir.string()} );
		ServerTrust::Install( _config, "/gateway", TestHandle, "opc.tcp://server.under.test:4840" );
		EXPECT_EQ( ServerTrust::AnchorCount(_config), 1u );
		EXPECT_EQ( Verify(_trusted), UA_STATUSCODE_GOOD );
		EXPECT_EQ( Verify(_other), UA_STATUSCODE_BADCERTIFICATEUNTRUSTED );
	}

	//EnsureDirs - the gateway's startup step - creates what is this product's to create:  its ssl/servers, where an operator drops
	//a third-party server's certificate.  Another product's directory is that product's:  absent there means not installed.
	TEST_F( ServerTrustTests, EnsureDirsCreatesOnlyThisProductsDirectories ){
		let own = Process::AppDataFolder()/"ssl"/Ƒ( "servers-test-{}", Process::ProcessId() );
		let foreign = _trustedDir/"another-product"/"ssl"/"certs";
		ASSERT_FALSE( fs::exists(own) ) << own;
		RestoreTrustedCertDirs restore;
		Settings::Set( "/gateway/trustedCertDirs", jarray{own.string(), foreign.string()} );
		ServerTrust::EnsureDirs( "/gateway" );
		EXPECT_TRUE( fs::is_directory(own) ) << own;
		EXPECT_FALSE( fs::exists(foreign) ) << foreign;
		std::error_code ec; fs::remove( own, ec );
	}

	//The live half:  a connect to the embedded OpcServer with no anchors must be refused by OUR verifier, with the detail
	//naming the server and the switch, and leave nothing behind - the same credential connects once the anchors are back.
	//IssuedToken, as TrustReloadTests:  the per-slug issued cert is what every non-certificate credential presents, and
	//the OpcServer offers no Username policy.  The anchors are swapped through ServerTrust's seam rather than the setting,
	//which every other client in the process is reading (see the header).
	class ServerTrustLiveTests : public Auth{
	protected:
		ServerTrustLiveTests()ι:Auth{ETokenType::IssuedToken}{}
		Ω SetUpTestCase()ε->void{ _jwt = BlockAwait<Web::Client::ClientSocketAwait<Jde::Web::Jwt>,Web::Jwt>( AppClient()->Jwt() ); }
		α TearDown()ι->void override{
			ServerTrust::OverrideTrustedCertDirs( nullopt );//whatever the assertions did.
			if( _client )
				UAClient::RemoveClient( move(_client) );
		}
		α Connect( atomic_flag& flag )ι->ConnectAwait::Task{
			try{
				_exception = nullptr;
				_client = co_await UAClient::GetClient( Connection->Slug, Credential{_jwt->Payload()} );
			}
			catch( Exception& e ){
				_exception = e.Move();
			}
			flag.test_and_set();
			flag.notify_all();
		}
		static optional<Web::Jwt> _jwt;
		up<Exception> _exception;
		sp<UAClient> _client;
	};
	optional<Web::Jwt> ServerTrustLiveTests::_jwt;

	TEST_F( ServerTrustLiveTests, RejectsAServerOutsideTheTrustedDirs ){
		if( auto cached = UAClient::Find(Connection->Slug, Credential{_jwt->Payload()}); cached )
			UAClient::RemoveClient( move(cached) );//a client another suite left cached would skip Configuration() - the next connect must build a fresh one.
		ServerTrust::OverrideTrustedCertDirs( vector<fs::path>{ fs::temp_directory_path()/"jde-servertrust-no-anchors" } );

		atomic_flag first;
		Connect( first );
		first.wait( false );
		ASSERT_TRUE( _exception ) << "connected through an empty trust list";
		EXPECT_FALSE( _client );
		let what = string{ _exception->what() };
		EXPECT_NE( what.find("server certificate for"), string::npos ) << what;//ours, not the server rejecting the gateway's.
		EXPECT_NE( what.find(Connection->Url), string::npos ) << what;
		EXPECT_NE( what.find("/gateway/verifyServerCertificate"), string::npos ) << what;

		ServerTrust::OverrideTrustedCertDirs( nullopt );//the harness's trustedCertDirs hold the embedded server's cert.
		atomic_flag second;
		Connect( second );
		second.wait( false );
		EXPECT_FALSE( _exception ) << _exception->what();
		EXPECT_TRUE( _client );
	}
}
