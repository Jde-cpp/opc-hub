#include <jde/opc/uatypes/Logger.h>
#include <open62541/types.h>

#define let const auto
namespace Jde::Opc{
	constexpr array<sv,14> EOpcLogTagstrings{ "opc",
		"uaNet", "uaSecure", "uaSession", "uaServer", "uaClient", "uaUser", "uaSecurity", "uaEvent", "uaPubSub", "uaDiscovery",
		"monitoring", "browse", "processingLoop" };
	constexpr uint _uaLogCategoryCount{ (uint)UA_LOGCATEGORY_DISCOVERY+1 };
	static_assert( EOpcLogTagstrings.size()==1+_uaLogCategoryCount+3, "EOpcLogTagstrings must name Opc, every UA_LogCategory, then our three - see EOpcLogTags in Logger.h" );

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
	α UALogParser::ToString( ELogTags tags )Ι->string{
		string y;
		for( uint i=0; i<EOpcLogTagstrings.size(); ++i ){
			if( (uint)tags & (1ull << (OpcLogTagBase+i)) )
				y += string{ EOpcLogTagstrings[i] }+'.';
		}
		if( y.size() )
			y.pop_back();
		return y;
	}
	α UALogParser::ToTag( str name )Ι->ELogTags{
		ELogTags tag{};
		for( uint i=0; empty(tag) && i<EOpcLogTagstrings.size(); ++i )
			if( EOpcLogTagstrings[i]==name )
				tag = (ELogTags)(1ull << (OpcLogTagBase+i));
		return tag;
	}
	α UALogParser::Tags()Ι->flat_map<string,uint>{
		flat_map<string,uint> y;
		for( uint i=0; i<EOpcLogTagstrings.size(); ++i )
			y.emplace( string{EOpcLogTagstrings[i]}, 1ull << (OpcLogTagBase+i) );
		return y;
	}
}