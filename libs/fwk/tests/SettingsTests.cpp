#include <jde/fwk/settings.h>
#include <jde/fwk/process/process.h>
#include <jde/fwk/log/MemoryLog.h>
#include <jde/fwk/log/SpdLog.h>
#include <cstdlib>
#ifdef _WIN32 //CommandLineToArgvW, to round-trip ServiceCommandLine through the parser that reads it back.
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#include <windows.h>
	#include <shellapi.h>
	#pragma comment( lib, "shell32.lib" )
#endif

#define let const auto

namespace Jde::Tests{
	//Args() feeds -settings/-include/-tests, so every suite's config selection goes through these rules and none of
	//them was asserted anywhere.  ParseArgs is the seam: Args() itself can only ever see this process's own argv,
	//and the rules that matter are the ones it applies to tokens the platform already split.
	Ω parsed( const vector<string>& tokens )ι->flat_multimap<string,string>{ return Process::ParseArgs( tokens ); }
	Ω one( const flat_multimap<string,string>& args, str key )ι->optional<string>{
		let p = args.find( key );
		return p!=args.end() ? p->second : optional<string>{};
	}

	TEST( ProcessTests, ParseArgsBindsEachForm ){
		let args = parsed( {"exe", "-settings=/a/b.jsonnet", "-tests", "-include", "args/sqlite", "positional", "-arg", "path=:memory:"} );
		EXPECT_EQ( one(args,"-settings"), "/a/b.jsonnet" ) << "-k=v splits on the first '='";
		EXPECT_EQ( one(args,"-tests"), "" ) << "a flag with a flag after it binds empty, and must still be *present*";
		EXPECT_TRUE( args.contains("-tests") ) << "an empty value is not absence - FindArg('-tests') gates the whole test-mode path";
		EXPECT_EQ( one(args,"-include"), "args/sqlite" ) << "-k v binds on the next token";
		EXPECT_EQ( one(args,"-arg"), "path=:memory:" ) << "only the *flag* splits on '=' - a value keeps its own";
		//non-flag tokens with no flag pending are positional, under the empty key.  argv[0] is one of them, which is
		//what Process::Executable() reads on windows.
		let positionals = args.equal_range( string{} );
		EXPECT_EQ( std::distance(positionals.first, positionals.second), 2 );
		EXPECT_EQ( positionals.first->second, "exe" ) << "argv[0] is the first positional";
	}

	TEST( ProcessTests, ParseArgsStripsQuotesAndHandlesEdges ){
		//lldb and VS Code pass argv unshelled, so the quotes arrive as literal characters.
		EXPECT_EQ( one(parsed({"-settings=\"/a b/c.jsonnet\""}), "-settings"), "/a b/c.jsonnet" );
		EXPECT_EQ( one(parsed({"-k=\"unclosed"}), "-k"), "\"unclosed" ) << "only a matched pair is stripped";
		EXPECT_EQ( one(parsed({"-k=\"\""}), "-k"), "" ) << "an empty quoted value is empty, not a lone quote";
		EXPECT_EQ( one(parsed({"-k="}), "-k"), "" ) << "a trailing '=' is an empty value";
		EXPECT_EQ( one(parsed({"-k=a=b"}), "-k"), "a=b" ) << "the *first* '=' splits - a value may contain more, as -arg path=:memory: does";
		EXPECT_EQ( one(parsed({"-a", "-b", "-c"}), "-a"), "" ) << "every flag in a run of flags is kept";
		EXPECT_TRUE( parsed({"-a","-b","-c"}).contains("-c") ) << "including the last, which only the post-loop flush emplaces";
		EXPECT_EQ( parsed({}).size(), 0u );
		//a repeated flag keeps both: it is a multimap, and -arg is passed more than once by the ctest registration.
		let repeated = parsed( {"-arg", "a=1", "-arg", "b=2"} );
		EXPECT_EQ( repeated.count("-arg"), 2u );
	}

	//What -install hands the SCM.  The other end is CommandLineToArgvW (Args() on windows), so the rules are its.  An
	//unquoted `C:\Program Files\…\x.exe` came back as argv[0]=`C:\Program`: Executable() pointed at nothing, ProductName()
	//fell back to "Jde-cpp" and the service grew a second data tree - hence the exe is quoted whether or not it needs it.
	TEST( ProcessTests, ServiceCommandLineQuotesExeAndSpacedArgs ){
		const fs::path exe{ "C:\\Program Files\\Jde-Cpp\\OpcHub\\Jde.Opc.Hub.exe" };
		EXPECT_EQ( Process::ServiceCommandLine(exe, {}), "\"C:\\Program Files\\Jde-Cpp\\OpcHub\\Jde.Opc.Hub.exe\"" );
		EXPECT_EQ( Process::ServiceCommandLine(exe, {"-settings=C:\\ProgramData\\Jde-Cpp\\config\\Opc.Hub.jsonnet", "-include=args/install", "-sync"}),
			"\"C:\\Program Files\\Jde-Cpp\\OpcHub\\Jde.Opc.Hub.exe\" -settings=C:\\ProgramData\\Jde-Cpp\\config\\Opc.Hub.jsonnet -include=args/install -sync" ) << "plain tokens pass through unquoted - `sc qc` stays readable";
		EXPECT_EQ( Process::ServiceCommandLine("x.exe", {"-settings=C:\\My Dir\\a.jsonnet"}), "\"x.exe\" \"-settings=C:\\My Dir\\a.jsonnet\"" ) << "a space quotes the whole token";
		EXPECT_EQ( Process::ServiceCommandLine("x.exe", {"a\"b"}), "\"x.exe\" \"a\\\"b\"" ) << "an embedded quote is escaped";
		EXPECT_EQ( Process::ServiceCommandLine("x.exe", {"C:\\dir\\"}), "\"x.exe\" C:\\dir\\" ) << "unquoted, a trailing backslash is literal";
		EXPECT_EQ( Process::ServiceCommandLine("x.exe", {"C:\\my dir\\"}), "\"x.exe\" \"C:\\my dir\\\\\"" ) << "quoted, it doubles or it would escape the closing quote";
		EXPECT_EQ( Process::ServiceCommandLine("x.exe", {""}), "\"x.exe\" \"\"" ) << "an empty token survives as a quoted empty";
	}

#ifdef _WIN32
	//and the composed line parses back to the tokens it was built from, through the parser Args() uses.
	TEST( ProcessTests, ServiceCommandLineRoundTrips ){
		const fs::path exe{ "C:\\Program Files\\Jde-Cpp\\OpcHub\\Jde.Opc.Hub.exe" };
		const vector<string> args{ "-settings=C:\\ProgramData\\Jde-Cpp\\config\\Opc.Hub.jsonnet", "-include=args/install", "-arg", "path=C:\\my db\\a.db", "quote\"inside", "trail\\", "" };
		let line = Process::ServiceCommandLine( exe, args );
		const std::wstring wide{ line.begin(), line.end() };//ascii by construction
		int count{};
		LPWSTR* argv = ::CommandLineToArgvW( wide.c_str(), &count );
		ASSERT_NE( argv, nullptr );
		vector<string> tokens;
		for( int i=0; i<count; ++i ){
			const std::wstring w{ argv[i] };
			tokens.emplace_back( w.begin(), w.end() );
		}
		::LocalFree( argv );
		ASSERT_EQ( tokens.size(), args.size()+1 );
		EXPECT_EQ( tokens[0], exe.string() ) << "argv[0] is the whole path - what Executable() and loadResource read";
		for( uint i=0; i<args.size(); ++i )
			EXPECT_EQ( tokens[i+1], args[i] ) << "token " << i;
		let parsed = Process::ParseArgs( tokens );
		EXPECT_EQ( one(parsed,"-settings"), "C:\\ProgramData\\Jde-Cpp\\config\\Opc.Hub.jsonnet" );
		EXPECT_EQ( one(parsed,"-include"), "args/install" );
		EXPECT_EQ( one(parsed,"-arg"), "path=C:\\my db\\a.db" );
	}
#endif

	//and the parsed rules really are what this process is running under - the two forms every suite passes.
	TEST( ProcessTests, ThisSuiteArgsParsed ){
		//a direct run passes -tests, addJdeTest passes -ctest - either one binds the ext vars (settings.cpp checks both).  Asserting
		//only -tests made this test fail under ctest, which the config's one-test filter had been hiding.
		let testFlag = Process::FindArg("-tests") ? optional<string>{"-tests"} : Process::FindArg("-ctest") ? optional<string>{"-ctest"} : nullopt;
		ASSERT_TRUE( testFlag.has_value() ) << "-tests or -ctest is how the config binds its ext vars; without one settings evaluation fails";
		EXPECT_EQ( *Process::FindArg(*testFlag), "" ) << "a bare flag, no value";
		let settings = Process::FindArg( "-settings" );
		ASSERT_TRUE( settings.has_value() );
		EXPECT_TRUE( settings->ends_with("Framework.Tests.jsonnet") ) << *settings;
	}

	Ω setEnv( const char* name, const char* value )ι->void{
#ifdef _WIN32
		::_putenv_s( name, value );//what GetEnv's _dupenv_s reads.
#else
		::setenv( name, value, 1 );
#endif
	}
	//Settings::Value() is a jvalue, so every lookup is a json pointer and try_at_pointer rejects one with no leading
	//'/' - a path spelled "cryptoTests/clear" resolves to nothing and the caller's value_or() default wins silently.
	//That is how /http/accessControl/allowOrigin, /server/host, /web/client/timeout and /cryptoTests/clear all sat
	//dead for months.  These pin both halves: the rooted spelling reads the config, the unrooted one reads nothing.
	struct SettingsTests : public ::testing::Test{};

	TEST_F( SettingsTests, RootedPathResolves ){
		EXPECT_EQ( Settings::FindBool("/cryptoTests/clear"), false );//not just has_value - the config sets it to the opposite of every caller's default, so a silent miss is indistinguishable from a hit unless the value is asserted.
		EXPECT_EQ( Settings::FindString("/logging/spd/tags/default"), "Information" );
		EXPECT_EQ( Settings::FindNumber<uint16>("/workers/executor/threads"), 2 );
		EXPECT_TRUE( Settings::FindPath("/testing/file").has_value() );
		EXPECT_NE( Settings::FindObject("/logging/spd"), nullptr );
	}

	//The trap, pinned deliberately: a Find* that comes back empty for a key plainly present in the config means the
	//path is missing its leading '/'.  If tolerance is ever added to Settings, this test is what says so.
	TEST_F( SettingsTests, UnrootedPathResolvesToNothing ){
		EXPECT_FALSE( Settings::FindBool("cryptoTests/clear").has_value() );
		EXPECT_FALSE( Settings::FindString("logging/spd/tags/default").has_value() );
		EXPECT_FALSE( Settings::FindNumber<uint16>("workers/executor/threads").has_value() );
		EXPECT_EQ( Settings::FindObject("logging/spd"), nullptr );
	}

	//a rooted path that matches nothing is still empty - no partial-match fallback.
	TEST_F( SettingsTests, MissingPathIsEmpty ){
		EXPECT_FALSE( Settings::FindBool("/cryptoTests/notAKnob").has_value() );
		EXPECT_FALSE( Settings::FindString("/notASection/notAKnob").has_value() );
	}

	//Set's create_objects replaced a hand-rolled fallback that corrupted the root, so the unrelated key below is
	//half the point of the test.  Everything here writes under /settingsTests, which nothing else reads.
	TEST_F( SettingsTests, SetCreatesIntermediateObjects ){
		Settings::Set( "/settingsTests/nested/value", 42 );
		EXPECT_EQ( Settings::FindNumber<uint>("/settingsTests/nested/value"), 42u );
		EXPECT_NE( Settings::FindObject("/settingsTests/nested"), nullptr ) << "the intermediate object must exist, not just the leaf";
		EXPECT_EQ( Settings::FindBool("/cryptoTests/clear"), false ) << "an unrelated key must survive the set";

		Settings::Set( "/settingsTests/nested/value", 43 );//and an existing leaf overwrites in place.
		EXPECT_EQ( Settings::FindNumber<uint>("/settingsTests/nested/value"), 43u );
	}

	//$(NAME) expansion has produced silent production misconfigurations twice - HostName resolving to "" before it
	//was a builtIn, and array entries left literal - and had no test either time.
	TEST_F( SettingsTests, EnvExpansion ){
		setEnv( "JDE_TEST_ENV", "expanded" );
		Settings::Set( "/settingsTests/env", "[$(JDE_TEST_ENV)][$(HostName)][$(JDE_NOT_SET_12345)]" );
		EXPECT_EQ( Settings::FindString("/settingsTests/env"), Ƒ("[expanded][{}][]", Process::HostName()) ) << "env var, builtIn, and unknown -> empty";

		//a real environment variable outranks the builtIn of the same name.
		let hostName = Process::GetEnv( "HostName" );
		setEnv( "HostName", "overridden-host" );
		Settings::Set( "/settingsTests/host", "$(HostName)" );
		EXPECT_EQ( Settings::FindString("/settingsTests/host"), "overridden-host" );
		setEnv( "HostName", hostName ? hostName->c_str() : "" );//"" reads back as unset, which is where it started.
		EXPECT_EQ( Settings::FindString("/settingsTests/host"), Process::HostName() );
	}

	//$(ExeDir) is how the installed args files reach the db driver and proc modules beside the exe wherever the installer
	//put it (Program Files, or a per-user Programs dir) - the OS's exe path, not argv[0], which is whatever the launcher spelled.
	TEST_F( SettingsTests, ExeDirBuiltInExpands ){
		Settings::Set( "/settingsTests/exeDir", "$(ExeDir)/x.dll" );
		EXPECT_EQ( Settings::FindString("/settingsTests/exeDir"), Process::ExePath().parent_path().string()+"/x.dll" );
		setEnv( "ExeDir", "/env-wins" );
		EXPECT_EQ( Settings::FindString("/settingsTests/exeDir"), "/env-wins/x.dll" ) << "a real environment variable outranks the builtIn, as for HostName";
		setEnv( "ExeDir", "" );//"" reads back as unset.
		EXPECT_EQ( Settings::FindString("/settingsTests/exeDir"), Process::ExePath().parent_path().string()+"/x.dll" );
	}

	//the rest of the table.  LibPrefix and LibExt are how the installed args files name the db driver on both platforms, and
	//no suite reaches them:  every suite's driver comes from sqlite-common.libsonnet, which spells it out in jsonnet.
	TEST_F( SettingsTests, PlatformBuiltInsExpand ){
		Settings::Set( "/settingsTests/lib", "$(ExeDir)/$(LibPrefix)Jde.DB.Sqlite$(LibExt)" );
		EXPECT_EQ( Settings::FindString("/settingsTests/lib"), Process::ExePath().parent_path().string()+(_windows ? "/Jde.DB.Sqlite.dll" : "/libJde.DB.Sqlite.so") ) << "the installed args' driver spelling";
		Settings::Set( "/settingsTests/build", "$(JDE_BUILD_TYPE)" );
		EXPECT_EQ( Settings::FindString("/settingsTests/build"), _debug ? "Debug" : "Release" );
		Settings::Set( "/settingsTests/product", "$(PRODUCT_NAME)/$(ProgramData)" );
		EXPECT_EQ( Settings::FindString("/settingsTests/product"), Ƒ("{}/{}", Process::ProductName(), Process::ProgramDataFolder().string()) );
	}

	//a value naming itself rescans forever without the 32-pass bound.  Note the failure mode this guards is a hang,
	//so removing the bound would wedge this test rather than fail it.
	TEST_F( SettingsTests, CyclicExpansionTerminates ){
		setEnv( "JDE_TEST_CYCLE", "$(JDE_TEST_CYCLE)" );
		Settings::Set( "/settingsTests/cycle", "$(JDE_TEST_CYCLE)" );
		EXPECT_EQ( Settings::FindString("/settingsTests/cycle"), "$(JDE_TEST_CYCLE)" ) << "gives up unexpanded after the bound, with one WARN";
	}

	//expanding only objects at load left every trustedCertDirs/scriptPaths entry literal unless its reader happened
	//to be Find{String,Path}Array - which do expand, and are what this pins.
	TEST_F( SettingsTests, ArrayEntriesExpand ){
		setEnv( "JDE_TEST_DIR", "/jde-test-dir" );
		Settings::Set( "/settingsTests/dirs", jarray{"$(JDE_TEST_DIR)/a", "$(JDE_TEST_DIR)/b", 7} );
		let strings = Settings::FindStringArray( "/settingsTests/dirs" );
		ASSERT_EQ( strings.size(), 2u ) << "non-string entries are skipped, not stringified";
		EXPECT_EQ( strings[0], "/jde-test-dir/a" );
		let paths = Settings::FindPathArray( "/settingsTests/dirs" );
		ASSERT_EQ( paths.size(), 2u );
		EXPECT_EQ( paths[1], fs::path{"/jde-test-dir/b"} );
		EXPECT_TRUE( Settings::FindStringArray("/settingsTests/notAnArray").empty() );
	}

	TEST_F( SettingsTests, FindDuration ){
		Settings::Set( "/settingsTests/duration", "PT1M" );
		EXPECT_EQ( Settings::FindDuration("/settingsTests/duration"), 1min );
		EXPECT_FALSE( Settings::FindDuration("/settingsTests/notADuration").has_value() );
	}

	//A key present but no duration is nullopt, and an operator has to see it:  it logs at Error, from the exception's
	//destructor, since one at Debug is silent at construction.  This suite takes the exception tag at Trace, which logs
	//every exception at construction at its own level, so the tag is lowered on spd, which the cumulative follows, and
	//restored however the test ends.
	TEST_F( SettingsTests, FindDurationReportsABadStringAtError ){
		Settings::Set( "/settingsTests/badDuration", "5M" );
		auto& spd = Logging::GetLogger<Logging::SpdLog>();
		struct Restore final{ Logging::SpdLog& Spd; ~Restore(){ Spd.ClearLevel( ELogTags::Exception ); } } restore{ spd };
		spd.SetLevel( ELogTags::Exception, ELogLevel::Debug );
		Logging::ClearMemory();
		EXPECT_FALSE( Settings::FindDuration("/settingsTests/badDuration").has_value() );
		let reported = Logging::Find( [](const Logging::Entry& e){ return e.Level==ELogLevel::Error && e.Message().contains("Expected 'P'"); } );
		EXPECT_EQ( reported.size(), 1u ) << "a bad config duration must reach the log at Error, once";
	}

	//FindArg*:  a valued command-line arg outranks the setting, and an empty-valued flag is no value - the rule the soak
	//runner's argFlag works around.  This process's own argv has both:  -settings=<path> and a bare -tests or -ctest.
	TEST_F( SettingsTests, FindArgPrefersAValuedArg ){
		let testFlag = Process::FindArg("-tests") ? "-tests" : "-ctest";
		constexpr sv none{ "-noSuchArg12345" };
		EXPECT_EQ( Settings::FindArgString("-settings", "/logging/spd/tags/default"), Process::FindArg("-settings") ) << "a valued arg outranks the setting";
		EXPECT_EQ( Settings::FindArgString(testFlag, "/logging/spd/tags/default"), "Information" ) << "an empty-valued flag is not a value - the setting wins";
		EXPECT_EQ( Settings::FindArgString(none, "/logging/spd/tags/default"), "Information" );
		EXPECT_FALSE( Settings::FindArgString(none, "/settingsTests/notAKnob").has_value() );

		Settings::Set( "/settingsTests/argDuration", "PT2S" );
		EXPECT_EQ( Settings::FindArgDuration(none, "/settingsTests/argDuration"), 2s );
		EXPECT_EQ( Settings::FindArgDuration(testFlag, "/settingsTests/argDuration"), 2s );
		EXPECT_THROW( Settings::FindArgDuration("-settings", "/settingsTests/argDuration"), Exception ) << "a valued arg that is not a duration throws, it does not fall back";

		Settings::Set( "/settingsTests/argNumber", 7 );
		EXPECT_EQ( Settings::FindArgNumber(none, "/settingsTests/argNumber"), 7u );
		EXPECT_THROW( Settings::FindArgNumber("-settings", "/settingsTests/argNumber"), Exception ) << "nor one that is not a number";
	}
}