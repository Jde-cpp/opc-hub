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
		let a = Connect( opcId, token(Jwt()) );
		ASSERT_TRUE( a->Wait() );
		let b = Connect( opcId, token(Jwt()) );//concurrent:  the detached form.
		let c = Connect( opcId, token(Jwt()) );
		ASSERT_TRUE( b->Wait() );
		ASSERT_TRUE( c->Wait() );
		for( let& connect : {a, b, c} )
			EXPECT_FALSE( connect->Error ) << connect->Error->what();
	}

	TEST_F( TokenTests, Authenticate_Bad ){
		Attempt( Connection->Slug, token(Jwt(), true) );
		EXPECT_TRUE( _exception );
		EXPECT_TRUE( _exception && string{_exception->what()}.contains("BadIdentityTokenInvalid") );
		DBG( "{}", _exception ? _exception->what() : "Error no exception." );
	}
}