#include "jde/fwk/log/ILogger.h"
#include <jde/fwk/log/MemoryLog.h>
#include <jde/fwk/log/SpdLog.h>
#include <fstream>
#define let const auto
#pragma warning( disable: 4702 )

namespace Jde::Tests{
	struct LogGeneralTests : public ::testing::Test{
	protected:
		LogGeneralTests() {}
		~LogGeneralTests() override{}

		static constexpr ELogTags MutatedTags{ ELogTags::Scheduler };
		Ω SetUpTestCase()ι->void{ }
		α SetUp()->void override{ Logging::ClearMemory(); }
		α TearDown()->void override{ Logging::GetLogger<Logging::MemoryLog>().ClearLevel( MutatedTags ); }

		α ExpectConfiguredLevel()->void{
			auto& logger = Logging::GetLogger<Logging::MemoryLog>();
			EXPECT_EQ( logger.MinLevel(MutatedTags), logger.DefaultLevel() ) << "an earlier test left an override on " << ToString( MutatedTags, false );
		}
	};

	//app-review3 L6: Logging::Add swallowed the logger constructor's exception in an empty catch, so a logger the settings ask for
	//and that cannot be built - an App::ProtoLog whose `path` cannot be created, most plainly - was simply not there: no output from
	//it, no answers from it, and nothing saying why.  The exception self-logs on destruction, but at Debug and without naming which
	//logger is missing, which is the half that made it hard to find.
	struct UnbuildableLogger final : Logging::ILogger{
		UnbuildableLogger( const jobject& o )ε:ILogger{o}{ throw Exception{"this logger cannot be created"}; }
		α Write( const Logging::Entry& )ι->void override{}
		α Write( const Logging::Entry&, uint32, uint32 )ι->void override{}
		α Shutdown( bool, SL )ι->void override{}
		α Name()Ι->sv override{ return "Unbuildable"; }
	};

	TEST_F( LogGeneralTests, AConfiguredLoggerThatCannotBeCreatedSaysSo ){
		Settings::Set( "/logging/l6-probe", jobject{} );//a section, so Add gets as far as constructing.
		Logging::ClearMemory();
		EXPECT_EQ( Logging::Add<UnbuildableLogger>("l6-probe"), nullptr );
		let reported = Logging::Find( [](const Logging::Entry& e){ return e.Level>=ELogLevel::Error && e.Text.contains("could not be created"); } );
		EXPECT_FALSE( reported.empty() ) << "a configured logger that did not start said nothing at Error";
	}

	//ELogTags is a 64-bit space that libraries extend above fwk's own 27 bits - Jde::Opc registers 32-45 through
	//AddTagParser - so All has to span the whole underlying type.  Spelled ~0ul it stopped at bit 31 under the windows
	//ABI, where unsigned long is 32-bit, and silently dropped every extension tag from an All-based subscription.
	TEST_F( LogGeneralTests, AllCoversTheFullUnderlyingWidth ){
		EXPECT_EQ( (uint)ELogTags::All, ~(std::underlying_type_t<ELogTags>)0 );
		EXPECT_NE( (uint)ELogTags::All & (1ull<<32), 0ull );//the bit range that actually went missing.
	}

	//AddTagParser takes ownership as up<ITagParser>, so every parser is destroyed through a base pointer.  Without a
	//virtual destructor on the base that is undefined behaviour, and in practice the derived destructor never runs - it
	//goes unnoticed only because the one implementation, Jde::Opc::UALogParser, holds no state to leak.
	TEST_F( LogGeneralTests, TagParserIsDestroyedThroughTheBase ){
		static bool destroyed;//a local class may use a static local of its enclosing function, not an automatic one.
		struct Counted : Logging::ITagParser{
			~Counted(){ destroyed = true; }
			α ToTag( str )Ι->ELogTags override{ return ELogTags::None; }
			α ToString( ELogTags )Ι->string override{ return {}; }
			α Tags()Ι->flat_map<string,uint> override{ return {}; }
		};
		destroyed = false;
		up<Logging::ITagParser> parser = mu<Counted>();//the conversion AddTagParser performs.
		parser.reset();
		EXPECT_TRUE( destroyed );
	}

	//config keys are split on TagSeparator and each part looked up in ELogTagStrings, so an unrecognised part is simply
	//dropped and the key silently collapses onto a shorter one's mask - where parseTags' insert_or_assign overwrites it.
	TEST_F( LogGeneralTests, CompositeTagNamesResolveDistinctly ){
		let tags = []( sv name ){ return ToLogTags( name ); };//sv, not a literal - ToLogTags(sv)/ToLogTags(jvalue) are ambiguous for const char*.
		EXPECT_EQ( tags("socket.client.read.subscription"), ELogTags::SocketClientReadSub );
		EXPECT_EQ( tags("socket.client.write.subscription"), ELogTags::SocketClientWriteSub );
		//the collision the ".sub" spelling caused: it must not resolve to the plain read/write tag.
		EXPECT_NE( tags("socket.client.read.subscription"), tags("socket.client.read") );
		EXPECT_NE( tags("socket.client.write.subscription"), tags("socket.client.write") );
		EXPECT_EQ( tags("socket.client.read"), ELogTags::SocketClientRead );
		//an unknown part contributes nothing, which is exactly how ".sub" aliased onto socket.client.read.
		EXPECT_EQ( tags("socket.client.read.sub"), ELogTags::SocketClientRead );
		//and the old separator is now just an unknown name, not a second spelling of the same tag.
		EXPECT_EQ( tags("socket_client_read"), ELogTags::None );
	}

	//tags travel as a json *value*: one tag is the bare string, several are an array, and ToLogTags reads either back.
	//`jvalue{tagString}` does not spell the first one - value's initializer_list constructor wins over the string one and
	//wraps it in an array - so a single tag came back as ELogTagStrings[0], "none", whatever tag it actually was.
	TEST_F( LogGeneralTests, ToValueSpellsOneTagBare ){
		EXPECT_EQ( ToValue(ELogTags::Sql).as_string(), "sql" );
		EXPECT_EQ( ToString(ELogTags::Sql, false), "sql" );
		EXPECT_EQ( ToValue(ELogTags::None).as_string(), "none" );
		let multi = ToValue( ELogTags::SocketClientRead );
		ASSERT_TRUE( multi.is_array() );
		EXPECT_EQ( multi.as_array().size(), 3u );//no tag parsers here, so socket|client|read is exactly its three fwk names.
		EXPECT_EQ( ToLogTags(multi), ELogTags::SocketClientRead );
		EXPECT_EQ( ToLogTags(ToValue(ELogTags::Sql)), ELogTags::Sql );
	}

	//instanceTagLevel groups the tags under their level, since a combined tag is only spellable as an array and an array
	//is no object key; SetLevels and the config file key on the tag.  The flip between them has to be lossless, or an
	//instance silently drops at startup the overrides the app server just handed it.
	TEST_F( LogGeneralTests, ToTagLevelsFlipsTheGrouping ){
		jobject grouped;
		grouped["Debug"] = jarray{ ToValue(ELogTags::Sql), ToValue(ELogTags::SocketClientRead) };
		grouped["Warning"] = jarray{ jstring{"default"} };
		let flat = ToTagLevels( grouped );
		ASSERT_EQ( flat.size(), 3u );
		EXPECT_EQ( flat.at("sql").as_string(), "Debug" );
		EXPECT_EQ( flat.at("default").as_string(), "Warning" ) << "the default level rides as the 'default' tag - parseTags looks for that exact spelling";
		auto found = false;//the combined tag joins back with the separator ToLogTags splits on, whatever order the bits came out in.
		for( let& kv : flat )
			found |= kv.key()!="sql" && kv.key()!="default" && ToLogTags(sv{kv.key()})==ELogTags::SocketClientRead;
		EXPECT_TRUE( found );
	}

	//updateInstanceTagLevel takes a record per override for the same reason: `tags` is a value, so a combined tag can be
	//an array there too.  Either spelling of it arrives - the parts, or the joined name - and both flatten the same.
	TEST_F( LogGeneralTests, ToTagLevelsReadsTheMutationArgument ){
		let flat = ToTagLevels( jarray{
			jobject{ {"tags", jarray{jstring{"socket"},jstring{"client"},jstring{"read"}}}, {"level", "Debug"} },
			jobject{ {"tags", jarray{jstring{"socket.client.write"}}}, {"level", "Error"} },
			jobject{ {"tags", jarray{jstring{"crypto"}}}, {"level", nullptr} },
			jobject{ {"tags", jarray{jstring{"parsing"}}} }//no level at all - the same "remove this override" a null is.
		} );
		ASSERT_EQ( flat.size(), 4u );
		EXPECT_TRUE( flat.contains("socket.client.read") ) << "the parts join with the separator ToLogTags splits on";
		EXPECT_EQ( flat.at("socket.client.write").as_string(), "Error" );
		EXPECT_TRUE( flat.at("crypto").is_null() );
		EXPECT_TRUE( flat.at("parsing").is_null() );

		let roundTrip = ToTagLevels( ToTagLevelArray(flat) );//and back out to the wire and in again.
		EXPECT_EQ( serialize(roundTrip), serialize(flat) );
	}

	// The level-resolution engine itself had no test.  fwk-max #3 was MinLevel counting split(tags).size() rather
	// than the overlap, so the first configured entry sharing any bit won and the answer followed
	// concurrent_flat_map iteration order; fwk-max #2 was the ctor leaving _minLevel at Information instead of the
	// parsed default.  Every case here has a strict popcount winner - a tie (http vs client for HttpClientRead)
	// really would be order-dependent, because the comparison is `iterCount>matches`.
	TEST_F( LogGeneralTests, LogTagsResolvesBestMatch ){
		//three single-bit distractors, so the buggy 'first overlapping entry wins' has to draw the one right entry
		//out of four to look correct.  They are safe for the lookups below: only socket|client|read is asserted
		//against them, and there its 2-bit entry beats each of them outright.
		LogTags t{ jobject{{"tags", jobject{{"default","Warning"},{"socket.client","Debug"},{"http","Trace"},
			{"socket","Information"},{"client","Error"},{"read","Critical"}}}} };
		EXPECT_EQ( t.DefaultLevel(), ELogLevel::Warning );
		EXPECT_EQ( t.MinLevel(), ELogLevel::Warning ) << "the ctor must carry the parsed default into _minLevel";
		EXPECT_EQ( t.MinLevel(ELogTags::Http), ELogLevel::Trace );//exact match.
		EXPECT_EQ( t.MinLevel(ELogTags::SocketClientRead), ELogLevel::Debug ) << "socket.client's 2 shared bits must beat every 1-bit entry";
		EXPECT_EQ( t.MinLevel(ELogTags::Sql), ELogLevel::Warning ) << "nothing overlaps sql - it falls back to the default";
		EXPECT_TRUE( t.ShouldLog(ELogLevel::Debug, ELogTags::SocketClientRead) );
		EXPECT_FALSE( t.ShouldLog(ELogLevel::Trace, ELogTags::SocketClientRead) );
	}

	// MinLevel memoizes its answer into ExtrapolatedTags, so both mutators have to drop the memo as well as the
	// configuration - otherwise a cleared or re-set tag keeps resolving to whatever it was when first asked.
	TEST_F( LogGeneralTests, LogTagsMutatorsDropTheMemo ){
		LogTags t{ jobject{{"tags", jobject{{"default","Warning"},{"socket.client","Debug"},{"client","Error"}}}} };
		ASSERT_EQ( t.MinLevel(ELogTags::SocketClientRead), ELogLevel::Debug );//memoizes socket|client|read -> Debug
		ASSERT_EQ( t.MinLevel(ELogTags::Sql), ELogLevel::Warning );//and sql -> the default

		t.ClearLevel( ToLogTags(sv{"socket.client"}) );
		EXPECT_EQ( t.MinLevel(ELogTags::SocketClientRead), ELogLevel::Error ) << "the memoized best match survived ClearLevel";

		t.SetLevels( jobject{{"default","Critical"},{"sql","Trace"}} );//the flat tag->level map, 'default' included.
		EXPECT_EQ( t.DefaultLevel(), ELogLevel::Critical );
		EXPECT_EQ( t.MinLevel(ELogTags::Sql), ELogLevel::Trace ) << "the memoized default survived SetLevels";
	}

	// Logging::min is not std::min: NoLog is -1, below every real level, so a plain minimum would let one logger
	// that wants nothing silence every other logger through the cumulative.
	TEST_F( LogGeneralTests, CumulativeNoLogYieldsToARealLevel ){
		LogTags quiet{ ELogLevel::NoLog };
		quiet.SetLevel( ELogTags::Sql, ELogLevel::NoLog );
		LogTags loud{ jobject{{"tags", jobject{{"default","Debug"},{"sql","Debug"}}}} };
		quiet += loud;
		EXPECT_EQ( quiet.DefaultLevel(), ELogLevel::Debug );
		EXPECT_EQ( quiet.MinLevel(), ELogLevel::Debug );
		EXPECT_EQ( quiet.MinLevel(ELogTags::Sql), ELogLevel::Debug ) << "a configured NoLog must yield to the other side's level too";
		EXPECT_TRUE( quiet.ShouldLog(ELogLevel::Debug, ELogTags::Sql) );
	}

	// fwk-max #16 was the UserPK-taking variadic ctor delegating to the overload without one, so every entry built
	// for a user came out anonymous.  fwk-review2 cites a regression test named exactly this for it; the test was
	// never written, and nothing else in the suite reads Entry::UserPK back off the variadic ctors.
	TEST_F( LogGeneralTests, EntryKeepsUserPK ){
		constexpr auto userPK = Jde::UserPK{ 42 };
		Logging::Entry e{ SRCE_CUR, ELogLevel::Debug, ELogTags::Test, userPK, string{"msg {}"}, 1 };
		EXPECT_EQ( e.UserPK, userPK );
		EXPECT_EQ( e.Level, ELogLevel::Debug );
		EXPECT_EQ( e.Tags, ELogTags::Test );
		ASSERT_EQ( e.Arguments.size(), 1u ) << "delegating to the vector<string> overload must not cost the args";
		EXPECT_EQ( e.Arguments[0], "1" );
		EXPECT_EQ( e.Message(), "msg 1" );

		Logging::Entry anonymous{ SRCE_CUR, ELogLevel::Debug, ELogTags::Test, string{"msg {}"}, 1 };
		EXPECT_EQ( anonymous.UserPK, Jde::UserPK{} ) << "the overload without a user must default it";

		Logging::Entry preformatted{ SRCE_CUR, ELogLevel::Debug, ELogTags::Test, userPK, string{"msg {}"}, vector<string>{"1"} };
		EXPECT_EQ( preformatted.UserPK, userPK );//the overload the variadic one delegates to.
		//the stacktrace-frame overloads are deliberately not exercised here: constructing one Entry from a frame
		//costs ~170ms of one-time symbol-machinery init (even for an empty frame) and their only caller is
		//Jde::Opc's LogStack - see opc-review2.md.
	}

	// The level asked for has to be one the tag does not already have, or the test proves nothing: the old version
	// set Debug, which /logging/memory/tags/default already grants, so its assertion passed with the SetLevel line
	// deleted.  Here Debug is established explicitly (so no prior test's leftover level can decide the outcome),
	// the DBG both proves that level and memoizes it into the logger's ExtrapolatedTags, and only then is the tag
	// raised to Trace - which fails unless SetLevel reaches the cumulative (it short-circuits the TRACE before any
	// logger is consulted) *and* drops that memo.
	//install-issues #7: SpdLog is handed its own /logging/spd object and looked `flushOn` up by a root path, which the jobject
	//overload (a key, not a path) never resolves - every config's flushOn was ignored and the default stood, Information in a
	//release build, so a service's text log showed nothing below that until a flush.  The key reads it; the default holds without.
	TEST_F( LogGeneralTests, SpdLogReadsFlushOnFromItsOwnSettings ){
		let dir = fs::temp_directory_path()/"jde-spd-flush";
		fs::create_directories( dir );
		auto settings = [&]( optional<sv> flushOn )->jobject{
			jobject o{ {"tags", jobject{{"default", "Information"}}}, {"sinks", jobject{{"file", jobject{{"path", dir.string()}}}}} };
			if( flushOn )
				o["flushOn"] = *flushOn;
			return o;
		};
		EXPECT_EQ( Logging::SpdLog{settings("Trace")}.FlushLevel(), ELogLevel::Trace );
		EXPECT_EQ( Logging::SpdLog{settings("Warning")}.FlushLevel(), ELogLevel::Warning );
		EXPECT_EQ( Logging::SpdLog{settings(nullopt)}.FlushLevel(), _debug ? ELogLevel::Debug : ELogLevel::Information );
		fs::remove_all( dir );
	}

	//install-issues #13: a start truncated the text log, so restarting a product after a failure erased the lines that said
	//why.  With `keep`, the file sink rolls the previous run aside on open - <stem>.1.log - instead of erasing it.
	TEST_F( LogGeneralTests, FileSinkKeepsPreviousRuns ){
		let dir = fs::temp_directory_path()/"jde-spd-keep";
		fs::remove_all( dir );
		fs::create_directories( dir );
		const jobject settings{ {"tags", jobject{{"default", "Information"}}}, {"sinks", jobject{{"file", jobject{{"path", dir.string()}, {"keep", 2}}}}} };
		let read = [&]( const fs::path& p )->string{ std::ifstream is{ p }; return string{ std::istreambuf_iterator<char>{is}, {} }; };
		{
			Logging::SpdLog spd{ settings };
			spd.Write( Logging::Entry{SRCE_CUR, ELogLevel::Warning, ELogTags::Test, string{"first run"}} );
			spd.Shutdown( false, SRCE_CUR );
		}
		string stem;//<settings file stem>.log - the one file the first run left; Settings::FileStem is not exported.
		for( let& entry : fs::directory_iterator(dir) )
			stem = entry.path().stem().string();
		ASSERT_FALSE( stem.empty() ) << "the first run wrote no log file";
		ASSERT_TRUE( fs::exists(dir/(stem+".log")) );
		EXPECT_FALSE( fs::exists(dir/(stem+".1.log")) );
		{
			Logging::SpdLog spd{ settings };//a second start: the first run's file rolls to .1.log and this one starts empty.
			spd.Write( Logging::Entry{SRCE_CUR, ELogLevel::Warning, ELogTags::Test, string{"second run"}} );
			spd.Shutdown( false, SRCE_CUR );
		}
		ASSERT_TRUE( fs::exists(dir/(stem+".1.log")) );
		EXPECT_NE( read(dir/(stem+".1.log")).find("first run"), string::npos );
		let current = read( dir/(stem+".log") );
		EXPECT_NE( current.find("second run"), string::npos );
		EXPECT_EQ( current.find("first run"), string::npos );
		fs::remove_all( dir );
	}

	TEST_F( LogGeneralTests, CachedTags ){
		auto& logger = Logging::GetLogger<Logging::MemoryLog>();

		let _tags = MutatedTags;
		ExpectConfiguredLevel();
		Logging::ClearMemory();
		constexpr auto logMessage = "scheduler msg";
		constexpr auto debugMessage = "scheduler dbg";
		logger.SetLevel( _tags, ELogLevel::Debug );
		TRACE( logMessage );
		ASSERT_TRUE( logger.Find(logMessage).empty() ) << "Debug must not admit Trace";
		DBG( debugMessage );
		ASSERT_FALSE( logger.Find(debugMessage).empty() );

		logger.SetLevel( _tags, ELogLevel::Trace );
		TRACE( logMessage );
		EXPECT_FALSE( logger.Find(logMessage).empty() ) << "SetLevel did not reach the cumulative, or the memoized tag level survived it";
	}

	TEST_F( LogGeneralTests, ArgsNotCalled ){
		auto& logger = Logging::GetLogger<Logging::MemoryLog>();
		let unConfiguredTags = MutatedTags;
		ExpectConfiguredLevel();
		logger.SetLevel( unConfiguredTags, ELogLevel::Error );
		auto arg = []()->string {
			throw std::runtime_error("should not be called");
			return "";
		};
		ASSERT_NO_THROW( TRACET(unConfiguredTags, "{}", arg()) );
	}

	//%U is what makes the console log's osc-8 links clickable, and a wrong branch shows up only as a link that does
	//not open - nothing fails, nothing logs, the path just does not resolve.  Four spellings reach it and each takes a
	//different route: repo-relative (the -fmacro-prefix-map form, and on windows that form has *backslashes*),
	//already-absolute posix, and a drive letter.  JDE_SOURCE_ROOT is a compile definition on this target too, so the
	//root can be named here rather than guessed.
	TEST( SpdLogTests, FormatSourceUriMapsEachSpelling ){
		let root = string{ JDE_SOURCE_ROOT };
		ASSERT_FALSE( root.empty() ) << "JDE_SOURCE_ROOT is not defined for the test target - the repo-relative cases below prove nothing";

		let relative = Logging::FormatSourceUri( "libs/x.cpp" );
		EXPECT_TRUE( relative.ends_with("libs/x.cpp") ) << relative;
		EXPECT_NE( relative.find(root), string::npos ) << "a repo-relative source has to be made absolute: " << relative;
		EXPECT_EQ( relative[0], '/' ) << "the pattern is file://%U, so the uri must open with a slash: " << relative;

		//the shape __FILE__ actually has on windows: -fmacro-prefix-map rewrites mapped paths with backslashes.
		let backslashed = Logging::FormatSourceUri( "libs\\fwk\\x.cpp" );
		EXPECT_TRUE( backslashed.ends_with("libs/fwk/x.cpp") ) << backslashed;
		EXPECT_EQ( backslashed.find('\\'), string::npos ) << "a file:// uri has no backslashes: " << backslashed;
		EXPECT_NE( backslashed.find(root), string::npos ) << backslashed;

		//already absolute: left alone, and never prefixed with the repo root.
		EXPECT_EQ( Logging::FormatSourceUri("/abs/x.cpp"), "/abs/x.cpp" ) << "an absolute posix path is not remapped and needs no extra slash";
		EXPECT_EQ( Logging::FormatSourceUri("c:\\r\\x.cpp"), "/c:/r/x.cpp" ) << "file:// + c:/x has to become file:///c:/x";
		EXPECT_EQ( Logging::FormatSourceUri("c:/r/x.cpp"), "/c:/r/x.cpp" );

		EXPECT_TRUE( Logging::FormatSourceUri("").empty() ) << "a message with no source location contributes nothing to the pattern";
	}

	//TODO makesure cumulative is updated when some obscure tag is sent.
	//How quick is md5 calculation.
	//How quick is noop.
	//How quick is memory.
	//How quick is spdlog.
}
