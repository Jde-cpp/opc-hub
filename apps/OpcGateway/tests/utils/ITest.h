#pragma once

namespace Jde::Opc::Gateway{
	struct UAClient;
namespace Tests{
	struct ITest : ::testing::Test{
	protected:
		Ω SetUpTestSuite()ε->void;
		Ω TearDownTestSuite()ι->void;
		Ω Reconnect()ε->void;//a new suite client, as SetUpTestSuite connects it, for a test that removed or lost the one the suite shares.
		static sp<UAClient> _client;
	};
}}