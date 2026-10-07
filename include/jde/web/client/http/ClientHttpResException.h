#pragma once
#include <jde/fwk/exceptions/Exception.h>
#include "../exports.h"
#include "ClientHttpRes.h"

namespace Jde::Web::Client{
	struct ΓWC ClientHttpResException final : ExternalException{
		//the status alone is not a message - without the reason phrase & body, what() is empty and the failure surfaces as a blank line.
		ClientHttpResException( ClientHttpRes&& res, string url, SRCE )ι:
			ExternalException{
				Ƒ("({}){}", (uint32)res.Status(), Detail(res)),
				Ƒ("http request failed: {}", url), {ELogTags::Http|ELogTags::Client, (uint32)res.Status()},
				sl
			},
			_res{ move(res) }
		{}
		α Res()Ι->const ClientHttpRes&{ return _res; }
		α Status()Ι->http::status{ return _res.Status(); }
		α HttpStatus()Ι->EHttpStatus override{ return _statusCode ? _statusCode : (EHttpStatus)_res.Status(); }//relay the received status - truer than a blanket 500.
		α Move()ι->up<Exception> override{ return mu<ClientHttpResException>(move(*this)); }
		[[noreturn]] α Throw()->void override{ throw move(*this); }
	private:
		//error bodies are usually a sentence, but a proxy/gateway can answer with a whole html page - cap it.
		Ω Detail( const ClientHttpRes& res )ι->string{
			constexpr uint maxBody{ 512 };
			const auto& body = res.Body();
			return body.empty() ? string{}
				: body.size()>maxBody ? Ƒ( ": {}...", sv{body.data(), maxBody} )
				: Ƒ( ": {}", body );
		}
		ClientHttpRes _res;
	};
}
