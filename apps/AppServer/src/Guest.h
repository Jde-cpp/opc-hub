#pragma once
#include "usings.h"

namespace Jde::Access{ struct AccessListener; }
//The user a session nobody signed in to acts as (#483, docs/demo/spec.md *Guests*):  /access/guest, a user's slug, handed to the
//web library as its pk (Web::Server::Sessions::Guest) once the access load has run, and kept current from the listener's user
//events - set when a user of the slug is created or restored, or renamed to it, cleared with a Warning when it is deleted,
//purged or renamed away.  Unset, as shipped, every session with no sign-in is anonymous, as before.
namespace Jde::App::Server::Guest{
	α Configure( Access::AccessListener& listener )ε->void;//reads the setting, hooks the listener and resolves.
	α Resolve()ε->void;//the slug's user read as System:  the guest where it is live;  a Warning and none where there is no such user, or it is deleted.
	α Slug()ι->str;//what /access/guest names;  empty where it is unset.
}