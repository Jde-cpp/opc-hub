#include "gtest/gtest.h"
#include <fstream>
#include <absl/cleanup/cleanup.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/log/Entry.h>
#include <jde/fwk/log/MemoryLog.h>
#include <jde/db/db.h>
#include <jde/db/DBException.h>
#include <jde/db/IDataSource.h>
#include <jde/db/generators/Syntax.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Catalog.h>
#include <jde/db/meta/Cluster.h>
#include <jde/db/meta/Column.h>
#include <jde/db/meta/DBSchema.h>
#include <jde/db/meta/Table.h>
#include "../../db/src/meta/IServerMeta.h"
#include "../../db/src/meta/ServerMetaFold.h" //SameDefault
#include "../../db/src/meta/ddl/ColumnDdl.h"
#include "../../db/src/meta/ddl/TableDdl.h"
#include "globals.h"

#define let const auto

namespace Jde::Access::Tests{
	using namespace Jde::DB;
	//#241: a `default:` added to, or changed on, a column of a table that already exists never reached the database -
	//SchemaDdl::SyncTables' reconciliation compared the wrong thing and never fired - so a release that changed one
	//changed it for fresh installs alone.  This is the release's path on whichever dialect the args select: a small
	//schema of its own synced once, its meta rewritten with the defaults a later release would ship, synced again.
	//The table lives on between the two syncs:  the sqlite catalog is pointed at a file for that, since the `:memory:`
	//ctest passes is a new empty database per data source.
	//The ad-hoc schema is built from a copy of the first cluster's config with its app schemas replaced by this one, and
	//the same entry is set into the global settings, which Sync reads back by AppSchema::ConfigPath for the scripts and
	//seeds (none here).  The schemas are kept for the process' life, on a heap that is never freed:  the meta graph is
	//cyclic, so a released one is a leak, and a global's destructor runs before LeakSanitizer looks.  Everything else the
	//test makes - the table, the settings entries, the files - goes when it ends, an ASSERT's early return included.
	Ω metaText( sv color, bool flag, uint qty, bool createdNow, sv label )ι->string{
		return Ƒ( R"({{"tables":{{"widgets":{{"columns":{{
			"name":{{"type":"VarChar","length":32,"sk":0,"i":0}},
			"color":{{"type":"VarChar","length":16,"default":"{}","i":1}},
			"flag":{{"type":"Bit","default":{},"i":2}},
			"qty":{{"type":"Int","default":{},"i":3}},
			"created":{{"type":"DateTime","nullable":true{},"i":4}},
			"label":{{"type":"VarChar","length":16,"nullable":true,"default":"{}","i":5}},
			"ratio":{{"type":"Float","default":1e20,"i":6}}
		}}}}}}}})", color, flag, qty, createdNow ? R"(,"default":"$now")" : "", label ); //ratio never changes:  MySQL reads it back as `1e20`, DefaultLiteral spells it `1e+20` (#9).
	}
	auto& _keep = *new vector<sp<AppSchema>>{};
	vector<std::pair<string,jvalue>> _mounted; //each settings object sync() adds the mount to, as it was before.
	Ω removeFiles()ι->void{ //sqlite's file with its wal and shm, where the dialect uses one, and the meta.
		for( sv name : {"zz241-meta.jsonnet", "zz241-sync.db", "zz241-sync.db-wal", "zz241-sync.db-shm"} ){
			std::error_code ec;
			fs::remove( fs::current_path()/name, ec );
		}
	}
	Ω sync( const fs::path& metaPath, str text )ε->sp<AppSchema>{
		{ std::ofstream os{ metaPath }; os << text; }
		let& first = *Schemas().front()->DBSchema;
		let clusterName = first.Catalog->Cluster->ConfigName;
		jobject cluster = Settings::AsObject( Ƒ("/dbServers/{}", clusterName) );
		const jobject mount{ {"meta", metaPath.string()}, {"prefix", "zz241_"}, {"dynamicLib", nullptr} }; //the sqlite driver wants the key, null or not.
		for( auto& [catalogName, catalog] : Json::AsObject(cluster, "catalogs") ){
			auto& c = catalog.get_object();
			if( c.contains("path") ) //sqlite: its own file, so the table outlives the data source that created it.
				c["path"] = (fs::current_path()/"zz241-sync.db").string();
			for( auto& [dbName, schemas] : Json::AsObject(c, "schemas") ){
				schemas = jobject{ {"zz241", mount} };
				let path = Ƒ( "/dbServers/{}/catalogs/{}/schemas/{}", clusterName, catalogName, dbName );
				if( find_if(_mounted, [&](let& m){ return m.first==path; })==_mounted.end() )
					_mounted.emplace_back( path, Settings::AsObject(path) );
				Settings::Set( path+"/zz241", mount );
			}
		}
		auto schema = GetAppSchema( "zz241", nullptr, jobject{{clusterName, cluster}} );
		_keep.push_back( schema );
		SyncSchema( *schema, nullptr ); //nothing to seed, so no ql.
		return schema;
	}

	TEST( SchemaSyncTests, AChangedDefaultReachesAnExistingTable ){
		let metaPath = fs::current_path()/"zz241-meta.jsonnet";
		removeFiles(); //a prior run's, killed before its cleanup.
		IDataSource* pDS{};
		string table;
		absl::Cleanup cleanup = [&]{ //a destructor - nothing may leave it.
			try{
				if( pDS )
					pDS->ExecuteSync( {Ƒ("drop table {}", table)} ); //not left in a shared dev schema.
			}
			catch( const std::exception& e ){ ADD_FAILURE() << e.what(); }
			for( let& schema : _keep ){ //closes sqlite's file, so its wal and shm go too.
				try{ schema->DS()->Disconnect(); }
				catch( const std::exception& e ){ ADD_FAILURE() << e.what(); }
			}
			for( auto& [path, before] : _mounted ){
				try{ Settings::Set( path, move(before) ); }
				catch( const std::exception& e ){ ADD_FAILURE() << e.what(); }
			}
			_mounted.clear();
			removeFiles();
		};
		let v1 = sync( metaPath, metaText("red", false, 1, false, R"(a'b\\)") ); //json-escaped: a'b\ - MySQL's escape character, ending the literal.
		auto& ds = *v1->DS();
		pDS = &ds;
		table = Ƒ( "{}widgets", v1->ObjectPrefix() );
		ds.ExecuteSync( {Ƒ("delete from {}", table)} ); //a run that died before its drop leaves the table on a server-backed dialect, with whatever defaults it got to.
		ds.ExecuteSync( {Ƒ("insert into {}(name) values('w1')", table)} );
		EXPECT_EQ( ds.ScalerSync<string>({Ƒ("select color from {} where name='w1'", table)}), "red" );
		EXPECT_EQ( ds.ScalerSync<string>({Ƒ("select label from {} where name='w1'", table)}), R"(a'b\)" );

		let v2 = sync( metaPath, metaText("blue", true, 2, true, R"(c'\\td)") );
		auto& syntax = ds.Syntax();
		let loaded = ds.ServerMeta().LoadTable( v1->DBSchema->Name, table );
		for( let& column : v2->GetTablePtr("widgets")->Columns ){ //what the loader reads back is DefaultLiteral's spelling of what the configuration says - the invariant that makes the next sync a no-op.
			let dbColumn = std::dynamic_pointer_cast<ColumnDdl>( loaded->FindColumn(column->Name) );
			ASSERT_TRUE( dbColumn ) << column->Name;
			if( column->Default )
				EXPECT_TRUE( SameDefault(syntax.DefaultLiteral(*column->Default), dbColumn->DefaultSql) ) << column->Name << ": " << dbColumn->DefaultSql;
			else
				EXPECT_TRUE( dbColumn->DefaultSql.empty() ) << column->Name; //`name`: never had one.
		}
		ds.ExecuteSync( {Ƒ("insert into {}(name) values('w2')", table)} );
		EXPECT_EQ( ds.ScalerSync<string>({Ƒ("select color from {} where name='w2'", table)}), "blue" );
		EXPECT_EQ( ds.ScalerSync<string>({Ƒ("select label from {} where name='w2'", table)}), R"(c'\td)" ); //not a tab.
		EXPECT_EQ( ds.ScalerSync<uint>({Ƒ("select qty from {} where name='w2'", table)}), 2u );
		EXPECT_EQ( ds.ScalerSync<uint>({Ƒ("select count(*) from {} where name='w2' and flag=1 and created is not null", table)}), 1u );
		EXPECT_EQ( ds.ScalerSync<string>({Ƒ("select color from {} where name='w1'", table)}), "red" ); //a default is for the rows to come - the ones there keep theirs.
		EXPECT_EQ( ds.ScalerSync<uint>({Ƒ("select count(*) from {} where name='w1' and flag=0 and created is null", table)}), 1u );

		if( !Logging::FindLogger<Logging::MemoryLog>() )
			Logging::AddLogger( mu<Logging::MemoryLog>() );
		Logging::ClearMemory();
		let v3 = sync( metaPath, metaText("blue", true, 2, true, R"(c'\\td)") ); //unchanged: a no-op, and the table is still there with its rows.
		for( let& e : Logging::Find([](const Logging::Entry& e){ return e.Message().starts_with("Set the default"); }) )
			ADD_FAILURE() << "unchanged, but altered: " << e.Message();
		EXPECT_EQ( ds.ScalerSync<uint>({Ƒ("select count(*) from {}", table)}), 2u );
		EXPECT_TRUE( v3->GetTablePtr("widgets") );
	}
}