#include "MySqlQueryAwait.h"
#include <boost/asio/co_spawn.hpp>
#include <jde/db/generators/Functions.h>
#include "field.h"
#include "MySqlRow.h"
#include "MySqlException.h"
#include "../../../src/DBLog.h"

#define let const auto

namespace Jde::DB::MySql{
	MySqlQueryAwait::MySqlQueryAwait( sp<MySqlDataSource> ds, Sql&& s, bool outParams, SL sl )ι:
		TAwait<Result>{ sl },
		_ctx{ Executor() },
		_ds{ ds },
		_outParams{ outParams },
		_sql{ move(s) }
	{}

	α MySqlQueryAwait::Main()ι->asio::awaitable<void>{
		CallText( _sql );
		DB::Log( _sql, _sl );
		let params = ToFields( _sql, _outParams, _sl );

		mysql::any_connection conn( co_await asio::this_coro::executor );
		co_await conn.async_connect( _ds->ConnectionParams() );
		mysql::results tz;
		co_await conn.async_execute( UtcSession, tz );
		auto stmt = co_await conn.async_prepare_statement( _sql.Text );
		mysql::results mySqlResult;
		co_await conn.async_execute( stmt.bind(params.begin(), params.end()), mySqlResult );
		auto result = ToResult( mySqlResult, _outParams );
		co_await conn.async_close_statement( stmt );
		co_await conn.async_close();
		if( mySqlResult.has_value() ){
			TRACET( ELogTags::Sql, "MySqlQueryAwait::Main: RowsAffected: {} rows: {}.", result.RowsAffected, result.Rows.size() );
		}else
			TRACET( ELogTags::Sql, "MySqlQueryAwait::Main: No rows affected." );
		Resume( move(result) );
	}

	α MySqlQueryAwait::Suspend()ι->void{
		asio::co_spawn(
			*_ctx,
			[this]{ return Main(); },
			[this]( std::exception_ptr e ){
				if( !e )
					return;
				try{
					std::rethrow_exception( e );
				}
				catch( mysql::error_with_diagnostics& e ){
					ResumeExp( MySqlException{move(_sql), move(e), _sl} );
				}
				catch( runtime_error& e ){
					ResumeExp( move(e) );
				}
				catch( ... ){
					ResumeExp( move(*Exception::FromPtr(e, _sl)) );
				}
			}
		);
	}
}