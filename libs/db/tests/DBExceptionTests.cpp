#include <gtest/gtest.h>
#include <jde/db/DBException.h>
#include <jde/db/generators/Sql.h>
#include <jde/fwk/log/MemoryLog.h>
#include <jde/fwk/log/Entry.h>

#define let const auto

namespace Jde::DB::Tests{
	Ω memoryLog()ι->void{
		if( !Logging::FindLogger<Logging::MemoryLog>() )
			Logging::AddLogger( mu<Logging::MemoryLog>() ); //captures every level; self-contained, no shared-config change.
		Logging::ClearMemory();
	}

	//db-review2 #12: DBException::Log must honor the base _logged protocol - a moved-from / already-logged exception must not re-log
	//(before the fix, the moved-from source logged a junk "sqle: " with its emptied Sql, and a BreakLog'd exception double-logged).
	TEST( DBExceptionTests, LogsOnce ){
		memoryLog();
		let countSqle = []{ return Logging::Find( [](const Logging::Entry& e){ return e.Message().starts_with("sqle:"); } ).size(); };

		{
			DB::Sql sql; sql.Text = "select dbexception_logonce";
			DBException src{ 7, move(sql), "boom", SRCE_CUR };
			DBException dst{ move(src) }; //src is now moved-from: Sql emptied, _logged=true.
		} //dtors run: dst logs exactly once; src stays silent (was the junk "sqle: " / double-log before the fix).
		EXPECT_EQ( countSqle(), 1u );
	}

	//db-error-review #5: UserError() reaches unauthenticated clients as the 401 body (ServerImpl's App branch), so it must carry the driver
	//message without the statement text that what() keeps for the log.
	TEST( DBExceptionTests, UserErrorOmitsSql ){
		DB::Sql sql; sql.Text = "exec access_user_insert_key ?,?,?";
		DBException e{ EDbError::App, move(sql), "Slug 'x' already exists.", {ELogLevel::NoLog, {}, 50000} };

		let userError = e.UserError();
		EXPECT_FALSE( userError.contains("access_user_insert_key") ) << userError;
		EXPECT_TRUE( userError.contains("Slug 'x' already exists.") ) << userError;
		EXPECT_TRUE( string{e.what()}.contains("access_user_insert_key") ) << e.what(); //the split is the point: the log still gets the statement.
	}

	//db-error-review #13: engine codes are documented in decimal (sqlite 2067, mysql 1062, mssql 2627), so rendering them in hex made
	//"(813)" read as a decimal code that does not exist. UA status and OpenSSL errors are quoted in hex and keep it -
	//hence ECodeBase rather than a blanket change.
	TEST( DBExceptionTests, CodeRendersDecimal ){
		DBException e{ EDbError::Duplicate, DB::Sql{}, "UNIQUE constraint failed", {ELogLevel::NoLog, {}, 2067}, SRCE_CUR };
		EXPECT_TRUE( string{e.what()}.starts_with("(2067)") ) << e.what();
	}

	//db-error-review #10: what() is the exception text - the statement with its placeholders. Bound values belong to the log, which
	//DB::Log appends via EmbedParams; keeping them out of what() also keeps them out of whatever wraps or forwards it.
	TEST( DBExceptionTests, ParamValuesReachLogNotWhat ){
		memoryLog();
		{
			DB::Sql sql; sql.Text = "insert into t( secret ) values( ? )"; sql.Params.push_back( Value{string{"hunter2"}} );
			DBException e{ EDbError::None, move(sql), "boom", {ELogLevel::Error, {}, 7}, SRCE_CUR };
			let what = string{ e.what() };
			EXPECT_TRUE( what.contains("values( ? )") ) << what;
			EXPECT_FALSE( what.contains("hunter2") ) << what;
		}
		EXPECT_EQ( Logging::Find([](const Logging::Entry& e){ return e.Message().contains("hunter2");}).size(), 1u );
	}

	//the EDbError -> http status map lives on DBException::EHttpStatus; after a wire round trip the reconstructed
	//DBException recomputes it from Error, and ServerImpl answers with it.
	TEST( DBExceptionTests, HttpStatusMapsError ){
		let status = []( EDbError error ){ return DBException{ error, DB::Sql{}, "boom", {ELogLevel::NoLog} }.HttpStatus(); };
		EXPECT_EQ( 409u, status(EDbError::Duplicate) );
		EXPECT_EQ( 409u, status(EDbError::ForeignKey) );
		EXPECT_EQ( 409u, status(EDbError::Constraint) );
		EXPECT_EQ( 400u, status(EDbError::App) );
		EXPECT_EQ( 400u, status(EDbError::NotNull) );
		EXPECT_EQ( 400u, status(EDbError::Check) );
		EXPECT_EQ( 403u, status(EDbError::Permission) );
		EXPECT_EQ( 503u, status(EDbError::Connection) );
		EXPECT_EQ( 503u, status(EDbError::Deadlock) );
		EXPECT_EQ( 504u, status(EDbError::Timeout) );
		EXPECT_EQ( 500u, status(EDbError::None) );
		EXPECT_EQ( 500u, status(EDbError::Syntax) );

		DBException e{ EDbError::Duplicate, DB::Sql{}, "boom", {ELogLevel::NoLog} };
		EXPECT_EQ( Exception::ECategory::DB, e.Category() );
		EXPECT_EQ( (uint32)EDbError::Duplicate, e.CategoryCode() );
		e.SetHttpStatus( EHttpStatus::IAmATeapot );
		EXPECT_EQ( EHttpStatus::IAmATeapot, e.HttpStatus() ); //an explicit SetHttpStatus wins, same contract as the base.
	}

	//db-error-review #7: odbc's SQL_SUCCESS_WITH_INFO path re-wraps a classified inner exception to attach the statement it was thrown
	//without. The legacy 4-arg ctor reset Error to None and Code to the RETCODE; this mirrors the replacement expression,
	//const ref included - the odbc TU is WIN32-only, so this is what compiles it on Linux.
	TEST( DBExceptionTests, RewrapKeepsClassification ){
		DB::Sql statement; statement.Text = "exec proc_with_constraint ?";
		DBException inner{ EDbError::Duplicate, DB::Sql{}, "Violation of UNIQUE KEY constraint", {ELogLevel::NoLog, {}, 2627}, SRCE_CUR };
		const DBException& e = inner;

		DBException rewrapped{ e.Error, move(statement), e.Message(), {ELogLevel::NoLog, {}, e.Code()}, SRCE_CUR };
		EXPECT_EQ( EDbError::Duplicate, rewrapped.Error );
		EXPECT_EQ( 2627u, rewrapped.Code() );
		EXPECT_EQ( "Violation of UNIQUE KEY constraint", rewrapped.Message() ); //what() here would have nested a second "(a43)" prefix.
		EXPECT_TRUE( string{rewrapped.what()}.contains("exec proc_with_constraint") ) << rewrapped.what();
	}
}