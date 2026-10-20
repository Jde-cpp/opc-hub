#include <jde/fwk/settings.h>
#include <iostream> // !important
#include <regex> // !important
#include <jde/fwk/chrono.h>
#include <jde/fwk/process/process.h>
#include <jde/fwk/str.h>
#include <jde/fwk/io/file.h>
#include <jde/fwk/log/MemoryLog.h>

#define let const auto

namespace Jde{
	constexpr ELogTags _tags{ ELogTags::Settings };
	up<jvalue> _settings; //up so doesn't get initialized 2x.
	α Settings::Value()ι->const jvalue&{
		if( !_settings )
			Load();
		return *_settings;
	}

	α Settings::FindDuration( sv path, SL sl )ι->optional<Duration>{
		optional<Duration> y;
		if( let setting = FindSV(path); setting.has_value() )
			y = Chrono::TryToDuration( string{*setting}, ELogLevel::Error, sl );
		return y;
	}

	α Settings::FindArgString( sv arg, sv path )ι->optional<string>{
		if( auto a = Process::FindArg(string{arg}); a && a->size() )
			return a;
		return FindString( path );
	}
	α Settings::FindArgDuration( sv arg, sv path, SL sl )ε->optional<Duration>{
		if( auto a = Process::FindArg(string{arg}); a && a->size() )
			return Chrono::ToDuration( move(*a), sl );
		return FindDuration( path, sl );
	}
	α Settings::FindArgNumber( sv arg, sv path )ε->optional<uint>{
		if( auto a = Process::FindArg(string{arg}); a && a->size() ){
			let n = Str::TryTo<uint>( *a );
			THROW_IF( !n, "{} '{}' is not a number.", arg, *a );
			return n;
		}
		return FindNumber<uint>( path );
	}

	consteval α buildTypeSubDir()->sv{
		if constexpr( _debug )
			return "Debug";
		else
			return "Release";
	}

	//$(NAME) names that resolve without an environment variable.  A table, not a branch chain: every one of these was
	//added later than the code around it, and the ones that were missing (HostName) silently expanded to "" instead of
	//failing.  Consulted only when the environment has no such variable, so a real one always wins.
	Ω builtIns()ι->const flat_map<sv,function<string()>>&{
		static const flat_map<sv,function<string()>> y{
			{ "JDE_BUILD_TYPE", []{ return string{buildTypeSubDir()}; } },
			{ "ProgramData", []{ return Process::ProgramDataFolder().string(); } },
			{ "PRODUCT_NAME", []{ return string{Process::ProductName()}; } },
			{ "HostName", []{ return Process::HostName(); } },
			//the dir the running exe sits in:  the installed args find the db driver and its proc modules beside the exe with
			//it, wherever the installer put them (Program Files, or a per-user Programs dir) - ExePath(), not argv[0], which
			//is whatever the launcher spelled.
			{ "ExeDir", []{ return Process::ExePath().parent_path().string(); } },
			//the platform's shared-library spelling, so one installed args file names the driver and the proc modules on both:
			//"$(ExeDir)/$(LibPrefix)Jde.DB.Sqlite$(LibExt)" - Jde.DB.Sqlite.dll on windows, libJde.DB.Sqlite.so on linux.
#ifdef _WIN32
			{ "LibPrefix", []{ return string{}; } },
			{ "LibExt", []{ return string{".dll"}; } }
#else
			{ "LibPrefix", []{ return string{"lib"}; } },
			{ "LibExt", []{ return string{".so"}; } }
#endif
		};
		return y;
	}

	Ω expandEnvVariable( string setting )ι->string{
		static const std::regex regex{ "\\$\\((.+?)\\)" };
		constexpr uint maxPasses{ 32 };//expansion is transitive but must be bounded - a value containing its own $(NAME) rescans forever.
		for( uint i=0;; ++i ){
			auto begin = std::sregex_iterator( setting.begin(), setting.end(), regex );
			if( begin==std::sregex_iterator() )
				break;
			if( i==maxPasses ){
				WARN( "'{}' - env expansion exceeded {} passes, cyclical $()?", setting, maxPasses );
				break;
			}
			let match = begin->str();
			let group = match.substr( 2, match.size()-3 );
			auto env = Process::GetEnv( group ).value_or( "" );
			if( env.empty() ){
				if( let p = builtIns().find( group ); p!=builtIns().end() )
					env = p->second(); //may legitimately be "" (LibPrefix on windows) - found, not missing.
				else
					DBG( "Environment variable '{}' not found", group );
			}
			setting = Str::Replace( setting, match, env );
		}
		return setting;
	}
	α Settings::FindString( sv path )ι->optional<string>{
		auto setting = Json::FindString( Value(), path );
		if( setting )
			setting = expandEnvVariable( *setting );
		return setting;
	}

	α Settings::FindStringArray( sv path )ι->vector<string>{
		vector<string> y;
		if( let jarray = Json::FindArray(Value(), path); jarray ){
			for( let& s : *jarray ){
				if( s.is_string() )
					y.push_back( expandEnvVariable(string{s.get_string()}) );
			}
		}
		return y;
	}

	α Settings::FindPathArray( sv path )ι->vector<fs::path>{
		let strings = FindStringArray( path );//same lookup & env expansion - only the element type differs.
		return { strings.begin(), strings.end() };
	}

	α Settings::FileStem()ι->string{
		let executable = Process::Executable().filename();
	#ifdef _MSC_VER
			auto stem = executable.stem().string();
			return stem.size()>4 ? stem.substr( 4 ) : stem;
	#else
			return executable.string().starts_with( "Jde." ) ? fs::path( executable.string().substr(4) ) : executable;
	#endif
		}

	optional<vector<fs::path>> _importPaths;
	Ω path()ι->fs::path{
		static fs::path _path;
		if( !_path.empty() )
			return _path;
		let fileName = fs::path{ Ƒ("{}.jsonnet", Settings::FileStem()) };

		vector<fs::path> paths{ fileName, fs::path{"../config"}/fileName, fs::path{"config"}/fileName };
		if( auto settingsFile = _importPaths ? nullopt : Process::FindArg("-settings"); settingsFile ){
			#ifdef _WIN32 //normalize once, before `paths` is built: a bash-style '/c/…' won't fs::exists on windows,
				//and -include resolves against whatever this produces.  Idempotent - a native path just round-trips
				//through the '\\?\' prefix that substr(4) strips back off.
				settingsFile = IO::BashToWindows( *settingsFile ).string().substr( 4 ); //remove //?/
			#endif
			paths = { fs::path{*settingsFile} };
		}

		auto p = find_if( paths, [](let& path){return fs::exists(path);} );
		_path = p!=paths.end() ? *p : Process::AppDataFolder()/fileName;
		//Each -include dir is relative to the config's own directory, so this can only run once the config is resolved.
		//It used to sit inside the -settings branch for want of that path, which silently dropped the flag on any run
		//that found its config by search instead - the one import mechanism there is, ignored without a word.
		if( auto cli = Process::FindArg("-include"); cli ){
			_importPaths = vector<fs::path>{};
			for( auto& relPath : Str::Split(*cli, ';') )
				_importPaths->push_back( _path.parent_path()/relPath );
		}
		std::cout << "settings path=" << _path.string() << std::endl;
		return _path;
	}
	α Settings::Directory()ι->fs::path{
		return path().parent_path();
	}

	α SetEnv( jvalue& value )->void{
		if( value.is_string() )
			value = expandEnvVariable( string{value.get_string()} );
		else if( value.is_object() )
			for( auto& [key,child] : value.get_object() )
				SetEnv( child );
		else if( value.is_array() )//arrays too: expanding only objects left every trustedCertDirs/scriptPaths entry
			for( auto& child : value.get_array() )//literal unless its reader happened to be Find{String,Path}Array.
				SetEnv( child );
	}
	α SetEnv( jobject& j )->void{
		for( auto& [key,value] : j )
			SetEnv( value );
	}

	Ω argMap( sv flag )ι->flat_map<string,string>{
		flat_map<string,string> y;
		let range = Process::Args().equal_range( string{flag} );
		for( auto it = range.first; it != range.second; ++it ){
			if( it->second.empty() )
				continue;
			if( let i = it->second.find('='); i!=string::npos )
				y.emplace( it->second.substr(0, i), it->second.substr(i+1) );
			else
				WARN( "Ignoring {} '{}' - must be key=value.", flag, it->second );
		}
		return y;
	}

	α Settings::Load()ι->void{
		let settingsPath = path();
		try{
			if( !fs::exists(settingsPath) )
				throw std::runtime_error{ Ƒ("file does not exist: '{}'", settingsPath.string()) };
			flat_map<string,string> args;
			if( Process::FindArg("-tests") || Process::FindArg("-ctest") ){
				args["buildTarget"] = _debug ? "debug" : "release";
				args["cwd"] = fs::current_path().string();
				args["logsDir"] = args["cwd"] + "/logs";
				args["windows"] = _windows ? "true" : "false";
				args["recreateDB"] = "true";//a suite starts from a clean schema; -arg=recreateDB=false is applied below and wins.
			}
			for( let& [key, value] : argMap("-arg") )
				args[key] = value;
			let tlas = Process::FindArg("-sync") ? flat_map<string,string>{{"sync", "true"}} : flat_map<string,string>{};
			let settings = Json::TryReadJsonNet( settingsPath, _importPaths ? *_importPaths : vector<fs::path>{}, args, tlas );
			if( !settings ){
				let joinKV = []( let& pairs ){ //Str::Join streams items, and pair<> has no operator<< - map to k=v first.
					vector<string> y;
					for_each( pairs, [&](let& pair){ y.push_back( pair.first+"="+pair.second ); } );
					return Str::Join( y, ";" );
				};
				vector<string> importPaths;
				if( _importPaths )
					for( let& path : *_importPaths )
						importPaths.push_back( path.string() ); //.string(): operator<<(fs::path) would quote it.

				auto error = Ƒ("Could not load settings from '{}': {}\n-include: {}\nargs: {}\ntlas: {}", settingsPath.string(), settings.error(), Str::Join(importPaths,";"), joinKV(args), joinKV(tlas) );
				//The -include/-tests pairing: an args dir reads ext vars that only a test flag binds, and jsonnet names
				//the undefined variable without saying who was supposed to bind it.  -include is the only way to reach
				//such a dir, so the two flags travel together and the omission is worth spelling out.
				if( !Process::FindArg("-tests") && !Process::FindArg("-ctest") && settings.error().contains("external variable") )
					error += "\nThe buildTarget/cwd/logsDir/windows ext vars are bound only under -tests/-ctest, so an args dir that reads one needs a test flag alongside -include (or an explicit -arg=<name>=<value>).";
				throw std::runtime_error{ error };
			}
			_settings = mu<jvalue>( *settings );
			if( _settings->is_object() )
				SetEnv( _settings->get_object() );
			INFOT( ELogTags::App, "Settings path={}", settingsPath.string() );
		}
		catch( const runtime_error& e ){
			_settings = mu<jvalue>( jobject{{"error", e.what()}} );
			std::cerr << e.what() << std::endl;
			CRITICAL( "({})Could not load settings - {}", settingsPath.string(), e.what() );
			BREAK;
		}
	}
	α Settings::Set( sv path, jvalue v, SL sl )ε->jvalue*{
		ASSERT( _settings );
		boost::system::error_code ec;
		boost::json::set_pointer_options opts;
		opts.create_objects = true;//create missing intermediate objects - replaces a hand-rolled fallback that corrupted the root.
		auto y = _settings->set_at_pointer( path, move(v), ec, opts );
		THROW_IFSL( !y, "Could not set '{}': {}", path, ec.message() );
		return y;
	}
}