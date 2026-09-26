#include <gtest/gtest.h>
#include <jde/db/db.h>
#include <jde/db/IDataSource.h>
#include <jde/db/DBException.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/fwk/process/process.h>

#define let const auto

namespace Jde::DB::Odbc::Tests{
	//reviews/install-issues.md #66:  the hub on SQL Server crashed with a fail-fast when the database could not be reached at
	//start.  A connect that fails has to come back as a DBException through every entry point the startup uses, not end the
	//process.  A DSN that does not exist fails in the same SQLDriverConnect as an unreachable server, with no SQL Server needed.
	Ω unreachable()ε->sp<IDataSource>{
		let driver = Process::ExePath().parent_path()/"Jde.DB.Odbc.dll";
		return DB::DataSource( jobject{ {"driver", driver.string()}, {"connectionString", "DSN=jde_no_such_dsn_66"} } );
	}

	TEST( ConnectTests, ExecuteThrows ){
		auto ds = unreachable();
		EXPECT_THROW( ds->ExecuteSync(Sql{"select 1"}), DBException );
	}

	TEST( ConnectTests, QueryThrows ){//the async path: OdbcQueryAwait's detached thread
		auto ds = unreachable();
		EXPECT_THROW( (BlockAwait<QueryAwait,Result>(ds->Query(Sql{"select 1"}))), DBException );
	}

	//The hub's own path, as args/install-sqlServer mounts it:  a cluster over the driver (GetAppSchema -> Cluster::Initialize,
	//which asks SQL Server its schema name), then the startup's -sync (SyncSchema).  Either may fail;  neither may end the process.
	TEST( ConnectTests, SchemaThrows ){
		let driver = ( Process::ExePath().parent_path()/"Jde.DB.Odbc.dll" ).string();
		let meta = string{ JDE_SOURCE_ROOT }+"libs/access/config/access-meta.jsonnet";
		const jobject dbServers{ {"localhost", jobject{
			{"driver", driver}, {"connectionString", "DSN=jde_no_such_dsn_66"},
			{"catalogs", jobject{ {"jde", jobject{ {"schemas", jobject{ {"acc", jobject{ {"access", jobject{ {"meta", meta}, {"prefix", ""} }} }} }} }} }}
		}} };
		sp<AppSchema> schema;
		try{
			schema = DB::GetAppSchema( "access", nullptr, dbServers );
		}
		catch( const Exception& e ){
			SUCCEED() << "GetAppSchema: " << e.what();
			return;
		}
		EXPECT_THROW( DB::SyncSchema(*schema, nullptr), Exception );
	}
}
