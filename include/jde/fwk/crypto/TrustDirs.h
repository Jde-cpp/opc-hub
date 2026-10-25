#pragma once
#include "jde/fwk/log/logTags.h"
#define Φ Γ auto

namespace Jde::Crypto{
	//What a file dropped in a certificate directory may be called - an operator's drop-dir is scanned, not a configured path,
	//so something has to keep the .crl/.key/README out of the parser.  `.der`/`.cer` are the encoding a third party publishes:
	//an OPC UA server writes DER and a UA trust list is a directory of .der by
	//convention, and skipping those in silence was install-issues #33 - the operator copied in the file the server actually
	//produces and nothing changed.  One list for every such scan (ServerTrust, UATrust, the enrollment anchors) so they cannot
	//drift, and one sentence to quote back when a scan found nothing.
	constexpr sv CertificateExtensions{ ".pem, .crt, .der and .cer" };
	Ξ IsCertificateFile( const fs::path& p )ι->bool{ const auto ext = p.extension(); return ext==".pem" || ext==".crt" || ext==".der" || ext==".cer"; }
	//The skip, said once - the filter's other half, shared for the same reason.  Each scan warns, with a count, when it ends
	//having anchored *nothing*;  but a drop-dir that already holds a certificate never anchors nothing - an installed OpcServer's
	//only trusted dir always has the hub's own OpcHub.pem - so there that warning cannot fire, and a file passed over beside it
	//(`client.pfx`, `SERVER.DER`) was a Debug line and nothing else (reviews/m2-closing.md #11).  The per-file line cannot simply
	//be louder:  every one of these scans reruns - on a failed verify, on each connect - and a junk-certificate flood would
	//repeat it.  True the first time this process passes `p` over, false from then on:  the caller logs a Warning once and
	//Debug after, as each already does for a missing directory.
	Φ FirstSkip( const fs::path& p )ι->bool;

	//The walk of an operator's certificate drop-dirs, for the three lists built from them:  the OpcServer's trusted clients
	//(UATrust), the enrollment anchors (Access::Server::Trust) and a UA client's trusted servers (Opc::ServerTrust).  Each
	//rescans - on a failed verify, on each connect - so the walk says things once:  a missing directory warns once per list
	//until it appears, and a file passed over for its extension warns the first time (FirstSkip), Debug after.  Not
	//thread-safe:  the caller serializes Scan.
	struct Γ TrustDirs final{
		using OnCertificate = function<void( const fs::path& path, vector<byte>&& der )>;
		//`noun` is what the log says a file would have been:  "trust anchor".  `unreadable` is the level of a file or directory
		//that will not read - Critical for a list that gates logins, where it refuses them, Warning for one among many servers.
		TrustDirs( string noun, ELogTags tags, ELogLevel unreadable )ι;
		//Hands onCertificate each certificate file new or changed since the last scan - every one, on the first - with its DER,
		//or with none for one that no longer reads or has gone, which must not throw.  A throw on a DER is the file not loading.
		//True when it handed it any.
		α Scan( const vector<fs::path>& dirs, const OnCertificate& onCertificate, SRCE )ι->bool;
		α Scan( sv setting, const OnCertificate& onCertificate, SRCE )ι->bool;//the directories `setting` lists.
		α Known()Ι->uint{ return _files.size(); }//certificate files in the directories, read or not.
		α Skipped()Ι->uint{ return _skipped; }//files the last scan passed over for their extension.
		α SkippedNote()Ι->string;//"1 file was passed over for its extension - .pem, ... are read":  why nothing was trusted (install-issues #33).
	private:
		string _noun;
		ELogTags _tags;
		ELogLevel _unreadable;
		flat_map<fs::path,fs::file_time_type> _files;//failed files too:  retried only when they change.
		uint _skipped{};
	};
}
#undef Φ