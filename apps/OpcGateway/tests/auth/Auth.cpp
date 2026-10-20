#include "Auth.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	optional<ServerCnnctn> Auth::Connection={};
	ETokenType Auth::Tokens{};
	α Auth::SetUp()ε->void{
		if( !Connection )
			Connection = GetConnection( OpcServerSlug );
		if( empty(Tokens) )
			Tokens = AvailableUserTokens( Connection->Url );
		if( empty(Tokens & ETokenType(TokenType)) ){
			GTEST_SKIP() << "Authentication type is not allowed on this server.";
		}
	}
	α Auth::TearDownTestSuite()->void{
		PurgeServerCnnctn( OpcServerSlug );
		Connection = nullopt;
	}
}