#include <jde/fwk/io/file.h>
#include "helpers.h"
#include "Harness.h"

//docs/open-protocol/plan.md Phase 10b (#473):  frames a controller sent, captured on the bench and checked in under
//fixtures/ as they came, the codec's vectors beside the ones written from the specification's tables (fixtures/README.md).
//Each is a frame the driver has to read, so the decoder takes it, and what it takes goes round as a frame of the fuzz
//corpus does.  A frame the codec refuses by the tables is no fixture:  it is a case of its own with the refusal it gets,
//as the simulator's are (SimulatorTests.cpp).
namespace Jde::Opc::OpenProtocol::Tests{
	Ω fixtures()ι->fs::path{ return Settings::FindPath( "/testing/fixtures" ).value_or( fs::path{} ); }
	//The MID and the revision a fixture's name gives it, `-mid0061-r7`:  none where it names neither.
	Ω named( sv file )ι->optional<std::pair<uint16_t,uint16_t>>{
		let mid = file.rfind( "-mid" );
		let revision = mid==sv::npos ? sv::npos : file.find( "-r", mid+4 );
		if( revision==sv::npos || revision!=mid+8 )
			return nullopt;
		let number = []( sv digits )ι->optional<uint16_t>{
			uint16_t y{};
			let end = digits.data()+digits.size();
			let [last, ec] = std::from_chars( digits.data(), end, y );
			return ec==std::errc{} && last!=digits.data() ? optional{ y } : nullopt;
		};
		let m = number( file.substr(mid+4, 4) ), r = number( file.substr(revision+2) );
		return m && r ? optional{ std::pair{*m, *r} } : nullopt;
	}

	TEST( FixtureTests, AControllersFramesRead ){
		ASSERT_TRUE( fs::is_directory(fixtures()) ) << "/testing/fixtures is '" << fixtures().string() << "' - is REPO_SOURCE_DIR set?";
		for( let& entry : fs::directory_iterator{fixtures()} ){
			if( !entry.is_regular_file() || entry.path().extension()!=".frame" )
				continue;
			let file = entry.path().filename().string();
			let frame = IO::Load( entry.path() );
			let decoded = Decode( frame );
			if( !decoded ){
				ADD_FAILURE() << file << ":  " << decoded.error().What;
				continue;
			}
			let head = DecodeHeader( frame )->Head;//the wire's:  an acknowledgement decodes under its table's revision.
			if( let name = named(file) )
				EXPECT_EQ( (std::pair{head.Mid, head.Revision}), *name ) << file << " is named for another MID or revision than its header's";
			else
				ADD_FAILURE() << file << " names no MID and revision, `-mid0061-r7`";
			EXPECT_EQ( Exercise(frame), "" ) << file;
		}
	}

	TEST( FixtureTests, AFixturesNameIsItsMidAndRevision ){
		EXPECT_EQ( named("pf6000-s1-mid0061-r7-2.frame"), (optional{std::pair<uint16_t,uint16_t>{61, 7}}) );
		EXPECT_EQ( named("pf6000-mid9997-r1-1.frame"), (optional{std::pair<uint16_t,uint16_t>{9997, 1}}) );
		EXPECT_EQ( named("pf6000-mid61-r7.frame"), nullopt ) << "four digits";
		EXPECT_EQ( named("pf6000-s1-0061.frame"), nullopt );
	}
}