#include <jde/tests/Capture.h>

#define let const auto
namespace Jde::Tests{
	constexpr ELogTags _tags{ ELogTags::Scheduler };//no logger configures it:  spd's Information, the suite capture's Debug.

	//reviews-issue/MemoryLog.md #4:  a test's levels were the process's until something put them back.  A capture's last as
	//long as it does.
	TEST( CaptureTests, LevelsLastAsLongAsTheCapture ){
		ASSERT_FALSE( Logging::ShouldLog(ELogLevel::Trace, _tags) );
		{
			Logging::Capture trace{ _tags, ELogLevel::Trace };
			EXPECT_TRUE( Logging::ShouldLog(ELogLevel::Trace, _tags) );
			EXPECT_FALSE( Logging::ShouldLog(ELogLevel::Trace, ELogTags::Sql) ) << "a capture of one tag opened another";
		}
		EXPECT_FALSE( Logging::ShouldLog(ELogLevel::Trace, _tags) ) << "the capture's level outlived it";
	}

	//A capture reads from its construction, so what came before - an earlier test's thread, say - is not its to find.
	TEST( CaptureTests, ReadsFromItsConstruction ){
		DBG( "capture before" );
		Logging::Capture capture{ _tags, ELogLevel::Debug };
		DBG( "capture after" );
		EXPECT_TRUE( capture.Find("capture before").empty() );
		EXPECT_EQ( capture.Find("capture after").size(), 1u );
		capture.Restart();
		EXPECT_TRUE( capture.Find("capture after").empty() ) << "Restart kept what came before it";
	}

	//A capture only widens (reviews-issue/MemoryLog.md #8):  a capture at Warning leaves the tag at the suite's Debug, and a
	//tag set's level reaches only the lines that carry all of it.
	TEST( CaptureTests, ACaptureNeverNarrowsTheSuite ){
		ASSERT_TRUE( Logging::Captures(ELogLevel::Debug, _tags) ) << "the suite captures Debug";
		Logging::Capture warnings{ _tags, ELogLevel::Warning };
		EXPECT_TRUE( Logging::Captures(ELogLevel::Debug, _tags) ) << "a capture at Warning narrowed the suite's Debug";
		Logging::Capture combined{ _tags | ELogTags::Sql, ELogLevel::Trace };
		EXPECT_TRUE( Logging::Captures(ELogLevel::Trace, _tags | ELogTags::Sql) );
		EXPECT_FALSE( Logging::Captures(ELogLevel::Trace, _tags) ) << "a combined tag's level reached a line carrying only part of it";
	}

	//reviews-issue/MemoryLog.md #5:  an entry views its template, file and function in static storage, which a FreeLibrary
	//unmaps while a capture may still hold the entry.  The capture keeps its own copies, under the same ids.
	TEST( CaptureTests, KeptEntriesOwnTheirNames ){
		static constexpr char literal[]{ "capture owns {}" };
		Logging::Capture capture{ _tags, ELogLevel::Debug };
		const Logging::Entry e{ SRCE_CUR, ELogLevel::Debug, _tags, literal, 1 };
		ASSERT_EQ( e.Text().data(), literal );
		Logging::Log( e );
		let kept = capture.Find( e.Id() );
		ASSERT_EQ( kept.size(), 1u );
		EXPECT_NE( kept[0].Text().data(), literal ) << "the template still views the literal";
		EXPECT_EQ( kept[0].Text(), e.Text() );
		EXPECT_NE( kept[0].File().data(), e.File().data() ) << "the file still views the source_location";
		EXPECT_EQ( kept[0].File(), e.File() );
		EXPECT_EQ( kept[0].FileId(), e.FileId() );
		EXPECT_NE( kept[0].Function().data(), e.Function().data() ) << "the function still views the source_location";
		EXPECT_EQ( kept[0].Function(), e.Function() );
		EXPECT_EQ( kept[0].FunctionId(), e.FunctionId() );
		EXPECT_EQ( kept[0].Message(), "capture owns 1" );
	}

	//Each capture reads every line kept, and one's close neither takes the other's levels nor drops what the other still reads.
	TEST( CaptureTests, NestedCapturesCloseApart ){
		Logging::Capture outer{ _tags, ELogLevel::Trace };
		{
			Logging::Capture inner{ ELogTags::Sql, ELogLevel::Trace };
			TRACE( "nested capture line" );
			EXPECT_EQ( inner.Find("nested capture line").size(), 1u ) << "a capture reads only what its own levels took";
		}
		EXPECT_TRUE( Logging::Captures(ELogLevel::Trace, _tags) ) << "the inner capture's close took the outer's level";
		EXPECT_FALSE( Logging::Captures(ELogLevel::Trace, ELogTags::Sql) ) << "the inner capture's level outlived it";
		EXPECT_EQ( outer.Find("nested capture line").size(), 1u ) << "the inner capture's close dropped what the outer still reads";
	}
}