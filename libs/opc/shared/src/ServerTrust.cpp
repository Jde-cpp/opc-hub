#include <jde/opc/ServerTrust.h>
#include <open62541/client_config_default.h>
#include <open62541/plugin/certificategroup_default.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/io/file.h>
#include <jde/fwk/process/process.h>
#include <jde/fwk/str.h>
#include <jde/fwk/crypto/Certificate.h>
#include <jde/fwk/crypto/TrustDirs.h>
#include <jde/fwk/crypto/TrustStore.h>

#define let const auto
namespace Jde::Opc{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::OpcCrypto };
	namespace{
		//the group's context - owned by the group, freed by clear() when open62541 clears the client config.
		struct Context final{
			Context( Jde::Handle h, string url, sv settingsRoot, string expectedUri, fs::path rejectedDir )ε: Store{false}, Handle{h}, Url{move(url)}, SettingsRoot{settingsRoot}, ExpectedUri{move(expectedUri)}, RejectedDir{move(rejectedDir)}{}
			Crypto::TrustStore Store;//anchors only - see the header.
			Jde::Handle Handle;
			string Url;
			string SettingsRoot;//the caller's own settings, named in the rejection - see the header.
			string ExpectedUri;//the chosen endpoint's applicationUri (the connection's certificateUri), for the SAN-URI warning;  empty skips it.
			fs::path RejectedDir;//where a rejected certificate is written, <thumbprint>.der.
			uint Anchors{};
			string Skipped;//TrustDirs::SkippedNote() when files in the directories carried no certificate extension - named in the rejection when nothing loaded (install-issues #33).
			string Rejection;//the last failure;  written by verifyCertificate and read by StateCallback, both on the client's strand.
			string RejectedThumbprint;//the SHA-1 of the last rejected certificate, the <thumbprint>.der it wrote;  read by StateCallback with Rejection.
		};
		α context( const UA_CertificateGroup& g )ι->Context*{ return (Context*)g.context; }
		α anchorsNote( const Context& c )ι->string{ return Ƒ( "{} trusted certificate{}", c.Anchors, c.Anchors==1 ? "" : "s" ); }

		//<thumbprint>.der once per certificate for the whole run:  a server that keeps presenting the same rejected certificate -
		//every reconnect, every client of the slug - writes it once, as UATrust's rejected list does on the server side.
		std::mutex _rejectedWrittenMutex;
		flat_set<string> _rejectedWritten;

		//The url's host, opc.tcp://<host>:<port>/<path>, for the SAN host/address warning - empty when it will not parse.
		Ω urlHost( str url )ι->string{
			UA_String host{}, path{}; UA_UInt16 port{};
			const UA_String ua{ url.size(), (UA_Byte*)url.data() };
			if( UA_parseEndpointUrl(&ua, &host, &port, &path) || !host.length )
				return {};
			return string{ (const char*)host.data, host.length };
		}
		//The DNS and IP entries of a SAN in openssl config syntax ("URI:x,DNS:host,IP:1.2.3.4"), their prefixes stripped:  what the
		//certificate says it vouches for, to hold the url's host against (spec *Trust*).
		Ω sanHosts( sv subjectAltName )ι->vector<string>{
			vector<string> y;
			for( uint i=0; i<subjectAltName.size(); ){
				let end = subjectAltName.find( ',', i );
				let entry = subjectAltName.substr( i, end==sv::npos ? sv::npos : end-i );
				if( entry.starts_with("DNS:") )
					y.emplace_back( entry.substr(4) );
				else if( entry.starts_with("IP:") )
					y.emplace_back( entry.substr(3) );
				i = end==sv::npos ? subjectAltName.size() : end+1;
			}
			return y;
		}
		//The *Trust* rule (spec, *Open questions* #14):  a trusted server certificate whose SAN URI is not the endpoint's
		//applicationUri, or whose SAN names neither the url's host nor its address, is logged at Warning and not refused - a
		//self-signed PLC certificate often names neither, and the trust list is the gate.  Best effort:  a certificate that will
		//not parse is left to the connect, which fails on it with its own message.
		Ω warnOnUriMismatch( const Context& c, std::span<const byte> der )ι->void{
			optional<Crypto::Certificate> cert;
			try{ cert.emplace( der ); }catch( const std::exception& ){ return; }
			let sanUri = cert->SanUri();
			if( c.ExpectedUri.size() && sanUri!=c.ExpectedUri )
				WARNT( _tags, "[{}]'{}' is trusted, but its certificate's subjectAltName URI '{}' is not the applicationUri '{}' the endpoint advertises - a server holds a client's certificate URI to its own applicationUri, so this may be refused as BadCertificateUriInvalid.", hex(c.Handle), c.Url, sanUri, c.ExpectedUri );
			let host = urlHost( c.Url );
			if( host.size() ){
				let hosts = sanHosts( cert->SubjectAltName );
				let lower = Str::ToLower( host );
				let named = std::ranges::any_of( hosts, [&lower]( str h ){ return Str::ToLower(h)==lower; } );
				if( !named )
					WARNT( _tags, "[{}]'{}' is trusted, but its certificate's subjectAltName names neither the host nor the address '{}' of the url - {} - a strict client would reject it.", hex(c.Handle), c.Url, host, hosts.empty() ? "it carries no DNS or IP name" : Ƒ("it names {}", Str::Join(hosts, ", ")) );
			}
		}
		//The rejected certificate written to RejectedDir as <thumbprint>.der (the SHA-1, the thumbprint a UA client shows), once per
		//certificate and run, and its thumbprint recorded on the context for the client to keep per slug.  Junk that is no
		//certificate - an endpoint carrying none, a garbage senderCertificate - has no thumbprint and nothing to write.
		Ω recordRejected( Context& c, const UA_ByteString* certificate )ι->void{
			if( c.RejectedDir.empty() || !certificate || !certificate->length )
				return;
			try{
				const std::span<const byte> der{ (const byte*)certificate->data, certificate->length };
				const Crypto::Certificate parsed{ der };
				c.RejectedThumbprint = parsed.Thumbprint;
				let path = c.RejectedDir/Ƒ( "{}.der", parsed.Thumbprint );
				{
					std::lock_guard _{ _rejectedWrittenMutex };
					if( !_rejectedWritten.emplace(parsed.Thumbprint).second )
						return;//written this run already.
				}
				if( fs::exists(path) )
					return;
				fs::create_directories( c.RejectedDir );
				Crypto::WriteCertificate( path, der, SRCE_CUR );
				INFOT( _tags, "[{}]The certificate '{}' presented is written to '{}' - trust it from '{}' to admit the server.", hex(c.Handle), c.Url, path.string(), c.RejectedDir.string() );
			}
			catch( const std::exception& e ){
				DBG( "[{}]A rejected certificate was not written to '{}':  {}", hex(c.Handle), c.RejectedDir.string(), e.what() );
			}
		}

		//C shim in the group vtable (the UATrust::verifyCertificate pattern:  try/catch -> status).
		Ω verifyCertificate( UA_CertificateGroup* g, const UA_ByteString* certificate )ι->UA_StatusCode{
			auto& c = *context( *g );
			try{
				THROW_IF( !certificate || !certificate->length, "the endpoint carries no certificate" );
				let der = std::span<const byte>{(const byte*)certificate->data, certificate->length};
				c.Store.Verify( der );
				c.Rejection.clear();
				c.RejectedThumbprint.clear();
				warnOnUriMismatch( c, der );//trusted, but its SAN may still not match the endpoint's uri or the url's host (spec *Trust*).
				return UA_STATUSCODE_GOOD;
			}
			catch( const std::exception& e ){
				//"0 trusted certificates loaded" alone reads as "the directory is empty" - the operator who *did* copy the
				//certificate in is then told to do the thing they just did (install-issues #33), so say what was passed over.
				let skipped = c.Anchors || c.Skipped.empty() ? string{} : ", "+c.Skipped;
				c.Rejection = Ƒ( "server certificate for '{}' rejected: {} ({} loaded from {}/trustedCertDirs{}) - add the server's certificate to one of those directories, or set {}/verifyServerCertificate=false", c.Url, e.what(), anchorsNote(c), c.SettingsRoot, skipped, c.SettingsRoot );
				ERRT( _tags, "[{}]{}", hex(c.Handle), c.Rejection );
				recordRejected( c, certificate );//the <thumbprint>.der the admin trusts, and the thumbprint the client keeps per slug.
				return UA_STATUSCODE_BADCERTIFICATEUNTRUSTED;
			}
		}
		Ω clear( UA_CertificateGroup* g )ι->void{
			delete context( *g );
			g->context = nullptr;
			UA_NodeId_clear( &g->certificateGroupId );
			g->verifyCertificate = nullptr;
			g->clear = nullptr;
		}
		Ω loadAnchors( Context& c, const vector<fs::path>& dirs, SL sl )ι->void{
			Crypto::TrustDirs scan{ "trusted server certificate", _tags, ELogLevel::Warning };//a new one per connect, so every file is read again.
			scan.Scan( dirs, [&]( const fs::path&, vector<byte>&& der ){
				if( der.empty() )
					return;//one unreadable file must not empty the trust list.
				c.Store.AddCertificate( der, sl );
				++c.Anchors;
			}, sl );
			if( scan.Skipped() )
				c.Skipped = scan.SkippedNote();
		}
		std::mutex _overrideMutex;
		optional<vector<fs::path>> _override;//the test seam - see the header.
	}

	α ServerTrust::Enabled( sv settingsRoot )ι->bool{ return Settings::FindBool( Ƒ("{}/verifyServerCertificate", settingsRoot) ).value_or(true); }
	Ω settingDirs( sv settingsRoot )ι->vector<fs::path>{
		vector<fs::path> dirs;
		for( let& dir : Settings::FindStringArray(Ƒ("{}/trustedCertDirs", settingsRoot)) )
			dirs.emplace_back( dir );
		return dirs;
	}
	α ServerTrust::RejectedDir( sv settingsRoot )ι->fs::path{
		return Settings::FindPath( Ƒ("{}/rejectedCertDir", settingsRoot) ).value_or( Process::AppDataFolder()/"ssl"/"servers"/"rejected" );
	}
	Ω createOwn( const fs::path& configured, sv settingsRoot, sv note )ι->void{//creates `configured` when it lies under this process's own data folder;  another product's directory is that product's to create (see the header).
		let own = Process::AppDataFolder().lexically_normal();
		let dir = configured.lexically_normal();
		std::error_code ec;
		if( fs::exists(dir, ec) )
			return;
		let [ownEnd, _] = std::mismatch( own.begin(), own.end(), dir.begin(), dir.end() );
		if( ownEnd!=own.end() )//another product's directory - see the header.
			return;
		fs::create_directories( dir, ec );
		if( ec )
			WARNT( _tags, "Could not create the {} directory '{}': {}", note, dir.string(), ec.message() );
		if( !ec )//not `else`: the log macros expand to their own `if`.
			INFOT( _tags, "Created '{}' ({}/{}) - {}.", dir.string(), settingsRoot, note, note=="trustedCertDirs" ? Ƒ("copy an OPC server's certificate ({}) here to trust it", Crypto::CertificateExtensions) : "a rejected server certificate is written here" );
	}
	α ServerTrust::EnsureDirs( sv settingsRoot )ι->void{
		for( let& configured : settingDirs(settingsRoot) )
			createOwn( configured, settingsRoot, "trustedCertDirs" );
		createOwn( RejectedDir(settingsRoot), settingsRoot, "rejectedCertDir" );
	}

	α ServerTrust::OverrideTrustedCertDirs( optional<vector<fs::path>> dirs )ι->void{
		std::lock_guard _{ _overrideMutex };
		_override = move( dirs );
	}
	α ServerTrust::Install( UA_ClientConfig& config, sv settingsRoot, Jde::Handle h, str url, sv expectedUri, SL sl )ε->void{
		//On the optional, not on what it holds:  an override of no directories is the plainest way to say "this client trusts
		//nothing", and testing the copy for empty read it as "no override" - the production list came back in silence, and a
		//test written that way passed for trusting the very anchors it meant to leave out (reviews/m2-closing.md #12).
		optional<vector<fs::path>> overridden;
		{
			std::lock_guard _{ _overrideMutex };
			overridden = _override;
		}
		Install( config, Enabled(settingsRoot), overridden ? *overridden : settingDirs(settingsRoot), h, url, settingsRoot, expectedUri, sl );
	}
	α ServerTrust::Install( UA_ClientConfig& config, bool verify, const vector<fs::path>& dirs, Jde::Handle h, str url, sv settingsRoot, sv expectedUri, SL sl )ε->void{
		auto& g = config.certificateVerification;
		if( !verify ){
			UA_CertificateGroup_AcceptAll( &g );//clears whatever was there first.
			WARNT( _tags, "[{}]Server-certificate verification is off ({}/verifyServerCertificate) - any certificate '{}' presents is accepted.", hex(h), settingsRoot, url );
			return;
		}
		auto c = mu<Context>( h, url, settingsRoot, string{expectedUri}, RejectedDir(settingsRoot) );
		loadAnchors( *c, dirs, sl );
		if( g.clear )
			g.clear( &g );//AcceptAll, or an earlier Install.
		g.certificateGroupId = UA_NS0ID( SERVERCONFIGURATION_CERTIFICATEGROUPS_DEFAULTAPPLICATIONGROUP );
		g.logging = config.logging;
		g.verifyCertificate = &verifyCertificate;
		g.clear = &clear;
		g.getTrustList = nullptr; g.setTrustList = nullptr; g.addToTrustList = nullptr; g.removeFromTrustList = nullptr; g.getRejectedList = nullptr; g.getCertificateCrls = nullptr;//as UA_CertificateGroup_AcceptAll leaves them - the client only ever calls verifyCertificate and clear.
		if( c->Anchors )
			INFOT( _tags, "[{}]Verifying '{}'s certificate against {} from {}/trustedCertDirs.", hex(h), url, anchorsNote(*c), settingsRoot );
		//A config from before the split (security-matrix #3) names its servers under /access/trustedCertDirs alone:  say where they belong now.
		let moved = dirs.empty() && !Settings::FindStringArray( "/access/trustedCertDirs" ).empty();
		let skipped = c->Skipped.size() ? "  "+c->Skipped+"." : string{};
		if( !c->Anchors )//not `else`: the log macros expand to their own `if`.
			WARNT( _tags, "[{}]No trusted certificates under {}/trustedCertDirs ({} director{}) - every certificate '{}' presents will be rejected.{}{}", hex(h), settingsRoot, dirs.size(), dirs.size()==1 ? "y" : "ies", url, skipped, moved ? "  /access/trustedCertDirs is the enrollment anchors' list and is no longer read for this - name the directories under this setting." : "" );
		g.context = c.release();
	}
	α ServerTrust::Rejection( const UA_ClientConfig& config )ι->string{
		let& g = config.certificateVerification;
		return g.verifyCertificate==&verifyCertificate && g.context ? context(g)->Rejection : string{};
	}
	α ServerTrust::RejectedThumbprint( const UA_ClientConfig& config )ι->string{
		let& g = config.certificateVerification;
		return g.verifyCertificate==&verifyCertificate && g.context ? context(g)->RejectedThumbprint : string{};
	}
	α ServerTrust::AnchorCount( const UA_ClientConfig& config )ι->uint{
		let& g = config.certificateVerification;
		return g.verifyCertificate==&verifyCertificate && g.context ? context(g)->Anchors : 0;
	}
}