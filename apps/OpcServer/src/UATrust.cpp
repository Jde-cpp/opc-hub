#include "UATrust.h"
#include <mutex>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/fwk/settings.h>
#include <jde/opc/UAException.h>

#define let const auto
namespace Jde::Opc::Server{
	constexpr ELogTags _tags = ( ELogTags )EOpcLogTags::OpcCrypto;

	static std::mutex _mutex;//verifies all run on the UAServer jthread today - uncontended; kept for parity with access's _anchorMutex and against future threading changes.
	struct Entry{ fs::file_time_type MTime; vector<byte> Der; };//empty Der = unreadable; retried only on mtime change (mirrors _anchorFiles).
	static flat_map<fs::path, Entry> _files;
	static flat_set<fs::path> _missingDirs;//warned about once - a failed verify rescans, and a dir that is never created (a product not installed) would warn on each (reviews/install-issues.md, "Noise in a production log").
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
		bool changed{}, scanOk{ true };
		flat_set<fs::path> seen;
		for( const string& sdir : Settings::FindStringArray("/access/trustedCertDirs") ){
			try{
				const fs::path dir{ sdir };
				if( !fs::exists(dir) || !fs::is_directory(dir) ){//files under a vanished dir legitimately drop out of trust below.
					let level = _missingDirs.emplace( dir ).second ? ELogLevel::Warning : ELogLevel::Debug;//not inline in LOG - the macro evaluates its level twice.
					LOG( level, _tags, "Trusted certificate directory does not exist: '{}' - no client certificate is trusted from it until it does (rescanned on a failed verify).", dir.string() );
					continue;
				}
				_missingDirs.erase( dir );
				for( let& entry : fs::directory_iterator(dir) ){
					if( entry.path().extension()!=".pem" && entry.path().extension()!=".crt" )
						continue;
					seen.emplace( entry.path() );
					let mtime = entry.last_write_time();
					auto it = _files.find( entry.path() );
					if( it!=_files.end() && it->second.MTime==mtime )
						continue;
					try{
						auto der = Crypto::ReadCertificate( entry.path() );
						INFO( "{} certificate:  {}", it==_files.end() ? "Added" : "Updated", entry.path().string() );
						_files[entry.path()] = { mtime, move(der) };
					}
					catch( Exception& e ){
						e.PrependWhat( Ƒ("Could not load trusted certificate '{}'", entry.path().string()) );
						_files[entry.path()] = { mtime, {} };
					}
					changed = true;
				}
			}
			catch( const fs::filesystem_error& e ){
				CRITICAL( "Could not scan trusted certificate directory '{}': {}", sdir, e.what() );
				scanOk = false;//don't treat unlisted-because-unreadable as removed.
			}
		}
		if( scanOk ){
			erase_if( _files, [&]( const auto& kv ){
				let vanished = !seen.contains( kv.first );
				if( vanished ){
					INFO( "Removed certificate:  {}", kv.first.string() );
					changed = true;
				}
				return vanished;
			});
		}
		vector<UA_ByteString> certs;//shallow views into the cache, only used to feed UA_Array_copy.
		for( let& [_2,entry] : _files ){
			if( entry.Der.size() )
				certs.emplace_back( UA_ByteString{ entry.Der.size(), (UA_Byte*)entry.Der.data() } );
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
			if( g->verifyCertificate && g->verifyCertificate!=&verifyCertificate ){//null covers the non-ssl UA_ServerConfig_setDefault path.
				_originals[i] = g->verifyCertificate;
				g->verifyCertificate = &verifyCertificate;
			}
		}
	}
}
