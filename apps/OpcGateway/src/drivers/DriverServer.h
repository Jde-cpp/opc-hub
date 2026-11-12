#pragma once
#include <jde/opc/server/UASettings.h>
#include <jde/opc/usings.h>

//A UA server the gateway runs for itself on loopback, a driver row's (docs/open-protocol/spec.md *The driver server*):  what
//it is built from and what its clients trust it by.  Each takes a slug and a port and no row, so the flow server builds the same.
namespace Jde::Opc::Gateway::DriverServer{
	//The server's settings:  the process's own /http/ssl pair on 127.0.0.1 at `port`, one endpoint, Sign & Encrypt under
	//Basic256Sha256, and no None endpoint;  the applicationUri the gateway's own, the pair's, and the applicationName
	//`Jde-Cpp <kind> <slug>`;  certificate and issued tokens, the pair's own a session as the gateway's own user;  no rejected
	//directory and no trusted one, but a trust list of two by file:  the certificate the gateway issues the slug, made here
	//where it is not there yet, which a web session's channel presents, and the pair's, which the collector presents.  Throws
	//where the process has no pair with a URI in its subjectAltName.
	α Settings( sv kind, const ServerCnnctnNK& slug, PortType port, SRCE )ε->Opc::Server::UASettings;
	//What a client of such a server trusts beside /gateway/trustedCertDirs:  the process's own certificate, which neither
	//shipped directory holds.
	α Anchors()ι->vector<fs::path>;
}