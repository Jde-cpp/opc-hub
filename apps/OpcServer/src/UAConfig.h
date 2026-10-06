#pragma once
#include "jde/fwk/usings.h"
#include <jde/opc/uatypes/Logger.h>

namespace Jde::Crypto{ struct CryptoSettings; }
namespace Jde::Opc::Server {
	struct UAHistory;
	struct UAConfig final : UA_ServerConfig {
		UAConfig( UAHistory* history=nullptr )ε;//history:  the server's historian, whose backend is installed when it has one.
	private:
		α SetConfig( PortType port, ByteStringPtr&& certificate, const ByteStringPtr&& privateKey )ε->void;
		α SetupSecurityPolicies( const Crypto::CryptoSettings& settings, SRCE )ε->void;
		α AddSecurityPolicies( ByteStringPtr&& certificate, const ByteStringPtr&& privateKey )ε->void;
		Logger _logger;
	};
}