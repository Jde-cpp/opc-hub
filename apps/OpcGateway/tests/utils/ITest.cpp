#include "ITest.h"
#include "../../src/GatewayAppClient.h"
#include "../../src/async/ConnectAwait.h"
#include "../../src/UAClient.h"
#include "helpers.h"
#include <jde/fwk/co/BlockAwait.h>

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };
	α ITest::SetUpTestSuite()ε->void{
		let& jwt = Jwt();
		let sessionId = *Str::TryTo<SessionPK,16>( jwt.SessionId );
		TRACE( "UserPK: {}, SessionId: {}", hex(jwt.UserPK.Value), hex(sessionId) );
		auto con = GetConnection( OpcServerSlug );
		auto cred = SessionCredential( jwt.UserPK );
		_client = BlockAwait( ConnectAwait{move(con.Slug), cred} );
		AddSession( sessionId, OpcServerSlug, move(cred) );
	}
	α ITest::Reconnect()ε->void{
		_client = BlockAwait( ConnectAwait{string{OpcServerSlug}, SessionCredential(Jwt().UserPK)} );
	}
	α ITest::TearDownTestSuite()ι->void{
		if( _client )
			UAClient::RemoveClient( move(_client) );
	}

	sp<UAClient> ITest::_client;
}