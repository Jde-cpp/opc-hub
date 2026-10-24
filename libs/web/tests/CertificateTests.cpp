#include <jde/fwk/co/BlockAwait.h>
#include <jde/web/client/http/ClientHttpAwait.h>
//#include <jde/web/server/Flex.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include "mocks/ServerMock.h"


#define let const auto
namespace Jde::Web{
	using Mock::Host;
	using Mock::Port;

	using CryptoSettings = Crypto::CryptoSettings;
	struct CertificateTests : public ::testing::Test{
	protected:
		CertificateTests(){}
		~CertificateTests() override{}

		Ω SetUpTestCase()->void{};
		α SetUp()->void override{}
		α TearDown()->void override;
	};

	α SslSettings( const fs::path& baseDir = {}, string passcode = {}, string commonName = "web-certificate-tests" )ι->jobject{
		//sv what, const Container::Variant& v, bool save=true, SRCE )ε->void;
		let prefix = "/http/ssl"s;
		//certificate has to be an object: CryptoSettings reads sub-objects, so the flat strings this used to pass were ignored and
		//the generated cert took every default - including no subjectAltName.  That went unnoticed while the client accepted any
		//certificate; now that it verifies the peer's name (C1), a cert that names nobody is correctly refused.
		return jobject{
			{"port", Port},
			{"ssl", jobject{
				{"certificate", jobject{
					{"path", (baseDir/"certs/server.pem").string()},
					//distinct per fixture: an X509_STORE finds anchors by subject, so two self-signed certs sharing a DN make the
					//store answer with whichever it saw first and the other fails to verify against its own key.
					{"commonName", commonName},
					{"subjectAltName", "DNS:localhost,IP:127.0.0.1"}
				}},
				{"privateKey", jobject{ {"path", (baseDir/"private/server.pem").string()}, {"passcode", passcode} }},
				{"publicKey", jobject{ {"path", (baseDir/"public/server.pem").string()} }},
				{"passcode", passcode}
			}}
		};
	}

	α CertificateTests::TearDown()->void{
		Mock::Stop();
		Settings::Load();
	}
	using Web::Client::ClientHttpAwait;
	using Web::Client::ClientHttpRes;
	TEST_F( CertificateTests, DefaultSettings ){
		Mock::Start( SslSettings(Process::AppDataFolder()/"ssl") );
		auto await = ClientHttpAwait{ Host, "/ping", Port, {.ContentType="text/ping", .Verb=http::verb::post} };
		let res = BlockAwait<ClientHttpAwait,ClientHttpRes>( move(await) );
		ASSERT_TRUE( res[http::field::server].contains("SSL") );
	}

	TEST_F( CertificateTests, NewDirectory ){
		let path = Settings::FindPath( "/testing/certDir" ).value_or( fs::temp_directory_path()/"webTests/ssl" );
		fs::remove_all( path );
		Mock::Start( SslSettings(path, "PaSsCoDe", "web-certificate-tests-newdir") );
		auto await = ClientHttpAwait{ Host, "/ping", Port, {.ContentType="text/ping", .Verb=http::verb::post} };
		let res = BlockAwait<ClientHttpAwait,ClientHttpRes>( move(await) );
		ASSERT_TRUE( res[http::field::server].contains("SSL") );
	}
}