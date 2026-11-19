#include <jde/fwk/exceptions/CodeException.h>

#define let const auto
namespace Jde{
	CodeException::CodeException( std::error_code code, ELogTags tags, ELogLevel level, SL sl )ι:
		CodeException{ code, tags, {}, level, sl }
	{}
	CodeException::CodeException( std::error_code code, ELogTags tags, string msg, ELogLevel level, SL sl )ι:
		ExternalException{ ToString(code), move(msg), {level,tags, (uint32)code.value()}, sl },
		_errorCode{ code }
	{}

	α CodeException::ToString( const std::error_code& errorCode )ι->string{
		let& category = errorCode.category();
		let message = errorCode.message();
		return Ƒ( "{} - {}", category.name(), message );
	}
	α CodeException::Throw()->void{ throw move(*this); }
}