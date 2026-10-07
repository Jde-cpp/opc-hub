#pragma once
#include "Sessions.h"

namespace Jde::Web::Server{
	struct IRequestHandler;
	α BodyLimit()ι->uint;
	α SocketMessageMax()ι->uint;
	α Start( sp<IRequestHandler> handler )ε->void;
	//Throws if Start could not listen on address:port - a second copy of a running product, before it touches anything the first holds.
	α ThrowIfPortTaken( str address, PortType port, SRCE )ε->void;
	α Stop( sp<IRequestHandler>&& handler, bool terminate=false, SRCE )ι->void;
}
