//EOpcLogTags extends fwk's ELogTags into bits 32-45.  UALogParser is what makes those bits addressable by name from a
//jsonnet config or a remote log subscription, and main.cpp registers it before Process::Startup.
#include <gtest/gtest.h>
#include <jde/opc/uatypes/Logger.h>

#define let const auto

namespace Jde::Opc::Tests{
	Ω format( const char* fmt, ... )->string{
		va_list args; va_start( args, fmt );
		let y = Opc::Format( fmt, args );
		va_end( args );
		return y;
	}

	//L5: the old fixed 512-byte buffer made UA_String_vformat report BADENCODINGLIMITSEXCEEDED, and Format then discarded
	//what had been written and logged the format string itself - so the messages that overflowed, the cert and endpoint
	//diagnostics, were exactly the ones that became unreadable.
	TEST( UALogParserTests, FormatKeepsMessagesPastTheOldBufferSize ){
		EXPECT_EQ( format("plain %d", 42), "plain 42" );//the short path is unchanged.
		for( let size : { 511u, 512u, 513u, 4096u } ){
			let arg = string( size, 'x' );
			let y = format( "%s", arg.c_str() );
			EXPECT_EQ( y.size(), size );
			EXPECT_EQ( y, arg ) << size;//not "%s", which is what a discarded buffer used to leave behind.
		}
	}

	TEST( UALogParserTests, TagNamesRoundTrip ){
		UALogParser parser;
		EXPECT_EQ( parser.ToTag("opc"), (ELogTags)EOpcLogTags::Opc );
		EXPECT_EQ( parser.ToTag("uaClient"), (ELogTags)EOpcLogTags::Client );
		EXPECT_EQ( parser.ToTag("monitoring"), (ELogTags)EOpcLogTags::Monitoring );
		EXPECT_EQ( parser.ToTag("browse"), (ELogTags)EOpcLogTags::Browse );
		EXPECT_EQ( parser.ToTag("processingLoop"), (ELogTags)EOpcLogTags::ProcessingLoop );
		EXPECT_EQ( parser.ToTag("nope"), ELogTags{} ); //an unknown name yields no bits, it does not guess.

		EXPECT_EQ( parser.ToString((ELogTags)EOpcLogTags::Opc), "opc" );
		EXPECT_EQ( parser.ToString((ELogTags)EOpcLogTags::Browse), "browse" );
		EXPECT_EQ( parser.ToString(ELogTags::App), "" );  //a plain fwk tag is not ours to name.
		EXPECT_EQ( parser.Tags().size(), 14u );
	}

	TEST( UALogParserTests, CombinedTagsSerializeInBitOrder ){
		UALogParser parser;
		let both = (ELogTags)( (uint)EOpcLogTags::Opc | (uint)EOpcLogTags::Browse );
		EXPECT_EQ( parser.ToString(both), "opc.browse" );
	}

	//The framework consults the registered parsers, so the jsonnet `logging.spd.tags` block can name opc tags.
	TEST( UALogParserTests, RegisteredWithTheFramework ){
		EXPECT_EQ( ToLogTags(sv{"browse"}), (ELogTags)EOpcLogTags::Browse );
		EXPECT_EQ( ToLogTags(sv{"opc"}), (ELogTags)EOpcLogTags::Opc );
		EXPECT_TRUE( Logging::Tags().contains("monitoring") );
		EXPECT_TRUE( Logging::Tags().contains("app") ); //fwk's own tags are still there.
	}

	//The composite tags the library hands to the log macros.
	TEST( UALogParserTests, CompositeTags ){
		EXPECT_EQ( (uint)IotReadTag & (uint)ELogTags::Read, (uint)ELogTags::Read );
		EXPECT_EQ( (uint)IotReadTag & (uint)EOpcLogTags::Opc, (uint)EOpcLogTags::Opc );
		EXPECT_EQ( (uint)DataChangesTag & (uint)ELogTags::Pedantic, (uint)ELogTags::Pedantic );
		EXPECT_EQ( (uint)DataChangesTag & (uint)EOpcLogTags::Monitoring, (uint)EOpcLogTags::Monitoring );
	}

	//Every EOpcLogTags bit is above 31 - they extend the fwk space rather than colliding with it.
	TEST( UALogParserTests, TagsExtendRatherThanOverlapTheFrameworkSpace ){
		for( let& [name, bit] : UALogParser{}.Tags() )
			EXPECT_EQ( bit & 0xFFFFFFFFull, 0u ) << name;
	}

	//#14: fwk spelled ELogTags::All as ~0ul.  `unsigned long` is 32-bit under the windows ABI while the enum's underlying
	//type is 64-bit, so All was 0xFFFFFFFF and masked off every opc tag - SubscribeLog's default Tags{ELogTags::All}
	//dropped the whole opc/uaClient/monitoring/browse channel, on the win-clang build only (this passed on linux).
	TEST( UALogParserTests, AllIncludesTheOpcTags ){
		EXPECT_NE( (uint)ELogTags::All & (uint)EOpcLogTags::Opc, 0u );
		EXPECT_NE( (uint)ELogTags::All & (uint)EOpcLogTags::Browse, 0u );
		EXPECT_NE( (uint)ELogTags::All & (uint)EOpcLogTags::Monitoring, 0u );
		for( let& [name, bit] : UALogParser{}.Tags() )//not just the three above:  every registered bit has to survive the mask.
			EXPECT_EQ( (uint)ELogTags::All & bit, bit ) << name;
	}
}
