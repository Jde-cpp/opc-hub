#include <jde/fwk/enum.h>

#define let const auto

namespace Jde::Tests{
	//these back Access::ToRight/ToString(ERights), ToLogLevel, Syntax, Introspection and
	//OpcServerSession - the permission and level strings the whole system parses - and had no test anywhere.

	//index-based: stringValues[i] names the value i.
	enum class EColor : uint8{ None=0, Red=1, Green=2, Blue=3 };
	constexpr array<sv,4> ColorStrings{ "none", "red", "green", "blue" };

	//bit-based: stringValues[0] names zero and stringValues[i+1] names bit i.
	enum class ETestFlag : uint{ None=0, A=1, B=2, C=4 };
	constexpr array<sv,4> FlagStrings{ "none", "a", "b", "c" };

	//the same dense shape carried out to bit 40 - where ELogTags' extension tags live (Jde::Opc registers 32-45).
	Ω wideStrings()ι->const std::array<string,42>&{
		static const auto y = []{
			std::array<string,42> a;
			a[0] = "none";
			for( uint i=1; i<a.size(); ++i )
				a[i] = Ƒ( "bit{}", i-1 );
			return a;
		}();
		return y;
	}

	TEST( EnumTests, ToEnum ){
		EXPECT_EQ( ToEnum<EColor>(ColorStrings, sv{"green"}), EColor::Green );
		EXPECT_EQ( ToEnum<EColor>(ColorStrings, sv{"none"}), EColor::None );
		EXPECT_EQ( ToEnum<EColor>(ColorStrings, sv{"2"}), EColor::Green ) << "a numeric string is read as an index";
		EXPECT_FALSE( ToEnum<EColor>(ColorStrings, sv{"4"}).has_value() ) << "an index past the table is rejected, not cast through";
		EXPECT_FALSE( ToEnum<EColor>(ColorStrings, sv{"mauve"}).has_value() );
		EXPECT_FALSE( ToEnum<EColor>(ColorStrings, sv{""}).has_value() );
	}

	TEST( EnumTests, FromEnum ){
		EXPECT_EQ( FromEnum(ColorStrings, EColor::Blue), "blue" );
		EXPECT_EQ( FromEnum(ColorStrings, EColor::None), "none" );
		EXPECT_EQ( FromEnum(ColorStrings, (EColor)9), "9" ) << "out of range renders the number rather than an empty string";
		for( let color : {EColor::None, EColor::Red, EColor::Green, EColor::Blue} ){
			let name = FromEnum( ColorStrings, color );
			EXPECT_EQ( ToEnum<EColor>(ColorStrings, sv{name}), color );
		}
	}

	TEST( EnumTests, ToFlag ){
		EXPECT_FALSE( ToFlag<ETestFlag>(FlagStrings, sv{"none"}).has_value() ) << "index 0 names zero, not a bit";
		EXPECT_EQ( ToFlag<ETestFlag>(FlagStrings, sv{"a"}), ETestFlag::A ) << "the second string is bit 0";
		EXPECT_EQ( ToFlag<ETestFlag>(FlagStrings, sv{"c"}), ETestFlag::C );
		EXPECT_FALSE( ToFlag<ETestFlag>(FlagStrings, sv{"nope"}).has_value() );
	}

	TEST( EnumTests, FromEnumFlag ){
		EXPECT_EQ( FromEnumFlag(FlagStrings, ETestFlag::A|ETestFlag::C), "a,c" ) << "comma-joined, in bit order, with no trailing separator";
		EXPECT_EQ( FromEnumFlag(FlagStrings, ETestFlag::B), "b" );
		EXPECT_EQ( FromEnumFlag(FlagStrings, ETestFlag::None), "none" ) << "zero renders stringValues[0]";
		for( let flag : {ETestFlag::A, ETestFlag::B, ETestFlag::C} ){
			let name = FromEnumFlag( FlagStrings, flag );
			EXPECT_EQ( ToFlag<ETestFlag>(FlagStrings, sv{name}), flag ) << "round trip of " << name;
		}
	}

	// Both helpers shifted a 32-bit literal - `1ul<<i` in FromEnumFlag and `1<<(index-1)` in ToFlag - so every bit
	// above 31 was lost (and the int shift was undefined outright).  Same width class as
	// LogGeneralTests.AllCoversTheFullUnderlyingWidth, which is where ELogTags::All was fixed for the same reason.
	TEST( EnumTests, WideFlagsSurviveTheShift ){
		let& wide = wideStrings();
		constexpr auto high = (ETestFlag)( 1ull<<40 );
		EXPECT_EQ( FromEnumFlag(wide, high), "bit40" );
		EXPECT_EQ( ToFlag<ETestFlag>(wide, sv{"bit40"}), high );
		EXPECT_EQ( FromEnumFlag(wide, (ETestFlag)((1ull<<40)|1ull)), "bit0,bit40" );
	}
}
