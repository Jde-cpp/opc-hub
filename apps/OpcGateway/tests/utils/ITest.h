#pragma once

namespace Jde::Web{ struct Jwt; }
namespace Jde::Opc::Gateway{
	struct UAClient;
namespace Tests{
	struct ITest : ::testing::Test{
	protected:
		Ω SetUpTestSuite()ε->void;
		Ω TearDownTestSuite()ι->void;
		static optional<Web::Jwt> _jwt;
		static sp<UAClient> _client;
	};
}}