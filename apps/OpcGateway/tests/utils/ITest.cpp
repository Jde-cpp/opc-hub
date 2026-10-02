#include "ITest.h"
#include "../../src/GatewayAppClient.h"
#include "../../src/async/ConnectAwait.h"
#include "../../src/UAClient.h"
#include "helpers.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	α ITest::SetUpTestCase()ε->void{
		_jwt = BlockAwait<Web::Client::ClientSocketAwait<Jde::Web::Jwt>,Web::Jwt>( AppClient()->Jwt() );
		let sessionId = *Str::TryTo<SessionPK>(_jwt->SessionId, 16);
		TRACE( "UserPK: {:x}, SessionId: {:x}", _jwt->UserPK.Value, sessionId );
		auto con = GetConnection( OpcServerSlug );
		Credential cred{ _jwt->Payload() }; cred.SetUserPK( _jwt->UserPK );
		_client = BlockTAwait<sp<UAClient>>( ConnectAwait{move(con.Slug), cred} );
		AddSession( sessionId, OpcServerSlug, move(cred) );
	}
	α ITest::TearDownTestCase()ι->void{
		if( _client )
			UAClient::RemoveClient( move(_client) );
	}

	optional<Web::Jwt> ITest::_jwt;
	sp<UAClient> ITest::_client;
}