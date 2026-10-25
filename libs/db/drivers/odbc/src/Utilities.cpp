#include "Utilities.h"
#include <sqlext.h>
#include <jde/db/DBException.h>
#include <jde/db/generators/Sql.h>

#define var const auto
namespace Jde::DB::Odbc{
	constexpr ELogTags _tags{ ELogTags::App };

	//SQLSTATE gives the class, the native number the detail - sql server reports both (2627/2601 unique, 547 fk, 515 not-null…).
	Ω toDbError( sv state, int32 native )ι->EDbError{
		switch( native ){//sql server's own numbers - more precise than the SQLSTATE class below.
		case 2627: case 2601: return EDbError::Duplicate;//unique constraint / duplicate key row in a unique index.
		case 547: return EDbError::ForeignKey;//also raised for check constraints, which report the same 23000 class.
		case 515: return EDbError::NotNull;
		case 1205: return EDbError::Deadlock;
		case 229: case 230: case 262: return EDbError::Permission;
		case 102: case 207: case 208: return EDbError::Syntax;//syntax error near / invalid column / invalid object.
		}
		if( native>=50000 )//`throw 50000,…` and raiserror in a proc - the app raised it, the engine did not.
			return EDbError::App;
		if( state.starts_with("23") ) return EDbError::Constraint;
		if( state.starts_with("08") ) return EDbError::Connection;
		if( state.starts_with("HYT") ) return EDbError::Timeout;
		if( state=="40001" ) return EDbError::Deadlock;
		if( state.starts_with("42") ) return EDbError::Syntax;
		if( state.starts_with("28") ) return EDbError::Permission;
		return EDbError::None;
	}

	[[noreturn]] α ThrowDiagnostic( sv functionName, SQLHANDLE hHandle, RETCODE retCode, Sql&& sql, SL sl )ε->void{
		DiagnosticRecord rec;
		var msg = HandleDiagnosticRecord( functionName, hHandle, SQL_HANDLE_STMT, retCode, sl, &rec );
		throw DBException{ rec.Error, move(sql), msg, {ELogLevel::Error, ELogTags::Sql, (uint32)rec.Native}, sl };
	}

	α HandleDiagnosticRecord( sv functionName, SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE retCode, SL sl, DiagnosticRecord* pFirst )ε->string{
		THROW_IFX( retCode==SQL_INVALID_HANDLE, DBException(retCode, Sql{}, Ƒ("({}) {} - Invalid handle", functionName, hType), sl) );
		SQLSMALLINT iRec = 0;
		SQLINTEGER iError;
		SQLCHAR szMessage[SQL_MAX_MESSAGE_LENGTH];
		SQLCHAR szState[SQL_SQLSTATE_SIZE + 1];
		SQLSMALLINT msgLen;
		string y;
		RETCODE diag;
		//SQL_SUCCESS_WITH_INFO means the text was truncated to szMessage; the state and native number are still valid, and exiting on it dropped the whole record - message and classification both.
		while( (diag=SQLGetDiagRec(hType, hHandle, ++iRec, szState, &iError, szMessage, (SQLSMALLINT)(sizeof(szMessage) / sizeof(char)),  &msgLen))==SQL_SUCCESS || diag==SQL_SUCCESS_WITH_INFO ){
			const string state{ (const char*)szState, SQL_SQLSTATE_SIZE };
			var level{ (retCode!=1 && state=="01004") ? ELogLevel::Error : ELogLevel::Debug };
			var msg{ Jde::format("[{:<5}] {} {}", state, (char*)szMessage, iError) };
			if( y.size() )
				y += '\n';
			y += msg;
			var error = toDbError( state, (int32)iError );
			//record 1 is normally the error and later ones context ("The statement has been terminated."), but a proc that PRINTs before raising puts an informational 01000 there - let a classified record replace an unclassified one.
			if( pFirst && (iRec==1 || (pFirst->Error==EDbError::None && error!=EDbError::None)) )
				*pFirst = DiagnosticRecord{ (int32)iError, error };
			if( state=="01000" && iError!=3621 )//3621=The statement has been terminated.
				Logging::LogOnce( SRCE_CUR, ELogLevel::Information, _tags, "{}", msg );
			else{
				if( functionName=="SQLDriverConnect" && level==ELogLevel::Error )
					throw DBException{ error, Sql{}, Ƒ("[{:<5}] {}", state, (char*)szMessage), {ELogLevel::Critical, ELogTags::Sql, (uint32)iError}, sl };
				else if( retCode==1 && state=="23000" )//23000=Integrity constraint violation.  multiple statements why retCode==1.
					throw DBException{ error, Sql{}, msg, {ELogLevel::Error, ELogTags::Sql, (uint32)iError}, sl };
				else if( iError )
					TRACESL( "({}){} - {}", iError, functionName, msg );
			}
		}
		if( pFirst && !pFirst->Native ) //nothing carried a native number (empty queue, or an informational record): keep the RETCODE, which was the exception's code before the record supplied one.
			pFirst->Native = (int32)retCode;
		return y;
	}
}