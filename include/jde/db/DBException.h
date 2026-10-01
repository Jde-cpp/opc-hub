#pragma once
#include "exports.h"
#include "jde/fwk/exceptions/Exception.h"
#include "jde/fwk/log/logTags.h"
#include <jde/db/generators/Sql.h>

namespace Jde::DB{
	//Backend-independent classification of a driver error. The native code stays in Exception::Code(), where it means
	//something different in every engine (sqlite result code, mysql errno, sql server error number) - callers that need
	//to branch on what went wrong read Error instead. Each driver maps its own codes at the throw site.
	enum class EDbError : uint8{
		None,       //unmapped - the native code is all there is.
		Duplicate,  //unique index or primary key violation.
		ForeignKey,
		NotNull,
		Check,
		Constraint, //integrity violation the driver could not narrow further.
		Deadlock,
		Timeout,
		Connection,
		Permission,
		Syntax,     //the statement is wrong: parse error, no such table/column.
		App         //raised by a proc: mysql `signal`, sql server `throw`, the sqlite twins' THROW_IFSL.
	};
	ΓDB α ToString( EDbError x )ι->string;

	//not final: the drivers derive (MySqlException, SqliteException) so one catch handles every backend.
	struct ΓDB DBException : ExternalException{
		DBException( int32 errorCode, DB::Sql&& sql, string what, SRCE )ι;
		DBException( DB::Sql&& sql, string what, SRCE )ι:DBException{ 0, move(sql), move(what), sl }{}
		DBException( EDbError error, DB::Sql&& sql, string what, ExceptionArgs args, SRCE )ι; //, ELogTags tags=ELogTags::Sql
		DBException( DBException&& from )ι:ExternalException{move(from)}, Sql{move(from.Sql)}, Error{from.Error}, _message{move(from._message)}{}
		DBException( const DBException& from )ι=delete;
		~DBException(){ Log(); SetLevel( ELogLevel::NoLog ); };

		α Log()Ι->void override;
		α Move()ι->up<Exception> override{ return mu<DBException>(move(*this)); }
		β UserError()Ι->string{ return Ƒ("({}) {}", ToString(Error), _message); }
		//only a proc-raised message is the app talking to the user; engine errors name our schema.
		α ClientDetail()Ι->string override{ return Error==EDbError::App ? UserError() : string{}; }
		α Message()Ι->str{ return _message; }
		α HttpStatus()Ι->EHttpStatus override;//EDbError -> http status; the one mapping site.
		α Category()Ι->ECategory override{ return ECategory::DB; }
		α CategoryCode()Ι->uint32 override{ return (uint32)Error; }
		[[noreturn]] α Throw()ε->void override{ throw move(*this); }

		DB::Sql Sql;
		EDbError Error{ EDbError::None };
	protected:
		string _message;
	};
}