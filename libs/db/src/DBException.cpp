#include <jde/db/DBException.h>
#include <jde/db/Value.h>
#include <jde/db/generators/Sql.h>
#include "DBLog.h"
#include "jde/fwk/log/logTags.h"
#include "jde/fwk/usings.h"


#define let const auto

namespace Jde::DB{
	constexpr array<sv,12> EDbErrorStrings = { "none", "duplicate", "foreignKey", "notNull", "check", "constraint", "deadlock", "timeout", "connection", "permission", "syntax", "app" }; //EDbError order.
	α ToString( EDbError x )ι->string{ return FromEnum( EDbErrorStrings, x ); }

	DBException::DBException( int32 errorCode, DB::Sql&& sql, string what, SL sl )ι:
		DBException{ EDbError::None, move(sql), move(what), {ELogLevel::Error, ELogTags::Sql, (uint32)errorCode}, sl }
	{}

	DBException::DBException( EDbError error, DB::Sql&& sql, string what, ExceptionArgs args, SL sl )ι:
		ExternalException{
			ExternalException::ECodeBase::Decimal,
			string{what},
			sql.Text,
			ExceptionArgs{ELogLevel::NoLog, args.Tags==ELogTags::None ? ELogTags::Sql : args.Tags, args._code},
			sl
		}, //NoLog here so BreakLog below dispatches to DBException::Log, which keeps the Sql context.
		Sql{ move(sql) },
		Error{ error },
		_message{ move(what) }{
		SetLevel( args.Level() ); //drivers raise connect failures as Critical; everything else defaults to Error.
		BreakLog();
	}

	α DBException::HttpStatus()Ι->EHttpStatus{
		if( _statusCode )//an explicit SetHttpStatus wins, same contract as the base.
			return _statusCode;
		switch( Error ){
			case EDbError::Duplicate: case EDbError::ForeignKey: case EDbError::Constraint: return EHttpStatus::Conflict;
			case EDbError::NotNull: case EDbError::Check: return EHttpStatus::BadRequest;//the client sent an invalid/incomplete row.
			case EDbError::App: return EHttpStatus::BadRequest;//proc-raised business rule rejecting the request - the only Error whose message ClientDetail surfaces.
			case EDbError::Permission: return EHttpStatus::Forbidden;
			case EDbError::Deadlock: case EDbError::Connection: return EHttpStatus::ServiceUnavailable;//transient/unreachable backend - retryable.
			case EDbError::Timeout: return EHttpStatus::GatewayTimeout;
			default: return EHttpStatus::InternalServerError;//None (unclassified driver error) and Syntax (our statement is wrong) are server faults.
		}
	}

	α DBException::Log()Ι->void{
		if( _logged || Level()==ELogLevel::NoLog || Process::LoggersGone() ) //participate in the base _logged protocol: don't re-log a moved-from/BreakLog'd exception, and stay quiet during teardown.
			return;
		_logged = true;
		DB::Log( Sql, Level(), _inner ? string{_inner->what()} : what(), _sl );
	}
}