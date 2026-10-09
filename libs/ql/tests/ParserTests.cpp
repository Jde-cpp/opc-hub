//Tokenizer + argument parsing.  Everything here is schema-free: Parser::Next/Peek/Trim and the static
//Parser::ParseArgs are pure string->string/json, so no AppSchema or data source is needed.
#include <gtest/gtest.h>
#include <jde/ql/ql.h>
#include <jde/ql/types/Parser.h>
#include "UnitSchema.h"

#define let const auto

namespace Jde::QL::Tests{
	Ω parser( string text, sv delimiters="{}()," )ι->Parser{ return Parser{ move(text), delimiters }; }

	TEST( ParserTests, NextSplitsOnWhitespaceAndDelimiters ){
		auto p = parser( "users( id: 42 ){ name }" );
		EXPECT_EQ( p.Next(), "users" );
		EXPECT_EQ( p.Next(), "(" );   //a delimiter hit at the token start is returned as a 1-char token.
		EXPECT_EQ( p.Next(), "id:" ); //':' is not a delimiter - it stays glued to the name.
		EXPECT_EQ( p.Next(), "42" );
		EXPECT_EQ( p.Next(), ")" );
		EXPECT_EQ( p.Next(), "{" );
		EXPECT_EQ( p.Next(), "name" );
		EXPECT_EQ( p.Next(), "}" );
		EXPECT_EQ( p.Next(), "" );    //exhausted.
	}

	TEST( ParserTests, PeekDoesNotConsume ){
		auto p = parser( "users { id }" );
		EXPECT_EQ( p.Peek(), "users" );
		EXPECT_EQ( p.Peek(), "users" ); //idempotent.
		EXPECT_EQ( p.Next(), "users" );
		EXPECT_EQ( p.Next(), "{" );
	}

	TEST( ParserTests, NextToCharReturnsThroughTerminator ){
		auto p = parser( "( id: 42 ) { name }" );
		EXPECT_EQ( p.Next(')'), "( id: 42 )" );
		EXPECT_EQ( p.Next(), "{" );
	}

	//Next(char) has to walk over quoted strings so a terminator inside a literal doesn't end the token early.
	TEST( ParserTests, NextToCharSkipsQuotedTerminator ){
		auto p = parser( R"((name: "a)b") rest)" );
		EXPECT_EQ( p.Next(')'), R"((name: "a)b"))" );
	}

	TEST( ParserTests, NextToCharUnterminatedQuoteThrows ){
		auto p = parser( R"((name: "abc)" );
		EXPECT_THROW( p.Next(')'), Exception );
	}

	//review3 #38: Next(char) skipped any backslash followed by a quote as an escaped quote, with no escaped-backslash state, so
	//a value *ending* in a backslash - which is how every JSON.stringify'd arg spells one - ate its own closing quote and the
	//scan ran to the end of the request.  parseString has kept the state correctly since #15;  this is the same rule, in the
	//other scanner.  Only an even run of backslashes before the quote ever mis-scanned, which is why it survived this long.
	TEST( ParserTests, ValueEndingInABackslashKeepsItsClosingQuote ){
		const vector<sp<DB::AppSchema>> noSchemas;
		EXPECT_NO_THROW( QL::Parse(R"(logs(path:"C:\\"){ id })", {}, noSchemas) );
		EXPECT_NO_THROW( QL::Parse(R"(logs(path:"C:\\", x:1){ id })", {}, noSchemas) );//and with an argument after it.
		let q = QL::ParseQuery( R"(logs(path:"C:\\"){ id })", {}, noSchemas );
		EXPECT_EQ( q.As<jstring>("path"), R"(C:\)" );//one backslash - the value the client sent, not a parse error.
		EXPECT_EQ( parser(R"((a:"x\\") rest)").Next(')'), R"((a:"x\\"))" );
	}
	//the control the fix must not break:  an escaped quote is still an escaped quote, and an escaped backslash before one does
	//not cancel it.
	TEST( ParserTests, EscapedQuoteStillDoesNotEndTheString ){
		const vector<sp<DB::AppSchema>> noSchemas;
		EXPECT_NO_THROW( QL::Parse(R"(logs(path:"a\"b"){ id })", {}, noSchemas) );
		EXPECT_EQ( parser(R"((a:"x\\\"y") rest)").Next(')'), R"((a:"x\\\"y"))" );
	}

	TEST( ParserTests, TrimStripsLeadingTokenAndOuterBraces ){
		auto p = parser( "query { users { id } }" );
		EXPECT_TRUE( p.Trim("query") );
		EXPECT_EQ( p.Next(), "users" ); //the outer { } went with the trim.
		EXPECT_FALSE( parser("users { id }").Trim("query") );
	}

	//Trim() is called after Peek() in QL::Parse - the peeked token must not be re-emitted afterwards.
	TEST( ParserTests, TrimAfterPeekResetsPeek ){
		auto p = parser( "query { users { id } }" );
		EXPECT_EQ( p.Peek(), "query" );
		EXPECT_TRUE( p.Trim("query") );
		EXPECT_EQ( p.Peek(), "users" );
	}

	TEST( ParserTests, ParseArgsQuotesBareKeys ){
		let args = Parser::ParseArgs( R"({id: 42, name: "bob", active: true, missing: null, ratio: -1.5})" );
		EXPECT_EQ( args.at("id").to_number<uint>(), 42u );
		EXPECT_EQ( args.at("name").as_string(), "bob" );
		EXPECT_TRUE( args.at("active").as_bool() );
		EXPECT_TRUE( args.at("missing").is_null() );
		EXPECT_DOUBLE_EQ( args.at("ratio").to_number<double>(), -1.5 );
	}

	//$name becomes the escaped string "\b$name"; Input::ExtrapolateVariables resolves it later.
	TEST( ParserTests, ParseArgsEncodesVariables ){
		let args = Parser::ParseArgs( R"({id: $userId})" );
		EXPECT_EQ( args.at("id").as_string(), string{Input::Escape}+"userId" );
	}

	TEST( ParserTests, ParseArgsNestedObjectsAndArrays ){
		let args = Parser::ParseArgs( R"({id: {in: [1,2,3]}, names: ["a","b"], nested: {a: {b: "c"}}})" );
		let& in = args.at("id").as_object().at("in").as_array();
		ASSERT_EQ( in.size(), 3u );
		EXPECT_EQ( in[2].to_number<uint>(), 3u );
		EXPECT_EQ( args.at("names").as_array().size(), 2u );
		EXPECT_EQ( args.at("nested").as_object().at("a").as_object().at("b").as_string(), "c" );
	}

	//review3 #15: an unterminated string ran parseString to the end and it returned json.size(); parseObject added 1 and
	//substr walked off the end with a std::out_of_range - a logic_error, which no wire handler catches (kSubscription's
	//ι frame terminated the server, kQuery/HTTP simply never answered).  Both the shape ParseArgs manufactures and the
	//query text that manufactures it are now ordinary Jde::Exceptions.
	TEST( ParserTests, UnterminatedStringThrowsAJdeException ){
		for( let args : {R"({"id})", R"({name: "bob)", R"({name: "a\")"} ){
			try{
				Parser::ParseArgs( string{args} );
				ADD_FAILURE() << args << " parsed";
			}
			catch( const Exception& e ){
				EXPECT_NE( string{e.what()}.find("Expected ending quote"), string::npos ) << e.what();
			}
		}
	}
	//the query shapes that reach it:  an arg list whose ')' never arrives, which ParseArgs() used to overwrite blindly.
	TEST( ParserTests, UnterminatedArgListThrowsAJdeException ){
		const vector<sp<DB::AppSchema>> noSchemas;
		for( let text : {R"(logs("id")", R"(createLog("id")", R"(subscription userCreated { userCreated("id")"} ){
			try{
				QL::Parse( string{text}, {}, noSchemas );
				ADD_FAILURE() << text << " parsed";
			}
			catch( const Exception& e ){
				EXPECT_NE( string{e.what()}.find("Expected"), string::npos ) << e.what();
			}
		}
	}

	//review3 #18: Next(char) stops at size-1 whether or not it saw the terminator, and ParseArgs() overwrote that last
	//character with '}' - so `deleteSetting(id:12` was not an error, it was a delete of id *1*.  Same root as #15, one check.
	//The shapes below are the write-up's own, and the last two are the ones that made it silent: a 2+-character literal is
	//truncated rather than malformed, where `true` or a single digit happens to break the json and throws anyway.
	TEST( ParserTests, UnterminatedArgListIsNotSilentlyTruncated ){
		const vector<sp<DB::AppSchema>> noSchemas;
		for( let text : {"deleteSetting(id:12", "{ deleteSetting(id:12 }", "mutation{ deleteSetting(id:12", "logs(id:12", "logs(a:$b"} ){
			try{
				QL::Parse( string{text}, {}, noSchemas );
				ADD_FAILURE() << "'" << text << "' parsed";
			}
			catch( const Exception& e ){
				EXPECT_NE( string{e.what()}.find("Expected"), string::npos ) << e.what();
			}
		}
	}
	//the control the finding names:  the same text, closed.
	TEST( ParserTests, TerminatedArgListStillParses ){
		let args = Parser::ParseArgs( R"({id: 12})" );
		EXPECT_EQ( args.at("id").to_number<uint>(), 12u ); //12, not 1 - the digit the truncation used to eat.
	}
	//LoadUnsubscriptions is the other Next(char) caller.
	TEST( ParserTests, UnterminatedUnsubscribeThrows ){
		const vector<sp<DB::AppSchema>> noSchemas;
		try{
			QL::Parse( "unsubscribe{ id:[1,2]", {}, noSchemas );
			ADD_FAILURE() << "parsed";
		}
		catch( const Exception& e ){
			//the message matters:  without the check parseObject still refuses it, but for the wrong reason and about the
			//character the truncation ate rather than the one that never arrived.
			EXPECT_NE( string{e.what()}.find("to end unsubscribe"), string::npos ) << e.what();
		}
		EXPECT_NO_THROW( QL::Parse("unsubscribe{ id:[1,2] }", {}, noSchemas) );
	}

	//review3 #19: ParseArgs escaped newlines by running Str::Replace over the whole stringified buffer, which caught the
	//structural ones parseWhitespace copies between tokens as well - so any arg list a client pretty-printed across lines came
	//back as `(1)boost.json - syntax error`.  The escaping now happens inside parseString, where being in a literal is known.
	TEST( ParserTests, ParseArgsAcceptsNewlinesBetweenTokens ){
		let args = Parser::ParseArgs( "{\n\tname: \"x\",\n\tid: 12\n}" );
		EXPECT_EQ( args.at("name").as_string(), "x" );
		EXPECT_EQ( args.at("id").to_number<uint>(), 12u );

		EXPECT_NO_THROW( Parser::ParseArgs("{\r\n a:1\r\n}") );  //crlf, as a windows client sends it.
		EXPECT_NO_THROW( Parser::ParseArgs("{a: {\n b:1 }}") );   //nested object across lines.
		EXPECT_NO_THROW( Parser::ParseArgs("{a: [1,\n 2]}") );    //and array.
		EXPECT_NO_THROW( Parser::ParseArgs("{a: 1\t}") );         //tabs always worked - the control that nothing else changed.
	}
	//the case the old post-hoc replace was *for*:  a newline inside a string literal still has to be escaped, or json rejects it.
	TEST( ParserTests, ParseArgsEscapesNewlinesInsideStrings ){
		let args = Parser::ParseArgs( "{a: \"x\ny\"}" );
		EXPECT_EQ( args.at("a").as_string(), "x\ny" );
		let crlf = Parser::ParseArgs( "{a: \"x\r\ny\"}" );
		EXPECT_EQ( crlf.at("a").as_string(), "x\r\ny" );
	}
	//end to end, the write-up's own shape.
	TEST( ParserTests, MultiLineMutationParses ){
		let schema = schemas();
		let request = QL::Parse( "mutation createProvider(\n\tname:\"x\"\n){ id }", {}, schema );
		ASSERT_TRUE( request.IsMutation() );
		EXPECT_EQ( Json::AsSV(request.Mutations()[0].Args, "name"), "x" );
	}

	TEST( ParserTests, ParseArgsKeepsEscapedQuoteInString ){
		let args = Parser::ParseArgs( R"({name: "a\"b"})" );
		EXPECT_EQ( args.at("name").as_string(), R"(a"b)" );
	}

	//a quoted key used to skip whatever followed its closing quote as if it were the ':'.
	TEST( ParserTests, ParseArgsQuotedKeyNeedsColon ){
		let args = Parser::ParseArgs( R"({"a" : 1, "b":2})" );
		EXPECT_EQ( args.at("a").to_number<uint>(), 1u );
		EXPECT_EQ( args.at("b").to_number<uint>(), 2u );
		EXPECT_THROW( Parser::ParseArgs(R"({"a"X1})"), Exception );
		EXPECT_THROW( Parser::ParseArgs(R"({"a"})"), Exception );
		EXPECT_THROW( Parser::ParseArgs(R"({"a")"), Exception );
	}
	//a bare key ran on to the next ':' wherever it was, through ',' and '}':  `{a, b:1}` became the one key "a, b", and `{id}`
	//ate its own '}' and failed later as "Unexpected end".
	TEST( ParserTests, ParseArgsBareKeyNeedsColon ){
		EXPECT_EQ( Parser::ParseArgs("{a : 1}").at("a").to_number<uint>(), 1u );
		EXPECT_THROW( Parser::ParseArgs("{a, b:1}"), Exception );
		EXPECT_THROW( Parser::ParseArgs("{a: 1, b}"), Exception );
		EXPECT_THROW( Parser::ParseArgs("{: 1}"), Exception );
		EXPECT_THROW( Parser::ParseArgs("{a"), Exception );
		try{
			Parser::ParseArgs( "{id}" );
			ADD_FAILURE() << "parsed";
		}
		catch( const Exception& e ){
			EXPECT_NE( string{e.what()}.find("Expected ':'"), string::npos ) << e.what();
		}
	}

	TEST( ParserTests, ParseArgsEmptyObject ){
		EXPECT_TRUE( Parser::ParseArgs("{}").empty() );
	}

	//The number scanner is the json grammar: exponents parse (they used to stop the scan at the 'e'), and every other
	//json number shape survives the rewrite unchanged.
	TEST( ParserTests, ParseArgsNumbers ){
		let number = []( sv text )ε{ return Parser::ParseArgs( Ƒ("{{n: {}}}", text) ).at("n"); };
		EXPECT_DOUBLE_EQ( number("1e5").to_number<double>(), 1e5 );
		EXPECT_DOUBLE_EQ( number("1E5").to_number<double>(), 1e5 );
		EXPECT_DOUBLE_EQ( number("1.5e-3").to_number<double>(), 1.5e-3 );
		EXPECT_DOUBLE_EQ( number("-2.5E+2").to_number<double>(), -250. );
		EXPECT_EQ( number("0").to_number<uint>(), 0u );
		EXPECT_EQ( number("-7").to_number<int>(), -7 );
		EXPECT_DOUBLE_EQ( number("0.5").to_number<double>(), 0.5 );
		EXPECT_EQ( number("9007199254740993").to_number<uint>(), 9007199254740993u ); //a uint64 past double's exact range.
	}

	//Malformed numbers are now caught where they are read, not by Json::Parse on the rewritten string.
	TEST( ParserTests, ParseArgsRejectsMalformedNumbers ){
		EXPECT_THROW( Parser::ParseArgs("{n: 1.2.3-}"), Exception );
		EXPECT_THROW( Parser::ParseArgs("{n: 1.}"), Exception );   //no digit after the '.'.
		EXPECT_THROW( Parser::ParseArgs("{n: .5}"), Exception );   //json numbers need a leading digit.
		EXPECT_THROW( Parser::ParseArgs("{n: -}"), Exception );
		EXPECT_THROW( Parser::ParseArgs("{n: 1e}"), Exception );   //no digit after the exponent.
		EXPECT_THROW( Parser::ParseArgs("{n: 1e+}"), Exception );
		EXPECT_THROW( Parser::ParseArgs("{n: 007}"), Exception );  //leading zeros.
	}

	TEST( ParserTests, ParseArgsRejectsMalformed ){
		EXPECT_THROW( Parser::ParseArgs("{id: 42"), Exception );      //unterminated object.
		EXPECT_THROW( Parser::ParseArgs(R"({active: tru})"), Exception ); //'t' must spell true.
		EXPECT_THROW( Parser::ParseArgs("{id: @}"), Exception );      //unexpected character.
	}

	//ql-refactor A7: the four literals go through one parseLiteral.  false round-trips (true and null already do, above); a
	//misspelling names the literal it expected and what it found; a truncation is "Unexpected end".  NaN is checked through its
	//errors only - the pre-scan admits it, Json::Parse (boost defaults) does not.  And a literal left as the last thing in the
	//text is now the *object's* complaint: the old per-literal length checks were literal+1, so they reported it first.
	TEST( ParserTests, ParseArgsLiterals ){
		EXPECT_EQ( Parser::ParseArgs("{a: false}").at("a").as_bool(), false );
		let what = []( string text )->string{ try{ Parser::ParseArgs( move(text) ); }catch( const Exception& e ){ return e.what(); } return {}; };
		for( let& [text, expected] : vector<std::pair<string,string>>{ {"{a: fals3}", "Expected 'false' vs 'fals3'"}, {"{a: nul1}", "Expected 'null' vs 'nul1'"}, {"{a: NaX}", "Expected 'NaN' vs 'NaX'"}, {"{a: tru3}", "Expected 'true' vs 'tru3'"}, {"{a: fal", "Unexpected end"}, {"{a: Na", "Unexpected end"} } )
			EXPECT_NE( what(text).find(expected), string::npos ) << text << " -> '" << what(text) << "'";
		EXPECT_NE( what("{a: true").find("Expected '}'"), string::npos ) << what( "{a: true" ); //the literal parsed; the object is what is unterminated.
	}

	//GHSA-p7cm-772p-hhgr: the arg pre-scan recursed once per nesting level with no bound, and runs before any session check - one
	//frame of a few tens of KB overflowed the stack.  The depths here are far past what crashed it; each must now be an Exception.
	Ω nested( uint depth, sv open, sv close, sv leaf )ι->string{
		string y;
		for( uint i=0; i<depth; ++i )
			y += open;
		y += leaf;
		for( uint i=0; i<depth; ++i )
			y += close;
		return y;
	}
	Ω expectThrows( function<void()> f, sv expected )ι->void{
		try{
			f();
			ADD_FAILURE() << "parsed";
		}
		catch( const Exception& e ){
			EXPECT_NE( string{e.what()}.find(expected), string::npos ) << e.what();
		}
		catch( const runtime_error& e ){
			ADD_FAILURE() << "not a Jde Exception: " << e.what();
		}
	}
	constexpr uint Deep{ 200'000 };
	TEST( ParserTests, DeepArgObjectThrows ){
		expectThrows( []{ Parser::ParseArgs( "{a:"+nested(Deep, "{a:", "}", "1")+"}" ); }, "nest deeper" );
		EXPECT_NO_THROW( Parser::ParseArgs("{a:"+nested(Parser::MaxArgDepth-1, "{a:", "}", "1")+"}") );//the outer object is level 1.
		expectThrows( []{ Parser::ParseArgs( "{a:"+nested(Parser::MaxArgDepth, "{a:", "}", "1")+"}" ); }, "nest deeper" );
	}
	TEST( ParserTests, DeepArgArrayThrows ){
		expectThrows( []{ Parser::ParseArgs( "{a:"+nested(Deep, "[", "]", "1")+"}" ); }, "nest deeper" );
		EXPECT_NO_THROW( Parser::ParseArgs("{a:"+nested(Parser::MaxArgDepth-1, "[", "]", "1")+"}") );
		expectThrows( []{ Parser::ParseArgs( "{a:"+nested(Parser::MaxArgDepth, "[", "]", "1")+"}" ); }, "nest deeper" );
	}
	//members are siblings, not nesting - the old memberValueParse recursed once per member anyway.
	TEST( ParserTests, ManyArgMembersParse ){
		string args{ "{" }; args.reserve( Deep*12 );
		for( uint i=0; i<Deep; ++i ){
			if( i )
				args += ',';
			args += 'a';
			args += std::to_string( i );
			args += ":1";
		}
		args += "}";
		EXPECT_EQ( Parser::ParseArgs(args).size(), Deep );
	}
	//the websocket shape:  the args of a query, through QL::Parse.
	TEST( ParserTests, DeepQueryArgsThrow ){
		const vector<sp<DB::AppSchema>> noSchemas;
		expectThrows( [&]{ QL::Parse( "logs(a:"+nested(Deep, "[", "]", "1")+"){ id }", {}, noSchemas ); }, "nest deeper" );
		expectThrows( [&]{ QL::Parse( "logs(a:"+nested(Deep, "{a:", "}", "1")+"){ id }", {}, noSchemas ); }, "nest deeper" );
		expectThrows( [&]{ QL::Parse( "unsubscribe{ id:"+nested(Deep, "[", "]", "1")+" }", {}, noSchemas ); }, "nest deeper" );
	}
	//LoadTable recursed once per selection level.  `logs` and `setting` are system tables (a `logCreated` subscription keys at `logs`), so their children resolve no view and need
	//no schema.  `{ logs{ a{ a id }} }` is `depth` levels for depth opening braces.
	Ω selection( sv head, uint depth, sv open="{ a" )ι->string{ return Ƒ( "{{ {}{} }}", head, nested(depth, open, "}", " id") ); }
	TEST( ParserTests, DeepSelectionThrows ){
		const vector<sp<DB::AppSchema>> noSchemas;
		expectThrows( [&]{ QL::Parse( selection("logs", Deep), {}, noSchemas ); }, "nest deeper" );
		expectThrows( [&]{ QL::Parse( selection("logs", Deep, "{ ... on a"), {}, noSchemas ); }, "nest deeper" );
		expectThrows( [&]{ QL::Parse( selection("mutation createSetting(id:1)", Deep), {}, noSchemas ); }, "nest deeper" );
		expectThrows( [&]{ QL::Parse( selection("subscription logCreated", Deep), {}, noSchemas ); }, "nest deeper" );
		EXPECT_NO_THROW( QL::Parse(selection("logs", Parser::MaxSelectionDepth), {}, noSchemas) );
		expectThrows( [&]{ QL::Parse( selection("logs", Parser::MaxSelectionDepth+1), {}, noSchemas ); }, "nest deeper" );
	}
	//a non-ASCII byte is a negative char, which <cctype>'s classifiers leave undefined (the MSVC debug CRT asserts), and the
	//query text and its args reach them before any session check.
	TEST( ParserTests, NonAsciiBytes ){
		EXPECT_EQ( Parser::ParseArgs("{a: \"é\"}").at("a").as_string(), "é" );
		expectThrows( []{ Parser::ParseArgs( "{a: é}" ); }, "Unexpected character" );
		auto p = parser( "users { é }" );
		EXPECT_EQ( p.Next(), "users" );
		EXPECT_EQ( p.Next(), "{" );
		EXPECT_EQ( p.Next(), "é" );
		EXPECT_EQ( p.Next(), "}" );
	}

	TEST( MutationQLTests, IsMutation ){
		EXPECT_TRUE( MutationQL::IsMutation("mutation") );
		EXPECT_TRUE( MutationQL::IsMutation("createUser") );
		EXPECT_TRUE( MutationQL::IsMutation("purgeGroup") );
		EXPECT_TRUE( MutationQL::IsMutation("removeGroupMember") );
		EXPECT_FALSE( MutationQL::IsMutation("users") );
		EXPECT_FALSE( MutationQL::IsMutation("query") );
	}
	//#42: the verb has to begin a type name, not just the string.  Every name here starts with one of the ten verbs and is a
	//perfectly ordinary table or column name;  each was routed to LoadMutations and refused with "Expected '(' to start
	//function".  The distinguishing mark is the capital ParseCommand already relies on to cut the json table name out.
	TEST( MutationQLTests, ATableNameThatMerelyStartsWithAVerbIsNotAMutation ){
		for( let name : {"startups", "addresses", "removedItems", "createdAt", "stopwatches", "updates", "deleted", "restored", "executions"} )
			EXPECT_FALSE( MutationQL::IsMutation(name) ) << name;
		EXPECT_FALSE( MutationQL::IsMutation("create") ) << "a bare verb names no type - `mutation create()` still reaches LoadMutations through the keyword";
		//and the ones that really are mutations, including a multi-word type and the two map-table verbs.
		for( let name : {"purgeHistory", "updateLogSetting", "addRoleMember", "startStatus", "stopStatus", "executeThing", "restoreResource", "deleteAc"} )
			EXPECT_TRUE( MutationQL::IsMutation(name) ) << name;
	}

	TEST( MutationQLTests, ParseCommand ){
		let [name,type] = MutationQL::ParseCommand( "createUser" );
		EXPECT_EQ( name, "user" ); //verb stripped, first letter lowered.
		EXPECT_EQ( type, EMutationQL::Create );
		EXPECT_EQ( get<1>(MutationQL::ParseCommand("removeGroupMember")), EMutationQL::Remove );
		EXPECT_THROW( MutationQL::ParseCommand("frobnicateUser"), Exception );
	}

	//ql-refactor B6: the verb a command starts with and the participle a subscription ends with are one row of MutationQLNames, so a
	//subscription resolves to the EMutationQL its mutation does - for every row with a participle (execute has none).  `logs` is a
	//system table, so no schema is needed to load the subscription's fields.
	TEST( MutationQLTests, SubscriptionParticipleMatchesTheMutationVerb ){
		static const vector<sp<DB::AppSchema>> noSchemas;
		for( uint i=0; i<MutationQLNames.size(); ++i ){
			let& [verb, participle] = MutationQLNames[i];
			EXPECT_EQ( get<1>(MutationQL::ParseCommand(string{verb}+"Log")), (EMutationQL)i ) << verb;
			if( participle.empty() )
				continue;
			auto subs = QL::ParseSubscriptions( Ƒ("subscription Log{0}{{ log{0}{{ id }} }}", participle), jobject{}, noSchemas );
			ASSERT_EQ( subs.size(), 1u ) << participle;
			EXPECT_EQ( subs[0].Type, (EMutationQL)i ) << participle;
			EXPECT_EQ( subs[0].TableName, "logs" ) << participle;
		}
	}

	TEST( MutationQLTests, ToStringRoundTripsArgs ){
		MutationQL m{ "createUser", Parser::ParseArgs(R"({slug: "bob"})"), ms<jobject>(), optional<TableQL>{}, true, vector<sp<DB::AppSchema>>{}, true };
		EXPECT_EQ( m.ToString(), R"(createUser("slug":"bob"))" );
		EXPECT_EQ( m.JTableName(), "user" );
	}
}
