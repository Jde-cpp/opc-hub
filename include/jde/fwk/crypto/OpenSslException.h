#pragma once
#include <jde/fwk/exceptions/ExternalException.h>
#include <jde/fwk/log/logTags.h>
#define Φ Γ auto

namespace Jde::Crypto{
	struct OpenSslException final : ExternalException{
		OpenSslException( string m, SRCE )ι:
			OpenSslException{ move(m), CurrentErrorCode(), sl }
		{}
		OpenSslException( string m, uint32 rc, SRCE )ι:
			OpenSslException{ CurrentError(rc), move(m), {ELogLevel::Warning, ELogTags::Crypto, rc}, sl }
		{}
		OpenSslException( string externalMessage, string description, ExceptionArgs args, SRCE )ι:
			ExternalException{ move(externalMessage), move(description), args, sl }
		{}
		static Φ CurrentError()ι->string;
		static Φ CurrentError( uint32 rc )ι->string;
		//The earliest queued error, and the queue emptied behind it.  One failed call queues several - a DER parse leaves "wrong
		//tag" and then "nested asn1 error", the same failure seen from further out - and popping the first alone left the rest
		//on the thread's queue to be the *first* thing the next OpenSslException on that thread found:  a trust scan that
		//skipped an unparseable .cer made a later, unrelated key failure read "asn1 encoding routines::wrong tag"
		//(reviews/m2-closing.md #10).  The earliest is the one kept - it is the cause; what follows it is its echo.
		static Φ CurrentErrorCode()ι->uint32;
		α Move()ι->up<Exception> override{ return mu<OpenSslException>(move(*this)); }
		[[noreturn]] α Throw()->void override{ throw move(*this); }
	};
}
#undef Φ