#include "jde/fwk/process/process.h"
#include <jde/fwk/log/SpdLog.h>
#include <iostream>
#include <numeric>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/pattern_formatter.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/log/log.h>

#define let const auto
#ifndef JDE_SOURCE_ROOT
	#define JDE_SOURCE_ROOT ""
#endif

namespace Jde::Logging{
	using spdlog::level::level_enum;

	//`-fmacro-prefix-map` makes source paths repo-relative;  the terminal's osc-8 link needs an absolute file:// uri.
	α FormatSourceUri( sv file )ι->string{
		string y;
		if( file.empty() )
			return y;
		let absolute = file[0]=='/' || file[0]=='\\' || ( file.size()>1 && file[1]==':' );//sources outside CMAKE_SOURCE_DIR (pch stubs, generated, 3rd party) are not remapped.
		let root = absolute ? sv{} : sv{ JDE_SOURCE_ROOT };
		let& first = root.empty() ? file : root;
		if( first[0]!='/' )
			y.push_back( '/' );//file:// + c:/x -> file:///c:/x
		for( let& part : {root, file} )
			for( let ch : part )
				y.push_back( ch=='\\' ? '/' : ch );
		return y;
	}
	struct SourceUri final : spdlog::custom_flag_formatter{
		α format( const spdlog::details::log_msg& msg, const std::tm&, spdlog::memory_buf_t& dest )ι->void override{
			let uri = FormatSourceUri( msg.source.filename ? sv{msg.source.filename} : sv{} );
			dest.append( uri.data(), uri.data()+uri.size() );
		}
		α clone()Ι->up<spdlog::custom_flag_formatter> override{ return mu<SourceUri>(); }
	};

	Ω loadSinks( const jobject& settings )ι->vector<spdlog::sink_ptr>{
		vector<spdlog::sink_ptr> sinks;
		let& sinkSettings = Json::FindDefaultObject( settings, "sinks" );
		for( let& [name,sink] : sinkSettings ){
			spdlog::sink_ptr pSink;
			string additional;
			auto pattern =  Json::FindSV( sink, "/pattern" );
			if( name=="console" && Process::IsConsole() ){
				if( !pattern ){
					if( Process::Args().contains("-ctest") || !Process::IsTerminal() )
						pattern = "%^%3!l%$-%H:%M:%S.%e %v %g:%#";//plain, the source at the end: ctest's log, or stdout on a pipe/the journal (a service's -c run) - the osc-8 link would be literal there.
					else
						pattern = "\033]8;;file://%U#%#\a%^%3!l%$\033]8;;\a-%H:%M:%S.%e %v";//osc-8 link on the level;  the message stays plain so vscode finds the paths inside it.
				}
				pSink = ms<spdlog::sinks::stdout_color_sink_mt>();
			}
			else if( name=="file" ){
				std::cout << "file sink:" << serialize( sink ) << std::endl;
				optional<fs::path> pPath;
				if( auto p = Json::FindString(sink, "/path"); p )
					pPath = fs::path{ *p };
#pragma warning( disable: 4127 )
				if( !_msvc && pPath && pPath->string().starts_with("/Jde-cpp") )
					pPath = fs::path{ fs::path{Process::GetEnv("HOME").value_or("")}/pPath->string().substr(1) };
				let markdown = Json::FindBool( sink, "/md" ).value_or( false );
				let fileNameWithExt = Settings::FileStem()+( markdown ? ".md" : ".log" );
				let path = pPath && !pPath->empty() ? *pPath/fileNameWithExt : Process::AppDataFolder()/"logs"/fileNameWithExt;
				let truncate = Json::FindBool( sink, "/truncate" ).value_or( true );
				//keep: previous runs kept beside the file as <stem>.1.log … <stem>.<keep>.log - restarting a product after a failure
				//used to erase the lines that said why (reviews/install-issues.md #13).  With truncate, a start rolls the file
				//aside rather than emptying it; it also rolls at maxSize (bytes, 10 MB).  Absent or 0: one file, as before.
				let keep = Json::FindNumber<uint32>( sink, "/keep" ).value_or( 0 );
				let maxSize = Json::FindNumber<size_t>( sink, "/maxSize" ).value_or( 10*1024*1024 );
				additional = Ƒ( " truncate='{}' keep='{}' path='{}'", truncate, keep, path.string() );
				try{
					if( keep )
						pSink = ms<spdlog::sinks::rotating_file_sink_mt>( path.string(), maxSize, keep, truncate );
					else
						pSink = ms<spdlog::sinks::basic_file_sink_mt>( path.string(), truncate );
				}
				catch( const spdlog::spdlog_ex& e ){
					ERRT( ELogTags::Settings, "Could not create log:  ({}) path='{}' - {}", string{name}, path.string(), string{e.what()} );
					std::cerr << Ƒ( "Could not create log:  ({}) path='{}' - {}", name, path.string(), e.what() ) << std::endl;
					continue;
				}
				if( !pattern )
					pattern = markdown ? "%^%3!l%$-%H:%M:%S.%e [%v](%g#L%#)\\" : "%^%3!l%$-%H:%M:%S.%e %v %-64@";
			}
			else
				continue;
			auto formatter = mu<spdlog::pattern_formatter>();
			formatter->add_flag<SourceUri>( 'U' ).set_pattern( string{*pattern} );//every sink - an unregistered %U would print as literal text.
			pSink->set_formatter( move(formatter) );
			let level = Json::FindEnum<ELogLevel>( sink, "/level", ToLogLevel ).value_or( ELogLevel::Trace );
			pSink->set_level( (level_enum)level );
			//std::cout << Ƒ( "({})level='{}' pattern='{}'{}", name, ToString(level), pattern, additional ) << std::endl;
			DBGT( ELogTags::Settings, "({})level='{}' pattern='{}'{}", name, ToString(level), *pattern, additional );
			sinks.push_back( pSink );
		}
		return sinks;
	}
	Ω logger( const jobject& settings )ι->spdlog::logger{
		auto sinks = loadSinks( settings );
		spdlog::logger logger{ "my_logger", sinks.begin(), sinks.end() };

		//`settings` is this logger's own object (/logging/spd - loadSinks reads its "sinks"), and the jobject overload of FindEnum
		//takes a key, not a path: the root path it used to spell never matched, every config's flushOn was ignored, and a
		//release build flushed on Information alone - a service's text log then showed nothing below that until a flush or
		//exit (reviews/install-issues.md #7).  FlushLevel() is what the test pins.
		let flushOn = Json::FindEnum<ELogLevel>( settings, "flushOn", ToLogLevel ).value_or( _debug ? ELogLevel::Debug : ELogLevel::Information );
		logger.flush_on( (level_enum)flushOn );

		let minSinkLevel = std::accumulate( sinks.begin(), sinks.end(), ELogLevel::Critical, [](ELogLevel min, auto& p){return std::min((ELogLevel)p->level(), min);} );
		logger.set_level( (level_enum)minSinkLevel );

		return logger;
	}

	SpdLog::SpdLog( const jobject& settings )ι:
		ILogger{ settings },
		_logger{ logger(settings) }{
		INFOT( ELogTags::Settings, "{} minLevel='{}' default='{}' flushOn='{}' {}", Name(),
			Jde::ToString( (ELogLevel)_logger.level() ),
			Jde::ToString( DefaultLevel() ),
			Jde::ToString( (ELogLevel)_logger.flush_level() ), ToString()
		);
	}
}