#include "SchemaDdl.h"
#include <jde/fwk/io/file.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <boost/uuid/uuid_io.hpp>
#include "TableDdl.h"
#include <jde/db/DBException.h>
#include <jde/db/IDataSource.h>
#include <jde/db/names.h>
#include <jde/db/generators/Functions.h>
#include <jde/db/generators/Syntax.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Catalog.h>
#include <jde/db/meta/Cluster.h>
#include <jde/db/meta/Column.h>
#include "../IServerMeta.h"
#include <jde/ql/IQL.h>
#include "Index.h"
#include "Procedure.h"


#define let const auto
namespace Jde::DB{
	constexpr ELogTags _tags{ ELogTags::Sql };

	SchemaDdl::SchemaDdl( str name, sv tablePrefix, const IServerMeta& loader, sp<QL::IQL> ql )ε:
		DBSchema{ name, loader.LoadTables(name, tablePrefix), tablePrefix },
		Procs{ loader.LoadProcs(name) },
		FKs{ loader.LoadForeignKeys(name) },
		_ql{ql}
	{}

	α ConfigurationJson( const AppSchema& config )ε->const jobject{
		let appSchema = Settings::AsObject( config.ConfigPath() );
		auto appMeta = Json::ReadJsonNet( Json::AsSV(appSchema, "meta"), {} );
		if( let prefix = Json::FindString(appSchema, "prefix"); prefix )
			appMeta["prefix"] = *prefix;
		return appMeta;
	}
	//`access_identities` -> `accs_idntty`: each word of the singular loses its vowels past the first character, unless that
	//leaves it under two shorter - the fk names SyncFKs builds from two of these have to fit the dialect's identifier limit.
	α abbrevName( sv schemaName )ι->string{
		auto abbrev = []( sv word )->string{
			string y;
			for( let ch : word ){
				if( y.empty() || (ch!='a' && ch!='e' && ch!='i' && ch!='o' && ch!='u') )
					y += ch;
			}
			return word.size()>2 && y.size()<word.size()-1 ? y : string{ word };
		};
		let singular = Names::ToSingular( schemaName );
		vector<string> words;
		for( let& word : Str::Split(singular, '_') )
			words.push_back( abbrev(word) );
		return Str::Join( words, "_" );
	}

	α exists( const DBSchema& config )ι->bool{
		try{
			return config.DS()->ScalerSyncOpt<string>( {string{config.DS()->Syntax().SchemaExistsSql()}, {Value{config.Name}}} ).has_value();
		}
		catch( Exception& e ){
			e.SetLevel( ELogLevel::Debug );
			return false;//connected to schema which doesn't exist.
		}
	}

	α UniqueIndexName( const DB::Index& index, sv tableName, const DB::Syntax& syntax, const vector<Index>& indexes )ε->string{
		let baseName = syntax.IndexName( tableName, index.Name ); //dialect owns the composition - schema-wide namespaces (sqlite) qualify with the table.
		auto indexName = baseName;
		for( uint i=2; ; indexName = Ƒ( "{}{}", baseName, i++ ) ){
			if( find_if(indexes, [&](let& x){ return ToIV(x.Name)==ToIV(indexName);})==indexes.end() )
				break;
		}
		return indexName;
	}

//todo add db version table.
	α SchemaDdl::Sync( const AppSchema& config, sp<QL::IQL> ql )ε->void{
		if( config.Syntax().HasSchemas() && !exists(*config.DBSchema) ){
			Create( *config.DBSchema );
			config.ResetDS();
		}
		let& initConfig = ConfigurationJson( config );//has data, scripts.
		string prefix = config.ObjectPrefix(); //single source of truth; was a swapped-ternary duplicate that yielded "" for an "db.um_"-style prefix and broke SyncTables' ObjectPrefix()+tableName lookups.

		auto catalog = ms<DB::Catalog>( config.DS() );
		auto db = ms<SchemaDdl>( config.DBSchema->Name, prefix, config.DS()->ServerMeta(), ql ); //dbSchema
		db->Initialize( catalog, db );
		catalog = nullptr;

		struct Guard{ SchemaDdl& Db; ~Guard(){ Db.Teardown(); } } teardown{ *db }; //on every exit, thrown or not - the graph is cyclic, see Teardown.
		db->SyncTables( config );
		db->SyncScripts( config, initConfig );
		db->SyncData( config, Json::AsObject(initConfig, "tables") );
		db->SyncFKs( config );
	}

	//The metadata graph is strongly cyclic - AppSchema::DBSchema, Table::Schema, Column::Table/PKTable, plus
	//QLView/Children/Map/Extends - so this temporary's refcount never reaches zero on scope exit.  Unwire only the
	//objects this graph initialized (Schema points at its own AppSchema): SyncTables also emplaces the config
	//schema's live views, which must merely be released, never mutated.
	α SchemaDdl::Teardown()ι->void{
		for( auto&& [_, appSchema] : AppSchemas ){
			auto clear = []( Table& t )ι{
				t.Columns.clear();
				t.Map.reset();
				t.QLView = nullptr;
				t.Children.clear();
				t.Schema = nullptr;
				t.Extends = nullptr;
			};
			for( auto&& [_, t] : appSchema->Tables ){
				if( t->Schema==appSchema )
					clear( *t );
			}
			for( auto&& [_, v] : appSchema->Views ){
				if( v->Schema==appSchema )
					clear( *v );
			}
			appSchema->Tables.clear();
			appSchema->Views.clear();
			appSchema->DBSchema = nullptr;
		}
		AppSchemas.clear();
	}

	α SchemaDdl::SyncFKs( const AppSchema& config )ε->void{
		let canAddFKs = config.DS()->Syntax().CanAddForeignKeys();
		let prefix = config.ObjectPrefix();
		for( let& [tableName, table] : config.Tables ){
			let dbTableName = prefix+tableName;
			for( auto& column : table->Columns ){
				if( !column->NeedsFK() )
					continue;
				if( find_if(FKs, [&](let& fk){
					return fk.second.Table==dbTableName && fk.second.Columns==vector<string>{column->Name};
				})!=FKs.end() ){
					continue;
				}
				if( !canAddFKs ){
					ERR( "Syntax can't add fks to existing tables - skipping sync for '{}.{}'.", tableName, column->Name );
					continue;
				}
				let pkTable = column->PKTable;
				auto getName = [&, &t=tableName](auto i)->string {//&t for clang
					return Ƒ( "{}_{}{}_fk", abbrevName(t), abbrevName(pkTable->Name), i==0 ? "" : std::to_string(i) );
				};
				uint i{};
				auto name = getName( i++ );
				for( ; FKs.find(name)!=FKs.end(); name = getName(i++) );

				auto createStatement = ForeignKey::Create( name, column->Name, *pkTable, *table );
				config.DS()->ExecuteSync( {move(createStatement)} );
				FKs.emplace( name, ForeignKey{name, table->Name, {column->Name}, pkTable->Name} );
				INFO( "Created fk '{}'.", name );
			}
		}
	}
	Ω forEachDir( sv jpath, sv extension, vector<string> prefixes, function<void(const fs::path& file)> process )ε{
		let dirs = Settings::FindStringArray( jpath );
		for( let& scriptDir : dirs ){
			const fs::path scriptRoot{ scriptDir };
			DBGT( ELogTags::App, "Processing '{}'.  Prefixes: [{}], extension: {}", scriptRoot.string(), Str::Join(prefixes, ", "), extension );
			THROW_IF( !fs::exists(scriptRoot) || !fs::is_directory(scriptRoot), "Script path '{}' does not exist.", scriptRoot.string() );
			flat_map<string,fs::path> files;  //abc order
			for( let& entry : fs::directory_iterator(scriptRoot) ){
				if( let& path = entry.path();
					!entry.is_directory()
						&& path.extension().string().starts_with(extension)
						&& (prefixes.empty() || find_if( prefixes, [&](let& x){ return path.filename().string().starts_with(x);})!=prefixes.end()) ){
					files.emplace( path.filename().string(), path );
				}
			}
			for( let& [fileName, file] : files )
				process( file );
		}
	}

	α SchemaDdl::SyncData( const AppSchema& config, const jobject& )ε->void{//the config is re-read by SeedData - the one entry point the app's later ".roles" pass shares.
		SeedData( config, ".mutation", _ql );
	}
	α SchemaDdl::SeedData( const AppSchema& config, sv extension, sp<QL::IQL> ql, bool skipUnchanged )ε->void{
		let json = ConfigurationJson( config );//by value - a reference into it below outlives a temporary.
		let& initConfig = Json::AsObject( json, "tables" );
		vector<string> prefixes{ config.Name };
		if( let& configPrefixes = Json::FindArray(initConfig, "dataPrefixes"); configPrefixes ){
			auto additional = Json::FromArray<string>( *configPrefixes );
			move( additional.begin(), additional.end(), std::back_inserter(prefixes) );
		}
		forEachDir( "/dbServers/dataPaths", extension, prefixes, [&](const fs::path& file){
			INFO( "Mutation: '{}'", file.string() );
			SeedFile( config, file.filename().string(), IO::Load(file), ql, skipUnchanged );
		});
	}
	//The .roles pass reran every file at every -sync start - every installed start - so a permission or child role an admin had
	//removed from a seeded role came back at the next reboot (reviews/m3-closing.md #12, ruled 09-21:  the admin's edits win).  A
	//schema with a `seeds` table records each file by content once it has applied, and skips it while it is unchanged;  a release
	//whose seed changed applies it again, adding what is missing (Upsert's adds rewrite nothing).  Recorded only after the text
	//applied, so a pass that throws is retried at the next start.  No `seeds` table (a schema without one), no skipping.
	α SchemaDdl::SeedFile( const AppSchema& config, string name, str text, sp<QL::IQL> ql, bool skipUnchanged )ε->bool{
		let seeds = skipUnchanged ? config.FindTable( "seeds" ) : nullptr;
		let hash = seeds ? boost::uuids::to_string( Crypto::CalcMd5(text) ) : string{};
		auto& ds = *config.DS();
		if( seeds && ds.ScalerSyncOpt<string>({Ƒ("select content_hash from {} where name=?", seeds->DBName), {Value{name}}})==hash ){
			INFO( "Seed '{}' is unchanged since it was applied - skipped.", name );
			return false;
		}
		ql->Upsert( text, {}, {UserPK::System} );
		if( seeds ){
			ds.ExecuteSync( {Ƒ("delete from {} where name=?", seeds->DBName), {Value{name}}} );
			ds.ExecuteSync( {Ƒ("insert into {}( name, content_hash, applied ) values( ?, ?, {} )", seeds->DBName, ds.Syntax().UtcNow()), {Value{name}, Value{hash}}} );
		}
		return true;
	}

	α SchemaDdl::SyncScripts( const AppSchema& config, const jobject& initConfig )ε->void{
		auto prefix = Json::FindString( initConfig, "prefix" ).value_or( "" );
		let stdPrefix = config.Name+"_"; //access_
		forEachDir( "/dbServers/scriptPaths", ".sql", {stdPrefix}, [&](const fs::path& scriptFile){
			let fileName = scriptFile.filename(); //[access_]user_insert.sql
			let stem = fileName.stem().string();
			let procViewName = stem.substr( stdPrefix.size() );
			let dbName = prefix+procViewName;
			if( Tables().find(dbName)!=Tables().end() )
				return;
			let text = IO::Load( scriptFile );
			TRACE( "Executing '{}'", scriptFile.string() );
			let queries = Str::Split<sv,Str::iv>( text, "\ngo"_iv );
			for( let& text : queries ){
				let query = Str::Replace(
					Str::Replace(text, "[dbo]"sv, Ƒ("[{}]", config.DBSchema->Name) ),
					stdPrefix, prefix );

				std::ostringstream os;
				for( uint i=0; i<query.size(); ++i ){
					if( query[i]=='#' )
						for( ; i<query.size() && query[i]!='\n'; ++i );
					if( i<query.size() )
						os.put( query[i] );
				}
				string sql{ os.str() };
				config.DS()->ExecuteSync( {move(sql)} );
			}
			INFO( "Finished '{}'", scriptFile.string() );
		});
	}

	α SchemaDdl::SyncTables( const AppSchema& config )ε->void{
		const DB::Syntax& syntax = config.DS()->Syntax();
		IDataSource& ds = *DS();
		let& schemaName = config.DBSchema->Name;
		let prefix = config.ObjectPrefix();
		for( let& [tableName, table] : config.Tables ){
			let dbTableName = prefix+tableName; //the server-side name: Tables() and the loaded indexes are keyed by it.
			sp<TableDdl> dbTable;
			if( let kv=Tables().find(dbTableName); kv!=Tables().end() ){
				dbTable = std::dynamic_pointer_cast<TableDdl>( kv->second );
				ASSERT( dbTable );
				for( auto& column : table->Columns ){
					auto pDBColumn = dbTable->FindColumn( column->Name ); if( !pDBColumn ){ CRITICAL("Could not find db column {}.{}", tableName, column->Name); continue; }
					pDBColumn->Insertable = column->Insertable;
					if( pDBColumn->Default && pDBColumn->Default->is_string() && pDBColumn->Default->get_string()!="$now" )
						ds.ExecuteSync( {syntax.AddDefault(table->SqlName(), column->Name, *pDBColumn->Default)} );
				}
			}
			else{
				dbTable = ms<TableDdl>( *table );
				ds.ExecuteSync( {dbTable->CreateStatement()} );
				INFO( "Created table '{}'.", table->DBName );
				dbTable = ds.ServerMeta().LoadTable( schemaName, dbTableName );
				dbTable->Initialize( FindAppSchema( "" ), dbTable );
				Tables().emplace( dbTable->Name, dbTable );
			}

			auto& dbIndexes = dbTable->Indexes;
			for( let& index : Index::GetConfig(*table) ){
				if( let db = find_if(dbIndexes, [&](let& db){ return db.TableName==dbTableName && db.Columns==index.Columns;} ); db!=dbIndexes.end() ){
					//matching on columns alone treats a non-unique index as satisfying a unique natural key - the case for schemas predating a naturalKeys addition. Not reconciled here: dropping an index on a live table is destructive, and recreating it unique fails outright once duplicates exist. Say so instead of skipping silently - callers treat the configured uniqueness as the integrity contract.
					if( db->Unique!=index.Unique )
						WARN( "Index '{}.{}' on ({}) is {}unique in the database but {}unique in the configuration - not reconciled; existing rows may already violate the configured constraint.", table->DBName, db->Name, Str::Join(index.Columns), db->Unique ? "" : "not ", index.Unique ? "" : "not " );
					continue;
				}
				let name = UniqueIndexName( index, dbTable->DBName, syntax, dbIndexes );
				let fqTableName = syntax.QualifiedName( schemaName, dbTable->DBName );
				let fqSqlTableName = syntax.QualifiedName( schemaName, dbTable->SqlName() );
				ds.ExecuteSync( {index.Create(name, fqTableName, fqSqlTableName, syntax)} );
				dbIndexes.push_back( Index{name, dbTable->DBName, index} );
				INFO( "Created index '{}.{}'.", table->DBName, name );
			}
			//DdlInsertProcName is empty when there is no server object to create - !HasProcs (sqlite) registers a
			//native twin through IProcs instead.  HasCustomInsertProc is create-specific: those come from .sql scripts.
			if( let procName = table->HasCustomInsertProc ? string{} : UnqualifiedProcName(table->DdlInsertProcName()); procName.size() ){
				let existed = Procs.find( procName )!=Procs.end();
				if( let drop = syntax.DropProcSql( Ƒ("{}.{}", schemaName, syntax.EscapeDdl(procName)) ); existed && drop.size() )
					ds.ExecuteSync( {drop} );
				ds.ExecuteSync( {dbTable->InsertProcCreateStatement(*table)} );
				if( !existed )
					Procs.emplace( procName, Procedure{procName} );
				INFO( "{} proc '{}'.", existed ? "Refreshed"sv : "Created"sv, table->InsertProcName() );
			}
		}
		for( let& [name, view] : config.Views ){
			Views().emplace( name, view );
		}
		if( !syntax.CanAddForeignKeys() )
			FKs = config.DS()->ServerMeta().LoadForeignKeys( schemaName );

		for( let& [_, table] : Tables() ){
			table->Initialize( Meta(), table );
		}
	}

	α SchemaDdl::Create( const DBSchema& config )ε->void{
		auto currentDS = config.DS();
		auto& syntax = currentDS->Syntax();
		if( !syntax.HasSchemas() )
			return;
		auto sysDS = syntax.CanSetDefaultSchema()
			? move(currentDS)->AtSchema( syntax.SysSchema() )
			: move(currentDS);
		sysDS->ExecuteSync( {Ƒ("CREATE SCHEMA {}", config.Name)} );
	}
#ifndef PROD
	α DropObjects( const AppSchema& config )ε->void{
		auto& ds = *config.DS();

		//A dialect that can't ALTER TABLE ADD CONSTRAINT can't drop one either - its fks are inline in the create
		//statement and go away with the table below (sqlite rejects 'alter table … drop constraint' as a syntax
		//error).  Guarding the loop also skips LoadForeignKeys' per-table pragma scan for those dialects.
		if( ds.Syntax().CanAddForeignKeys() ){
			for( let& [name, fk] : config.DS()->ServerMeta().LoadForeignKeys(config.DBSchema->Name) ){
				if( let p = find_if(config.Tables, [&fk](let& t){return t.second->DBName==fk.Table;}); p!=config.Tables.end() )
					ds.ExecuteSync( {Ƒ("ALTER TABLE {} DROP CONSTRAINT {}", p->second->SqlName(), name)} );
			}
		}

		let hasProcs = ds.Syntax().HasProcs(); //sqlite: procs are native twins, there is nothing on the server to drop.
		for( let& [tableName, table] : config.Tables ){
			if( let procName = table->DdlInsertProcName(); procName.size() ) //once, not twice - it allocates.
				ds.ExecuteSync( {Ƒ("DROP PROCEDURE IF EXISTS {}", procName)} );
			if( hasProcs && table->PurgeProcName.size() )
				ds.ExecuteSync( {Ƒ("DROP PROCEDURE IF EXISTS {}", table->PurgeProcName)} );
			ds.ExecuteSync( {Ƒ("DROP TABLE IF EXISTS {}", table->SqlName())} );
		}
		let& initConfig = ConfigurationJson( config );
		if( auto script = Json::FindString(initConfig, "meta"); script ){
			auto name = fs::path{ *script }.stem().string();
			let type = name.ends_with("_ql") ? "VIEW" : "PROCEDURE";
			ds.ExecuteSync( {Ƒ("DROP {} IF EXISTS {}", type, name)} );
		}
	}

	α SchemaDdl::Drop( const AppSchema& config )ε->void{
		if( exists(*config.DBSchema) ){
			// if catalogs, would have dropped catalog
			//C10: a commented-out SchemaDropsObjects() guard stood here; the virtual is gone with it.  Both dialects that
			//overrode it answered true, so the guard would have skipped DropObjects() on every backend that has schemas.
			config.DS()->ExecuteSync( {Ƒ("DROP SCHEMA {}", config.DBSchema->Name)} );
		}
	}
#endif
}