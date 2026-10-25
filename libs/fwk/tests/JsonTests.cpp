#include <jde/fwk/io/json.h>

#define let const auto

namespace Jde::Tests{
	struct JsonTests : public ::testing::Test{};

	// QL extrapolates mutation-result variables by merging objects: nested objects merge recursively and arrays
	// union de-duped, rather than b replacing a.  No test anywhere called Combine.
	TEST_F( JsonTests, Combine ){
		let a = jobject{ {"nested", jobject{{"x",1}}}, {"arr", jarray{1,2}}, {"onlyA", true} };
		let b = jobject{ {"nested", jobject{{"y",2}}}, {"arr", jarray{2,3}}, {"onlyB", "s"} };
		let y = Json::Combine( a, b );

		let& nested = y.at("nested").as_object();
		ASSERT_EQ( nested.size(), 2u ) << serialize(y);
		EXPECT_EQ( nested.at("x").as_int64(), 1 );
		EXPECT_EQ( nested.at("y").as_int64(), 2 );

		let& arr = y.at("arr").as_array();
		ASSERT_EQ( arr.size(), 3u ) << "arrays union rather than concatenate - the shared 2 appears once: " << serialize(y);
		EXPECT_EQ( arr[0].as_int64(), 1 );
		EXPECT_EQ( arr[2].as_int64(), 3 );

		EXPECT_TRUE( y.at("onlyA").as_bool() );//keys from either side survive.
		EXPECT_EQ( y.at("onlyB").as_string(), "s" );

		//a collision that is neither two objects nor two arrays keeps a's value: there is no else branch.
		EXPECT_EQ( Json::Combine(jobject{{"k",1}}, jobject{{"k",2}}).at("k").as_int64(), 1 );
		EXPECT_TRUE( Json::Combine(jobject{{"k",jobject{{"x",1}}}}, jobject{{"k",2}}).at("k").is_object() );
		EXPECT_EQ( Json::Combine(jobject{}, b).size(), b.size() );
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
		EXPECT_EQ( Json::FindNumber<int>(v.as_object().at("a").as_object().at("b"), {}), 7 ) << "empty path = the value itself";
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
		EXPECT_EQ( Json::FindNumberPath<int>(o, "a/b"), 7 ) << "what AclAwait's permissionRight/id goes through";
		const jvalue v = o;
		EXPECT_NE( Json::FindValue(v, "/a/b"), nullptr );
		EXPECT_EQ( Json::FindValue(v, "a/b"), nullptr ) << "the jvalue overload stays strict - SettingsTests.UnrootedPathResolvesToNothing";
	}
}