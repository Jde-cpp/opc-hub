#include <jde/fwk/chrono.h>
#include <jde/fwk/io/json.h>

#define let const auto

namespace Jde::Tests{
	struct JsonTests : public ::testing::Test{};
	//the what() of the CodeException convert throws - empty when it throws none.
	Ω codeWhat( auto&& convert )->string{
		try{ convert(); }
		catch( const CodeException& e ){ return e.what(); }
		return {};
	}

	// noexcept and hand-rolled because to_number<uint> throws for negatives: the sign handling across the
	// int64/uint64/double alternatives is exactly what regresses silently.  Used to order ql filter values.
	TEST_F( JsonTests, ValueOrdering ){
		EXPECT_TRUE( jvalue{-1} < jvalue{0u} ) << "a negative int64 is below every uint64";
		EXPECT_FALSE( jvalue{0u} < jvalue{-1} );
		EXPECT_TRUE( jvalue{1} < jvalue{2} );
		EXPECT_TRUE( jvalue{1u} < jvalue{2u} );
		EXPECT_TRUE( jvalue{1} < jvalue{1.5} );//mixed int/double goes through the double path.
		EXPECT_TRUE( jvalue{1.5} < jvalue{2} );
		EXPECT_TRUE( jvalue{1.5} < jvalue{2.5} );
		EXPECT_TRUE( jvalue{"a"} < jvalue{"b"} );
		EXPECT_FALSE( jvalue{"b"} < jvalue{"a"} );
		EXPECT_TRUE( jvalue{false} < jvalue{true} );

		EXPECT_FALSE( jvalue{"1"} < jvalue{2} ) << "no cross-kind ordering - a string is never below a number";
		EXPECT_FALSE( jvalue{2} < jvalue{"1"} );
		EXPECT_FALSE( jvalue{jarray{1}} < jvalue{jarray{2}} ) << "non-primitives are never less";

		EXPECT_TRUE( jvalue{1} <= jvalue{1} );//the derived operators, which build on both < and ==.
		EXPECT_TRUE( jvalue{2} > jvalue{1} );
		EXPECT_TRUE( jvalue{2} >= jvalue{2} );
	}

	//the settings-load failure an operator actually sees: the message has to name the file.
	TEST_F( JsonTests, TryReadJsonNetReportsThePath ){
		let missing = fs::temp_directory_path()/"jde-no-such-file-12345.jsonnet";
		ASSERT_FALSE( fs::exists(missing) );
		let y = Json::TryReadJsonNet( missing );
		ASSERT_FALSE( y.has_value() );
		EXPECT_NE( y.error().find(missing.string()), string::npos ) << y.error();
		EXPECT_THROW( Json::ReadJsonNet(missing), Exception );//the throwing sibling of the same call.
	}

	TEST_F( JsonTests, ParseRejectsNonObjects ){
		EXPECT_EQ( Json::Parse(R"({"a":1})").at("a").as_int64(), 1 );
		EXPECT_THROW( Json::Parse("[1]"), CodeException ) << "valid json, but not an object";
		EXPECT_THROW( Json::Parse("{not json"), CodeException );
		EXPECT_EQ( Json::ParseValue(string{"[1]"}).as_array().size(), 1u );//ParseValue takes any kind.
	}

	//the "one or many" shape the wire uses everywhere: a bare value or an array of them, and anything else throws
	//rather than being silently skipped.
	TEST_F( JsonTests, VisitAcceptsOneOrMany ){
		vector<string> strings;
		Json::Visit( jvalue{"one"}, [&](sv s){ strings.push_back(string{s}); } );
		Json::Visit( jvalue{jarray{"a","b"}}, [&](sv s){ strings.push_back(string{s}); } );
		ASSERT_EQ( strings.size(), 3u );
		EXPECT_EQ( strings[0], "one" );
		EXPECT_EQ( strings[2], "b" );
		EXPECT_THROW( Json::Visit(jvalue{1}, [](sv){}), Exception );
		EXPECT_THROW( Json::Visit(jvalue{jarray{"a",1}}, [](sv){}), Exception ) << "one bad element rejects the batch";

		uint objects{};
		jvalue single = jobject{{"a",1}};
		jvalue many = jarray{ jobject{{"a",1}}, jobject{{"b",2}} };
		Json::Visit( single, [&](jobject&){ ++objects; } );
		Json::Visit( many, [&](jobject&){ ++objects; } );
		EXPECT_EQ( objects, 3u );
		jvalue notAnObject = 1;
		EXPECT_THROW( Json::Visit(notAnObject, [](jobject&){}), Exception );
	}
	//fwk-refactor B2: FindValue(const jvalue&, path) returned optional<jvalue> - a deep copy of the addressed subtree on
	//every Settings::FindNumber/FindBool - while the jobject overload returned a pointer.  Now both point into the source.
	TEST_F( JsonTests, FindValueOnAValuePointsIntoIt ){
		const jvalue v = jobject{ {"a", jobject{{"b", 7}, {"flag", true}}}, {"arr", jarray{1,2}} };
		let p = Json::FindValue( v, "/a/b" );
		ASSERT_NE( p, nullptr );
		EXPECT_EQ( p, &v.as_object().at("a").as_object().at("b") ) << "a pointer into v, not a copy";
		EXPECT_EQ( Json::FindValue(v, "/missing"), nullptr );
		EXPECT_EQ( Json::FindNumber<int>(v, "/a/b"), 7 );
		EXPECT_EQ( Json::FindValue(v, ""), &v ) << "an empty pointer is the value itself - Boost's own rule (RFC 6901)";
		EXPECT_EQ( Json::FindNumber<int>(v.as_object().at("a").as_object().at("b"), {}), 7 ) << "so FindNumber needs no branch for it - GroupAwait's memberId list";
		EXPECT_EQ( Json::FindBool(v, "/a/flag"), true );
		EXPECT_FALSE( Json::FindBool(v, "/a/b").has_value() ) << "wrong kind is nullopt, not a throw";
		EXPECT_EQ( Json::FindObject(v, "/a"), &v.as_object().at("a").as_object() );
	}

	//The jobject overload is the tolerant one:  every path a product reads through a jobject is spelled without the leading
	//'/', which the jvalue overload, Settings', refuses.  The pointer check comes first, so the spelling one can't pass on two
	//nullptrs.
	TEST_F( JsonTests, FindValueOnAnObjectToleratesBothSpellings ){
		jobject o{ {"a", jobject{{"b", 7}}}, {"k", 1} };
		EXPECT_EQ( Json::FindValue(o, "a/b"), &o.at("a").as_object().at("b") ) << "the legacy unrooted spelling resolves";
		EXPECT_EQ( Json::FindValue(o, "/a/b"), Json::FindValue(o, "a/b") ) << "the rooted spelling strips its '/'";
		EXPECT_EQ( Json::FindValue(o, "k"), &o.at("k") ) << "a bare key";
		EXPECT_EQ( Json::FindValue(o, "missing/b"), nullptr );
		EXPECT_EQ( Json::FindValue(o, "a/missing"), nullptr );
		EXPECT_EQ( Json::FindValue(o, "a/"), nullptr ) << "a trailing slash addresses the empty key, not the parent";
		EXPECT_EQ( Json::FindValue(o, ""), nullptr ) << "unlike the jvalue overload, for which an empty path is the value itself";
		EXPECT_EQ( Json::FindNumber<int>(o, "a/b"), 7 ) << "what AclAwait's permissionRight/id goes through";
		const jvalue v = o;
		EXPECT_NE( Json::FindValue(v, "/a/b"), nullptr );
		EXPECT_EQ( Json::FindValue(v, "a/b"), nullptr ) << "the jvalue overload stays strict - SettingsTests.UnrootedPathResolvesToNothing";
	}

	//Every jobject overload resolves through FindValue, so a bare key and a path are one argument - there are no Path twins.
	//AsSV and AsArray used to look for a key with a slash in it, and throw.
	TEST_F( JsonTests, ObjectOverloadsTakeAPath ){
		jobject o{ {"a", jobject{ {"s","x"}, {"n",7}, {"flag",true}, {"arr",jarray{1}}, {"obj",jobject{{"k",1}}}, {"at","2024-01-02T03:04:05Z"}, {"every","PT1M"} }}, {"top","t"} };
		const jobject& c = o;
		EXPECT_EQ( Json::AsSV(c, "top"), "t" ) << "a bare key is a one-token path";
		EXPECT_EQ( Json::AsSV(c, "a/s"), "x" );
		EXPECT_EQ( Json::AsSV(c, "/a/s"), "x" ) << "the rooted spelling too";
		EXPECT_EQ( Json::AsString(c, "a/s"), "x" );
		EXPECT_EQ( Json::AsArray(c, "a/arr").size(), 1u );
		EXPECT_EQ( &Json::AsObject(c, "a/obj"), &o.at("a").as_object().at("obj").as_object() );
		EXPECT_EQ( &Json::AsObject(o, "a/obj"), &o.at("a").as_object().at("obj").as_object() ) << "the mutable overload";
		EXPECT_THROW( Json::AsSV(c, "a/missing"), Exception );
		EXPECT_THROW( Json::AsArray(c, "a/s"), CodeException ) << "found, but not an array";

		EXPECT_EQ( Json::FindSV(c, "a/s"), "x" );
		EXPECT_EQ( Json::FindString(c, "a/s"), "x" );
		EXPECT_EQ( Json::FindDefaultSV(c, "a/s"), "x" );
		EXPECT_EQ( Json::FindNumber<int>(c, "a/n"), 7 );
		EXPECT_EQ( Json::FindBool(c, "a/flag"), true );
		EXPECT_EQ( Json::FindArray(c, "a/arr"), &o.at("a").as_object().at("arr").as_array() );
		EXPECT_EQ( Json::FindDefaultArray(c, "a/arr").size(), 1u );
		EXPECT_EQ( Json::FindObject(c, "a/obj"), &o.at("a").as_object().at("obj").as_object() );
		EXPECT_EQ( Json::FindObject(o, "a/obj"), &o.at("a").as_object().at("obj").as_object() ) << "the mutable overload";
		EXPECT_EQ( Json::FindDefaultObject(c, "a/obj").size(), 1u );
		EXPECT_EQ( Json::FindDuration(c, "a/every"), 1min );
		EXPECT_EQ( Json::FindTimePoint(c, "a/at"), Chrono::ToTimePoint("2024-01-02T03:04:05Z") );
		EXPECT_FALSE( Json::FindSV(c, "a/n").has_value() ) << "wrong kind is nullopt, as with a key";
	}

	//Eval builds a conversion's message only once it is throwing, so a success pays for no serialize - the failure still names the
	//value, in the one shape every value conversion shares.
	TEST_F( JsonTests, AsConversionsNameTheValueOnFailure ){
		EXPECT_EQ( Json::AsNumber<int>(jvalue{7}), 7 );
		EXPECT_EQ( Json::AsSV(jvalue{"s"}), "s" );
		EXPECT_TRUE( Json::AsBool(jvalue{true}) );

		let number = codeWhat( []{ return Json::AsNumber<int>(jvalue{"seven"}); } );
		EXPECT_NE( number.find("'\"seven\"' could not convert to number."), string::npos ) << number;
		let string = codeWhat( []{ return Json::AsSV(jvalue{12}); } );
		EXPECT_NE( string.find("'12' could not convert to string."), string::npos ) << string;
		let boolean = codeWhat( []{ return Json::AsBool(jobject{{"flag", 1}}, "flag"); } );
		EXPECT_NE( boolean.find("'1' could not convert to bool."), string::npos ) << boolean;
		let array = codeWhat( []{ return Json::AsArray(jvalue{1}); } );
		EXPECT_NE( array.find("'1' could not convert to array."), string::npos ) << array;
		let object = codeWhat( []{ return Json::AsObject(jvalue{jarray{1}}); } );
		EXPECT_NE( object.find("'[1]' could not convert to object."), string::npos ) << object;
		EXPECT_THROW( Json::AsObject(jvalue{jobject{{"a",1}}}, "/b"), Exception ) << "a path on a value that isn't there";
		EXPECT_EQ( Json::AsObject(jvalue{jobject{{"a",jobject{{"b",1}}}}}, "/a").size(), 1u );
	}

	//One try_value_to body for every element type: a number converts as AsNumber did, exact or not at all, and a string as AsString did.
	TEST_F( JsonTests, FromArray ){
		EXPECT_EQ( Json::FromArray<uint32>(jarray{1, 2u, 3.0}), (vector<uint32>{1,2,3}) ) << "a whole double converts";
		EXPECT_EQ( Json::FromArray<string>(jarray{"a", "b"}), (vector<string>{"a","b"}) );
		EXPECT_TRUE( Json::FromArray<uint>(jarray{}).empty() );

		EXPECT_THROW( Json::FromArray<uint32>(jarray{1, 2.5}), CodeException ) << "not exact";
		EXPECT_THROW( Json::FromArray<uint32>(jarray{-1}), CodeException );
		EXPECT_THROW( Json::FromArray<uint32>(jarray{"1"}), CodeException ) << "no string-to-number";
		let what = codeWhat( []{ return Json::FromArray<string>(jarray{"a", 1}); } );
		EXPECT_NE( what.find("Element 1, '1', could not convert."), string::npos ) << what;
	}
}