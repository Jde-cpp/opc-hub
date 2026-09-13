#pragma once
#include "exports.h"

namespace Jde::Web::Server{
	//The Angular site, served from the listener the REST and socket routes share (reviews/install-issues.md #3):  one origin for
	//the page and its api, no IIS, no cross-origin question, and the current-user install gets a UI.  /http/site names the
	//directory - the installers put the built site at <program dir>/web, beside the exe's dir - and no key means no site (dev
	//serves it from ng serve).  A page route - a path without an extension, /login, /apps/gateways - is index.html, which the
	//page routes client-side:  #4's deep-link fallback, the one IIS needed the rewrite module for.
	struct ΓWS StaticSite{
		struct File{ string Body; string ContentType; string CacheControl; };
		StaticSite( fs::path root )ι;
		//the file for a GET target (decoded, no query) - nullopt for a miss, which is the caller's 404.  Never escapes the root:
		//a segment that is dots, a dotfile, a backslash or a drive colon is refused before any path is built, and the joined
		//path is checked against the root once normalized.
		α Resolve( sv target )Ε->optional<File>;
		α Root()Ι->const fs::path&{ return _root; }
		Ω ContentType( const fs::path& file )ι->sv;
	private:
		fs::path _root;
	};
	//the site /http/site names, nullptr without one - built on first use, once the settings are loaded.
	ΓWS auto Site()ι->StaticSite*;
}
