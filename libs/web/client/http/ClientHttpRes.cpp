#include <jde/web/client/http/ClientHttpRes.h>
#include <jde/web/Gzip.h>
#include <jde/web/client/usings.h>
#define let const auto

namespace Jde::Web::Client{
	α GetBody( const http::response<http::string_body>& res )ε->string{
		return res.base()[http::field::content_encoding]=="gzip" ? *Gunzip( res.body() ) : res.body();//no limit, so never nullopt.
	}
	ClientHttpRes::ClientHttpRes( const http::response<http::string_body>& res )ε:
		_body{ GetBody(res) },
		_status{ res.result() },
		_headers{ res.base() }
	{}

	α ClientHttpRes::RedirectVariables()Ε->tuple<string,string,PortType,bool>{
		let location = string{ _headers[http::field::location] };
		let url = Str::ParseUrl( location );
		if( url.Scheme.empty() ){
			THROW_IF( !location.starts_with('/'), "Could not parse redirect:  {}", location );
			return make_tuple( string{}, location, PortType{443}, true );//relative redirect - caller reuses the original host, port & scheme.
		}
		let isSsl = url.Scheme!="http";
		PortType port{ isSsl ? PortType{443} : PortType{80} };
		if( url.Port.size() ){
			let port16 = Str::TryTo<PortType>( url.Port );
			THROW_IF( !port16, "Could not parse redirect:  {}", location );
			port = *port16;
		}
		THROW_IF( url.Host.empty(), "Could not parse redirect:  {}", location );
		return make_tuple( string{url.Host}, url.Path.empty() ? string{"/"} : string{url.Path}, port, isSsl );
	}
}