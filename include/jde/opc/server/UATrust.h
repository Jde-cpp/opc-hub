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
		//The refused certificates kept, in each memory store's rejected list (UAConfig) and in rejectedDir, the newest:  an
		//unauthenticated OPN apiece, so a flood of fresh certificates would otherwise grow both without end:  setBasics' 0 is unbounded.
		static constexpr uint MaxRejected{ 10 };
		//rejectedDir:  where a certificate that fails the verify, the rescan included, is written as <thumbprint>.der, once, for the
		//admin to move into a trusted dir (UASettings::RejectedCertDir);  empty writes none.  files:  certificates trusted beside the
		//dirs', each by its own path (UASettings::TrustedCerts), read again where the dirs are walked again.
		UATrust( vector<fs::path> dirs, fs::path rejectedDir={}, vector<fs::path> files={} )ι;
		~UATrust();
		α LoadTrustList( UA_TrustListDataType& list )ι->bool;//fills TRUSTEDCERTIFICATES from the dirs and the files (mtime-cached); true if any file was added/changed/removed since the last call. Caller clears list.
		α Install( UA_Server& ua )ι->void;//wraps secureChannelPKI+sessionPKI verifyCertificate; call once after UA_Server_newWithConfig, before Run().
		α RejectedDir()Ι->const fs::path&{ return _rejectedDir; }
	private:
		Ω VerifyCertificate( UA_CertificateGroup* group, const UA_ByteString* certificate )ι->UA_StatusCode;
		α Verify( UA_CertificateGroup* group, const UA_ByteString* certificate )ι->UA_StatusCode;
		α WriteRejected( const UA_ByteString& certificate )ι->void;
		α PruneRejected()ι->void;//rejectedDir down to the newest MaxRejected .der files.
		α ScanFiles()ι->bool;//_filePaths into _ders, as the dirs' scan takes a directory's:  true if one was added, changed or went.  Under _mutex.
		const vector<fs::path> _dirPaths;
		const vector<fs::path> _filePaths;
		flat_map<fs::path,fs::file_time_type> _fileTimes;//_filePaths as last read, one that would not read too:  read again only when it changes.
		const fs::path _rejectedDir;
		flat_set<string> _rejected;//the thumbprints written, or being written, this run, so a client that retries writes once;  under _mutex.
		std::mutex _mutex;//verifies all run on the UAServer jthread today - uncontended; serializes the scan and _ders against future threading changes.
		Crypto::TrustDirs _dirs;
		flat_map<fs::path,vector<byte>> _ders;//the certificates that read, by file.
		std::array<UA_CertificateGroup*,2> _groups{};//secureChannelPKI, sessionPKI in the server-owned config; a change updates BOTH so the shared mtime cache stays honest.
		std::array<UA_StatusCode(*)( UA_CertificateGroup*, const UA_ByteString* ),2> _originals{};//per-group - no assumption both are memorystore.
		std::atomic<int64_t> _lastRescanNs{};//steady_clock nanoseconds; 0 = never, so the first failure always rescans.
	};
}