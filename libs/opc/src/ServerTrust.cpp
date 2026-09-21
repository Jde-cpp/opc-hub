#include <jde/opc/ServerTrust.h>
#include <open62541/plugin/certificategroup_default.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/process/process.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/fwk/crypto/TrustStore.h>

#define let const auto
namespace Jde::Opc{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::OpcCrypto };
	namespace{
		//the group's context - owned by the group, freed by clear() when open62541 clears the client config.
		struct Context final{
			Context( Jde::Handle h, string url, sv settingsRoot )ε: Store{false}, Handle{h}, Url{move(url)}, SettingsRoot{settingsRoot}{}
			Crypto::TrustStore Store;//anchors only - see the header.
			Jde::Handle Handle;
			string Url;
			string SettingsRoot;//the caller's own settings, named in the rejection - see the header.
			uint Anchors{};
			uint Skipped{};//files in the directories that carried no certificate extension - named in the rejection when nothing loaded (install-issues #33).
			string Rejection;//the last failure;  written by verifyCertificate and read by StateCallback, both on the client's strand.
		};
		α context( const UA_CertificateGroup& g )ι->Context*{ return (Context*)g.context; }

		//C shim in the group vtable (the UATrust::verifyCertificate pattern:  try/catch -> status).
		Ω verifyCertificate( UA_CertificateGroup* g, const UA_ByteString* certificate )ι->UA_StatusCode{
			auto& c = *context( *g );
			try{
				THROW_IF( !certificate || !certificate->length, "the endpoint carries no certificate" );
				c.Store.Verify( std::span<const byte>{(const byte*)certificate->data, certificate->length} );
				c.Rejection.clear();
				return UA_STATUSCODE_GOOD;
			}
			catch( const std::exception& e ){
				//"0 trusted certificates loaded" alone reads as "the directory is empty" - the operator who *did* copy the
				//certificate in is then told to do the thing they just did (install-issues #33), so say what was passed over.
				let skipped = c.Anchors || !c.Skipped ? string{} : Ƒ( ", {} file{} skipped for {} extension - {} are read", c.Skipped, c.Skipped==1 ? " was" : "s were", c.Skipped==1 ? "its" : "their", Crypto::CertificateExtensions );
				c.Rejection = Ƒ( "server certificate for '{}' rejected: {} ({} trusted certificate{} loaded from {}/trustedCertDirs{}) - add the server's certificate to one of those directories, or set {}/verifyServerCertificate=false", c.Url, e.what(), c.Anchors, c.Anchors==1 ? "" : "s", c.SettingsRoot, skipped, c.SettingsRoot );
				ERRT( _tags, "[{}]{}", hex(c.Handle), c.Rejection );
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
		std::mutex _missingMutex;
		flat_set<fs::path> _missingDirs;//warned about once:  the list is read on every connect, and a directory that is never created - another product's, not installed - would otherwise warn on each (install-issues, "Noise in a production log").
		Ω loadAnchors( Context& c, const vector<fs::path>& dirs, SL sl )ι->void{
			for( let& dir : dirs ){
				try{
					if( !fs::exists(dir) || !fs::is_directory(dir) ){
						bool first;
						{ std::lock_guard _{ _missingMutex }; first = _missingDirs.emplace( dir ).second; }
						let level = first ? ELogLevel::Warning : ELogLevel::Debug;//not inline in LOG - the macro evaluates its level twice.
						LOG( level, _tags, "[{}]Trusted certificate directory does not exist: '{}' - no server certificate is trusted from it until it does.", hex(c.Handle), dir.string() );
						continue;
					}
					{ std::lock_guard _{ _missingMutex }; _missingDirs.erase( dir ); }
					for( let& entry : fs::directory_iterator(dir) ){
						if( !Crypto::IsCertificateFile(entry.path()) ){
							++c.Skipped;
							let level = Crypto::FirstSkip( entry.path() ) ? ELogLevel::Warning : ELogLevel::Debug;//once per file, then quiet:  this scan runs on every connect - see FirstSkip (m2-closing #11).  Not inline in LOG - the macro evaluates its level twice.
							LOG( level, _tags, "[{}]Passed over, not a certificate by its extension: '{}' - no server is trusted from it.  {} are read.", hex(c.Handle), entry.path().string(), Crypto::CertificateExtensions );
							continue;
						}
						try{
							c.Store.AddCertificate( Crypto::ReadCertificate(entry.path(), sl), sl );
							++c.Anchors;
						}
						catch( const std::exception& e ){//one unreadable file must not empty the trust list.
							WARNT( _tags, "[{}]Could not load trusted certificate '{}': {}", hex(c.Handle), entry.path().string(), e.what() );
						}
					}
				}
				catch( const fs::filesystem_error& e ){
					WARNT( _tags, "[{}]Could not scan trusted certificate directory '{}': {}", hex(c.Handle), dir.string(), e.what() );
				}
			}
		}
	}

	α ServerTrust::Enabled( sv settingsRoot )ι->bool{ return Settings::FindBool( Ƒ("{}/verifyServerCertificate", settingsRoot) ).value_or(true); }
	Ω settingDirs( sv settingsRoot )ι->vector<fs::path>{
		vector<fs::path> dirs;
		for( let& dir : Settings::FindStringArray(Ƒ("{}/trustedCertDirs", settingsRoot)) )
			dirs.emplace_back( dir );
		return dirs;
	}
	α ServerTrust::EnsureDirs( sv settingsRoot )ι->void{
		let own = Process::AppDataFolder().lexically_normal();
		for( let& configured : settingDirs(settingsRoot) ){
			let dir = configured.lexically_normal();
			std::error_code ec;
			if( fs::exists(dir, ec) )
				continue;
			let [ownEnd, _] = std::mismatch( own.begin(), own.end(), dir.begin(), dir.end() );
			if( ownEnd!=own.end() )//another product's directory - see the header.
				continue;
			fs::create_directories( dir, ec );
			if( ec )
				WARNT( _tags, "Could not create the trusted server certificate directory '{}': {}", dir.string(), ec.message() );
			if( !ec )//not `else`: the log macros expand to their own `if`.
				INFOT( _tags, "Created '{}' ({}/trustedCertDirs) - copy an OPC server's certificate ({}) here to trust it.", dir.string(), settingsRoot, Crypto::CertificateExtensions );
		}
	}

	static std::mutex _overrideMutex;
	static optional<vector<fs::path>> _override;//the test seam - see the header.
	α ServerTrust::OverrideTrustedCertDirs( optional<vector<fs::path>> dirs )ι->void{
		std::lock_guard _{ _overrideMutex };
		_override = move( dirs );
	}
	α ServerTrust::Install( UA_ClientConfig& config, sv settingsRoot, Jde::Handle h, str url, SL sl )ε->void{
		//On the optional, not on what it holds:  an override of no directories is the plainest way to say "this client trusts
		//nothing", and testing the copy for empty read it as "no override" - the production list came back in silence, and a
		//test written that way passed for trusting the very anchors it meant to leave out (reviews/m2-closing.md #12).
		optional<vector<fs::path>> overridden;
		{
			std::lock_guard _{ _overrideMutex };
			overridden = _override;
		}
		Install( config, Enabled(settingsRoot), overridden ? *overridden : settingDirs(settingsRoot), h, url, settingsRoot, sl );
	}
	α ServerTrust::Install( UA_ClientConfig& config, bool verify, const vector<fs::path>& dirs, Jde::Handle h, str url, sv settingsRoot, SL sl )ε->void{
		auto& g = config.certificateVerification;
		if( !verify ){
			UA_CertificateGroup_AcceptAll( &g );//clears whatever was there first.
			WARNT( _tags, "[{}]Server-certificate verification is off ({}/verifyServerCertificate) - any certificate '{}' presents is accepted.", hex(h), settingsRoot, url );
			return;
		}
		auto c = mu<Context>( h, url, settingsRoot );
		loadAnchors( *c, dirs, sl );
		if( g.clear )
			g.clear( &g );//AcceptAll, or an earlier Install.
		g.certificateGroupId = UA_NS0ID( SERVERCONFIGURATION_CERTIFICATEGROUPS_DEFAULTAPPLICATIONGROUP );
		g.logging = config.logging;
		g.verifyCertificate = &verifyCertificate;
		g.clear = &clear;
		g.getTrustList = nullptr; g.setTrustList = nullptr; g.addToTrustList = nullptr; g.removeFromTrustList = nullptr; g.getRejectedList = nullptr; g.getCertificateCrls = nullptr;//as UA_CertificateGroup_AcceptAll leaves them - the client only ever calls verifyCertificate and clear.
		if( c->Anchors )
			INFOT( _tags, "[{}]Verifying '{}'s certificate against {} trusted certificate{} from {}/trustedCertDirs.", hex(h), url, c->Anchors, c->Anchors==1 ? "" : "s", settingsRoot );
		//A config from before the split (security-matrix #3) names its servers under /access/trustedCertDirs alone:  say where they belong now.
		let moved = dirs.empty() && !Settings::FindStringArray( "/access/trustedCertDirs" ).empty();
		//#33:  the directories are not empty, they hold files this build would not read - the one thing the operator cannot see.
		let skipped = c->Skipped ? Ƒ( "  {} file{} passed over for {} extension - {} are read.", c->Skipped, c->Skipped==1 ? " was" : "s were", c->Skipped==1 ? "its" : "their", Crypto::CertificateExtensions ) : string{};
		if( !c->Anchors )//not `else`: the log macros expand to their own `if`.
			WARNT( _tags, "[{}]No trusted certificates under {}/trustedCertDirs ({} director{}) - every certificate '{}' presents will be rejected.{}{}", hex(h), settingsRoot, dirs.size(), dirs.size()==1 ? "y" : "ies", url, skipped, moved ? "  /access/trustedCertDirs is the enrollment anchors' list and is no longer read for this - name the directories under this setting." : "" );
		g.context = c.release();
	}
	α ServerTrust::Rejection( const UA_ClientConfig& config )ι->string{
		let& g = config.certificateVerification;
		return g.verifyCertificate==&verifyCertificate && g.context ? context(g)->Rejection : string{};
	}
	α ServerTrust::AnchorCount( const UA_ClientConfig& config )ι->uint{
		let& g = config.certificateVerification;
		return g.verifyCertificate==&verifyCertificate && g.context ? context(g)->Anchors : 0;
	}
}
