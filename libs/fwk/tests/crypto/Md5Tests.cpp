#include <random>
#include <jde/fwk/crypto/digest.h>

namespace Jde::Tests{
	//The compiler's digest names every log template (Logging::Format), and CalcMd5 every file, function and argument at
	//run time, so an id from one has to find a string stored under the other.  Md5 runs at run time here, which is the same
	//function:  every length to 299 crosses the padding's edges at 55/56 and 119/120 and several whole blocks, over bytes of
	//every value.
	TEST( Md5Tests, MatchesCalcMd5AtEveryLength ){
		std::mt19937 random{ 42 };
		string text;
		for( uint size=0; size<300; ++size ){
			EXPECT_EQ( Crypto::Md5(text), Crypto::CalcMd5(text) ) << "length " << size;
			text.push_back( (char)random() );
		}
	}
}