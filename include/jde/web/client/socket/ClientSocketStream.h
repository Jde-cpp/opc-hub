#pragma once
#include "../usings.h"

namespace Jde::Web::Client{
	struct IClientSocketSession;
	//Non-template handle held by IClientSocketSession; ClientSocketStream<TWs> (ClientSocketStream.cpp) supplies the beast ops.
	struct IClientSocketStream{
		//plain or ssl, by whether ctx is set.
		Ω Create( net::io_context& ioc, optional<ssl::context>& ctx )ι->sp<IClientSocketStream>;
		virtual ~IClientSocketStream()=default;

		β OnResolve( tcp::resolver::results_type results, sp<IClientSocketSession> session )ι->void=0;
		β OnConnect( tcp::resolver::results_type::endpoint_type ep, string& host, sp<IClientSocketSession> session )ι->void=0;
		β AfterHandshake( const string& host, sp<IClientSocketSession> session )ι->void=0;
		β AsyncRead( sp<IClientSocketSession> session )ι->void=0;
		β AsyncWrite( string buffer, sp<IClientSocketSession> session )ι->void=0;
		β Close( sp<IClientSocketSession> session, bool terminate, SRCE )ι->void=0;
		β ReadBuffer()ι->std::span<uint8_t> =0;
		β IsSsl()Ι->bool=0;
		//Close early-returns on an already-closing stream without completing anyone's await, so callers have to ask first.
		β IsClosing()Ι->bool=0;
	};
}
