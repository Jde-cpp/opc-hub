#include "UATrust.h"
#include <mutex>
#include <jde/fwk/crypto/TrustDirs.h>
#include <jde/opc/UAException.h>

#define let const auto
namespace Jde::Opc::Server{
	constexpr ELogTags _tags = ( ELogTags )EOpcLogTags::OpcCrypto;

	static std::mutex _mutex;//verifies all run on the UAServer jthread today - uncontended; serializes the scan and _ders against future threading changes.
	static Crypto::TrustDirs _dirs{ "trusted client certificate", _tags, ELogLevel::Critical };
	static flat_map<fs::path,vector<byte>> _ders;//the certificates that read, by file.
	static std::array<UA_CertificateGroup*,2> _groups{};//secureChannelPKI, sessionPKI in the server-owned config; a change updates BOTH so the shared mtime cache stays honest.
	static std::array<UA_StatusCode(*)( UA_CertificateGroup*, const UA_ByteString* ),2> _originals{};//per-group - no assumption both are memorystore.
	//A failed verify is what an unauthenticated OPN with a junk senderCertificate produces - before any body decryption -
	//and each one used to rescan every trusted dir and deep-copy every cached DER, on the UA thread under the server lock
	//(opcserver-review3 L34).  Deliberately small:  the rescan exists so a certificate re-issued after startup heals on
	//the client's next handshake, and starving that is worse than the amplification this bounds - which the finding rates
	//at only ~1.1-1.5x anyway.  50ms is below any handshake latency (TrustReloadTests' whole re-issue-and-reconnect takes
	//~215ms, a 4x margin) while capping a junk-senderCertificate flood at 20 directory walks a second instead of one per
	//OPN.  Raise it only with that test in hand.
	constexpr auto _rescanInterval{ 50ms };
	static std::atomic<int64_t> _lastRescanNs{};//steady_clock nanoseconds; 0 = never, so the first failure always rescans.

	α UATrust::LoadTrustList( UA_TrustListDataType& list )ι->bool{
		std::lock_guard _{ _mutex };
		let changed = _dirs.Scan( "/access/trustedCertDirs", []( const fs::path& path, vector<byte>&& der ){
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
		if( certs.empty() && _dirs.Skipped() )
			WARN( "No trusted client certificates under /access/trustedCertDirs:  {}.", _dirs.SkippedNote() );
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
	Ω verifyCertificate( UA_CertificateGroup* group, const UA_ByteString* certificate )ι->UA_StatusCode{
		let i = group==_groups[0] ? 0u : 1u;//only ever installed on the two wrapped groups.
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
			if( UATrust::LoadTrustList(fresh) ){//a cert re-issued or copied in after startup shouldn't require a restart - rescan before failing.
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
		return y;
	}

	α UATrust::Install( UA_Server& ua )ι->void{
		auto config = UA_Server_getConfig( &ua );//the server-owned copy - UA_Server_newWithConfig memsets the UAConfig it was built from.
		_groups = { &config->secureChannelPKI, &config->sessionPKI };
		for( uint i=0; i<_groups.size(); ++i ){
			auto g = _groups[i];
			if( g->verifyCertificate && g->verifyCertificate!=&verifyCertificate ){//null: a group with no verifier.  The unsecured config that built one is gone (security-matrix #5); the guard stays a guard.
				_originals[i] = g->verifyCertificate;
				g->verifyCertificate = &verifyCertificate;
			}
		}
	}
}