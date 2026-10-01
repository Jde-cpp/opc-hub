//#include "jde/fwk/log/logTags.h"
//#include "jde/fwk/usings.h"
#include <jde/fwk/exceptions/Exception.h>
//#include <iostream>
#include <boost/system/error_code.hpp>

//#include <system_error>
#include <jde/fwk/str.h>
#include <jde/fwk/exceptions/CodeException.h>
#include <jde/fwk/log/Entry.h>
#define let const auto

namespace Jde{
	up<Exception> _empty;
	α Exception::EmptyPtr()ι->const up<Exception>&{ return _empty; }

	Exception::Exception( string value, ExceptionArgs args, SL sl )ι:
		runtime_error{ "" },
		ExceptionArgs{ args },
		_format{ move(value) },
		_sl{ sl }{
		BreakLog();
	}

	Exception::Exception( const Exception& from )ι:
		runtime_error{ from },
		ExceptionArgs{ from },
		_what{ from._what },
		_logged{ from._logged },
		_inner{ from._inner ? up<runtime_error>{mu<runtime_error>( from._inner->what() )} : nullptr },//a base std::exception copy slices - what() is all consumers read; preserve it.
		_format{ from._format },
		_args{ from._args },
		_sl{ from._sl }{
		BREAK;//should only be called by rethrow_exception
	};
	Exception::Exception( Exception&& from )ι:
		runtime_error{ from },
		ExceptionArgs{ move(from) },
		_what{ move(from._what) },
		_logged{ from._logged },
		_inner{ move(from._inner) },
		_format{ move(from._format) },
		_args{ move(from._args) },
		_sl{ from._sl }{
		from._logged = true;//the source made its log decision at construction - the moved-to object owns any future logging.
	}
	Exception::Exception( runtime_error&& from, ExceptionArgs args, SL sl )ι:
		runtime_error{ from },
		ExceptionArgs{ args },
		_sl{ sl }{
		if( let p = dynamic_cast<Exception*>(&from) )
			*this = move(*p);
		else
			_what = from.what();
	}
	α Exception::FromPtr( const std::exception_ptr& from, SL sl )ι->up<Exception>{
		try{
			std::rethrow_exception( from );
		}
		catch( Exception& e ){
			return e.Move();
		}
		catch( const std::exception& e ){
			return mu<Exception>( Ƒ("std::exception - {}", e.what()), ExceptionArgs{ELogLevel::Critical}, sl );
		}
		catch( ... ){
			return mu<Exception>( "unknown exception", ExceptionArgs{ELogLevel::Critical}, sl );
		}
	}
	α Exception::operator=( Exception&& from )ι->Exception&{
		ExceptionArgs::operator=( from );
		_logged = from._logged;
		from._logged = true;
		_what = move(from._what);
		_inner = move(from._inner);
		_format = move(from._format);
		_args = move(from._args);
		_sl = from._sl;
		return *this;
	}

	Exception::~Exception(){
		Log();
	}

	α Exception::BreakLog()Ι->void{
#ifndef NDEBUG
		if( Level()>=Logging::BreakLevel() || Logging::ShouldLog(ELogLevel::Trace, ELogTags::Exception) )
#else
		if( Logging::ShouldLog(ELogLevel::Trace, ELogTags::Exception) ) //no break level in release builds (log/break.h).
#endif
			Log();
	}
	α Exception::Log()Ι->void{
		if( _logged || Level()==ELogLevel::NoLog || Process::Finalizing() )
			return;
		_logged = true;
		if( auto sv = Format(); sv.size() )
			Logging::Log( Logging::Entry{_sl, Level(), Tags | ELogTags::Exception, string{sv}, _args} );
		else
			Logging::Log( Logging::Entry{_sl, Level(), Tags | ELogTags::Exception, string{what()}} );
	}

	α Exception::what()Ι->const char*{
		if( _what.empty() ){
			if( auto sv = Format(); sv.size() ){
				try{
					_what = _args.size() ? Str::Format( sv, _args ) : string{ sv };
				}
				catch( const std::exception& e ){
					_what = Ƒ( "bad format: {}, args: '{}', what: '{}'", sv, Str::Join(_args, ", ", true), e.what() );
				}
			}
			else if( _inner )
				_what = _inner->what();
		}
		return _what.c_str();
	}
}