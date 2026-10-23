#include <jde/opc/server/UATrust.h>
#include <shared_mutex>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/opc/UAException.h>

#define let const auto
namespace Jde::Opc::Server{
	constexpr ELogTags _tags = ( ELogTags )EOpcLogTags::OpcCrypto;

	//A failed verify is what an unauthenticated OPN with a junk senderCertificate produces - before any body decryption -
	//and each one used to rescan every trusted dir and deep-copy every cached DER, on the UA thread under the server lock
	//(opcserver-review3 L34).  Deliberately small:  the rescan exists so a certificate re-issued after startup heals on
	//the client's next handshake, and starving that is worse than the amplification this bounds - which the finding rates
	//at only ~1.1-1.5x anyway.  50ms is below any handshake latency (TrustReloadTests' whole re-issue-and-reconnect takes
	//~215ms, a 4x margin) while capping a junk-senderCertificate flood at 20 directory walks a second instead of one per
	//OPN.  Raise it only with that test in hand.
	constexpr auto _rescanInterval{ 50ms };
	//The verify wrapper is a C function pointer in the group's vtable, and the group's context is the memorystore's own:  so
	//a wrapped group finds its server's UATrust here, as the wrapper finds which of the two groups it is.
	static std::shared_mutex _installedMutex;
	static flat_map<const UA_CertificateGroup*,UATrust*> _installed;

	UATrust::UATrust( vector<fs::path> dirs, fs::path rejectedDir )ι:
		_dirPaths{ move(dirs) },
		_rejectedDir{ move(rejectedDir) },
		_dirs{ "trusted client certificate", _tags, ELogLevel::Critical }
	{}
	UATrust::~UATrust(){
		std::unique_lock _{ _installedMutex };
		for( auto g : _groups ){
			if( auto p = _installed.find(g); p!=_installed.end() && p->second==this )
				_installed.erase( p );
		}
	}

	α UATrust::LoadTrustList( UA_TrustListDataType& list )ι->bool{
		std::lock_guard _{ _mutex };
		let changed = _dirs.Scan( _dirPaths, [this]( const fs::path& path, vector<byte>&& der ){
			if( der.size() ){
				INFO( "{} certificate:  {}", _ders.contains(path) ? "Updated" : "Added", path.string() );
				_ders[path] = move( der );
			}
			else if( _ders.erase(path) )
				INFO( "Removed certificate:  {}", path.string() );
		});
		vector<UA_ByteString> certs;//shallow views into the cache, only used to feed UA_Array_copy.
		for( let& [_2,der] : _ders )
			certs.emplace_back( UA_ByteString{ der.size(), (UA_Byte*)der.data() } );
		//install-issues #33:  an empty trust list and a trust list of files this build would not read look identical from the
		//outside - the client is refused either way, and the operator has just copied its certificate in.  Warned once per
		//rescan that anchored nothing, not per file:  a failed verify rescans, so a per-file line would repeat under a flood.
		//It is the *whole* list, though, and an installed server's is never empty (the hub's own certificate is in it) - the
		//file beside that one is named by the scan's own once-only Warning, above.
		if( certs.empty() && _dirs.Skipped() ){
			vector<string> dirs;
			for( let& dir : _dirPaths )
				dirs.push_back( dir.string() );
			WARN( "No trusted client certificates under [{}]:  {}.", Str::Join(dirs), _dirs.SkippedNote() );
		}
		list.specifiedLists |= UA_TRUSTLISTMASKS_TRUSTEDCERTIFICATES;//always, even empty - UA_TrustListDataType_set only replaces sections named here, so a shrink-to-empty must carry the mask.
		if( certs.size() ){
			if( UA_Array_copy(certs.data(), certs.size(), (void**)&list.trustedCertificates, &UA_TYPES[UA_TYPES_BYTESTRING]) ){
				UA_TrustListDataType_clear( &list );//no mutation on OOM - the caller falls back to the original verify status.
				return false;
			}
			list.trustedCertificatesSize = certs.size();
		}
		return changed;
	}

	//C shim installed in the group vtable (UAServer::Constructor pattern: try/catch -> status).
	α UATrust::VerifyCertificate( UA_CertificateGroup* group, const UA_ByteString* certificate )ι->UA_StatusCode{
		UATrust* trust{};
		{
			std::shared_lock _{ _installedMutex };
			if( auto p = _installed.find(group); p!=_installed.end() )
				trust = p->second;
		}
		ASSERT( trust );//only ever installed on the two wrapped groups, which their UATrust outlives.
		return trust ? trust->Verify( group, certificate ) : UA_STATUSCODE_BADINTERNALERROR;
	}
	α UATrust::Verify( UA_CertificateGroup* group, const UA_ByteString* certificate )ι->UA_StatusCode{
		let i = group==_groups[0] ? 0u : 1u;
		ASSERT( group==_groups[i] && _originals[i] );
		let sc = _originals[i]( group, certificate );
		if( sc==UA_STATUSCODE_GOOD )
			return sc;//fast path - no lock, no disk.
		auto y{ sc };
		let nowNs = duration_cast<std::chrono::nanoseconds>( steady_clock::now().time_since_epoch() ).count();
		auto lastNs = _lastRescanNs.load( std::memory_order_relaxed );
		if( (lastNs && nowNs-lastNs<duration_cast<std::chrono::nanoseconds>(_rescanInterval).count())
			|| !_lastRescanNs.compare_exchange_strong(lastNs, nowNs, std::memory_order_relaxed) )
			return y;//rescanned just now, or another thread is: the original failure, exactly as before the reload existed.
		UA_TrustListDataType fresh;
		UA_TrustListDataType_init( &fresh );
		try{
			if( LoadTrustList(fresh) ){//a cert re-issued or copied in after startup shouldn't require a restart - rescan before failing.
				for( auto g : _groups ){
					if( g && g->setTrustList )
						UAε( g->setTrustList(g, &fresh) );//sets reloadRequired - the retry rebuilds lazily. Not UA_Server_setCertificates: that closes now-untrusted channels, re-entrant with the OPN handshake this runs inside.
				}
				y = _originals[i]( group, certificate );
				LOG( y==UA_STATUSCODE_GOOD ? ELogLevel::Information : ELogLevel::Warning, _tags, "Reloaded trusted certificates after failed verify: {} -> {}.", UA_StatusCode_name(sc), UA_StatusCode_name(y) );
			}
		}
		catch( UAException& e ){//degradation is exactly the pre-reload behavior: return the original failure.
			e.PrependWhat( "Trust rescan failed" );
		}
		UA_TrustListDataType_clear( &fresh );
		if( y!=UA_STATUSCODE_GOOD && !_rejectedDir.empty() )
			WriteRejected( *certificate );
		return y;
	}
	//The refused certificate, as the memory store's rejected list holds it, written out for the admin (spec *Trust*):  named by
	//its sha-256 thumbprint, once per certificate and run, so a client that keeps retrying costs one file;  a write that fails
	//gives the thumbprint back, for the client's next refusal to try again.  Junk that is no certificate - an OPN with a garbage
	//senderCertificate - is dropped:  the der ctor refuses it, and there is nothing to move.
	α UATrust::WriteRejected( const UA_ByteString& certificate )ι->void{
		string thumbprint;//set once this call holds the certificate's place in _rejected.
		try{
			const std::span<const byte> der{ (const byte*)certificate.data, certificate.length };
			const Crypto::Certificate parsed{ der };
			string fingerprint{ parsed.Fingerprint };
			std::erase( fingerprint, ':' );
			let path = _rejectedDir/Ƒ( "{}.der", fingerprint );
			{
				std::lock_guard _{ _mutex };
				if( !_rejected.emplace(fingerprint).second )
					return;
			}
			thumbprint = move( fingerprint );
			if( fs::exists(path) )
				return;
			fs::create_directories( _rejectedDir );
			Crypto::WriteCertificate( path, vector<byte>{der.begin(), der.end()}, SRCE_CUR );
			INFO( "Rejected client certificate '{}' written to '{}' - move it into a trusted dir to admit the client.", parsed.DistinguishedName, path.string() );
		}
		catch( const runtime_error& e ){
			if( thumbprint.size() ){
				std::lock_guard _{ _mutex };
				_rejected.erase( thumbprint );
			}
			DBG( "A rejected certificate was not written to '{}':  {}", _rejectedDir.string(), e.what() );
			return;
		}
		PruneRejected();
	}
	//The oldest go, not the newest refused:  a client refused after a flood is written, and the admin finds it.  Their thumbprints
	//leave _rejected with them, so one still refused is written again on its next try, and the set stays as small as the directory.
	α UATrust::PruneRejected()ι->void{
		vector<std::pair<fs::file_time_type,fs::path>> files;
		std::error_code ec;
		for( fs::directory_iterator it{_rejectedDir, ec}, end; !ec && it!=end; it.increment(ec) ){
			if( it->path().extension()==".der" && it->is_regular_file(ec) )
				files.emplace_back( it->last_write_time(ec), it->path() );
		}
		if( files.size()<=MaxRejected )
			return;
		std::ranges::sort( files );
		let excess = files.size()-MaxRejected;
		for( uint i=0; i<excess; ++i ){
			fs::remove( files[i].second, ec );
			std::lock_guard _{ _mutex };
			_rejected.erase( files[i].second.stem().string() );
		}
		DBG( "Removed the {} oldest rejected certificate(s) from '{}' - it keeps the newest {}.", excess, _rejectedDir.string(), MaxRejected );
	}

	α UATrust::Install( UA_Server& ua )ι->void{
		auto config = UA_Server_getConfig( &ua );//the server-owned copy - UA_Server_newWithConfig memsets the UAConfig it was built from.
		_groups = { &config->secureChannelPKI, &config->sessionPKI };
		std::unique_lock _{ _installedMutex };
		for( uint i=0; i<_groups.size(); ++i ){
			auto g = _groups[i];
			if( g->verifyCertificate && g->verifyCertificate!=&VerifyCertificate ){//null: a group with no verifier.  The unsecured config that built one is gone (security-matrix #5); the guard stays a guard.
				_originals[i] = g->verifyCertificate;
				g->verifyCertificate = &VerifyCertificate;
				_installed.insert_or_assign( g, this );
			}
		}
	}
}