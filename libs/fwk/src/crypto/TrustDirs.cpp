#include <jde/fwk/crypto/TrustDirs.h>
#include <mutex>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/fwk/settings.h>

#define let const auto
namespace Jde::Crypto{
	static std::mutex _missingMutex;
	//(noun, directory):  a directory never created - a product not installed - would otherwise warn on each rescan
	//(reviews/install-issues.md, "Noise in a production log").  Not on the scanner, which ServerTrust makes per connect, and not by
	//directory alone, which let the first list to find one missing hide it from the others (opcserver-refactor-review #4).
	static flat_set<std::pair<string,fs::path>> _missingDirs;

	TrustDirs::TrustDirs( string noun, ELogTags tags, ELogLevel unreadable )ι:
		_noun{ move(noun) },
		_tags{ tags },
		_unreadable{ unreadable }
	{}

	α TrustDirs::Scan( sv setting, const OnCertificate& onCertificate, SL sl )ι->bool{
		vector<fs::path> dirs;
		for( let& dir : Settings::FindStringArray(setting) )
			dirs.emplace_back( dir );
		return Scan( dirs, onCertificate, sl );
	}

	α TrustDirs::Scan( const vector<fs::path>& dirs, const OnCertificate& onCertificate, SL sl )ι->bool{
		bool changed{}, complete{ true };
		_skipped = 0;
		flat_set<fs::path> seen;
		for( let& dir : dirs ){
			try{
				if( !fs::is_directory(dir) ){//files under a vanished directory drop out below.
					bool first;
					{ std::lock_guard _{ _missingMutex }; first = _missingDirs.emplace( _noun, dir ).second; }
					let level = first ? ELogLevel::Warning : ELogLevel::Debug;//not inline in LOG - the macro evaluates its level twice.
					LOG( level, _tags, "Trusted certificate directory does not exist: '{}' - no {} comes from it until it does.", dir.string(), _noun );
					continue;
				}
				{ std::lock_guard _{ _missingMutex }; _missingDirs.erase( {_noun, dir} ); }
				for( let& entry : fs::directory_iterator(dir) ){
					let& path = entry.path();
					if( !IsCertificateFile(path) ){//install-issues #33:  a UA peer publishes DER, and skipping it in silence made a copied-in certificate do nothing.
						++_skipped;
						let level = FirstSkip( path ) ? ELogLevel::Warning : ELogLevel::Debug;//once per file, then quiet (m2-closing #11).
						LOG( level, _tags, "Passed over, not a certificate by its extension: '{}' - no {} comes from it.  {} are read.", path.string(), _noun, CertificateExtensions );
						continue;
					}
					seen.emplace( path );
					//not the throwing form:  a dangling link or a file gone mid-walk would end the directory's walk, its neighbours
					//unhanded.  On error it reads min(), which is stored like any time, so the read below fails once and stays quiet.
					std::error_code ec;
					let mtime = entry.last_write_time( ec );
					if( auto it = _files.find(path); it!=_files.end() && it->second==mtime )
						continue;
					_files[path] = mtime;
					changed = true;
					try{
						onCertificate( path, ReadCertificate(path, sl) );
						continue;
					}
					catch( Exception& e ){//logs itself as it goes.
						e.SetLevel( _unreadable );
						e.PrependWhat( Ƒ("Could not load '{}' - no {} comes from it:  ", path.string(), _noun) );
					}
					catch( const std::exception& e ){
						LOG( _unreadable, _tags, "Could not load '{}' - no {} comes from it:  {}", path.string(), _noun, e.what() );
					}
					onCertificate( path, {} );//a file that read before and no longer does is trusted no longer.
				}
			}
			catch( const std::exception& e ){
				LOG( _unreadable, _tags, "Could not scan trusted certificate directory '{}': {}", dir.string(), e.what() );
				complete = false;//unlisted for being unreadable is not removed.
			}
		}
		if( complete ){
			for( auto it = _files.begin(); it!=_files.end(); ){
				if( seen.contains(it->first) )
					++it;
				else{
					onCertificate( it->first, {} );
					it = _files.erase( it );
					changed = true;
				}
			}
		}
		return changed;
	}

	α TrustDirs::SkippedNote()Ι->string{
		return Ƒ( "{} file{} passed over for {} extension - {} are read", _skipped, _skipped==1 ? " was" : "s were", _skipped==1 ? "its" : "their", CertificateExtensions );
	}
}