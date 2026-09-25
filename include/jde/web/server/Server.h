#pragma once
#include "Sessions.h"

#define Φ ΓWS auto
namespace Jde::Web::Server{
	struct IRequestHandler;
	α BodyLimit()ι->uint;
	α SocketMessageMax()ι->uint;
	α MaxLogLength()ι->uint16;
	Φ Start( sp<IRequestHandler> handler )ε->void;
	//Throws if Start could not listen on address:port - a second copy of a running product, before it touches anything the first holds.
	Φ ThrowIfPortTaken( str address, PortType port, SRCE )ε->void;
	Φ Stop( sp<IRequestHandler>&& handler, bool terminate=false, SRCE )ι->void;
}
#undef Φ