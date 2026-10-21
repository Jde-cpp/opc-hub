#include <jde/fwk/crypto/TrustDirs.h>
#include <jde/fwk/log/MemoryLog.h>

#define let const auto
namespace Jde::Crypto{
	//opcserver-refactor-review #4:  a directory missing from two lists warns for each, once.  A second scanner for a list already
	//warned - ServerTrust makes one per connect - says it at Debug; another list's first scan still warns.
	TEST( TrustDirsTests, AMissingDirectoryWarnsOncePerList ){
		let dir = fs::temp_directory_path()/"TrustDirsTests-missing";
		fs::remove_all( dir );
		Logging::ClearMemory();
		let warnings = [&dir]( sv noun )->uint{
			return Logging::Find( [&]( const Logging::Entry& e ) {
				let message = e.Message();
				return e.Level==ELogLevel::Warning && message.contains( dir.string() ) && message.contains( noun );
			} ).size();
		};
		const vector<fs::path> dirs{ dir };
		let none = []( const fs::path&, vector<byte>&& ){};
		TrustDirs{ "test anchor", ELogTags::Test, ELogLevel::Warning }.Scan( dirs, none );
		TrustDirs{ "test anchor", ELogTags::Test, ELogLevel::Warning }.Scan( dirs, none );
		TrustDirs{ "test server certificate", ELogTags::Test, ELogLevel::Warning }.Scan( dirs, none );
		EXPECT_EQ( warnings("test anchor"), 1u ) << "a list warned again for a directory it had already reported";
		EXPECT_EQ( warnings("test server certificate"), 1u ) << "the first list to find the directory missing hid it from the second";
	}
}