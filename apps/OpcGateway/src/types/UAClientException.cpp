#include "UAClientException.h"
#include "../UAClient.h"
#include "jde/opc/uatypes/Logger.h"

namespace Jde::Opc::Gateway{
	α httpStatus( StatusCode sc )ι->Jde::EHttpStatus{
		switch( sc ){
			case UA_STATUSCODE_BADIDENTITYTOKENREJECTED: return EHttpStatus::Forbidden;
			case UA_STATUSCODE_BADCONNECTIONREJECTED: return EHttpStatus::BadGateway;
			default: return EHttpStatus::InternalServerError;
		}
	}
	UAClientException::UAClientException( StatusCode sc, Handle uaHandle, RequestId requestId, SL sl, ELogLevel level )ι:
		UAException{ sc, {}, {.Level=level, .HttpStatus=httpStatus(sc)}, sl },
		_handle{ uaHandle },
		_requestId{ requestId }
	{}

	UAClientException::UAClientException( StatusCode sc, Handle uaHandle, string description, SL sl )ι:
		UAException{ sc, description, {.HttpStatus=httpStatus(sc)}, sl },
		_handle{ uaHandle },
		_requestId{ 0 }{
		_userMessage = description.size() ? Ƒ( "{} - {}", UAException::ClientDetail(), move(description) ) : string{};
	}

	α UAClientException::what()const noexcept->const char*{
		if( _what.empty() ){
			if( _requestId )
				_what = Ƒ( "[{:x}.{:x}]{}", _handle, _requestId, Exception::what() );
			else
				_what = Ƒ( "[{:x}]{}", _handle, Exception::what() );
		}
		return _what.c_str();
	}
	α UAClientException::UserMessage()Ι->const string&{
		if( _userMessage.empty() )
			_userMessage = UAException::ClientDetail();
		return _userMessage;
	}
}