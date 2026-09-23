#include "MySqlDataSource.h"
#include <jde/db/DBException.h>
#include <jde/db/generators/Functions.h>
#include "field.h" //!important
#include "MySqlException.h"
#include "MySqlQueryAwait.h" //!important
#include "MySqlRow.h" //!important
#include "MySqlServerMeta.h"
#include "../../../src/DBLog.h" //!important


#if !defined(NDEBUG) && !defined(_GLIBCXX_DEBUG) && !defined(__clang__)
	#error "_GLIBCXX_DEBUG must be defined to compile this code."
#endif

#define let const auto

namespace Jde::DB::MySql{
	namespace mysql = boost::mysql;
	α toString( const mysql::connect_params& cs )ι->string{
		return Ƒ( "'{}@{}:{}/{}' pwd:'{}' collation:{}, ssl:{}, multi:{}",
			cs.username,
			cs.server_address.hostname(),
			cs.server_address.port(),
			cs.database,
			cs.password.empty() ? "<empty>" : "<set>",
			cs.connection_collation,
			underlying(cs.ssl),
			cs.multi_queries
		);
	}
	struct Session final{
		Session( const mysql::connect_params& cs, SL sl )ε:
			Conn{ _ctx }{
			Logging::LogOnce( SRCE_CUR, ELogTags::DBDriver, "mysql::connect_params: {}", toString(cs) );
			try{
				Conn.connect( cs );
				//Once per connection - a session variable survives, so a pooled session reused by AcquireSession keeps it.
				mysql::results tz;
				Conn.execute( UtcSession, tz );
			}
			catch( mysql::error_with_diagnostics& e ){
				throw MySqlException{ toString(cs), move(e), {ELogLevel::Critical, ELogTags::DBDriver}, sl };
			}
		}
	private:
		asio::io_context _ctx;
	public:
		mysql::any_connection Conn;
	};
	constexpr uint MaxIdleSessions{ 4 };

	Ω closeSessions( vector<up<Session>>& sessions )ι->void{
		for( auto& session : sessions ){
			try{
				session->Conn.close();
			}
			catch( ... ){}
		}
	}
	MySqlDataSource::~MySqlDataSource(){
		closeSessions( _idleSessions );
	}
	//The idle pool only - a session in use goes back to ReleaseSession, which pools it afresh.
	α MySqlDataSource::Disconnect()ε->void{
		vector<up<Session>> sessions;
		{
			lg l{ _idleSessionsMutex };
			sessions.swap( _idleSessions );
		}
		closeSessions( sessions );
	}

	α MySqlDataSource::AcquireSession( SL sl )ε->up<Session>{
		for(;;){
			up<Session> session;
			{
				lg l{ _idleSessionsMutex };
				if( _idleSessions.empty() )
					break;
				session = move( _idleSessions.back() );
				_idleSessions.pop_back();
			}
			try{ //liveness check + restore default schema (a pooled connection loses it if the schema was dropped) - still much cheaper than a fresh connect.
				if( _cs.database.empty() )
					session->Conn.ping();
				else{
					mysql::results r;
					session->Conn.execute( Ƒ("use `{}`", _cs.database), r );
				}
				return session;
			}
			catch( const boost::system::system_error& ){} //stale - discard & try the next one.
		}
		return mu<Session>( _cs, sl );
	}

	α MySqlDataSource::ReleaseSession( up<Session>&& session )ι->void{
		lg l{ _idleSessionsMutex };
		if( _idleSessions.size()<MaxIdleSessions )
			_idleSessions.push_back( move(session) );
	}

	α MySqlDataSource::SetConfig( const jobject& config )ε->void{
		auto host = Json::FindSV( config, "host" ).value_or( "localhost" );
		_cs.server_address = mysql::any_address{ mysql::host_and_port{string{host}, Json::FindNumber<PortType>(config, "port").value_or(3306)} };
		_cs.username = Json::AsSV( config, "username" );
		_cs.password = Json::AsSV( config, "password" );
		_cs.database = Json::AsSV( config, "schema" );
		_cs.connection_collation = 45; //utf8mb4_general_ci
		_cs.ssl = mysql::ssl_mode::disable;
		_cs.multi_queries = true;
	}

	α MySqlDataSource::Execute( Sql&& sql, SL sl, Params exeParams )ε->uint{
		CallText( sql );
		if( exeParams.Log )
			DB::Log( sql, sl );
		//#47: an OUT param is a *proc* convention - the trailing placeholder of `call p(?,?,?)`.  Keying on IsProc, not
		//outValue alone, is what keeps ExecuteScalerSync on plain SQL from treating the caller's last param as one; sqlite
		//binds every param for a non-proc and lets the row callback answer, and so does this.
		let outParam = exeParams.HasOut() && sql.IsProc;
		let params = ToFields( sql, outParam, sl );
		auto session = AcquireSession( sl ); //not returned to the pool on exception - connection state is uncertain.
		mysql::results result;
		mysql::statement stmt;
		try{
			if( params.empty() )
				session->Conn.execute( sql.Text, result ); //text protocol: runs a multi-statement script, which a prepared statement cannot.
			else{
				stmt = session->Conn.prepare_statement( sql.Text );
				session->Conn.execute( stmt.bind(params.begin(), params.end()), result );
			}
		}
		catch( mysql::error_with_diagnostics& e ){
			throw MySqlException{ move(sql), move(e), sl };
		}
		if( exeParams.Function ){
			for( auto&& row : ToResult(result, outParam).Rows )
				(*exeParams.Function)( move(row) );
		}
		if( stmt.valid() ){
			mysql::error_code ec; mysql::diagnostics diag;
			session->Conn.close_statement( stmt, ec, diag );
			if( ec ){
				WARN( "close_statement failed - dropping the session rather than pooling it: {} - {}", ec.message(), diag.server_message() );
				session = nullptr;
			}
		}
		if( session )
			ReleaseSession( move(session) );

		return result.has_value()
			? exeParams.Sequence ? result.last_insert_id() : result.affected_rows()
			: 0;
	}

	α MySqlDataSource::SchemaNameConfig( SL )ι->string{ return _cs.database.empty() ? string{} : _cs.database; }

	α MySqlDataSource::AtSchema( sv schema, SL )ε->sp<IDataSource>{
		string schemaName;
		try{
			schemaName = SchemaName();
		}
		catch( const Exception& e ){//assume can't connect on current schema.
		}
		sp<MySqlDataSource> ds;
		if( schema==schemaName )
			ds = dynamic_pointer_cast<MySqlDataSource>( shared_from_this() );
		else{
			ds = sp<MySqlDataSource>( (MySqlDataSource*)GetDataSource() );
			ds->_cs = _cs;
			ds->_cs.database = schema;
		}
		return ds;
	}


	α MySqlDataSource::InsertSeqSyncUInt( DB::InsertClause&& insert, SL sl )ε->uint{
		insert.Add( {}, 0ull ); //0ul is 32-bit under LLP64, so it matches both the unsigned int and unsigned long long alternatives of Value::Underlying - name the 64-bit one the OUT param wants.
		uint y{};
		RowΛ f = [&y]( Row&& r ){ y = r.Get<uint>(0); };
		Execute( insert.Move(), sl, {.Function=&f, .OutValue=EValue::UInt64} );
		return y;
	}


	α MySqlDataSource::ServerMeta()ι->IServerMeta&{
		if( !_schemaProc )
			_schemaProc = mu<MySqlServerMeta>( *this );
		return *_schemaProc;
	}

	α MySqlDataSource::Query( Sql&& sql, bool outParams, SL sl )ε->QueryAwait{
		return QueryAwait{ mu<MySqlQueryAwait>(dynamic_pointer_cast<MySqlDataSource>(shared_from_this()), move(sql), outParams, sl), sl };
	}

}
Jde::DB::IDataSource* GetDataSource(){ //below Session definition: implicit MySqlDataSource ctor needs the complete type.
	return new Jde::DB::MySql::MySqlDataSource();
}