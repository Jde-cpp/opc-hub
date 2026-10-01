#pragma once
#include "ExternalException.h"

namespace Jde{
	struct Γ CodeException /*final*/ : ExternalException{
		CodeException( std::error_code code, ELogTags tags, ELogLevel level=DefaultExceptionLevel, SRCE )ι;
		CodeException( std::error_code code, ELogTags tags, string value, ELogLevel level=DefaultExceptionLevel, SRCE )ι;

		 α Move()ι->up<Exception> override{ return mu<CodeException>(move(*this)); }
		[[noreturn]] α Throw()->void override{ throw move(*this); }

		Ω ToString( const std::error_code& errorCode )ι->string;
	protected:
		std::error_code _errorCode;
	};
}