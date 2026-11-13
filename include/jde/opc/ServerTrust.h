#pragma once
#include <span>
#include <jde/opc/uatypes/Logger.h>

struct UA_ClientConfig;

//Server-certificate verification for this repo's UA clients - the gateway's (MVP "Gateway→OPC-server certificate
//verification") and the PLC emulator's.  open62541 verifies the server's certificate through
//UA_ClientConfig::certificateVerification in initSecurityPolicy, for every endpoint that carries one - None security
//mode included - and UA_ClientConfig_setDefault installs AcceptAll there, so until this a client trusted whatever
//certificate a server answered with.  Install puts a Crypto::TrustStore-backed group in its place, anchored on the
//certificates under the caller's own <root>/trustedCertDirs, or AcceptAll when its <root>/verifyServerCertificate is
//false.  Anchors only, no OS root store:  an OPC server's certificate is self-signed, so a public CA vouches for nothing
//here.  Loaded per client, i.e. per connect, so a certificate copied in or re-issued after startup is trusted by the
//next connection without a rescan.  A certificate file is taken by extension - .pem, .crt, .der or .cer - and either
//encoding parses:  a UA server publishes DER, and skipping it in silence was install-issues #33.  Anything else in the
//directory is logged at Debug and counted, so a rejection with no anchors can say the files were passed over rather than
//leave "0 trusted certificates loaded" to be read as an empty directory.
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
	//Creates <root>/rejectedCertDir too (the same own-data-folder rule), where the verifier writes a server certificate it
	//rejects, so an admin can see it and trustServer can move it.
	α EnsureDirs( sv settingsRoot )ι->void;
	//<root>/serversDir, this product's own trusted-server directory, default <AppDataFolder>/ssl/servers:  where an operator
	//drops a third-party server's certificate, and where trustServer moves one it trusts - so it must be among
	//<root>/trustedCertDirs, the list the verifier reads.
	α ServersDir( sv settingsRoot )ι->fs::path;
	//<root>/rejectedCertDir, the directory a rejected server certificate is written to as <thumbprint>.der, default
	//<ServersDir>/rejected-servers.  Not ssl/servers/rejected:  that is the gateway endpoint's for the client certificates it
	//refuses (/gateway/server/rejectedCertDir), which it prunes to the newest few.
	α RejectedDir( sv settingsRoot )ι->fs::path;
	//Before UA_ClientConfig_setDefault, which only fills a null verifyCertificate.  `url` names the server in the log and
	//the rejection; `settingsRoot` - "/gateway", "/emulator" - is where both settings are read and what the rejection names.
	//`expectedUri` is the applicationUri the chosen endpoint carries - the connection's certificateUri, open62541's endpoint
	//filter - against which the verifier holds a trusted certificate's SAN URI, warning (never refusing) on a mismatch (spec
	//*Trust*, *Open questions* #14);  empty where the connection pins no url (a None channel), so the check is skipped.
	//`anchors` are certificates trusted beside the directories', each by its own file and in memory alone, copied into no
	//directory:  the process's own pair, for the client of a server that process runs for itself and presents the pair on, a
	//driver row's (docs/open-protocol/spec.md *The driver server*).  The list and verifyServerCertificate keep their meaning.
	α Install( UA_ClientConfig& config, sv settingsRoot, Jde::Handle h, str url, sv expectedUri={}, std::span<const fs::path> anchors={}, SRCE )ε->void;
	α Install( UA_ClientConfig& config, bool verify, const vector<fs::path>& trustedCertDirs, Jde::Handle h, str url, sv settingsRoot="/gateway", sv expectedUri={}, std::span<const fs::path> anchors={}, SRCE )ε->void;//explicit, for tests.
	//Test seam:  the directories TrustedDirs answers - and so the settings-driven Install uses - instead of <root>/trustedCertDirs,
	//for every client created until nullopt restores the setting - the live rejection is provoked here, mid-run, without
	//rewriting a setting other clients in the process are reading.  An empty list is an override too:  no directories, nothing trusted.
	α OverrideTrustedCertDirs( optional<vector<fs::path>> dirs )ι->void;
	//The directories the verifier trusts a server certificate from:  OverrideTrustedCertDirs' while one is set, else
	//<root>/trustedCertDirs.  What the settings-driven Install loads, and what trustServer holds its target against.
	α TrustedDirs( sv settingsRoot )ι->vector<fs::path>;
	//Why this client's verifier last rejected a server certificate, "" if it never did or verification is off - the detail
	//StateCallback hands the waiting requests, since the status alone (BadCertificateUntrusted) reads the same as the server
	//rejecting OUR certificate.
	α Rejection( const UA_ClientConfig& config )ι->string;
	//The SHA-1 thumbprint of the certificate this client's verifier last rejected, "" if it never did or that file could not be
	//written - the name of the <thumbprint>.der in RejectedDir, which the client records per slug (UAClient) for
	//rejectedCertificate{} to read and trustServer to move.
	α RejectedThumbprint( const UA_ClientConfig& config )ι->string;
	α AnchorCount( const UA_ClientConfig& config )ι->uint;//0 when verification is off.
}