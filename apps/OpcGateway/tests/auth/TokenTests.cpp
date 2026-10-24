#include <jde/fwk/io/json.h>
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
	};

	Ω token( const Web::Jwt& jwt, bool bad=false )ι->Credential{
		auto text = jwt.Payload();
		if( bad )
			text[42] = text[42]-1;
		return Credential{ text };
	}

	TEST_F( TokenTests, Authenticate ){
		let opcId{ Connection->Slug };
		atomic_flag a,b,c;
		sp<UAClient> clientA, clientB, clientC;//one each:  b and c run at once.
		Connect( opcId, token(Jwt()), &clientA, &_exception, &a );
		a.wait( false );
		Connect( opcId, token(Jwt()), &clientB, &_exception, &b );//concurrent:  the detached form.
		Connect( opcId, token(Jwt()), &clientC, &_exception, &c );
		b.wait( false );
		c.wait( false );
		EXPECT_FALSE( _exception );
		std::this_thread::sleep_for( 100ms );//a wait returns once its flag is set, while the coroutine may still be in notify_all on these locals.
	}

	TEST_F( TokenTests, Authenticate_Bad ){
		Attempt( Connection->Slug, token(Jwt(), true) );
		EXPECT_TRUE( _exception );
		EXPECT_TRUE( _exception && string{_exception->what()}.contains("BadIdentityTokenInvalid") );
		DBG( "{}", _exception ? _exception->what() : "Error no exception." );
	}
}