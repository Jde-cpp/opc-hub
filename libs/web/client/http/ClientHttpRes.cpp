#include <jde/web/client/http/ClientHttpRes.h>
#include <zlib.h>
#include <jde/web/client/usings.h>
#define let const auto

namespace Jde::Web::Client{
	α GetBody( const http::response<http::string_body>& res )ε->string{
		string body{ res.body() };
		if( res.base()[http::field::content_encoding]=="gzip" ){
			z_stream zs{ (Bytef*)body.data(), (uInt)body.size() }; //zs.zalloc = Z_NULL; zs.zfree = Z_NULL; zs.opaque = Z_NULL;
			string unziped;
			auto hr = inflateInit2( &zs, MAX_WBITS | 16 ); CHECK(hr==Z_OK);
			hr = Z_BUF_ERROR;
			for( auto nextOutSize = std::max<size_t>(body.size(), 512); hr==Z_BUF_ERROR && zs.avail_out==0; nextOutSize*=2 ){//Z_BUF_ERROR with unused output space means truncated input, not a full buffer - stop instead of growing forever.
				let start = unziped.size();
				unziped.resize( start+nextOutSize );
				zs.next_out = (Bytef*)unziped.data()+start;
				zs.avail_out = nextOutSize;
				hr = inflate( &zs, Z_FINISH );
			}
			let totalOut = zs.total_out;
			inflateEnd( &zs );
			CHECK( hr==Z_STREAM_END );
			unziped.resize( totalOut );
			body = move( unziped );
		}
		return body;
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