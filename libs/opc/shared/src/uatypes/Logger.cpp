#include <jde/opc/uatypes/Logger.h>
#include <open62541/types.h>

#define let const auto
namespace Jde::Opc{
	α Format( const char* format, va_list args )->string{
		UA_String str{};
		let result = UA_String_vformat( &str, format, args );
		string y = result==UA_STATUSCODE_GOOD
			? string{ (char*)str.data, str.length }
			: Ƒ( "({:x})UA_String_vformat failed for: '{}'.", result, format );
		UA_String_clear( &str );
		return y;
	}
	//open62541's default config gives each event loop an interrupt manager, and without epoll (Windows) a process holds one at a
	//time, so every default config made while another lives logs this - the hub's second UA server, after its flow server.  Nothing
	//here registers an interrupt.
	constexpr sv _noInterruptManager{ "Cannot create the Interrupt Manager (only relevant if used)" };
	α Logger::Log( void *context, UA_LogLevel uaLevel, UA_LogCategory category, const char *m, va_list args )ι->void{
		auto level = (ELogLevel)( (int)uaLevel/100-1 ); //level==UA_LOGLEVEL_DEBUG=200
		if( level==ELogLevel::Error && category==UA_LOGCATEGORY_APPLICATION && m==_noInterruptManager )
			level = ELogLevel::Debug;
		uint tag_ = 1ull << (UaLogTagBase+(uint)category);
		const EOpcLogTags tag = (EOpcLogTags)( tag_ );
		if( Logging::ShouldLog(level, (ELogTags)tag) ){
			let message = Opc::Format(m,args);
			Logging::LogStack( level, (ELogTags)tag, 2, "[{}]{}", hex((uint)context), message );
		}
	}

	Logger::Logger( Handle uaHandle )ι:
		UA_Logger{ Log, (void*)uaHandle, Clear }
	{}
}