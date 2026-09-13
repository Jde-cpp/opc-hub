#include <jde/web/server/StaticSite.h>
#include <jde/fwk/io/file.h>
#include <jde/fwk/str.h>
#define let const auto

namespace Jde::Web::Server{
	constexpr ELogTags _tags{ ELogTags::Server | ELogTags::Http };

	StaticSite::StaticSite( fs::path root )ι: _root{ move(root) }{
		let ok = fs::is_directory( _root );//two ifs: the log macros expand to an if of their own and cannot precede an else.
		if( ok )
			INFO( "Serving the site from '{}'.", _root.string() );
		if( !ok )
			WARN( "/http/site '{}' is not a directory - no page is served (404).", _root.string() );
	}

	α StaticSite::ContentType( const fs::path& file )ι->sv{
		static const flat_map<string,sv> types{
			{".html", "text/html; charset=utf-8"}, {".js", "text/javascript; charset=utf-8"}, {".mjs", "text/javascript; charset=utf-8"},
			{".css", "text/css; charset=utf-8"}, {".map", "application/json"}, {".json", "application/json"}, {".xml", "application/xml"},
			{".ico", "image/x-icon"}, {".png", "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"}, {".gif", "image/gif"},
			{".svg", "image/svg+xml"}, {".webp", "image/webp"}, {".woff", "font/woff"}, {".woff2", "font/woff2"}, {".ttf", "font/ttf"},
			{".txt", "text/plain; charset=utf-8"}, {".md", "text/markdown; charset=utf-8"}, {".wasm", "application/wasm"}
		};
		let p = types.find( Str::ToLower(file.extension().string()) );
		return p==types.end() ? sv{"application/octet-stream"} : p->second;
	}

	//the build's output hashing - main-MHJYHGLH.js, chunk-BBIwwlZT.js: a changed file gets a new name, so a cached copy can never
	//go stale and the browser may keep it for good.  Everything else - index.html above all - is revalidated on every load.
	//The hash is esbuild's: 8 chars of a base64 alphabet, so `_` and `-` occur in it (chunk-D_2EVqc6.js, chunk-DJVAsKa_.js) -
	//the 8 are counted from the end, not from the last dash.  The lazy chunks carry one even in an unhashed build
	//(--output-hashing=none: web/opc/scripts/setup.sh); the entry files do only for a release build.
	Ω hashed( const fs::path& file )ι->bool{
		let stem = file.stem().string();
		return stem.size()>9 && stem[stem.size()-9]=='-' && std::all_of( stem.end()-8, stem.end(), [](char c){ return std::isalnum((unsigned char)c)!=0 || c=='_' || c=='-'; } );
	}

	α StaticSite::Resolve( sv target )Ε->optional<File>{
		if( target.empty() || target.front()!='/' || target.find('\\')!=sv::npos )
			return nullopt;
		for( let& segment : Str::Split(target.substr(1), '/') ){//dots (.. above all), dotfiles, and a drive colon - "C:" would make fs::path::operator/ replace the root.
			if( segment.empty() || segment.front()=='.' || segment.find(':')!=sv::npos )
				return nullopt;
		}
		let relative = fs::path{ string{target.substr(1)} };
		auto file = relative.empty() ? _root/"index.html" : _root/relative;
		bool page = relative.empty();
		if( !page && !fs::is_regular_file(file) ){
			if( relative.has_extension() )//a missing asset is a 404, not the page - the browser would otherwise cache html as a script.
				return nullopt;
			file = _root/"index.html";//a route of the page, routed client-side (#4)
			page = true;
		}
		if( !fs::is_regular_file(file) )
			return nullopt;
		auto rootNormal = _root.lexically_normal().string();//belt and braces on the segment checks above: the file has to sit under the root.
		if( !rootNormal.ends_with(fs::path::preferred_separator) )
			rootNormal += fs::path::preferred_separator;
		if( !file.lexically_normal().string().starts_with(rootNormal) )
			return nullopt;
		return File{ IO::Load(file), string{ContentType(file)}, page || !hashed(file) ? "no-cache" : "public, max-age=31536000, immutable" };
	}

	auto Site()ι->StaticSite*{
		static const up<StaticSite> site = []{ let dir = Settings::FindPath( "/http/site" ); return dir ? mu<StaticSite>(*dir) : up<StaticSite>{}; }();
		return site.get();
	}
}
