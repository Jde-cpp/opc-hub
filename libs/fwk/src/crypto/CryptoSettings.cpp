#include <jde/fwk/crypto/CryptoSettings.h>
#include <jde/fwk/crypto/PrivateKey.h>
#include <jde/fwk/io/file.h>
#include <jde/fwk/io/json.h>
#include <jde/fwk/settings.h>

#define let const auto

namespace Jde{
	namespace Crypto{
		Ω defaultCommonName()ι->string{
			string cn = Settings::FindString("/instanceName").value_or( string{Process::ProductName()} );
			if constexpr( _debug )
				cn+= ".debug";
			cn+= ".webServer";
			return cn;
		}

		//certInstance is the OPC Slug (settable via createServerConnection) and fileName the subject CN - both become
		//part of a file name, so separators must not survive.  Substituted, not rejected: getPath's callers are noexcept.
		Ω safeComponent( sv x )ι->string{
			string y{ x };
			for( auto& c : y ){
				if( c=='/' || c=='\\' || c==':' || c=='*' || c=='?' || c=='"' || c=='<' || c=='>' || c=='|' || (unsigned char)c<0x20 )
					c = '_';
			}
			return y;
		}

		Ω getPath( const jobject& settings, str jpath, fs::path subDir, sv fileName, sv certInstance={} )ι->fs::path{
			auto fqFileName = [certInstance]( fs::path base )ι->fs::path {
				if( certInstance.size() )
					base += Ƒ( ".{}.pem", safeComponent(certInstance) );
				return base;
			};

			auto filePath = Json::FindString( settings, jpath );
			if( filePath )
				return fqFileName( *filePath );//an operator-supplied whole path - its separators are meant.

			auto productName = Json::FindString( settings, "productName" ).value_or( string{Process::ProductName()} );
			auto parent = Process::ProgramDataFolder()/Process::CompanyRootDir()/safeComponent(productName)/"ssl";
			if( !subDir.empty() )
				parent /= subDir;
			parent /= safeComponent( fileName );
			if( certInstance.empty() )
				parent += ".pem";
			return fqFileName( parent );
		}

		CertificateSettings::CertificateSettings( const jobject& settings, sv certInstance )ι:
			CommonName{ Json::FindString(settings, "commonName").value_or(string{defaultCommonName()}) },
			FileStem{ Json::FindString(settings, "fileName").value_or(CommonName) },
			Path{ getPath(settings, "path", "certs", FileStem, certInstance) },
			//unconfigured defaults to the localhost pair: a generated server cert with no SAN is guaranteed to fail
			//host_name_verification, and the hand-spelled per-config line kept getting forgotten or misspelled.  An
			//explicit "" opts out; any configured value overrides.  The default belongs HERE, not in IssueCertificate -
			//ReissueReason reads this field, and a generator-only default would mismatch the comparison and re-issue on every start.
			SubjectAltName{ Json::FindString(settings, "subjectAltName").value_or("DNS:localhost,IP:127.0.0.1") },
			Country{ Json::FindString(settings, "country").value_or("") },
			Company{ Json::FindString(settings, "company").value_or("Jde-Cpp") },
			Managed{ Json::FindBool(settings, "managed").value_or(true) }{
			ASSERT( CommonName.size() && CommonName!="localhost" );
		}

		PrivateKeySettings::PrivateKeySettings( const jobject& settings, sv defaultFileName )ι:
			Path{ getPath(settings, "path", "private", defaultFileName) },
			Passcode{ Json::FindString(settings, "passcode").value_or("") }
		{}

		CryptoSettings::PublicKeyPath::PublicKeyPath( const jobject& settings, sv defaultFileName )ι:
			Path{ getPath(settings, "path", "public", defaultFileName) }
		{}

		α CryptoSettings::PublicKeyPath::Read( SL sl )Ε->Crypto::PublicKey{
			return ReadPublicKey( Path, sl );
		}

		CryptoSettings::CryptoSettings( str settingsPath )ι:
			CryptoSettings{ Settings::FindDefaultObject(settingsPath) }
		{}

		//ssl-level productName is the default tree for every path derived from the block (the soak's issuedCerts block sets it
		//once for all three); a sub-object's own still wins.  Without this only `dh` honored the ssl level, so
		//sibling servers configured with different productNames derived one shared cert path from Process::ProductName().
		Ω withDefaultProductName( jobject sub, const jobject& parent )ι->jobject{
			if( let p = parent.if_contains("productName"); p && !sub.contains("productName") )
				sub["productName"] = *p;
			return sub;
		}

		CryptoSettings::CryptoSettings( const jobject& settings, sv certInstance )ι:
			Certificate{ withDefaultProductName(Json::FindDefaultObject(settings, "certificate"), settings), certInstance },
			PrivateKey{ withDefaultProductName(Json::FindDefaultObject(settings, "privateKey"), settings), Certificate.FileStem },
			PublicKey{ withDefaultProductName(Json::FindDefaultObject(settings, "publicKey"), settings), Certificate.FileStem },
			DhPath{ getPath(settings, "dh", "", "dh") }
		{}

		α CryptoSettings::CreateDirectories()Ε->void{
			IO::CreateDirectories( Certificate.Path.parent_path() );
			IO::CreateDirectories( PrivateKey.Path.parent_path() );
			IO::CreateDirectories( PublicKey.Path.parent_path() );
			if( !DhPath.empty() )
				IO::CreateDirectories( DhPath.parent_path() );
		}
	}

	α Crypto::ReadPrivateKey( const PrivateKeySettings& settings, SL sl )ε->vector<byte>{
		return PrivateKey{ settings.Path, settings.Passcode, sl }.Der( sl );
	}
}