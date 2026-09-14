#pragma once
#include "usings.h"
#include "exports.h"

#define Φ ΓWC auto
namespace Jde::Web::Client::Ssl{
	//asio's default verify mode is verify_none, so before this every https/wss client here accepted any certificate the peer
	//offered - including the google jwks fetch that mints logins.  contexts come from here now so peer verification is on by
	//default; a self-signed internal peer is trusted by naming its certificate, never by turning verification off.
	//
	//  /web/client/ssl/verifyPeer  bool, default true.  false restores the old accept-anything behaviour.
	//  /web/client/ssl/caFile      pem bundle of extra trust anchors, on top of the OS roots.
	//
	//AddTrustAnchor is the programmatic equivalent for callers that only learn the path at runtime (the tests' generated
	//self-signed server cert).  It only affects contexts built afterwards, so register anchors before the first connection.
	Φ AddTrustAnchor( fs::path pem )ι->void;
	Φ VerifyPeer()ι->bool;
	//a fresh context for a caller that owns one (the websocket sessions take an optional<ssl::context> by value).
	Φ MakeContext()ι->ssl::context;
	//the shared context the pooled http sessions hand to every stream.  Built once - except that a configured anchor absent
	//at that build (a peer's certificate written after this process started) is looked for again on each call, and the
	//context rebuilt when it exists, so a retried connection can succeed where the first one could not (install-issues #12).
	Φ Context()ι->ssl::context&;
	//per-connection: the context carries the trust anchors, but the name to match is only known once we know who we dialled.
	Φ SetVerifyHost( beast::ssl_stream<beast::tcp_stream>& stream, str host )ι->void;
}
#undef Φ
