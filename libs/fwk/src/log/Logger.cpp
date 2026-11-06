#include <jde/fwk/log/log.h>

namespace Jde{
	concurrent_flat_set<StringMd5> _loggedEntries;
	α Logging::MarkLogged( StringMd5 id )ι->bool{
		return _loggedEntries.insert(id);
	}

	//The one fan-out loop, behind Emit and Log( const Record& ).  Each logger checks its own level.
	Ω write( const Logging::Record& record )ι->void{
		for( auto& logger : Logging::Loggers() ){
			try{
				if( logger->ShouldLog(record.Level, record.Tags) )
					logger->Write( record );
			}
			catch( const fmt::format_error& e ){
				CRITICALT( ELogTags::App, "could not log '{}': '{}'", record.Format() ? record.Format()->Text : record.Entry().Text(), e.what() );
			}
		}
	}
	Ω breakFor( [[maybe_unused]] ELogLevel level, [[maybe_unused]] ELogTags tags )ι->void{
		BREAK_IF( BreaksFor(tags) && level>=Logging::BreakLevel() );
	}
	α Logging::Emit( ELogLevel level, ELogTags tags, SL sl, const FormatView& m, fmt::format_args args )ι->void{
		BREAK_IF( m.Text.empty() );
		write( Record{level, tags, sl, m, args} );
		breakFor( level, tags );
	}
	α Logging::Log( const Record& record )ι->void{
		if( Process::Finalizing() || !ShouldLog(record.Level, record.Tags) )
			return;
		write( record );
		if( !record.AppPK )//a forwarded entry is another instance's, not this process's own.
			breakFor( record.Level, record.Tags );
	}
}