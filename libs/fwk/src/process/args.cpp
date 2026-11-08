#include <jde/fwk/process/process.h>

#define let const auto

namespace Jde{
	α Process::ParseArgs( const vector<string>& tokens )ι->flat_multimap<string,string>{
		flat_multimap<string,string> y;
		optional<string> key; //flag awaiting a value: `-k v` binds on the next token, `-k` alone binds empty.
		for( let& current : tokens ){
			if( !current.starts_with('-') ){ //the pending flag's value, or a positional (empty key).
				y.emplace( key ? move(*key) : string{}, current );
				key.reset();
				continue;
			}
			if( key ) //previous flag never got a value.
				y.emplace( move(*key), string{} );
			key.reset(); //every path that consumes key resets it - reading a moved-from optional emplaced junk.
			if( uint i=current.find('='); i<current.size() ){
				auto value = current.substr( i+1 );
				if( value.size()>1 && value.front()=='"' && value.back()=='"' ) //lldb/VS Code pass argv unshelled, so the quotes are literal.
					value = value.substr( 1, value.size()-2 );
				y.emplace( current.substr(0, i), move(value) );
			}
			else
				key = current;
		}
		if( key )
			y.emplace( move(*key), string{} );
		return y;
	}

	α Process::ParseStartupArgs( const vector<string>& tokens, optional<bool> console )ι->StartupArgs{
		StartupArgs y{ console.value_or(false), !_debug };
		for( let& token : tokens ){
			if( token=="-c" )//this run's, never the service's.
				y.Console = console.value_or( true );
			else if( token=="-t" )
				y.SetTerminate = !y.SetTerminate;
			else if( token=="-install" )
				y.Install = true;
			else if( token=="-uninstall" )
				y.Uninstall = true;
			else
				y.ServiceArgs.push_back( token );
		}
		return y;
	}

	α Process::FindArg( string key )ι->optional<string>{
		auto p = Args().find( key );
		return p!=Args().end() ? p->second : optional<string>{};
	}

	//CommandLineToArgvW's rules, since that is what reads a service's ImagePath back (Args()):  a backslash is literal
	//unless it precedes a '"', where the run doubles and the quote is escaped; a run before the closing quote doubles too.
	//Only tokens that need it are quoted - a plain -settings=C:\ProgramData\… stays readable in `sc qc`.
	Ω quoteArg( sv arg, bool force )ι->string{
		if( !force && !arg.empty() && arg.find_first_of(" \t\"")==sv::npos )
			return string{ arg };
		string y{ '"' };
		uint backslashes{};
		for( char c : arg ){
			if( c=='\\' ){
				++backslashes;
				continue;
			}
			y.append( c=='"' ? backslashes*2+1 : backslashes, '\\' );
			backslashes = 0;
			y += c;
		}
		y.append( backslashes*2, '\\' );
		return y += '"';
	}
	α Process::ServiceCommandLine( const fs::path& exe, const vector<string>& args )ι->string{
		//The exe is quoted unconditionally:  unquoted, `C:\Program Files\…\x.exe` parsed back as argv[0]=`C:\Program`, which
		//Executable() handed to loadResource - ProductName() then fell back to "Jde-cpp" and the service grew a second data tree.
		string y = quoteArg( exe.string(), true );
		for( const auto& arg : args )
			y += ' ' + quoteArg( arg, false );
		return y;
	}
}