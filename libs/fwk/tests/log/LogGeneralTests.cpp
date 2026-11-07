#include "jde/fwk/log/ILogger.h"
#include <jde/tests/Capture.h>
#include <jde/fwk/log/SpdLog.h>
#include <fstream>
#include "../testsDir.h"
#define let const auto

namespace Jde::Tests{
	//A logger whose levels a test edits, which writes nothing.  At the suite capture's Debug, it widens nothing until edited.
	struct EditedLogger final : Logging::ILogger{
		EditedLogger()ι:ILogger{ "Edited", jobject{{"tags", jobject{{"default", "Debug"}}}} }{}
		α Write( const Logging::Record& )ι->void override{}
		α Shutdown( bool, SL )ι->void override{}
	};
	struct LogGeneralTests : public ::testing::Test{
	protected:
		LogGeneralTests() {}
		~LogGeneralTests() override{}

		static constexpr ELogTags MutatedTags{ ELogTags::Scheduler };
		static constexpr ELogTags LibraryTag{ (ELogTags)(1ull<<LibraryTagBase) };//no library registers its tag names here.
		α SetUp()->void override{ Logging::ClearMemory(); }
		α TearDown()->void override{ Edited().ClearLevel( MutatedTags ); }
		Ω Edited()ι->EditedLogger&{
			static auto& edited = *static_cast<EditedLogger*>( Logging::AddLogger(mu<EditedLogger>()) );
			return edited;
		}
		α ExpectConfiguredLevel()->void{
			EXPECT_EQ( Edited().MinLevel(MutatedTags), Edited().DefaultLevel() ) << "an earlier test left an override on " << ToName( MutatedTags );
			EXPECT_FALSE( Logging::Captures(ELogLevel::Trace, MutatedTags) ) << "a capture of " << ToName( MutatedTags ) << " at Trace outlived its test";
		}
	};

	//app-review3 L6: Logging::Add swallowed the logger constructor's exception in an empty catch, so a logger the settings ask for
	//and that cannot be built - an App::ProtoLog whose `path` cannot be created, most plainly - was simply not there: no output from
	//it, no answers from it, and nothing saying why.  The exception self-logs on destruction, but at Debug and without naming which
	//logger is missing, which is the half that made it hard to find.
	struct UnbuildableLogger final : Logging::ILogger{
		UnbuildableLogger( const jobject& o )ε:ILogger{"Unbuildable", o}{ throw Exception{"this logger cannot be created"}; }
		α Write( const Logging::Record& )ι->void override{}
		α Shutdown( bool, SL )ι->void override{}
	};

	TEST_F( LogGeneralTests, AConfiguredLoggerThatCannotBeCreatedSaysSo ){
		Settings::Set( "/logging/l6-probe", jobject{} );//a section, so Add gets as far as constructing.
		Logging::ClearMemory();
		EXPECT_EQ( Logging::Add<UnbuildableLogger>("l6-probe"), nullptr );
		Settings::Set( "/logging/l6-probe", jvalue{} );//and gone again:  a null reads as no section.
		let reported = Logging::Find( [](const Logging::Entry& e){ return e.Level>=ELogLevel::Error && e.Text().contains("could not be created"); } );
		EXPECT_FALSE( reported.empty() ) << "a configured logger that did not start said nothing at Error";
	}

	//6df60754:  Log() walks the logger list unlocked from any thread, so AddLogger publishes a new list and keeps the one it
	//replaces, unchanged, for a reader mid-walk.  A push_back into the one list moved it under such a walk - "vector iterators
	//incompatible" in the gateway suite.  This logger takes nothing, so it stays inert for the rest of the run.
	struct QuietLogger final : Logging::ILogger{
		QuietLogger():ILogger{ "Quiet", jobject{{"tags", jobject{{"default", "None"}}}} }{}
		α Write( const Logging::Record& )ι->void override{}
		α Shutdown( bool, SL )ι->void override{}
	};
	TEST_F( LogGeneralTests, AddLoggerPublishesANewListAndKeepsTheOld ){
		const auto& before = Logging::Loggers();
		let n = before.size();
		auto* added = Logging::AddLogger( mu<QuietLogger>() );
		const auto& after = Logging::Loggers();
		EXPECT_NE( &before, &after ) << "a change publishes a new list";
		ASSERT_EQ( after.size(), n+1 );
		EXPECT_EQ( after.back(), added );
		EXPECT_EQ( before.size(), n ) << "the replaced list is kept, unchanged, for a reader still walking it";
		EXPECT_EQ( std::ranges::find(before, added), before.end() );
		EXPECT_EQ( before.front(), after.front() );
	}

	//ELogTags is a 64-bit space that libraries extend above fwk's own 27 bits - Jde::Opc registers 32-45 through
	//AddTags - so All has to span the whole underlying type.  Spelled ~0ul it stopped at bit 31 under the windows
	//ABI, where unsigned long is 32-bit, and silently dropped every extension tag from an All-based subscription.
	static_assert( (uint)ELogTags::All==~std::underlying_type_t<ELogTags>{} );
	static_assert( ((uint)ELogTags::All & (1ull<<32))!=0 );//the bit range that actually went missing.

	//Two libraries naming one bit would each read the other's lines under its own name, so AddTags takes a table whole or
	//not at all, never over a bit or a name already registered.  It runs before Init, with no logger to say so:  it throws.
	TEST_F( LogGeneralTests, AddTagsRefusesATakenBitOrName ){
		EXPECT_THROW( Logging::AddTags(array{TagName{"library", LibraryTag}, TagName{"sqlAgain", ELogTags::Sql}}), std::logic_error );
		EXPECT_THROW( Logging::AddTags(array{TagName{"sql", LibraryTag}}), std::logic_error );
		EXPECT_FALSE( Logging::Tags().contains("library") ) << "the refused table's free entry was not added either";
		EXPECT_TRUE( ToValue(LibraryTag).is_null() ) << "nor its bit named";
	}

	//config keys are split on TagSeparator and each part looked up by name, so an unrecognised part is simply
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
	//wraps it in an array - so a single tag came back as "none", whatever tag it actually was.
	TEST_F( LogGeneralTests, ToValueSpellsOneTagBare ){
		EXPECT_EQ( ToValue(ELogTags::Sql).as_string(), "sql" );
		EXPECT_EQ( ToName(ELogTags::Sql), "sql" );
		EXPECT_EQ( ToName(ELogTags::SocketClientRead), "client.read.socket" ) << "the parts in bit order, joined with TagSeparator";
		EXPECT_EQ( ToName(ELogTags::None), "none" );
		EXPECT_EQ( ToName(LibraryTag), "" ) << "a bit no table names has no name";
		EXPECT_EQ( ToValue(ELogTags::None).as_string(), "none" );
		let multi = ToValue( ELogTags::SocketClientRead );
		ASSERT_TRUE( multi.is_array() );
		EXPECT_EQ( multi.as_array().size(), 3u );//no library tags here, so socket|client|read is exactly its three fwk names.
		EXPECT_EQ( ToLogTags(multi), ELogTags::SocketClientRead );
		EXPECT_EQ( ToLogTags(ToValue(ELogTags::Sql)), ELogTags::Sql );
	}

	// The level-resolution engine itself had no test.  fwk-max #3 was MinLevel counting split(tags).size() rather
	// than the overlap, so the first configured entry sharing any bit won and the answer followed
	// concurrent_flat_map iteration order; fwk-max #2 was the ctor leaving the default at Information instead of the
	// parsed one.  Every case here has a strict popcount winner;  LogTagsTieTakesTheLowerLevel has the ties.
	TEST_F( LogGeneralTests, LogTagsResolvesBestMatch ){
		//three single-bit distractors, so the buggy 'first overlapping entry wins' has to draw the one right entry
		//out of four to look correct.  They are safe for the lookups below: only socket|client|read is asserted
		//against them, and there its 2-bit entry beats each of them outright.
		let t = TagLevels::Parse( jobject{{"default","Warning"},{"socket.client","Debug"},{"http","Trace"},
			{"socket","Information"},{"client","Error"},{"read","Critical"}} );
		EXPECT_EQ( t.DefaultLevel(), ELogLevel::Warning );
		EXPECT_EQ( t.Resolve(ELogTags::Http), ELogLevel::Trace );//exact match.
		EXPECT_EQ( t.Resolve(ELogTags::SocketClientRead), ELogLevel::Debug ) << "socket.client, 2 bits the line carries, must beat every 1-bit entry";
		EXPECT_EQ( t.Resolve(ELogTags::Sql), ELogLevel::Warning ) << "nothing configured is part of sql - it falls back to the default";
		EXPECT_EQ( t.Resolve(LibraryTag | ELogTags::Read), ELogLevel::Critical ) << "read is all there beside a library's bit, as in Opc's IotReadTag";
		Logging::Gate gate{ "test", t };
		EXPECT_TRUE( gate.ShouldLog(ELogLevel::Debug, ELogTags::SocketClientRead) );
		EXPECT_FALSE( gate.ShouldLog(ELogLevel::Trace, ELogTags::SocketClientRead) );
	}

	//Entries the line carries all of, with as many bits each, tie, and the lower level wins, as combining loggers does - not
	//whichever entry comes first.  Each case has distractors above the winner, so that order would miss it.
	TEST_F( LogGeneralTests, LogTagsTieTakesTheLowerLevel ){
		EXPECT_EQ( TagLevels::Parse(jobject{{"default","Warning"},{"http","Critical"},{"client","Trace"},{"read","Error"}}).Resolve(ELogTags::HttpClientRead), ELogLevel::Trace );
		EXPECT_EQ( TagLevels::Parse(jobject{{"default","Warning"},{"http","None"},{"client","Error"},{"read","Critical"}}).Resolve(ELogTags::HttpClientRead), ELogLevel::Error ) << "None yields to a real level, as in the cumulative";
		auto gateway = TagLevels::Parse( jobject{{"default","Information"},{"socket.client.read","Information"},{"http.client.read","Information"}} );
		gateway.Levels.insert_or_assign( LibraryTag, ELogLevel::Trace );//the gateway's opc: "Trace" against its *.read levels, for IotReadTag.
		EXPECT_EQ( gateway.Resolve(LibraryTag | ELogTags::Read), ELogLevel::Trace );
		let sql = jobject{ {"sql", "Error"} };
		Logging::Threshold file{ "test", &sql, jobject{{"default", "Warning"}, {"exception", "Trace"}} };
		EXPECT_TRUE( file.ShouldLog(ELogLevel::Debug, ELogTags::Sql | ELogTags::Exception) ) << "an exception keeps its source whatever its other tags' levels";
	}

	//reviews-issue/logTags.md #8, ruled 2026-11-06:  an entry applies to a line only when the line carries every tag in it.  Counting
	//any shared bit, http.client.read put opc's read lines at Trace, sharing only `read`, and socket.client.read.subscription
	//turned on every plain socket.client.read line, its 3 shared bits beating any 1-bit entry.
	TEST_F( LogGeneralTests, ACombinedTagAppliesOnlyToLinesCarryingAllOfIt ){
		auto levels = TagLevels::Parse( jobject{{"default","Information"},{"http.client.read","Trace"},{"socket.client.read.subscription","Trace"},{"read","Debug"}} );
		EXPECT_EQ( levels.Resolve(LibraryTag | ELogTags::Read), ELogLevel::Debug ) << "read is all there;  http.client.read is not";
		EXPECT_EQ( levels.Resolve(ELogTags::SocketClientRead), ELogLevel::Debug ) << "the subscription set is not all there";
		EXPECT_EQ( levels.Resolve(ELogTags::SocketClientReadSub), ELogLevel::Trace ) << "and is when it is";
		EXPECT_EQ( levels.Resolve(ELogTags::HttpClientRead | ELogTags::Sessions), ELogLevel::Trace ) << "a line with more than an entry still takes it, the most bits winning";
		levels.Levels.erase( ELogTags::Read );
		EXPECT_EQ( levels.Resolve(LibraryTag | ELogTags::Read), ELogLevel::Information ) << "nothing all there:  the default";
	}

	// MinLevel memoizes into the logger's Gate, so every edit has to publish a new one - otherwise a cleared or re-set tag
	// keeps resolving to whatever it was when first asked.
	TEST_F( LogGeneralTests, LogTagsMutatorsDropTheMemo ){
		LogLevels t{ "test", jobject{{"tags", jobject{{"default","Warning"},{"client","Error"}}}} };
		//an override, not a configured level: ClearLevel lets a configured level show through (install-issues #21), and the
		//point here is the best match moving to the next entry once the cleared one is gone.
		t.SetLevel( ToLogTags(sv{"socket.client"}), ELogLevel::Debug );
		ASSERT_EQ( t.MinLevel(ELogTags::SocketClientRead), ELogLevel::Debug );//memoizes socket|client|read -> Debug
		ASSERT_EQ( t.MinLevel(ELogTags::Sql), ELogLevel::Warning );//and sql -> the default

		t.ClearLevel( ToLogTags(sv{"socket.client"}) );
		EXPECT_EQ( t.MinLevel(ELogTags::SocketClientRead), ELogLevel::Error ) << "the memoized best match survived ClearLevel";

		t.SetLevels( jobject{{"default","Critical"},{"sql","Trace"}} );//the flat tag->level map, 'default' included.
		EXPECT_EQ( t.DefaultLevel(), ELogLevel::Critical );
		EXPECT_EQ( t.MinLevel(ELogTags::Sql), ELogLevel::Trace ) << "the memoized default survived SetLevels";
	}

	//reviews-issue/logTags.md #2:  each edit reset a logger's memo while lines resolved into it on other threads, so an
	//answer computed from the old levels could land in the fresh memo and stay there.  A Gate is never edited:  a change
	//publishes another, and a reader still in the old one gets the old answers, consistently.
	TEST_F( LogGeneralTests, AnEditPublishesANewGate ){
		LogLevels t{ "test", jobject{{"tags", jobject{{"default","Warning"}}}} };
		let& before = t.Levels();
		ASSERT_EQ( before.MinLevel(ELogTags::Sql), ELogLevel::Warning );//memoized in the first Gate.
		t.SetLevel( ELogTags::Sql, ELogLevel::Debug );
		EXPECT_NE( &t.Levels(), &before );
		EXPECT_EQ( t.MinLevel(ELogTags::Sql), ELogLevel::Debug );
		EXPECT_EQ( before.MinLevel(ELogTags::Sql), ELogLevel::Warning ) << "the replaced Gate is unchanged, memo and all";
		let& current = t.Levels();
		t.SetLevel( ELogTags::Sql, ELogLevel::Debug );
		EXPECT_EQ( &t.Levels(), &current ) << "an edit that moves nothing publishes nothing";
	}

	//#3:  SetLevel and ClearLevel republished the cumulative filter and SetDefaultLevel did not, so a default edited at run
	//time reached a logger's own Gate but not the test every line passes first.  Every edit publishes both now.
	TEST_F( LogGeneralTests, ADefaultEditReachesTheCumulative ){
		auto& edited = Edited();
		ExpectConfiguredLevel();
		ASSERT_FALSE( Logging::ShouldLog(ELogLevel::Trace, MutatedTags) ) << "the config's lowest default is Debug, the suite capture's and this logger's";
		edited.SetDefaultLevel( ELogLevel::Trace );
		EXPECT_TRUE( Logging::ShouldLog(ELogLevel::Trace, MutatedTags) );
		edited.ClearDefaultLevel();
		EXPECT_FALSE( Logging::ShouldLog(ELogLevel::Trace, MutatedTags) ) << "cleared back to the settings' default";
	}

	// install-issues #21: ClearLevel erased the tag outright, so an override on a tag the settings had configured fell to the
	// default instead of back to the configured level - and stayed there until a restart, since nothing at runtime knew what
	// the settings had said.  The settings' levels are kept apart from the overrides that sit on them.
	TEST_F( LogGeneralTests, ClearLevelRestoresTheSettingsLevel ){
		LogLevels t{ "test", jobject{{"tags", jobject{{"default","Warning"},{"sql","Trace"}}}} };
		t.SetLevel( ELogTags::Sql, ELogLevel::Critical );
		ASSERT_EQ( t.MinLevel(ELogTags::Sql), ELogLevel::Critical );
		t.ClearLevel( ELogTags::Sql );
		EXPECT_EQ( t.MinLevel(ELogTags::Sql), ELogLevel::Trace ) << "the level the settings gave the tag, not the default";

		t.SetLevel( ELogTags::Http, ELogLevel::Debug );
		t.ClearLevel( ELogTags::Http );
		EXPECT_EQ( t.MinLevel(ELogTags::Http), ELogLevel::Warning ) << "a tag the settings never named falls back to the default";

		t.SetDefaultLevel( ELogLevel::Critical );
		ASSERT_EQ( t.MinLevel(ELogTags::Http), ELogLevel::Critical );
		t.ClearDefaultLevel();
		EXPECT_EQ( t.DefaultLevel(), ELogLevel::Warning ) << "the default override cleared: the settings' default";
		EXPECT_EQ( t.MinLevel(ELogTags::Http), ELogLevel::Warning ) << "and the memo dropped with it";

		t.SetLevels( jobject{{"sql","Error"},{"default","Debug"}} );//the instance_tag_levels rows applied at start are overrides too…
		ASSERT_EQ( t.MinLevel(ELogTags::Sql), ELogLevel::Error );
		t.ClearLevel( ELogTags::Sql );
		t.ClearDefaultLevel();
		EXPECT_EQ( t.MinLevel(ELogTags::Sql), ELogLevel::Trace ) << "…so clearing one reaches the settings underneath, not the row";
		EXPECT_EQ( t.DefaultLevel(), ELogLevel::Warning );

		t.SetSettings( jobject{{"sql","Error"}} );//…where a map given as settings is what a later clear falls back to.
		t.SetLevel( ELogTags::Sql, ELogLevel::NoLog );
		t.ClearLevel( ELogTags::Sql );
		EXPECT_EQ( t.MinLevel(ELogTags::Sql), ELogLevel::Error );
		EXPECT_TRUE( t.Overrides()==TagLevels{} ) << "every override cleared";
	}

	// Logging::min is not std::min: NoLog is -1, below every real level, so a plain minimum would let one logger
	// that wants nothing silence every other logger through the cumulative.
	TEST_F( LogGeneralTests, CumulativeNoLogYieldsToARealLevel ){
		TagLevels quiet{ ELogLevel::NoLog, {{ELogTags::Sql, ELogLevel::NoLog}} };
		quiet += TagLevels::Parse( jobject{{"default","Debug"},{"sql","Debug"}} );
		EXPECT_EQ( quiet.DefaultLevel(), ELogLevel::Debug );
		EXPECT_EQ( quiet.Resolve(ELogTags::Sql), ELogLevel::Debug ) << "a configured NoLog must yield to the other side's level too";
		EXPECT_TRUE( Logging::Gate("test", quiet).ShouldLog(ELogLevel::Debug, ELogTags::Sql) );
	}

	//reviews-issue/MemoryLog.md #8:  += kept a tag only one side named at that side's level, so spd's sql at Information capped
	//sql in the cumulative for proto, whose default is Debug, and the binary log never had sql's Debug lines.  A merged map
	//can't hold every line's lower level either:  an entry for a set shadows a lower level for a tag in it.
	TEST_F( LogGeneralTests, CombiningNeverNarrowsAPart ){
		auto combined = TagLevels::Parse( jobject{{"default","Information"},{"sql","Information"},{"http.client.read","Information"}} );//spd's shape
		combined += TagLevels::Parse( jobject{{"default","Debug"}} );//proto's
		EXPECT_EQ( combined.Resolve(ELogTags::Sql), ELogLevel::Debug ) << "a tag one part names capped the other's default";
		EXPECT_EQ( combined.Resolve(ELogTags::HttpClientRead), ELogLevel::Debug );
		EXPECT_EQ( combined.Resolve(ELogTags::App), ELogLevel::Debug );
		EXPECT_EQ( combined.Floor(), ELogLevel::Debug );
		EXPECT_TRUE( Logging::Gate("test", combined).ShouldLog(ELogLevel::Debug, ELogTags::Sql) );

		auto shadow = TagLevels::Parse( jobject{{"default","Warning"},{"http.client","Warning"}} );
		shadow += TagLevels::Parse( jobject{{"default","Warning"},{"read","Trace"}} );
		EXPECT_EQ( shadow.Resolve(ELogTags::HttpClientRead), ELogLevel::Trace ) << "http.client's entry shadowed read's, which the other part names lower";
		EXPECT_EQ( shadow.Resolve(ELogTags::Http | ELogTags::Client), ELogLevel::Warning );
	}
	//...and as this suite's config has it:  spd names io at Information, and the suite's capture takes everything at Debug.
	TEST_F( LogGeneralTests, TheCumulativeTakesEachLoggersLevel ){
		ASSERT_FALSE( Logging::FindLogger<Logging::SpdLog>()->ShouldLog(ELogLevel::Debug, ELogTags::IO) ) << "spd takes io at Information";
		ASSERT_TRUE( Logging::Captures(ELogLevel::Debug, ELogTags::IO) ) << "the suite's capture takes Debug";
		EXPECT_TRUE( Logging::ShouldLog(ELogLevel::Debug, ELogTags::IO) ) << "spd's io capped the capture's Debug in the cumulative";
	}

	//"None", the off value of every shipped config's externalLogger and of the UI, parses to 6, the wire's LogLevelNone - an
	//index past Critical, not the -1 above, which no config produces.  It silences its tag by lying past Critical, and yields
	//in the cumulative because the minimum of it and a real level is that level.
	TEST_F( LogGeneralTests, ConfiguredNoneSilencesATagAndYieldsInTheCumulative ){
		let quiet = TagLevels::Parse( jobject{{"default","Debug"},{"externalLogger","None"}} );
		Logging::Gate gate{ "test", quiet };
		EXPECT_FALSE( gate.ShouldLog(ELogLevel::Critical, ELogTags::ExternalLogger) ) << "None is off, whatever it parses to";
		EXPECT_EQ( Jde::ToString(gate.MinLevel(ELogTags::ExternalLogger)), "None" );
		EXPECT_TRUE( gate.ShouldLog(ELogLevel::Debug, ELogTags::Sql) ) << "and the other tags are untouched";
		auto combined = quiet;
		combined += TagLevels::Parse( jobject{{"default","Debug"},{"externalLogger","Debug"}} );
		EXPECT_EQ( combined.Resolve(ELogTags::ExternalLogger), ELogLevel::Debug ) << "a configured None yields to the other logger's level";
	}

	//#6:  no tag set resolves below the default or the lowest configured level, so a line under that is refused before any lookup.
	TEST_F( LogGeneralTests, AGateRefusesBelowItsFloor ){
		EXPECT_EQ( Logging::Gate("test", TagLevels::Parse(jobject{{"default","Warning"},{"sql","Debug"},{"http","None"}})).Floor(), ELogLevel::Debug ) << "None lies past Critical, and lowers nothing";
		EXPECT_EQ( Logging::Gate("test", TagLevels::Parse(jobject{{"default","Information"}})).Floor(), ELogLevel::Information );
		Logging::Gate gate{ "test", TagLevels{ELogLevel::NoLog, {{ELogTags::Sql, ELogLevel::Trace}}} };
		EXPECT_EQ( gate.Floor(), ELogLevel::Trace ) << "NoLog yields, as in the cumulative";
		EXPECT_TRUE( gate.ShouldLog(ELogLevel::Trace, ELogTags::Sql) );
		EXPECT_FALSE( gate.ShouldLog(ELogLevel::Critical, ELogTags::App) ) << "the default is still NoLog";
	}

	// opcserver-refactor-review #5:  UpdateCumulative keeps every Gate it publishes, so it publishes only one whose levels differ
	// from the current - and what MinLevel memoizes is no difference, or a Gate that had answered a lookup would never match.
	TEST_F( LogGeneralTests, LogTagsEqualityIgnoresTheMemo ){
		const jobject settings{{"tags", jobject{{"default","Warning"},{"sql","Trace"}}}};
		LogLevels a{ "a", settings }, b{ "b", settings };
		ASSERT_EQ( a.MinLevel(ELogTags::SocketClientRead), ELogLevel::Warning );//memoized in a's Gate, not in b's.
		EXPECT_TRUE( a.Effective()==b.Effective() ) << "a memoized lookup made equal levels unequal";
		b.SetLevel( ELogTags::Sql, ELogLevel::Error );
		EXPECT_FALSE( a.Effective()==b.Effective() );
		b.ClearLevel( ELogTags::Sql );
		EXPECT_TRUE( a.Effective()==b.Effective() ) << "cleared back to the settings' level";
		b.SetDefaultLevel( ELogLevel::Debug );
		EXPECT_FALSE( a.Effective()==b.Effective() );
	}
	//#5:  the Gate every line passes, and each logger's, is called directly:  nothing on the level path is virtual.
	static_assert( !std::is_polymorphic_v<LogLevels> && !std::is_polymorphic_v<Logging::Gate> );

	// fwk-max #16 was the UserPK-taking variadic ctor delegating to the overload without one, so every entry built
	// for a user came out anonymous.  fwk-review2 cites a regression test named exactly this for it; the test was
	// never written, and nothing else in the suite reads Entry::UserPK back off the variadic ctors.
	TEST_F( LogGeneralTests, EntryKeepsUserPK ){
		constexpr auto userPK = Jde::UserPK{ 42 };
		Logging::Entry e{ SRCE_CUR, ELogLevel::Debug, ELogTags::Test, userPK, "msg {}", 1 };
		EXPECT_EQ( e.UserPK, userPK );
		EXPECT_EQ( e.Level, ELogLevel::Debug );
		EXPECT_EQ( e.Tags, ELogTags::Test );
		ASSERT_EQ( e.Arguments.size(), 1u ) << "delegating to the stored-arguments overload must not cost the args";
		EXPECT_EQ( e.Arguments[0], Logging::Argument{int64_t{1}} );
		EXPECT_EQ( e.Message(), "msg 1" );

		Logging::Entry anonymous{ SRCE_CUR, ELogLevel::Debug, ELogTags::Test, "msg {}", 1 };
		EXPECT_EQ( anonymous.UserPK, Jde::UserPK{} ) << "the overload without a user must default it";

		Logging::Entry preformatted{ SRCE_CUR, ELogLevel::Debug, ELogTags::Test, userPK, string{"msg {}"}, vector<Logging::Argument>{"1"} };
		EXPECT_EQ( preformatted.UserPK, userPK );//the overload the variadic one delegates to.
		//the stacktrace-frame overloads are deliberately not exercised here: constructing one Entry from a frame
		//costs ~170ms of one-time symbol-machinery init (even for an empty frame) and their only caller is
		//Jde::Opc's LogStack - see opc-review2.md.
	}

	// The level asked for has to be one the tag does not already have, or the test proves nothing: the old version
	// set Debug, which /logging/memory/tags/default already grants, so its assertion passed with the SetLevel line
	// deleted.  Here Debug is established explicitly (so no prior test's leftover level can decide the outcome),
	// the DBG both proves that level and memoizes it into the logger's Gate, and only then is the tag
	// raised to Trace - which fails unless SetLevel reaches the cumulative (it short-circuits the TRACE before any
	// logger is consulted) *and* drops that memo.
	//A Threshold's settings go over its defaults, a logger's `tags` shape (ProtoLog's file and function, LogStack's stack).
	TEST_F( LogGeneralTests, ThresholdsApplyOverTheDefaults ){
		let defaults = jobject{ {"default", "Warning"}, {"exception", "Trace"} };
		let exception = ELogTags::Sql | ELogTags::Exception;
		Logging::Threshold dflt{ "test", nullptr, defaults };
		EXPECT_FALSE( dflt.ShouldLog(ELogLevel::Information, ELogTags::App) );
		EXPECT_TRUE( dflt.ShouldLog(ELogLevel::Warning, ELogTags::App) );
		EXPECT_TRUE( dflt.ShouldLog(ELogLevel::Debug, exception) );
		EXPECT_TRUE( dflt.ShouldLog(ELogLevel::Debug, LibraryTag | ELogTags::Exception) ) << "a library's exception, as a UAException";
		Logging::Threshold memoized{ "test", nullptr, defaults };
		for( uint i=0; i<LibraryTagBase; ++i ){//library lines memoized at the default, each sharing the library's bit with the exception.
			if( let tag = (ELogTags)(1ull<<i); tag!=ELogTags::Exception )
				memoized.ShouldLog( ELogLevel::Debug, LibraryTag | tag );
		}
		EXPECT_TRUE( memoized.ShouldLog(ELogLevel::Debug, LibraryTag | ELogTags::Exception) ) << "matched against an earlier line's memo";
		let everything = jobject{ {"default", "Trace"} };
		EXPECT_TRUE( Logging::Threshold("test", &everything, defaults).ShouldLog(ELogLevel::Trace, ELogTags::App) );
		let noExceptions = jobject{ {"exception", "None"} };
		Logging::Threshold quiet{ "test", &noExceptions, defaults };
		EXPECT_FALSE( quiet.ShouldLog(ELogLevel::Error, exception) ) << "exception: None drops it from exceptions";
		EXPECT_TRUE( quiet.ShouldLog(ELogLevel::Warning, ELogTags::App) ) << "...and leaves the default level in place";
	}

	//LogStack walks the stack only from /logging/stack's level, Warning as by default here:  below it an entry has no source,
	//at it the caller's frame.  The first walk pays the symbolizer's one-time start, ~170 ms in a debug build.
	TEST_F( LogGeneralTests, LogStackWalksFromItsThreshold ){
		ASSERT_FALSE( Logging::ShouldCaptureStack(ELogLevel::Information, ELogTags::Test) ) << "the suite's config sets /logging/stack";
		ASSERT_TRUE( Logging::ShouldCaptureStack(ELogLevel::Warning, ELogTags::Test) );
		Logging::ClearMemory();
		Logging::LogStack( ELogLevel::Information, ELogTags::Test, 0, "LogStack below its threshold {}", 1 );
		Logging::LogStack( ELogLevel::Warning, ELogTags::Test, 0, "LogStack at its threshold {}", 1 );
		let below = Logging::Find( [](let& e){ return e.Text()=="LogStack below its threshold {}"; } );
		ASSERT_EQ( below.size(), 1u );
		EXPECT_TRUE( below[0].File().empty() ) << below[0].File();
		EXPECT_EQ( below[0].Line, 0u );
		let at = Logging::Find( [](let& e){ return e.Text()=="LogStack at its threshold {}"; } );
		ASSERT_EQ( at.size(), 1u );
		EXPECT_TRUE( at[0].File().ends_with("LogGeneralTests.cpp") ) << "not the caller's frame: " << at[0].File();
		EXPECT_NE( at[0].Line, 0u );
	}

	//install-issues #7: SpdLog is handed its own /logging/spd object and looked `flushOn` up by a root path, which the jobject
	//overload (a key, not a path) never resolves - every config's flushOn was ignored and the default stood, Information in a
	//release build, so a service's text log showed nothing below that until a flush.  The key reads it; the default holds without.
	TEST_F( LogGeneralTests, SpdLogReadsFlushOnFromItsOwnSettings ){
		auto settings = []( optional<sv> flushOn )->jobject{
			jobject o{ {"tags", jobject{{"default", "Information"}}} };//no sinks:  the flush level is set before any sink matters.
			if( flushOn )
				o["flushOn"] = *flushOn;
			return o;
		};
		EXPECT_EQ( Logging::SpdLog{settings("Trace")}.FlushLevel(), ELogLevel::Trace );
		EXPECT_EQ( Logging::SpdLog{settings("Warning")}.FlushLevel(), ELogLevel::Warning );
		EXPECT_EQ( Logging::SpdLog{settings(nullopt)}.FlushLevel(), _debug ? ELogLevel::Debug : ELogLevel::Information );
	}

	//SpdLog formats from the call's arguments, in a noexcept Write.  When fmt refuses a value there, it writes the line from
	//the Entry instead, with the error in that argument's place.
	TEST_F( LogGeneralTests, SpdLogWritesALineFmtRefuses ){
		let dir = TestsDir( "spd/refused", true );
		fs::remove_all( dir );
		fs::create_directories( dir );
		const jobject settings{ {"tags", jobject{{"default", "Information"}}}, {"sinks", jobject{{"file", jobject{{"path", dir.string()}}}}} };
		constexpr Logging::Format<const char*> format{ "refused {}" };
		let view = format.View();
		const char* null{};
		let args = fmt::make_format_args( null );
		{
			Logging::SpdLog spd{ settings };
			spd.Write( Logging::Record{ELogLevel::Warning, ELogTags::Test, SRCE_CUR, view, args} );
			spd.Shutdown( false, SRCE_CUR );
		}
		string text;
		for( let& entry : fs::directory_iterator(dir) ){
			std::ifstream is{ entry.path() };
			text += string{ std::istreambuf_iterator<char>{is}, {} };
		}
		EXPECT_NE( text.find("refused <format error: string pointer is null>"), string::npos ) << text;
	}

	//install-issues #13: a start truncated the text log, so restarting a product after a failure erased the lines that said
	//why.  With `keep`, the file sink rolls the previous run aside on open - <stem>.1.log - instead of erasing it.
	TEST_F( LogGeneralTests, FileSinkKeepsPreviousRuns ){
		let dir = TestsDir( "spd/keep", true );//per process:  a concurrent run would remove this one's files between its two starts.
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

#ifdef _WIN32
	//install-issues #52:  a second copy of a running product rolled the live log on open - windows refused the rename of the file
	//the first copy held, and spdlog then truncated it under that copy.  A held log is appended to instead, and nothing rolls.
	TEST_F( LogGeneralTests, FileSinkLeavesALiveLogAlone ){
		let dir = TestsDir( "spd/live", true );
		fs::remove_all( dir );
		fs::create_directories( dir );
		const jobject settings{ {"tags", jobject{{"default", "Information"}}}, {"sinks", jobject{{"file", jobject{{"path", dir.string()}, {"keep", 2}}}}} };
		let read = [&]( const fs::path& p )->string{ std::ifstream is{ p, std::ios::binary }; return string{ std::istreambuf_iterator<char>{is}, {} }; };
		{
			Logging::SpdLog running{ settings };
			running.Write( Logging::Entry{SRCE_CUR, ELogLevel::Warning, ELogTags::Test, string{"running copy"}} );
			{
				Logging::SpdLog second{ settings };
				second.Write( Logging::Entry{SRCE_CUR, ELogLevel::Warning, ELogTags::Test, string{"second copy"}} );
				second.Shutdown( false, SRCE_CUR );
			}
			running.Write( Logging::Entry{SRCE_CUR, ELogLevel::Warning, ELogTags::Test, string{"still running"}} );
			running.Shutdown( false, SRCE_CUR );
		}
		string stem;
		for( let& entry : fs::directory_iterator(dir) )
			stem = entry.path().stem().string();
		ASSERT_FALSE( stem.empty() );
		EXPECT_FALSE( fs::exists(dir/(stem+".1.log")) ) << "the live log was rolled";
		let log = read( dir/(stem+".log") );
		EXPECT_NE( log.find("running copy"), string::npos ) << log;
		EXPECT_NE( log.find("second copy"), string::npos ) << log;
		EXPECT_NE( log.find("still running"), string::npos ) << log;
		EXPECT_EQ( log.find('\0'), string::npos ) << "a zero-filled head - the file was truncated under the running copy";
		fs::remove_all( dir );
	}
#endif
	TEST_F( LogGeneralTests, CachedTags ){
		let _tags = MutatedTags;
		ExpectConfiguredLevel();
		constexpr auto logMessage = "scheduler msg";
		constexpr auto debugMessage = "scheduler dbg";
		{
			Logging::Capture debug{ _tags, ELogLevel::Debug };
			TRACE( logMessage );
			ASSERT_TRUE( debug.Find(logMessage).empty() ) << "Debug must not admit Trace";
			DBG( debugMessage );
			ASSERT_FALSE( debug.Find(debugMessage).empty() );
		}
		Logging::Capture trace{ _tags, ELogLevel::Trace };
		TRACE( logMessage );
		EXPECT_FALSE( trace.Find(logMessage).empty() ) << "the capture's level did not reach the cumulative, or the memoized tag level survived it";
	}

	TEST_F( LogGeneralTests, ArgsNotCalled ){
		let unConfiguredTags = MutatedTags;
		ExpectConfiguredLevel();
		Edited().SetLevel( unConfiguredTags, ELogLevel::Error );
		auto arg = []()->string {
			throw std::runtime_error("should not be called");
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
}