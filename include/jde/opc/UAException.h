#pragma once
#include <jde/fwk/log/logTags.h>
#include "usings.h"
#include <jde/fwk/exceptions/ExternalException.h>
#include "uatypes/Logger.h"

#define UAε(x) if( let sc = x; sc ) throw UAException{ sc };

namespace Jde::Opc{
	struct UAExParams{
		ELogLevel Level{DefaultExceptionLevel};
		EHttpStatus HttpStatus{EHttpStatus::InternalServerError};
		EOpcLogTags Tags{EOpcLogTags::Opc};
	};
	struct UAException : ExternalException{
		UAException( StatusCode sc, string description={}, UAExParams params={}, SRCE ):ExternalException{ Message(sc), description, {params.Level, (ELogTags)params.Tags, (uint32)sc, params.HttpStatus}, sl }{}
		UAException( UAException&& from )ι:ExternalException{ move(from) }{}
		UAException( const UAException& from )ι:ExternalException{ from }{}
		Ω Message( StatusCode sc )ι->const char*{ return UA_StatusCode_name(sc); }
		Ω ToJson( StatusCode sc, bool includeMsg=false )ι->jobject{ return includeMsg ? jobject{{"sc", sc},{"message", UAException::Message(sc)}} :  jobject{{"sc", sc}}; }

		α Move()ι->up<Exception> override{ return mu<UAException>(move(*this)); }
		[[noreturn]] α Throw()->void override{ throw move(*this); }

		//the UA status names the protocol failure without exposing handles, request ids or source locations, so it is safe
		//to hand a client - what() keeps those internals.  RestException::Response appends this to the body.
		α ClientDetail()Ι->string override{ return format( "({:x}){}", Code(), Message((StatusCode)Code()) ); }
	};
}