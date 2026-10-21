#include <jde/web/server/IHttpRequestAwait.h>

#define let const auto

namespace Jde::Web::Server{
	α IHttpRequestAwait::Suspend()ι->void{
		_request.LogRead();
		let target = _request.Target();
		ResumeExp( RestException{EHttpStatus::NotFound, SRCE_CUR, move(_request), "Unknown target '{}'", target} );
	}

	α IHttpRequestAwait::await_resume()ε->HttpTaskResult{
		if( auto e = Promise() ? Promise()->MoveExp() : nullptr; e ){
			if( auto rest = dynamic_cast<RestException*>(e.get()); rest )
				rest->Throw();
			throw RestException{ e->HttpStatus(), move(*e), move(_request) };
		}
		return _readyResult
			? HttpTaskResult{ move(*_readyResult), move(_request) }
			: Promise()->Value() ? move( *Promise()->Value() ) : HttpTaskResult{ {}, move(_request) };
	}
}