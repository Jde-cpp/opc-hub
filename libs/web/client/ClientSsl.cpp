#include <jde/web/client/ClientSsl.h>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <openssl/ssl.h>
#include <jde/fwk/crypto/TrustStore.h>

#define let const auto

namespace Jde::Web::Client::Ssl{
	constexpr ELogTags _tags{ ELogTags::Http | ELogTags::Client };

	static shared_mutex _mutex;
	static vector<fs::path> _anchors;
	static up<ssl::context> _shared;
	//Configured anchors that were not on disk when the shared context was built - a peer's certificate this process started
	//ahead of, the OpcServer reading the hub's caFile 139 ms before the hub's first start wrote it (install-issues #12).  The
	//context was cached without them, so every later connection failed its verify however often the caller retried.  Context()
	//rebuilds once one of these exists.  Only absent files are recorded: a present-but-unreadable file would rebuild forever.
	static vector<fs::path> _missing;
	//Contexts a rebuild replaced.  The SSL objects of live streams hold their SSL_CTX either way, but keeping the wrappers
	//costs nothing and leaves no question of freeing one under a handshake.
	static vector<up<ssl::context>> _retired;

	α VerifyPeer()ι->bool{
		static const bool y = Settings::FindBool( "/web/client/ssl/verifyPeer" ).value_or( true );
		return y;
	}

	Ω load( ssl::context& ctx, const fs::path& pem, vector<fs::path>* missing )ι->void{
		beast::error_code ec;
		ctx.load_verify_file( pem.string(), ec );
		if( ec ){//not fatal: the OS roots may still cover the peer, and failing closed here would take the process down at startup.
			CodeException{ static_cast<std::error_code>(ec), _tags, Ƒ("Could not load trust anchor '{}'", pem.string()), ELogLevel::Error };
			std::error_code fsec;
			if( missing && !fs::exists(pem, fsec) )
				missing->push_back( pem );
		}
		else
			TRACET( _tags, "Loaded trust anchor '{}'.", pem.string() );
	}

	//_mutex held by the caller.  `missing` collects the configured anchors that were not on disk (see _missing).
	Ω make( vector<fs::path>* missing=nullptr )ι->ssl::context{
		ssl::context ctx{ ssl::context::tlsv12_client };
		if( !VerifyPeer() ){
			WARNT( _tags, "/web/client/ssl/verifyPeer is false - server certificates are not checked, so any peer can impersonate any host." );
			ctx.set_verify_mode( ssl::verify_none );
			return ctx;
		}
		//the OS roots, for public hosts like the google jwks endpoint.  ctx.set_default_verify_paths() is not enough: OpenSSL has
		//no notion of the windows certificate store, so there it resolves to a compiled-in OPENSSLDIR that does not exist and
		//leaves the context with no anchors at all - silently, because registering a lookup for a missing directory succeeds.
		//TrustStore snapshots the windows ROOT store and keeps the default-path lookups on linux.  set1 up-refs, so the context
		//owns the store once the local drops its reference, and the caFile/anchor loads below land in that context's own copy.
		try{
			Crypto::TrustStore roots{};
			::SSL_CTX_set1_cert_store( ctx.native_handle(), roots.Native() );
			TRACET( _tags, "Loaded {} OS trust anchors.", roots.CertCount() );
		}catch( const std::exception& e ){//not fatal: an internal-only client needs no public roots, and failing closed here would take the process down at startup.
			WARNT( _tags, "Could not load the OS trust store ({}) - public hosts will fail verification unless /web/client/ssl/caFile names an anchor.", e.what() );
		}
		if( let caFile = Settings::FindPath("/web/client/ssl/caFile"); caFile )
			load( ctx, *caFile, missing );
		for( let& anchor : _anchors )
			load( ctx, anchor, missing );
		ctx.set_verify_mode( ssl::verify_peer );
		return ctx;
	}

	α MakeContext()ι->ssl::context{
		sl _{ _mutex };
		return make();
	}

	α Context()ι->ssl::context&{
		ul _{ _mutex };
		if( _shared && _missing.size() ){
			std::error_code ec;
			if( auto p = find_if( _missing, [&](let& pem){ return fs::exists(pem, ec); } ); p!=_missing.end() ){
				INFOT( _tags, "Trust anchor '{}' exists now - rebuilding the client TLS context that was built without it.", p->string() );
				_retired.push_back( move(_shared) );
			}
		}
		if( !_shared ){
			_missing.clear();
			_shared = mu<ssl::context>( make(&_missing) );
		}
		return *_shared;
	}

	α AddTrustAnchor( fs::path pem )ι->void{
		ul _{ _mutex };
		_anchors.emplace_back( move(pem) );
		//the shared context may already be built - a pooled session can have connected before this anchor was known, which is
		//exactly what the tests do when they restart the mock server on a fresh self-signed cert.  X509_STORE accepts additions
		//after construction, so add it rather than rebuilding under live connections.  Register anchors at startup in a server:
		//mutating a context that handshakes are running against is not something OpenSSL promises.
		if( _shared && VerifyPeer() )
			load( *_shared, _anchors.back(), &_missing );
	}

	α SetVerifyHost( beast::ssl_stream<beast::tcp_stream>& stream, str host )ι->void{
		if( !VerifyPeer() )
			return;
		beast::error_code ec;
		//host_name_verification alone: without it a peer holding any certificate we trust could answer for any host - trusting an
		//anchor is not the same as accepting whoever presents it.  wrapped only to say *why* on rejection; a bare handshake
		//failure surfaces as "unspecified system error", which is not enough to tell a bad name from a missing anchor.
		stream.set_verify_callback( [name=string{host}]( bool preverified, ssl::verify_context& ctx )ι->bool {
			let ok = ssl::host_name_verification{name}( preverified, ctx );
			if( !ok ){
				let err = ::X509_STORE_CTX_get_error( ctx.native_handle() );
				char subject[256]{};
				if( auto cert = ::X509_STORE_CTX_get_current_cert(ctx.native_handle()); cert )
					::X509_NAME_oneline( ::X509_get_subject_name(cert), subject, sizeof(subject) );
				WARNT( _tags, "tls verify failed for '{}' at depth {}: ({}){} - peer presented '{}'", name, ::X509_STORE_CTX_get_error_depth(ctx.native_handle()), err, ::X509_verify_cert_error_string(err), subject );
			}
			return ok;
		}, ec );
		if( ec )
			CodeException{ static_cast<std::error_code>(ec), _tags, ELogLevel::Error };
	}
}
