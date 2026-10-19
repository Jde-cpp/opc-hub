#include <jde/fwk/io/json.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include "Auth.h"
#include "../../src/UAClient.h"
#include "../../src/auth/OpcServerSession.h"
#include "../../src/GatewayAppClient.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };

	class TokenTests : public Auth{
	protected:
		TokenTests()ι:Auth{ETokenType::IssuedToken}{}
		Ω SetUpTestSuite()ε->void;
		static optional<Web::Jwt> _jwt;
	};
	optional<Web::Jwt> TokenTests::_jwt;

	α TokenTests::SetUpTestSuite()ε->void{
		_jwt = BlockAwait<Web::Client::ClientSocketAwait<Jde::Web::Jwt>,Web::Jwt>( AppClient()->Jwt() );
	}

	static up<Exception> _exception;
	Ω authenticateTest( const Web::Jwt& jwt, ServerCnnctnNK opcId, atomic_flag& flag, bool bad=false )ι->TAwait<sp<UAClient>>::Task{
		try{
			auto text = jwt.Payload();
			if( bad )
				text[42] = text[42]-1;
			auto client = co_await UAClient::GetClient( move(opcId), Credential{text} );
		}
		catch( Exception& e ){
			_exception = e.Move();
		}
		flag.test_and_set();
		flag.notify_all();
	}

	TEST_F( TokenTests, Authenticate ){
		let opcId{ Connection->Slug };
		atomic_flag a,b,c;
		authenticateTest( *_jwt, opcId, a );
		a.wait( false );
		authenticateTest( *_jwt, opcId, b );
		authenticateTest( *_jwt, opcId, c );
		b.wait( false );
		c.wait( false );
		EXPECT_FALSE( _exception );
		std::this_thread::sleep_for( 100ms );//a wait returns once its flag is set, while the coroutine may still be in notify_all on these locals.
	}

	TEST_F( TokenTests, Authenticate_Bad ){
		atomic_flag flag;
		authenticateTest( *_jwt, Connection->Slug, flag, true );
		flag.wait( false );
		EXPECT_TRUE( _exception );
		EXPECT_TRUE( _exception && string{_exception->what()}.contains("BadIdentityTokenInvalid") );
		DBG( "{}", _exception ? _exception->what() : "Error no exception." );
	}
}