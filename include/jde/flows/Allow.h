#pragma once

//The hosts an `http request` may reach (docs/flows/spec.md *Sinks*):  the match its host makes at each send and each redirect.
namespace Jde::Opc::Flows{
	//Whether `host`, a url's, is one of `allow`:  each a name or a glob pattern, `*` any run of characters and `?` any one,
	//compared without case, an IPv6 literal with or without its brackets and a name with or without its last dot.  An
	//empty list allows none, and what holds a user, a path or a query is no host.
	α HostAllowed( sv host, std::span<const string> allow )ι->bool;
}