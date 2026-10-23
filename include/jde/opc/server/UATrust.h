#pragma once
#include <mutex>
#include <open62541/server.h>
#include "jde/fwk/usings.h"
#include <jde/fwk/crypto/TrustDirs.h>
#include <jde/opc/uatypes/Logger.h>

//Runtime reload of the host's trusted client certificate dirs for a server's UA PKI groups, so a client cert re-issued after
//startup (the gateway rewrites its file in place on a certificateUri change) heals without a server restart - the walk is
//Crypto::TrustDirs, as Access::Server::TrustVerify's is.  One per server, owned by its UAConfig.
namespace Jde::Opc::Server{
	struct UATrust final : noncopyable{
		//rejectedDir:  where a certificate that fails the verify, the rescan included, is written as <thumbprint>.der, once, for the
		//admin to move into a trusted dir (UASettings::RejectedCertDir);  empty writes none.
		UATrust( vector<fs::path> dirs, fs::path rejectedDir={} )ι;
		~UATrust();
		α LoadTrustList( UA_TrustListDataType& list )ι->bool;//fills TRUSTEDCERTIFICATES from the dirs (mtime-cached); true if any file was added/changed/removed since the last call. Caller clears list.
		α Install( UA_Server& ua )ι->void;//wraps secureChannelPKI+sessionPKI verifyCertificate; call once after UA_Server_newWithConfig, before Run().
		α RejectedDir()Ι->const fs::path&{ return _rejectedDir; }
	private:
		Ω VerifyCertificate( UA_CertificateGroup* group, const UA_ByteString* certificate )ι->UA_StatusCode;
		α Verify( UA_CertificateGroup* group, const UA_ByteString* certificate )ι->UA_StatusCode;
		α WriteRejected( const UA_ByteString& certificate )ι->void;
		const vector<fs::path> _dirPaths;
		const fs::path _rejectedDir;
		flat_set<string> _rejected;//the thumbprints written this run, so a client that retries writes once;  under _mutex.
		std::mutex _mutex;//verifies all run on the UAServer jthread today - uncontended; serializes the scan and _ders against future threading changes.
		Crypto::TrustDirs _dirs;
		flat_map<fs::path,vector<byte>> _ders;//the certificates that read, by file.
		std::array<UA_CertificateGroup*,2> _groups{};//secureChannelPKI, sessionPKI in the server-owned config; a change updates BOTH so the shared mtime cache stays honest.
		std::array<UA_StatusCode(*)( UA_CertificateGroup*, const UA_ByteString* ),2> _originals{};//per-group - no assumption both are memorystore.
		std::atomic<int64_t> _lastRescanNs{};//steady_clock nanoseconds; 0 = never, so the first failure always rescans.
	};
}