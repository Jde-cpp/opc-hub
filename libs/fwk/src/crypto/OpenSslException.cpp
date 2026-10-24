#include <jde/fwk/crypto/OpenSslException.h>
#include <openssl/err.h>

#define let const auto

namespace Jde::Crypto{
	α OpenSslException::CurrentError()ι->string{ return CurrentError(CurrentErrorCode()); }
	α OpenSslException::CurrentError( uint32 rc )ι->string{ if(!rc) return "no queued openssl error"; char b[256]; ::ERR_error_string_n(rc, b, sizeof(b)); return {b}; }//0 would format as 'error:00000000...' - noise masquerading as detail.
	α OpenSslException::CurrentErrorCode()ι->uint32{ let rc = (uint32)::ERR_get_error(); ::ERR_clear_error(); return rc; }
}