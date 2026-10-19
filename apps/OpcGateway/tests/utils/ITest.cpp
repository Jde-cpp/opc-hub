#include "ITest.h"
#include "../../src/GatewayAppClient.h"
#include "../../src/async/ConnectAwait.h"
#include "../../src/UAClient.h"
#include "helpers.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };
	α ITest::SetUpTestSuite()ε->void{
		_jwt = BlockAwait<Web::Client::ClientSocketAwait<Jde::Web::Jwt>,Web::Jwt>( AppClient()->Jwt() );
		let sessionId = *Str::TryTo<SessionPK,16>(_jwt->SessionId);
		TRACE( "UserPK: {}, SessionId: {}", hex(_jwt->UserPK.Value), hex(sessionId) );
		auto con = GetConnection( OpcServerSlug );
		Credential cred{ _jwt->Payload() }; cred.SetUserPK( _jwt->UserPK );
		_client = BlockTAwait<sp<UAClient>>( ConnectAwait{move(con.Slug), cred} );
		AddSession( sessionId, OpcServerSlug, move(cred) );
	}
	α ITest::TearDownTestSuite()ι->void{
		if( _client )
			UAClient::RemoveClient( move(_client) );
	}

	optional<Web::Jwt> ITest::_jwt;
	sp<UAClient> ITest::_client;
}