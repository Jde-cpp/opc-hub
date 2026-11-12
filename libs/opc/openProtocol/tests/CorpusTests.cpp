#include <fstream>
#include "helpers.h"
#include "Harness.h"

//The fuzz target's corpus (fuzz/Fuzz.cpp), checked in under corpus/:  the suite's frames and what the fuzzer found.  It
//is replayed here through what the target runs, so a crash it found stays fixed on every runner, none of which builds
//the fuzzer.
namespace Jde::Opc::OpenProtocol::Tests{
	Ω corpus()ι->fs::path{ return Settings::FindPath( "/testing/corpus" ).value_or( fs::path{} ); }
	Ω read( const fs::path& file )ι->string{
		std::ifstream is{ file, std::ios::binary };
		return { std::istreambuf_iterator<char>{is}, {} };
	}
	//The suite's frames by their files:  each MID at each revision, the specification's examples, and a frame refused each
	//way, for the fuzzer to start from.
	Ω seeds()ι->vector<std::pair<string,string>>{
		vector<std::pair<string,string>> y;
		for( let& cases : {Cases(), Examples()} ){
			for( let& c : cases )
				y.emplace_back( c.File, c.Frame );
		}
		y.emplace_back( "refused-length", ExampleFrame("00190001003", {}) );
		y.emplace_back( "refused-revision", ExampleFrame("002400050 1", "0018") );
		y.emplace_back( "refused-parts", ExampleFrame("00240005001001010023", "0018") );
		y.emplace_back( "refused-linked", ExampleFrame("00240005001001010021", "0018") );
		y.emplace_back( "refused-unknown-mid", ExampleFrame("00200900", {}) );
		y.emplace_back( "refused-unknown-revision", ExampleFrame("00200061008", {}) );
		y.emplace_back( "refused-field", ExampleFrame("00530071", "01E404021x31042008-06-02:10:14:26") );
		auto noNul = ExampleFrame( "00240005", "0018" );
		noNul.back() = ' ';
		y.emplace_back( "refused-nul", move(noNul) );
		return y;
	}

	TEST( CorpusTests, Replays ){
		ASSERT_TRUE( fs::is_directory(corpus()) ) << "/testing/corpus is '" << corpus().string() << "' - is REPO_SOURCE_DIR set?";
		uint files{};
		for( let& entry : fs::directory_iterator{corpus()} ){
			if( !entry.is_regular_file() )
				continue;
			++files;
			EXPECT_EQ( Exercise(read(entry.path())), "" ) << entry.path().filename().string();
		}
		EXPECT_GE( files, seeds().size() );
	}

	//Each of the suite's frames is in the corpus as it stands here.  A case added or changed without its file fails;
	//`--gtest_also_run_disabled_tests --gtest_filter=CorpusTests.DISABLED_WritesSeeds` writes the files.
	TEST( CorpusTests, HoldsTheSuitesFrames ){
		for( let& [file, frame] : seeds() )
			EXPECT_EQ( read(corpus()/file), frame ) << file;
	}

	TEST( CorpusTests, DISABLED_WritesSeeds ){
		for( let& [file, frame] : seeds() ){
			std::ofstream os{ corpus()/file, std::ios::binary };
			os.write( frame.data(), frame.size() );
			EXPECT_TRUE( os.good() ) << file;
		}
	}

	//The suite's own frames through the harness, whatever the files hold, and a few buffers that are no frame.
	TEST( CorpusTests, HarnessFindsNothingInTheSuitesFrames ){
		for( let& [file, frame] : seeds() )
			EXPECT_EQ( Exercise(frame), "" ) << file;
		EXPECT_EQ( Exercise({}), "" );
		EXPECT_EQ( Exercise("0"), "" );
		EXPECT_EQ( Exercise(string(20, '0')), "" );
		EXPECT_EQ( Exercise(string(21, '\0')), "" );
	}

	//What a runner has of the fuzzer:  each of the suite's frames with each byte changed in turn, and cut short at each byte.
	TEST( CorpusTests, HarnessFindsNothingInFramesChanged ){
		for( let& [file, frame] : seeds() ){
			for( uint i=0; i<frame.size(); ++i ){
				EXPECT_EQ( Exercise(sv{frame}.substr(0, i)), "" ) << file << " cut at " << i;
				for( char byte : {' ', '0', '9', 'A', '\0', '\x7F', '\xFF'} ){
					auto changed = frame;
					changed[i] = byte;
					EXPECT_EQ( Exercise(changed), "" ) << file << " byte " << i << " as " << (int)(uint8_t)byte;
				}
			}
		}
	}
}