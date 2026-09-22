#pragma once
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/DBSchema.h>
#include "ForeignKey.h"
#include "Procedure.h"

namespace Jde::QL{ struct IQL; }
namespace Jde::DB{
	struct AppSchema; struct Catalog; struct ForeignKey; struct IServerMeta; struct Procedure; struct Syntax; struct Table;

	//Cluster > Catalog > Schema > Table > Columns & Rows
	struct SchemaDdl : DBSchema, std::enable_shared_from_this<SchemaDdl>{
		SchemaDdl( str name, sv tablePrefix, const IServerMeta& loader, sp<QL::IQL> ql )ε;
		Ω Sync( const AppSchema& config, sp<QL::IQL> ql )ε->void;
		Ω SeedData( const AppSchema& config, sv extension, sp<QL::IQL> ql, bool skipUnchanged=false )ε->void;
		Ω SeedFile( const AppSchema& config, string name, str text, sp<QL::IQL> ql, bool skipUnchanged )ε->bool;//DB::SeedFile//the /dbServers/dataPaths files of one extension, upserted through `ql` - Sync's ".mutation" pass, and the ".roles" pass the app runs once the access server is up (DB::SyncData).
		Ω Create( const DBSchema& config )ε->void;
		α IsPhysical()Ι->bool { return true; }
		α Tables()ι->flat_map<string,sp<Table>>&{ return Meta()->Tables; }
		α Views()ι->flat_map<string,sp<Table>>&{ return Meta()->Views; }

		flat_map<string,Procedure> Procs;
		flat_map<string,ForeignKey> FKs;//fk name, fk
	private:
		α Teardown()ι->void;//the metadata graph is strongly cyclic - Sync must call this or the temporary never destructs.
		α SyncData( const AppSchema& config, const jobject& initConfig )ε->void;
		α SyncFKs( const AppSchema& config )ε->void;
		α SyncScripts(const AppSchema& config, const jobject& jconfig )ε->void;
		α SyncTables( const AppSchema& config )ε->void;

		α Meta()ι->sp<AppSchema>{ return AppSchemas.find("")->second; }
#ifndef PROD
	public:
		Ω Drop( const AppSchema& config )ε->void;
#endif
		sp<QL::IQL> _ql;
	};
}