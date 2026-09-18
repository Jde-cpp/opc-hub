#pragma once
#include "jde/fwk/usings.h"
#include <jde/opc/uatypes/Logger.h>

namespace Jde::Crypto{ struct CryptoSettings; }
namespace Jde::Opc::Server {
	struct UAConfig final : UA_ServerConfig {
		UAConfig()ε;
	private:
		α SetConfig( PortType port, ByteStringPtr&& certificate, const ByteStringPtr&& privateKey )ε->void;
		α SetupSecurityPolicies( const Crypto::CryptoSettings& settings, SRCE )ε->void;
		α AddSecurityPolicies( ByteStringPtr&& certificate, const ByteStringPtr&& privateKey )ε->void;
		Logger _logger;
	};
}