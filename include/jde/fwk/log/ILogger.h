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

	struct Record;
	struct Γ ILogger : LogTags, IShutdown{
		ILogger( const jobject& o ): LogTags( o ){}
		virtual ~ILogger(){} //important
		//Every line, one way (Record):  SpdLog formats from the call's arguments when it has them, and every other logger reads
		//record.Entry(), built once for the line;  a forwarded entry's origin is on the record.  Copy the Entry to keep it.
		β Write( const Record& record )ι->void=0;
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