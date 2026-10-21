#pragma once
#include "logTags.h"
#include "../settings.h"
#include "../process/process.h"

#define Φ Γ auto
namespace Jde::Logging{

	Φ DestroyLoggers( bool terminate )->void;
	Φ Loggers()->const vector<ILogger*>&;//the current list, safe to walk unlocked:  a change publishes a new one (log.cpp).
	Ŧ GetLogger()ε->T&;
	Ŧ FindLogger()ι->T*;
	Φ AddLogger( up<ILogger>&& logger )ι->ILogger*;
	Φ LogAddFailure( sv configName, const runtime_error& e )ι->void;
	Φ Init()ι->void;

	struct Entry;
	struct Γ ILogger : LogTags, IShutdown{
		ILogger( const jobject& o ): LogTags( o ){}
		virtual ~ILogger(){} //important
		β Write( const Entry& m )ι->void=0;
		β Write( const Entry& m, uint32 appPK, uint32 instancePK )ι->void=0;
		//The spdlog sink can format straight from the caller's args, skipping the Entry (which eagerly
		//formats a string and copies every arg into a vector<string>).  Taking the args type-erased as
		//fmt::format_args keeps that fast path out of line, so log.h needs only ILogger - not the
		//complete SpdLog, and through it <spdlog/logger.h>.  Returning false means "not handled, build
		//an Entry", which is what every logger but SpdLog does.
		β WriteFormatted( ELogLevel /*level*/, SL /*sl*/, fmt::string_view /*fmt*/, fmt::format_args /*args*/ )ι->bool{ return false; }
	};

	template<class T, class... Args>
	α Add( sv configName, Args&& ...args )ι->T*{
		T* y{};
		if( const auto settings = Settings::FindObject( Ƒ("/logging/{}", configName) ); settings ){
			try{
				y = (T*)AddLogger( mu<T>(*settings, FWD(args)...) );
			}
			catch( runtime_error& e ){
				//L6: was an empty catch, so a logger configured but unable to construct - a ProtoLog whose `path` cannot be created,
				//most plainly - was silently not added: no binary log, no logs() answers, and nothing saying why.  The exception
				//self-logs on destruction, but at Debug and without naming which logger is missing.
				LogAddFailure( configName, e );
			}
		}
		return y;
	}
}
namespace Jde{
	Ŧ Logging::FindLogger()ι->T*{
		for( auto logger : Loggers() ){
			if( auto log = dynamic_cast<T*>( logger ) )
				return log;
		}
		return nullptr;
	}
	Ŧ Logging::GetLogger()ε->T&{
		auto p = FindLogger<T>();
		if( !p )
			throw std::runtime_error( Ƒ("Logger of type {} not found.", typeid(T).name()) );
		return *p;
	}
}
#undef Φ