#pragma once
#include <sqlite3.h>
#include "jde/fwk/usings.h"
#include <jde/db/DBException.h>

namespace Jde::DB::Sqlite{
	//sqlite packs the class in the low byte and the detail in the high bits - the detail only arrives when
	//sqlite3_extended_result_codes is on (SqliteDataSource::Connection), otherwise every constraint is a bare
	//SQLITE_CONSTRAINT and the best we can say is EDbError::Constraint.
	constexpr α ToDbError( int rc )ι->EDbError{
		switch( rc ){
		case SQLITE_CONSTRAINT_UNIQUE: case SQLITE_CONSTRAINT_PRIMARYKEY: case SQLITE_CONSTRAINT_ROWID: return EDbError::Duplicate;
		case SQLITE_CONSTRAINT_FOREIGNKEY: return EDbError::ForeignKey;
		case SQLITE_CONSTRAINT_NOTNULL: return EDbError::NotNull;
		case SQLITE_CONSTRAINT_CHECK: return EDbError::Check;
		case SQLITE_CONSTRAINT_TRIGGER: return EDbError::App;//raise(abort, …) in a trigger:  the app's own rule, as a proc's `signal` is on MySQL - a 400 that carries the message (#520).
		}
		switch( rc & 0xff ){
		case SQLITE_OK: case SQLITE_ROW: case SQLITE_DONE: return EDbError::None;
		case SQLITE_CONSTRAINT: return EDbError::Constraint;
		case SQLITE_BUSY: return EDbError::Timeout; //"database is locked" - another connection holds it.
		case SQLITE_LOCKED: return EDbError::Deadlock; //same connection or shared cache - retrying will not clear it.
		case SQLITE_PERM: case SQLITE_AUTH: case SQLITE_READONLY: return EDbError::Permission;
		case SQLITE_CANTOPEN: case SQLITE_NOTADB: case SQLITE_IOERR: case SQLITE_CORRUPT: return EDbError::Connection;
		case SQLITE_ERROR: return EDbError::Syntax; //sqlite's catch-all for a bad statement: parse error, no such table/column.
		}
		return EDbError::None;
	}

	struct SqliteException final : DBException{
		template<class... Args>
		SqliteException( SL sl, int rc, DB::Sql&& sql, fmt::format_string<Args...> m, Args&&... sargs )ι:
			DBException{ ToDbError(rc), move(sql), Ƒ(FWD(m), FWD(sargs)...), {ELogLevel::Error, ELogTags::Sql, (uint32)rc}, sl }
		{}

		α Move()ι->up<Exception> override{ return mu<SqliteException>(move(*this)); }
		[[noreturn]] α Throw()ε->void override{ throw move(*this); }
	};
}