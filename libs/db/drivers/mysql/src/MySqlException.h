#pragma once
#include "jde/fwk/log/logTags.h"
#include "jde/fwk/usings.h"
#include "usings.h"
#include <jde/db/DBException.h>

namespace Jde::DB::MySql{
	//error_code::value() is only an errno when the category says so - client_errc reuses small integers in its own
	//category, so an unqualified value() would collide with the server numbers below.
	//client_errc mixes two unrelated populations: the connection could not be established or has gone unusable, and the
	//caller misused the library.  Only the first is retryable, so they cannot share a classification - lumping them all
	//under Connection made a bound-parameter-count mismatch surface as a transient backend outage (503).
	Ξ ToDbError( mysql::client_errc ec )ι->EDbError{
		using errc = mysql::client_errc;
		switch( ec ){
		//transport, handshake and protocol: there is no usable session, and a retry may well find one.
		case errc::incomplete_message: case errc::protocol_value_error: case errc::server_unsupported:
		case errc::extra_bytes: case errc::sequence_number_mismatch: case errc::unknown_auth_plugin:
		case errc::auth_plugin_requires_ssl: case errc::server_doesnt_support_ssl: case errc::bad_handshake_packet_type:
		case errc::not_connected: case errc::pool_cancelled: case errc::no_connection_available:
		case errc::unknown_openssl_error:
			return EDbError::Connection;
		//everything else is ours to fix, not the server's to recover from: wrong_num_params, the static-interface
		//mismatches, the format_* family, and the misuse codes (pool_not_running, operation_in_progress, …).  None is
		//already the 500 "server fault" bucket, which is what a programming error deserves.
		default:
			return EDbError::None;
		}
	}

	Ξ ToDbError( const boost::system::error_code& ec )ι->EDbError{
		const auto& category = ec.category();
		if( category==mysql::get_client_category() )
			return ToDbError( (mysql::client_errc)ec.value() );
		const auto isCommon = category==mysql::get_common_server_category();
		const auto isMySql = category==mysql::get_mysql_server_category();
		//asio/system/ssl/netdb: ECONNREFUSED, ECONNRESET, EPIPE, asio::error::eof and friends.  These *are* the real
		//connection failures - reporting them as None sent a downed server to the 500 "server fault" bucket.
		if( !isCommon && !isMySql && category!=mysql::get_mariadb_server_category() )
			return EDbError::Connection;
		//mysql-only numbers, gated on the category: MariaDB reuses some of this range for other things.
		if( isCommon || isMySql ){
			switch( ec.value() ){
			case mysql::mysql_server_errc::er_check_constraint_violated: return EDbError::Check;
			case mysql::mysql_server_errc::er_client_interaction_timeout: return EDbError::Connection;
			}
		}
		using errc = mysql::common_server_errc;
		switch( (errc)ec.value() ){
		case errc::er_dup_entry: case errc::er_dup_entry_with_key_name: case errc::er_dup_key: case errc::er_dup_unique: return EDbError::Duplicate;
		case errc::er_no_referenced_row: case errc::er_no_referenced_row_2: case errc::er_row_is_referenced: case errc::er_row_is_referenced_2: return EDbError::ForeignKey;
		case errc::er_bad_null_error: return EDbError::NotNull;
		case errc::er_lock_deadlock: return EDbError::Deadlock;
		case errc::er_lock_wait_timeout: return EDbError::Timeout;
		case errc::er_access_denied_error: case errc::er_dbaccess_denied_error: case errc::er_tableaccess_denied_error: return EDbError::Permission;
		//the statement names something that is not there - the sqlite and odbc drivers both call that Syntax.
		case errc::er_parse_error: case errc::er_no_such_table: case errc::er_bad_field_error: case errc::er_sp_does_not_exist: return EDbError::Syntax;
		case errc::er_server_shutdown: return EDbError::Connection;
		case errc::er_signal_exception: case errc::er_signal_warn: case errc::er_signal_not_found: return EDbError::App; //`signal sqlstate '45000'` in a proc - always 1644, whatever the sqlstate.
		default: return EDbError::None;
		}
	}

	struct MySqlException final : DBException{
		MySqlException( DB::Sql&& sql, const mysql::error_with_diagnostics& e, SRCE, ExceptionArgs args={ELogLevel::Error, ELogTags::Sql} ):
			MySqlException{ move(sql), e, Ƒ("{} - {}", e.what(), e.get_diagnostics().server_message()), args, sl }
		{}
		//no statement to attach - connect/handshake failures, where `context` is a connection description, not sql.
		MySqlException( sv context, const mysql::error_with_diagnostics& e, ExceptionArgs args, SRCE ):
			MySqlException{ DB::Sql{}, e, Ƒ("{} - {} - {}", e.what(), e.get_diagnostics().server_message(), context), args, sl }
		{}
		α UserError()Ι->string override{ return _serverMessage.size() ? _serverMessage : DBException::UserError(); }

		α Move()ι->up<Exception> override{ return mu<MySqlException>(move(*this)); }
		[[noreturn]] α Throw()->void override;//out of line, as Exception::Throw is.
	private:
		MySqlException( DB::Sql&& sql, const mysql::error_with_diagnostics& e, string what, ExceptionArgs args, SL sl ):
			DBException{
				ToDbError(e.code()),
				move(sql),
				move(what),
				{args.Level()==ELogLevel::NoLog ? ELogLevel::Error : args.Level(), args.Tags==ELogTags::None ? ELogTags::Sql : args.Tags, (uint32)e.code().value()},
				sl
			},
			_serverMessage{ e.get_diagnostics().server_message() }
		{}
		string _serverMessage;
	};
}