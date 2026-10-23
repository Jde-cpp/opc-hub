#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/fwk/crypto/TrustDirs.h>
#include <jde/fwk/log/MemoryLog.h>
#include <jde/fwk/log/SpdLog.h>
#include <fstream>
#include "../testsDir.h"

#define let const auto
namespace Jde::Crypto{
	//opcserver-refactor-review #4:  a directory missing from two lists warns for each, once.  A second scanner for a list already
	//warned - ServerTrust makes one per connect - says it at Debug; another list's first scan still warns.
	TEST( TrustDirsTests, AMissingDirectoryWarnsOncePerList ){
		let dir = Jde::Tests::TestsDir( "crypto/trustDirs/missing", true );//per process:  the test makes it and takes it away.
		const vector<fs::path> dirs{ dir };
		let none = []( const fs::path&, vector<byte>&& ){};
		let scan = [&]( sv noun ){ TrustDirs{ string{noun}, ELogTags::Test, ELogLevel::Warning }.Scan( dirs, none ); };
		let warnings = [&dir]( sv noun )->uint{
			return Logging::Find( [&]( const Logging::Entry& e ) {
				let message = e.Message();
				return e.Level==ELogLevel::Warning && message.contains( dir.string() ) && message.contains( noun );
			} ).size();
		};
		//the missing set is process-wide, and a scan that finds the directory is its only reset:  without one, every
		//--gtest_repeat pass after the first would find both lists already warned.
		fs::create_directories( dir );
		for( sv noun : {"test anchor", "test server certificate"} )
			scan( noun );
		fs::remove_all( dir );
		Logging::ClearMemory();

		scan( "test anchor" );
		scan( "test anchor" );
		scan( "test server certificate" );
		EXPECT_EQ( warnings("test anchor"), 1u ) << "a list warned again for a directory it had already reported";
		EXPECT_EQ( warnings("test server certificate"), 1u ) << "the first list to find the directory missing hid it from the second";

		fs::create_directories( dir );
		scan( "test anchor" );
		fs::remove_all( dir );
		Logging::ClearMemory();
		scan( "test anchor" );
		EXPECT_EQ( warnings("test anchor"), 1u ) << "a directory that appeared and went was not reported again";
	}

	//A certificate minted into mint, for a test to copy into the directory it walks:  its key pair is .pem too, and would be
	//walked as two more.
	Ω mintCertificate( const fs::path& mint, sv commonName )ε->fs::path{
		const CryptoSettings settings{ jobject{
			{"certificate", jobject{{"path", (mint/"cert.pem").string()}, {"subjectAltName", "DNS:localhost"}, {"company", "jde-cpp"}, {"country", "US"}, {"commonName", commonName}}},
			{"privateKey", jobject{{"path", (mint/"private.pem").string()}, {"passcode", "123456789"}}},
			{"publicKey", jobject{{"path", (mint/"public.pem").string()}}},
			{"dh", ""}
		}, {} };
		settings.CreateDirectories();
		Crypto::CreateKeyCertificate( settings, SRCE_CUR );
		return mint/"cert.pem";
	}

	//The walk:  a file new or changed since the last scan is handed with its DER - every one, on the first - and an unchanged
	//one is not;  one that stops parsing is handed with none, then kept quiet until it changes;  a gone one is handed with none
	//and forgotten.  Scan says whether it handed any - UATrust pushes a fresh list only then - and Known() counts a file that
	//failed too, which keeps install-issues #33's "copied in and ignored" warning quiet beside an unreadable anchor.
	TEST( TrustDirsTests, WalkHandsNewChangedUnreadableAndGoneFiles ){
		let root = Jde::Tests::TestsDir( "crypto/trustDirs/walk", true );
		fs::remove_all( root );
		let walk = root/"walk";
		fs::create_directories( walk );
		let a = walk/"a.pem";
		fs::copy_file( mintCertificate(root/"mint", "trust-dirs-walk"), a );

		vector<std::pair<fs::path,vector<byte>>> handed;
		let record = [&handed]( const fs::path& path, vector<byte>&& der ){ handed.emplace_back( path, move(der) ); };
		const vector<fs::path> dirs{ walk };
		TrustDirs trust{ "test anchor", ELogTags::Test, ELogLevel::Warning };
		let scan = [&]()->bool { handed.clear(); return trust.Scan( dirs, record ); };

		ASSERT_TRUE( scan() ) << "the first scan hands every file";
		ASSERT_EQ( handed.size(), 1u );
		EXPECT_EQ( handed[0].first, a );
		EXPECT_TRUE( handed[0].second==ReadCertificate(a) ) << "with its DER";
		EXPECT_EQ( trust.Known(), 1u );

		EXPECT_FALSE( scan() ) << "an unchanged file is not handed again";
		EXPECT_TRUE( handed.empty() );

		fs::last_write_time( a, fs::last_write_time(a)+1s );//an explicit bump:  now() may equal the copy's time.
		EXPECT_TRUE( scan() ) << "a file written since is";
		ASSERT_EQ( handed.size(), 1u );
		EXPECT_FALSE( handed[0].second.empty() );

		{ std::ofstream{ a, std::ios::trunc } << "-----BEGIN CERTIFICATE-----\ngarbage\n"; }
		EXPECT_TRUE( scan() );
		ASSERT_EQ( handed.size(), 1u );
		EXPECT_TRUE( handed[0].second.empty() ) << "a file that no longer parses is handed with no DER, so its owner drops it";
		EXPECT_EQ( trust.Known(), 1u ) << "and is still known - an unreadable anchor is not one never copied in";
		EXPECT_FALSE( scan() ) << "kept quiet until it changes";
		EXPECT_TRUE( handed.empty() );

		fs::remove( a );
		EXPECT_TRUE( scan() );
		ASSERT_EQ( handed.size(), 1u );
		EXPECT_TRUE( handed[0].second.empty() ) << "a file gone is handed with no DER";
		EXPECT_EQ( trust.Known(), 0u );

		Logging::ClearMemory();
		EXPECT_FALSE( scan() ) << "an empty directory hands nothing";
		EXPECT_TRUE( handed.empty() );
		EXPECT_TRUE( Logging::Find([&]( const Logging::Entry& e ){ return e.Level>=ELogLevel::Warning && e.Message().contains(walk.string()); }).empty() ) << "and says nothing about it";
		fs::remove_all( root );
	}

	//An entry whose time can't be read - a dangling link, a file gone mid-walk - is one file that fails, not the end of the
	//directory:  its neighbours are still handed, every one on the first scan, as ServerTrust's scanner per connect always is.
	//This tmpfs walks a directory newest first, so the link, made last, comes first, and when its time threw out of the walk
	//nothing was handed;  on a hash-ordered filesystem the order, so whether this could fail, is luck.
	TEST( TrustDirsTests, ABadEntryDoesNotHideItsNeighbours ){
		let root = Jde::Tests::TestsDir( "crypto/trustDirs/neighbours", true );
		fs::remove_all( root );
		let dir = root/"walk";
		fs::create_directories( dir );
		let good = dir/"good.pem";
		fs::copy_file( mintCertificate(root/"mint", "trust-dirs-neighbours"), good );
		let dangling = dir/"dangling.pem";
		std::error_code ec;
		fs::create_symlink( dir/"no-such-file", dangling, ec );
		if( ec ){
			fs::remove_all( root );
			GTEST_SKIP() << "no symlink here:  " << ec.message();
		}

		flat_map<fs::path,vector<byte>> handed;
		let record = [&handed]( const fs::path& path, vector<byte>&& der ){ handed[path] = move( der ); };
		const vector<fs::path> dirs{ dir };
		TrustDirs trust{ "test anchor", ELogTags::Test, ELogLevel::Warning };
		Logging::ClearMemory();
		EXPECT_TRUE( trust.Scan(dirs, record) );
		ASSERT_TRUE( handed.contains(good) ) << "the dangling link ended the walk before its neighbour";
		EXPECT_TRUE( handed[good]==ReadCertificate(good) ) << "with its DER";
		EXPECT_TRUE( handed.contains(dangling) && handed[dangling].empty() ) << "the link is a file that failed, handed with no DER";
		EXPECT_EQ( trust.Known(), 2u ) << "and is known, as a file that failed is";
		EXPECT_TRUE( Logging::Find([]( const Logging::Entry& e ){ return e.Message().contains( "Could not scan trusted certificate directory" ); }).empty() ) << "no directory failed";

		handed.clear();
		EXPECT_FALSE( trust.Scan(dirs, record) ) << "the link, unchanged, stays quiet";
		EXPECT_TRUE( handed.empty() );
		fs::remove_all( root );
	}

	//opcserver-refactor-review #2:  a file in a list's directories that will not load logs at the list's own level - Critical for
	//one that gates logins, Warning for one among many servers.  The exception says it from its destructor, once Scan has
	//re-levelled it, so this suite's exception: Trace, which would log it as it is thrown at its own Warning, is lowered on spd,
	//which the cumulative follows, and restored however the test ends.
	TEST( TrustDirsTests, AnUnreadableFileLogsAtTheListsLevel ){
		auto& spd = Logging::GetLogger<Logging::SpdLog>();
		struct Restore final{ Logging::SpdLog& Spd; ~Restore(){ Spd.ClearLevel( ELogTags::Exception ); } } restore{ spd };
		spd.SetLevel( ELogTags::Exception, ELogLevel::Debug );
		ASSERT_FALSE( Logging::ShouldLog(ELogLevel::Trace, ELogTags::Exception) ) << "another logger still takes the exception tag at Trace";
#ifndef NDEBUG
		ASSERT_GT( Logging::BreakLevel(), ELogLevel::Warning ) << "the parse failure would log as it is thrown";
#endif
		let dir = Jde::Tests::TestsDir( "crypto/trustDirs/unreadable", true );
		fs::remove_all( dir );
		fs::create_directories( dir );
		let bad = dir/"bad.pem";
		{ std::ofstream{ bad } << "not a certificate"; }
		const vector<fs::path> dirs{ dir };
		let none = []( const fs::path&, vector<byte>&& ){};
		let logged = [&bad]( ELogLevel level )->uint{
			return Logging::Find( [&]( const Logging::Entry& e ){ return e.Level==level && e.Message().contains( bad.string() ); } ).size();
		};
		Logging::ClearMemory();
		TrustDirs{ "test anchor", ELogTags::Test, ELogLevel::Critical }.Scan( dirs, none );
		EXPECT_EQ( logged(ELogLevel::Critical), 1u ) << "a list that gates logins says it at Critical";
		Logging::ClearMemory();
		TrustDirs{ "test server certificate", ELogTags::Test, ELogLevel::Warning }.Scan( dirs, none );
		EXPECT_EQ( logged(ELogLevel::Warning), 1u ) << "one among many servers, at Warning";
		EXPECT_EQ( logged(ELogLevel::Critical), 0u );
		fs::remove_all( dir );
	}
}