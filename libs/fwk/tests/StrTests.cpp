#include <jde/fwk/str.h>

#define let const auto

namespace Jde::Tests{
	using Bytes = vector<unsigned char>;

	//LTrim/RTrim fed sign-extended chars to iswspace (UB) and the multibyte space path was dead on Linux.
	TEST( StrTests, TrimAscii ){
		EXPECT_EQ( Str::Trim(sv{"  \t abc \n "}), "abc" );
		EXPECT_EQ( Str::LTrim(sv{"\t x"}), "x" );
		EXPECT_EQ( Str::RTrim(sv{"x \t"}), "x" );
		EXPECT_EQ( Str::Trim(string{"  abc  "}), "abc" );//string&& overload.
		EXPECT_EQ( Str::Trim(sv{"abc"}), "abc" );//no-op.
	}
	TEST( StrTests, TrimUnicodeSpace ){
		EXPECT_EQ( Str::LTrim(sv{"\xe2\x80\x89" "abc"}), "abc" );//U+2009 thin space - previously dead/UB on Linux.
		EXPECT_EQ( Str::LTrim(sv{"\xe2\x80\xaf" "abc"}), "abc" );//U+202F narrow no-break space.
		EXPECT_EQ( Str::LTrim(sv{"\xe2\x82\xac" "x"}), "\xe2\x82\xac" "x" );//U+20AC euro (not a space) preserved intact.
		EXPECT_EQ( Str::LTrim(sv{"\xe9" "x"}), "\xe9" "x" );//lone 0xE9 (invalid UTF-8 lead) - left as-is, no UB.
		//fwk-refactor B1: the right side and the string&& overloads were byte-wise, so a Unicode space trimmed from the left survived on the right.
		EXPECT_EQ( Str::RTrim(sv{"abc" "\xe2\x80\x83"}), "abc" );//U+2003 em space.
		EXPECT_EQ( Str::Trim(sv{"\xe2\x80\x89" "abc" "\xe2\x80\x83"}), "abc" );
		EXPECT_EQ( Str::Trim(string{"\xe2\x80\x89" " abc " "\xe2\x80\xaf"}), "abc" );//string&& overload decodes too.
		EXPECT_EQ( Str::RTrim(sv{"x" "\xe2\x82\xac"}), "x" "\xe2\x82\xac" );//euro on the right - not a space, kept whole, not split mid-sequence.
		EXPECT_EQ( Str::RTrim(sv{"x" "\xe9"}), "x" "\xe9" );//lone trailing 0xE9 kept.
		EXPECT_EQ( Str::RTrim(sv{"\xe2\x80\x83"}), "" );//a string that is only a Unicode space empties, both directions.
		EXPECT_EQ( Str::LTrim(sv{"\xe2\x80\x83"}), "" );
		EXPECT_EQ( Str::TrimFirstLast(string{"\xe2\x80\x83" "[abc]" "\xe2\x80\x83"}, '[', ']'), "abc" );//brackets inside Unicode padding.
	}

	//Decode64 used to strip every trailing 0x00 byte (confusing them with padding), corrupting binary payloads such as RSA signatures ending in zero.
	TEST( StrTests, Decode64TrailingZeroBytes ){
		let bytes = Bytes{ 0xAB, 0x00 };
		ASSERT_EQ( Str::Encode64(bytes), "qwA=" );
		ASSERT_EQ( Str::Decode64<Bytes>("qwA="), bytes );
		ASSERT_EQ( Str::Decode64<Bytes>("qwA"), bytes );//jwt segments are unpadded.

		ASSERT_EQ( Str::Decode64<Bytes>("AAAA"), (Bytes{0x00, 0x00, 0x00}) );//all-zero payload previously decoded to empty.
	}

	TEST( StrTests, Encode64FileSafeIsBase64Url ){
		let bytes = Bytes{ 0xFB, 0xFF };
		EXPECT_EQ( Str::Encode64(bytes), "+/8=" );
		EXPECT_EQ( Str::Encode64(bytes, true), "-_8" ) << "base64url alphabet, no padding (RFC 7515 §2)";
		EXPECT_EQ( Str::Decode64<Bytes>("-_8=", true), bytes ) << "padding still accepted";
		EXPECT_THROW( Str::Decode64("+/8", true), Exception ) << "standard alphabet in a base64url field";
		EXPECT_THROW( Str::Decode64("qwA=="), Exception ) << "bad padding";
	}

	//To is noexcept; the double specialization used to let stod's invalid_argument escape → std::terminate.
	TEST( StrTests, ToDoubleBadInput ){
		EXPECT_EQ( To<double>("abc"), 0.0 );
		EXPECT_EQ( To<double>(""), 0.0 );
		EXPECT_EQ( To<double>("1.5"), 1.5 );
	}

	//settings' $(NAME) expansion and ExternalException's brace doubling both run through Replace, and neither had
	//a test.  The empty-find case was an infinite loop until 2026-08-16 - source.find("",i) returns i and
	//i+find.length() never advances - so it could not even be asserted before.
	TEST( StrTests, Replace ){
		EXPECT_EQ( Str::Replace("abc", "b", "X"), "aXc" );
		EXPECT_EQ( Str::Replace("aaa", "a", "b"), "bbb" );//adjacent matches.
		EXPECT_EQ( Str::Replace("aaaa", "aa", "b"), "bb" );//overlapping candidates - the match consumes both chars.
		EXPECT_EQ( Str::Replace("{}", "{", "{{"), "{{}" );//ExternalException::FormatMsg's escaping: the inserted text must not be rescanned.
		EXPECT_EQ( Str::Replace("abc", "b", ""), "ac" );//removal.
		EXPECT_EQ( Str::Replace("abc", "z", "X"), "abc" );//no match.
		EXPECT_EQ( Str::Replace("a", "abc", "X"), "a" );//find longer than source.
		EXPECT_EQ( Str::Replace("", "a", "X"), "" );
		EXPECT_EQ( Str::Replace("abc", "", "X"), "abc" ) << "an empty find must return the source, not spin";
		EXPECT_EQ( Str::Replace(sv{"a/b/c"}, '/', '_'), "a_b_c" );//the char overload.
	}

	//two different split contracts: the char form drops every empty field, the sv/sv form keeps interior and
	//leading ones and drops only a trailing empty.  FileTests' line reader depends on the first.
	TEST( StrTests, Split ){
		EXPECT_EQ( Str::Split("a,b").size(), 2u );
		let dropped = Str::Split( "a,,b," );
		ASSERT_EQ( dropped.size(), 2u ) << "empty fields and a trailing delimiter are dropped";
		EXPECT_EQ( dropped[0], "a" );
		EXPECT_EQ( dropped[1], "b" );
		EXPECT_TRUE( Str::Split(",,").empty() );
		EXPECT_TRUE( Str::Split("").empty() );
		EXPECT_EQ( Str::Split("a\nb", '\n').size(), 2u );

		let multi = Str::Split<sv,sv>( sv{"a::b::c"}, sv{"::"} );//multi-char delimiter - a different implementation.
		ASSERT_EQ( multi.size(), 3u );
		EXPECT_EQ( multi[0], "a" );
		EXPECT_EQ( multi[2], "c" );
		let empties = Str::Split<sv,sv>( sv{":a::b:"}, sv{":"} );
		ASSERT_EQ( empties.size(), 4u ) << "leading and interior empties survive here; only the trailing one is dropped";
		EXPECT_EQ( empties[0], "" );
		EXPECT_EQ( empties[2], "" );
	}

	TEST( StrTests, Join ){
		let items = vector<string>{ "a", "b", "c" };
		EXPECT_EQ( Str::Join(items), "a,b,c" );
		EXPECT_EQ( Str::Join(items, "; "), "a; b; c" );
		EXPECT_EQ( Str::Join(items, ",", true), "\"a\",\"b\",\"c\"" );
		EXPECT_EQ( Str::Join(vector<string>{}), "" );
		EXPECT_EQ( Str::Join(vector<string>{"only"}), "only" );//no trailing separator.
	}

	//query strings and form bodies: an invalid escape has to pass through rather than be swallowed, and a
	//trailing '%' must not read past the end of a non-terminated view.
	TEST( StrTests, DecodeUri ){
		EXPECT_EQ( Str::DecodeUri("%41"), "A" );
		EXPECT_EQ( Str::DecodeUri("a%20b"), "a b" );
		EXPECT_EQ( Str::DecodeUri("a+b"), "a b" );
		EXPECT_EQ( Str::DecodeUri("%2f%2F"), "//" );//hex case-insensitive.
		EXPECT_EQ( Str::DecodeUri("%zz"), "%zz" );//not hex - literal.
		EXPECT_EQ( Str::DecodeUri("100%"), "100%" );//trailing '%' with nothing after it.
		EXPECT_EQ( Str::DecodeUri("100%4"), "100%4" );//trailing '%' with only one digit.
		EXPECT_EQ( Str::DecodeUri(""), "" );
	}

	//the modulus spelling every enrolled identity is keyed by - see OpenSslTests.PublicKeyIdentity.
	TEST( StrTests, ToHex ){
		let bytes = vector<byte>{ byte{0x00}, byte{0x0f}, byte{0xff}, byte{0xa5} };
		EXPECT_EQ( Str::ToHex(bytes), "000fffa5" ) << "lower case, two chars per byte, leading zeros kept";
		EXPECT_EQ( Str::ToHex(vector<byte>{}), "" );
		EXPECT_EQ( Str::ToHex(std::span<const byte>{bytes}), "000fffa5" ) << "the span form the container overload forwards to";
		EXPECT_EQ( Str::ToHex(string{"\x00\x0f", 2}), "000f" ) << "any byte-sized element type";
	}

	TEST( StrTests, TryTo ){
		EXPECT_EQ( Str::TryTo<uint32>(string{"42"}), 42u );
		EXPECT_FALSE( Str::TryTo<uint32>(string{"abc"}).has_value() );
		EXPECT_FALSE( Str::TryTo<uint32>(string{""}).has_value() );
		EXPECT_FALSE( Str::TryTo<uint>(string{"99999999999999999999999"}).has_value() ) << "past unsigned long long - out_of_range, not a throw";
		EXPECT_EQ( (Str::TryTo<uint,16>(string{"ff"})), 255u );
		EXPECT_FALSE( Str::TryTo<uint>("42abc").has_value() ) << "the whole string, not a prefix";
		EXPECT_FALSE( Str::TryTo<uint16_t>("70000").has_value() ) << "out of range for T, not truncated to 4464";
		EXPECT_EQ( Str::TryTo<uint16_t>("65535"), 65535u );
		EXPECT_FALSE( Str::TryTo<uint16_t>("-1").has_value() ) << "no sign on an unsigned type";
		EXPECT_FALSE( (Str::TryTo<uint32_t,16>("100000000").has_value()) ) << "nine hex digits overflow 32 bits";
		EXPECT_EQ( Str::TryTo<int>("-5"), -5 );
	}

	TEST( StrTests, TrimFirstLast ){
		EXPECT_EQ( Str::TrimFirstLast(string{" [abc] "}, '[', ']'), "abc" );
		EXPECT_EQ( Str::TrimFirstLast(string{"[abc]"}, '[', ']'), "abc" );
		EXPECT_EQ( Str::TrimFirstLast(string{" abc "}, '[', ']'), "abc" );//no bracket - plain trim.
		EXPECT_EQ( Str::TrimFirstLast(string{"[a[b]c]"}, '[', ']'), "a[b]c" );//only the outermost pair.
		EXPECT_EQ( Str::TrimFirstLast(string{""}, '[', ']'), "" );
	}

	TEST( StrTests, CaseInsensitiveComparison ){
		EXPECT_TRUE( Str::StartsWith("abcdef", "abc") );
		EXPECT_FALSE( Str::StartsWith("abc", "abcdef") );//starting longer than value.
		EXPECT_TRUE( Str::StartsWith("abc", "") );
		EXPECT_TRUE( Str::StartsWithInsensitive("ABCdef", "abc") );
		EXPECT_TRUE( Str::StartsWithInsensitive("abcdef", "ABC") );
		EXPECT_FALSE( Str::StartsWithInsensitive("abc", "ABCDEF") );
		EXPECT_FALSE( Str::StartsWithInsensitive("xbc", "abc") );

		EXPECT_TRUE( "ABC"_iv=="abc"_iv );//ci_traits - the comparison IssueCertificate's SAN matching relies on.
		EXPECT_FALSE( "ABC"_iv=="abd"_iv );
		EXPECT_EQ( ToSV("ABC"_iv), "ABC" );//the view still carries the original spelling.
		EXPECT_EQ( Str::ToLower("AbC"), "abc" );
		EXPECT_EQ( Str::ToUpper("AbC"), "ABC" );
		EXPECT_EQ( Str::ToLower("\xC3\x89T\xC3\x89"), "\xC3\x89t\xC3\x89" ) << "ASCII only, whatever the locale:  UTF-8 bytes pass through";
		EXPECT_TRUE( "\xC3\x89" "A"_iv=="\xC3\x89" "a"_iv ) << "bytes >= 0x80 compare as themselves - toupper on a negative char was ub";
		EXPECT_TRUE( Str::iv{"a"} < Str::iv{"\xC3"} ) << "and order as unsigned, as char_traits<char> does";
	}

	TEST( StrTests, Decode64RoundTrip ){
		Bytes signature( 256 );
		for( uint i=0; i<signature.size(); ++i )
			signature[i] = (unsigned char)(i*7);
		signature.back() = 0x00;
		ASSERT_EQ( Str::Decode64<Bytes>(Str::Encode64(signature)), signature );
		ASSERT_EQ( Str::Decode64<Bytes>(Str::Encode64(signature, true), true), signature );

		let text = string{ "any carnal pleasure." };//lengths 20,19,18 exercise 0,1,2 padding chars.
		for( uint size : {20u, 19u, 18u} ){
			let payload = text.substr( 0, size );
			ASSERT_EQ( Str::Decode64(Str::Encode64(payload)), payload );
		}
	}
}
