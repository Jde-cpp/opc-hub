#pragma once
#include <open62541/plugin/log.h>

namespace Jde::Opc{
	//These extend fwk's ELogTags rather than colliding with it:  fwk uses bits 0-26 of the same 64-bit space, so ours
	//start at its LibraryTagBase, the 32-bit line, which also keeps them from breaking into the debugger (BreaksFor).
	//Opc takes the base bit and the vendor's UA_LogCategory values land in their own order immediately above it, which is
	//what lets Logger::Log map a category to a tag by arithmetic.
	constexpr uint OpcLogTagBase{ LibraryTagBase };
	constexpr uint UaLogTagBase{ OpcLogTagBase+1 };	//UA_LOGCATEGORY_NETWORK==0 lands here.
	enum class EOpcLogTags : uint{
		Opc           = 1ull << OpcLogTagBase,
		Net           = 1ull << (UaLogTagBase+UA_LOGCATEGORY_NETWORK),
		Secure        = 1ull << (UaLogTagBase+(uint)UA_LOGCATEGORY_SECURECHANNEL),
		Session       = 1ull << (UaLogTagBase+(uint)UA_LOGCATEGORY_SESSION),
		Server        = 1ull << (UaLogTagBase+(uint)UA_LOGCATEGORY_SERVER),
		Client        = 1ull << (UaLogTagBase+(uint)UA_LOGCATEGORY_CLIENT),
		User          = 1ull << (UaLogTagBase+(uint)UA_LOGCATEGORY_USERLAND),
		Security      = 1ull << (UaLogTagBase+(uint)UA_LOGCATEGORY_SECURITYPOLICY),
		EventLoop     = 1ull << (UaLogTagBase+(uint)UA_LOGCATEGORY_EVENTLOOP),
		PubSub        = 1ull << (UaLogTagBase+(uint)UA_LOGCATEGORY_PUBSUB),
		Discovery     = 1ull << (UaLogTagBase+(uint)UA_LOGCATEGORY_DISCOVERY),
		//ours, continuing past the last vendor category (Discovery==9).
		OpcCrypto     = Opc+underlying(ELogTags::Crypto),
		Monitoring    = 1ull << (UaLogTagBase+10),
		Browse        = 1ull << (UaLogTagBase+11),
		ProcessingLoop= 1ull << (UaLogTagBase+12)
	};
	static_assert( underlying(EOpcLogTags::Monitoring)==1ull<<(UaLogTagBase+(uint)UA_LOGCATEGORY_DISCOVERY+1),
		"a UA_LogCategory was added - move Monitoring/Browse/ProcessingLoop up and add its name to TagNames" );
	//What config keys, the ui and the wire call each bit, registered by every main with Logging::AddTags before Startup.
	constexpr array<TagName,14> TagNames{{
		{"opc", (ELogTags)EOpcLogTags::Opc},
		{"uaNet", (ELogTags)EOpcLogTags::Net}, {"uaSecure", (ELogTags)EOpcLogTags::Secure}, {"uaSession", (ELogTags)EOpcLogTags::Session},
		{"uaServer", (ELogTags)EOpcLogTags::Server}, {"uaClient", (ELogTags)EOpcLogTags::Client}, {"uaUser", (ELogTags)EOpcLogTags::User},
		{"uaSecurity", (ELogTags)EOpcLogTags::Security}, {"uaEvent", (ELogTags)EOpcLogTags::EventLoop},
		{"uaPubSub", (ELogTags)EOpcLogTags::PubSub}, {"uaDiscovery", (ELogTags)EOpcLogTags::Discovery},
		{"monitoring", (ELogTags)EOpcLogTags::Monitoring}, {"browse", (ELogTags)EOpcLogTags::Browse},
		{"processingLoop", (ELogTags)EOpcLogTags::ProcessingLoop}
	}};
	static_assert( NamesBitsFrom(TagNames, OpcLogTagBase), "TagNames names Opc, every UA_LogCategory, then Monitoring/Browse/ProcessingLoop, in bit order" );

	constexpr ELogTags IotReadTag{ ELogTags::Read | (ELogTags)EOpcLogTags::Opc };
	constexpr ELogTags MonitoringTag{ (ELogTags)EOpcLogTags::Monitoring };
	constexpr ELogTags DataChangesTag{ MonitoringTag | ELogTags::Pedantic };
	constexpr ELogTags BrowseTag{ (ELogTags)EOpcLogTags::Browse };

	α Format( const char* format, va_list args )->string;

	struct Logger : UA_Logger{
		Logger( Handle context=0 )ι;
		Ω Log( void* context, UA_LogLevel level, UA_LogCategory category, const char* message, va_list args )ι->void;//to Logging::LogStack, tagged by category.
		Ω Clear( UA_Logger* )ι->void{}
	};
}