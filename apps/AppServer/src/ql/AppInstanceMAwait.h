#pragma once
#include <jde/ql/types/MutationQL.h>

namespace Jde::App::Server{
	//startApplicationInstance and stopApplicationInstance, routed here by Server::CustomMutation:  a start is not implemented, a
	//refusal;  a stop kills the instance when it advertises this host.  Null for any other mutation, which it judges before it
	//asks for the app client.
	α AppInstanceMutation( const QL::MutationQL& m, SL sl )ι->up<TAwait<jvalue>>;
}