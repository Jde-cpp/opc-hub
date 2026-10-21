#pragma once
#include "jde/fwk/log/logTags.h"

namespace Jde::Crypto{
	//The walk of an operator's certificate drop-dirs, for the three lists built from them:  the OpcServer's trusted clients
	//(UATrust), the enrollment anchors (Access::Server::Trust) and a UA client's trusted servers (Opc::ServerTrust).  Each
	//rescans - on a failed verify, on each connect - so the walk says things once:  a missing directory warns once per process
	//until it appears, and a file passed over for its extension warns the first time (FirstSkip), Debug after.  Not
	//thread-safe:  the caller serializes Scan.
	struct Γ TrustDirs final{
		using OnCertificate = function<void( const fs::path& path, vector<byte>&& der )>;
		TrustDirs( string noun, ELogTags tags )ι;//`noun` is what the log says a file would have been:  "trust anchor".
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
		flat_map<fs::path,fs::file_time_type> _files;//failed files too:  retried only when they change.
		uint _skipped{};
	};
}