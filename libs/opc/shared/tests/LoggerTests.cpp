//EOpcLogTags extends fwk's ELogTags into bits 32-45.  TagNames is what makes those bits addressable by name from a
//jsonnet config or a remote log subscription, and main.cpp registers it with Logging::AddTags before Process::Startup.
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
	TEST( OpcLoggerTests, FormatKeepsMessagesPastTheOldBufferSize ){
		EXPECT_EQ( format("plain %d", 42), "plain 42" );//the short path is unchanged.
		for( let size : { 511u, 512u, 513u, 4096u } ){
			let arg = string( size, 'x' );
			let y = format( "%s", arg.c_str() );
			EXPECT_EQ( y.size(), size );
			EXPECT_EQ( y, arg ) << size;//not "%s", which is what a discarded buffer used to leave behind.
		}
	}

	//The framework reads the registered names, so the jsonnet `logging.spd.tags` block can name opc tags.
	TEST( OpcLoggerTests, TagNamesRoundTrip ){
		EXPECT_EQ( ToLogTags(sv{"opc"}), (ELogTags)EOpcLogTags::Opc );
		EXPECT_EQ( ToLogTags(sv{"uaClient"}), (ELogTags)EOpcLogTags::Client );
		EXPECT_EQ( ToLogTags(sv{"monitoring"}), (ELogTags)EOpcLogTags::Monitoring );
		EXPECT_EQ( ToLogTags(sv{"browse"}), (ELogTags)EOpcLogTags::Browse );
		EXPECT_EQ( ToLogTags(sv{"processingLoop"}), (ELogTags)EOpcLogTags::ProcessingLoop );
		EXPECT_EQ( ToLogTags(sv{"nope"}), ELogTags::None ); //an unknown name yields no bits, it does not guess.

		EXPECT_EQ( ToName((ELogTags)EOpcLogTags::Opc), "opc" );
		EXPECT_EQ( ToName((ELogTags)EOpcLogTags::Browse), "browse" );
		EXPECT_TRUE( Logging::Tags().contains("monitoring") );
		EXPECT_TRUE( Logging::Tags().contains("app") ); //fwk's own tags are still there.
		EXPECT_EQ( Logging::Tags().size(), FwkTags.size()+TagNames.size() );
	}

	//One element per bit, fwk's first, whichever library named it.  The parser this replaced joined its own bits into one
	//element, so the log grid showed DataChanges.cpp's opc|monitoring lines as the single tag "opc.monitoring".
	TEST( OpcLoggerTests, CombinedTagsSerializeInBitOrder ){
		let both = (ELogTags)EOpcLogTags::Opc | (ELogTags)EOpcLogTags::Monitoring;
		EXPECT_EQ( serialize(ToValue(both)), R"(["opc","monitoring"])" );
		EXPECT_EQ( ToName(both), "opc.monitoring" );
		EXPECT_EQ( ToLogTags(ToValue(both)), both );
		EXPECT_EQ( serialize(ToValue(IotReadTag)), R"(["read","opc"])" );
		EXPECT_EQ( serialize(ToValue(DataChangesTag)), R"(["pedantic","monitoring"])" );
	}

	//The composite tags the library hands to the log macros.
	TEST( OpcLoggerTests, CompositeTags ){
		EXPECT_EQ( (uint)IotReadTag & (uint)ELogTags::Read, (uint)ELogTags::Read );
		EXPECT_EQ( (uint)IotReadTag & (uint)EOpcLogTags::Opc, (uint)EOpcLogTags::Opc );
		EXPECT_EQ( (uint)DataChangesTag & (uint)ELogTags::Pedantic, (uint)ELogTags::Pedantic );
		EXPECT_EQ( (uint)DataChangesTag & (uint)EOpcLogTags::Monitoring, (uint)EOpcLogTags::Monitoring );
	}

	//Every EOpcLogTags bit is above 31 - they extend the fwk space rather than colliding with it.  TagNames' static_assert
	//puts them there;  AddTags refusing a second registration is what keeps another library off them.
	TEST( OpcLoggerTests, TagsExtendRatherThanOverlapTheFrameworkSpace ){
		for( let& [name, tag] : TagNames )
			EXPECT_EQ( (uint)tag & 0xFFFFFFFFull, 0u ) << name;
		EXPECT_THROW( Logging::AddTags(TagNames), std::logic_error ) << "main.cpp registered them already";
	}

	//#14: fwk spelled ELogTags::All as ~0ul.  `unsigned long` is 32-bit under the windows ABI while the enum's underlying
	//type is 64-bit, so All was 0xFFFFFFFF and masked off every opc tag - SubscribeLog's default Tags{ELogTags::All}
	//dropped the whole opc/uaClient/monitoring/browse channel, on the win-clang build only (this passed on linux).
	TEST( OpcLoggerTests, AllIncludesTheOpcTags ){
		EXPECT_NE( (uint)ELogTags::All & (uint)EOpcLogTags::Opc, 0u );
		EXPECT_NE( (uint)ELogTags::All & (uint)EOpcLogTags::Browse, 0u );
		EXPECT_NE( (uint)ELogTags::All & (uint)EOpcLogTags::Monitoring, 0u );
		for( let& [name, tag] : TagNames )//not just the three above:  every registered bit has to survive the mask.
			EXPECT_EQ( (uint)ELogTags::All & (uint)tag, (uint)tag ) << name;
	}
}