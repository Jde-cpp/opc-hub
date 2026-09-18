#pragma once
#include <jde/opc/uatypes/Logger.h>

//Server-certificate verification for this repo's UA clients - the gateway's (MVP "Gateway→OPC-server certificate
//verification") and the PLC emulator's.  open62541 verifies the server's certificate through
//UA_ClientConfig::certificateVerification in initSecurityPolicy, for every endpoint that carries one - None security
//mode included - and UA_ClientConfig_setDefault installs AcceptAll there, so until this a client trusted whatever
//certificate a server answered with.  Install puts a Crypto::TrustStore-backed group in its place, anchored on the
//certificates under the caller's own <root>/trustedCertDirs, or AcceptAll when its <root>/verifyServerCertificate is
//false.  Anchors only, no OS root store:  an OPC server's certificate is self-signed, so a public CA vouches for nothing
//here.  Loaded per client, i.e. per connect, so a certificate copied in or re-issued after startup is trusted by the
//next connection without a rescan.
//
//Both settings are per app, under the root the caller passes: the gateway's are /gateway/trustedCertDirs and
///gateway/verifyServerCertificate, the emulator's /emulator/….  The list is the app's own, and NOT
///access/trustedCertDirs, which it was until 2026-09-18:  in the hub that one is the AppServer role's enrollment anchors
//- every certificate under it may create a user - so trusting a third-party OPC server meant copying its certificate in
//among them (reviews/security-matrix.md #3).  The enrollment anchors and the OpcServer's list of trusted *client*
//certificates (UATrust) keep /access/trustedCertDirs.  Whichever root the caller passes is the one the rejection
//message names, so a wrong root would send the operator to settings their app never reads.
namespace Jde::Opc::ServerTrust{
	α Enabled( sv settingsRoot )ι->bool;//<root>/verifyServerCertificate, default true.
	//At startup:  creates the <root>/trustedCertDirs entries that lie under this process's own data folder - its ssl/servers,
	//where an operator drops a third-party server's certificate - so the place exists before anyone needs it.  Another
	//product's directory (the bundled OpcServer's ssl/certs) is that product's to create:  absent means not installed.
	α EnsureDirs( sv settingsRoot )ι->void;
	//Before UA_ClientConfig_setDefault, which only fills a null verifyCertificate.  `url` names the server in the log and
	//the rejection; `settingsRoot` - "/gateway", "/emulator" - is where both settings are read and what the rejection names.
	α Install( UA_ClientConfig& config, sv settingsRoot, Jde::Handle h, str url, SRCE )ε->void;
	α Install( UA_ClientConfig& config, bool verify, const vector<fs::path>& trustedCertDirs, Jde::Handle h, str url, sv settingsRoot="/gateway", SRCE )ε->void;//explicit, for tests.
	//Test seam:  the directories the settings-driven Install uses instead of <root>/trustedCertDirs, for every client
	//created until nullopt restores the setting - the live rejection is provoked here, mid-run, without rewriting a
	//setting other clients in the process are reading.
	α OverrideTrustedCertDirs( optional<vector<fs::path>> dirs )ι->void;
	//Why this client's verifier last rejected a server certificate, "" if it never did or verification is off - the detail
	//StateCallback hands the waiting requests, since the status alone (BadCertificateUntrusted) reads the same as the server
	//rejecting OUR certificate.
	α Rejection( const UA_ClientConfig& config )ι->string;
	α AnchorCount( const UA_ClientConfig& config )ι->uint;//0 when verification is off.
}
