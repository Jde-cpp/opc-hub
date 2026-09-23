#include "OdbcDataSource.h"
#include <jde/db/DBException.h>
#include "../../../src/DBLog.h"
#include "MsSql/MsSqlSchemaProc.h"
#include "OdbcQueryAwait.h"
#include "OdbcRow.h"
#include "Handle.h"
#include "Utilities.h"

#define let const auto
Jde::DB::IDataSource* GetDataSource(){
	return new Jde::DB::Odbc::OdbcDataSource();
}

namespace Jde::DB::Odbc{
	constexpr ELogTags _tags = ELogTags::Sql;
	α OdbcDataSource::Disconnect()ε->void{}//holds no connection:  each statement's HandleSession connects and disconnects its own, pooled by the driver manager.
	α AllocateBindings( const HandleStatement& statement,  SQLSMALLINT columnCount )ε->vector<up<Binding>>;
	α OdbcDataSource::AtCatalog( sv catalog, SL /*sl*/ )ε->sp<IDataSource>{
		string catalogName;
		try{
			catalogName = CatalogName();
		}
		catch( const runtime_error& )//assume can't connect on current schema.
		{}
		sp<OdbcDataSource> ds;
		if( catalog==catalogName )
			ds = dynamic_pointer_cast<OdbcDataSource>( shared_from_this() );
		else{
			ds = sp<OdbcDataSource>( (OdbcDataSource*)GetDataSource() );
			//Bake Database=<catalog> into a *distinct* connection string so this ds gets its own pool that opens directly on <catalog>.
			//The old shared-string + per-statement `use <catalog>` landed on an arbitrary pooled connection and didn't stick for later statements.
			//SQL Server uses the first occurrence of a duplicated keyword, so prepending wins over any Database= already in _connectionString.
			ds->SetConnectionString( Ƒ("Database={};{}", catalog, _connectionString) );
		}
		return ds;
	}
	α OdbcDataSource::Execute( Sql&& sql, SL sl, Params params )ε->uint{
		HandleStatement statement{ _connectionString };
		vector<SQLUSMALLINT> paramStatusArray;
		vector<up<Binding>> parameters; parameters.reserve( sql.Params.size() ); //keepalive
		for( let& param : sql.Params ){
			auto binding = Binding::Create( param );
			void* pData = binding->Data();
			let size = std::max<SQLLEN>(binding->Size(),0);
			let bufferLength = binding->BufferLength();
			let decimals = binding->DecimalDigits();
			let paramType = params.HasOut() && parameters.size()+1==sql.Params.size() ? SQL_PARAM_OUTPUT : SQL_PARAM_INPUT;
			let dbType = binding->DBType();
			let result = ::SQLBindParameter( statement, parameters.size()+1, paramType, binding->CodeType, dbType, size, decimals, pData, bufferLength, &binding->Output );
			THROW_IFX( result < 0, DBException(result, move(sql), HandleDiagnosticRecord("SQLBindParameter", statement, SQL_HANDLE_STMT, result, sl), sl) );
			parameters.push_back( move(binding) );
		}

		if( sql.IsProc )
			sql.Text = Ƒ( "{{ call {} }}", move(sql.Text) );
		uint resultCount{};
		if( params.Log )
			DB::Log( sql, sl );
		let retCode = ::SQLExecDirect( statement, (SQLCHAR*)sql.Text.data(), static_cast<SQLINTEGER>(sql.Text.size()) );
		switch( retCode ){
		case SQL_NO_DATA://update with no records effected...
			DBG( "noData={}", sql.Text );
		case SQL_SUCCESS_WITH_INFO:
			try{
				HandleDiagnosticRecord( "SQLExecDirect", statement, SQL_HANDLE_STMT, retCode );
			}
			catch( const DBException& e ){
				throw DBException{ e.Error, move(sql), e.Message(), {ELogLevel::Error, ELogTags::Sql, e.Code()}, sl };
			}
		case SQL_SUCCESS:{
			if( params.HasOut() && params.Function ) //if not getting value, make sure nocount on
				( *params.Function )( Row{{(*parameters.rbegin())->GetValue()}} );

			SQLSMALLINT columnCount{};
			if( params.Function )
				CALL( statement, SQL_HANDLE_STMT, SQLNumResultCols(statement,&columnCount), "SQLNumResultCols" );
			if( columnCount>0 ){
				let bindings = AllocateBindings( statement, columnCount );
				for( SQLRETURN fetch; (fetch=::SQLFetch(statement))!=SQL_NO_DATA_FOUND; ){
					if( fetch!=SQL_SUCCESS && fetch!=SQL_SUCCESS_WITH_INFO ) //SQL_ERROR/SQL_INVALID_HANDLE: was an infinite loop feeding garbage rows to the callback.
						ThrowDiagnostic( "SQLFetch", statement, fetch, move(sql), sl );
					(*params.Function)( ToRow(bindings) );
					++resultCount;
				}
			}
			else{
				SQLLEN count;
				CALL( statement, SQL_HANDLE_STMT, SQLRowCount(statement,&count), "SQLRowCount" );
				resultCount = std::max<SQLLEN>( count, 0 ); //-1 when unavailable (e.g. selects).
			}
			break;
		}
		case SQL_INVALID_HANDLE:
			throw DBException( retCode, move(sql), "SQL_INVALID_HANDLE" );
		case SQL_ERROR: //the RETCODE (-1) was the exception's code - the native number and SQLSTATE only reached the message.
			ThrowDiagnostic( "SQLExecDirect", statement, retCode, move(sql), sl );
		default:
			throw DBException( retCode, move(sql), "Unknown error" );
		}
		return resultCount;
	}

	void OdbcDataSource::SetConnectionString( string x )ι{
		_connectionString = Ƒ( "{};APP={}", move(x), Process::AppName() );
		DBG( "connectionString={}", _connectionString );
	}

	vector<up<Binding>> AllocateBindings( const HandleStatement& statement,  SQLSMALLINT columnCount )ε{
		vector<up<Binding>> bindings; bindings.reserve( columnCount );
		for( SQLSMALLINT iCol = 1; iCol <= columnCount; ++iCol ){
			SQLLEN ssType;
			CALL( statement, SQL_HANDLE_STMT, ::SQLColAttribute(statement, iCol, SQL_DESC_CONCISE_TYPE, NULL, 0, NULL, &ssType), "SQLColAttribute::Concise" );

			SQLLEN bufferSize = 0;
			up<Binding> binding;
			if( ssType == SQL_CHAR || ssType == SQL_VARCHAR || ssType == SQL_LONGVARCHAR || ssType==SQL_VARBINARY ){
				CALL( statement, SQL_HANDLE_STMT, ::SQLColAttribute(statement, iCol, SQL_DESC_DISPLAY_SIZE, NULL, 0, NULL, &bufferSize), "SQLColAttribute::Display" );
				if( ssType==SQL_VARBINARY )
					binding = mu<BindingBinary>( ++bufferSize );
				else
					binding = mu<BindingString>( (SQLSMALLINT)ssType, ++bufferSize );
			}
			else if( ssType==-8/*nchar*/ || ssType==-9/*nvarchar*/ || ssType==-10/*ntext/nvarchar(max)*/ ){//Unicode: bind SQL_C_WCHAR + decode UTF-16, else non-ANSI comes back as '?'.
				CALL( statement, SQL_HANDLE_STMT, ::SQLColAttribute(statement, iCol, SQL_DESC_DISPLAY_SIZE, NULL, 0, NULL, &bufferSize), "SQLColAttribute::Display" );//in characters.
				if( ssType==-10 && bufferSize==0 )
					bufferSize = (1 << 14) - 1;//TODO MAX/LOB: stream via SQLGetData - still truncates at 16KB.
				++bufferSize;//+1 code unit for the null terminator.
				binding = mu<BindingWString>( (SQLSMALLINT)ssType, bufferSize );//allocates bufferSize char16_t.
				bufferSize *= (SQLLEN)sizeof(char16_t);//SQLBindCol BufferLength is in BYTES for SQL_C_WCHAR.
			}
			else
				binding = Binding::GetBinding( (SQLSMALLINT)ssType );
			CALL( statement, SQL_HANDLE_STMT, ::SQLBindCol(statement, iCol, (SQLSMALLINT)binding->CodeType, binding->Data(), bufferSize, &binding->Output), "SQLBindCol" );
			bindings.push_back( move(binding) );
		}
		return bindings;
	}


	α OdbcDataSource::ServerMeta()ι->IServerMeta&{
		if( !_schemaProc )
			_schemaProc = mu<MsSql::MsSqlSchemaProc>( *this );
		return *_schemaProc;
	}

	α OdbcDataSource::Select( Sql&& s, RowΛ f, bool outParams, SL sl )ε->uint{
		return Execute( move(s), sl, {.Function=&f, .OutValue=outParams ? EValue::UInt64 : EValue::Null} );
	}



	α OdbcDataSource::SetConfig( const jobject& config )ε->void{
		SetConnectionString( Json::AsString( config, "connectionString") );
	}

	α OdbcDataSource::Query( Sql&& sql, bool outParams, SL sl )ε->QueryAwait {
		return QueryAwait{ mu<OdbcQueryAwait>( dynamic_pointer_cast<OdbcDataSource>(shared_from_this()), move(sql), outParams, sl) };
	}

	α OdbcDataSource::InsertSeqSyncUInt( InsertClause&& insert, SL sl )ε->uint{
		insert.Add( (uint)0 );
		uint newId;
		RowΛ f = [&newId]( Row&& r ){ newId=r[0].get_number<uint>(); };
		Execute( insert.Move(), sl, {&f,EValue::UInt64} );
		return newId;
	}
}