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
	α Logger::Log( void *context, UA_LogLevel uaLevel, UA_LogCategory category, const char *m, va_list args )ι->void{
		let level = (ELogLevel)( (int)uaLevel/100-1 ); //level==UA_LOGLEVEL_DEBUG=200
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