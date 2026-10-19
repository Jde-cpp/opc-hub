#include "../utils/ClientFixture.h"

namespace Jde::Opc::Gateway::Tests{
	class Auth : public ClientFixture{
	protected:
		Auth( ETokenType tt )ι:TokenType{tt}{}
		α SetUp()ε->void override;
		Ω TearDownTestSuite()->void;
	public:
		static optional<ServerCnnctn> Connection;
		static ETokenType Tokens;
		ETokenType TokenType;
	};
}