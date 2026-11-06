#include <jde/fwk/utils/paramPack.h>

#define let const auto

namespace Jde::Tests{
	TEST( ParamPackTests, RvalueString ){
		vector<string> values;
		ParamPack::Append( values, string{"first"}, 2, string{"third"} );
		ASSERT_EQ( values, (vector<string>{"first", "2", "third"}) );
	}
	//What a log entry or exception stores is what fmt prints on the console for a {} field:  an ostream wrote a bool as 1, a
	//uint8_t as a raw character and a double to six significant digits.
	TEST( ParamPackTests, ArgumentsAsFmtPrintsThem ){
		vector<string> values;
		ParamPack::Append( values, true, (uint8_t)5, (int8_t)-3, 1234567.0, 0.1+0.2, 'c', "literal", sv{"view"}, boost::json::string{"json"} );
		ASSERT_EQ( values, (vector<string>{"true", "5", "-3", "1234567", fmt::format("{}", 0.1+0.2), "c", "literal", "view", "json"}) ) << "text is copied, even of a type fmt has no formatter for";
		EXPECT_EQ( values[4], "0.30000000000000004" ) << "shortest round trip, not six digits";
	}
}