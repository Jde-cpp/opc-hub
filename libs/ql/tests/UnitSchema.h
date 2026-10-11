#pragma once
//A two-table schema with no data source behind it:  a '_'-prefixed DBSchema is QL-only, so Initialize skips everything that
//needs a syntax, catalog or connection.  providers.provider_type_id is a real fk with a pk table; provider_types has only its
//own pk, named provider_type_id with no pk table - the users_ql.identity_id shape #3 and #7 turn on.
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/DBSchema.h>
#include <jde/db/meta/Table.h>

namespace Jde::QL::Tests{
	Ξ table( sv name, sv json )ε->std::pair<string,sp<DB::Table>>{
		return { string{name}, ms<DB::Table>( name, Json::Parse(json) ) };
	}
	//Initialize wires the tables into the same cyclic graph the real schema is - Column::PKTable to the pk table, whose
	//Table::Children points back, AppSchema::Tables to every table, whose Table::Schema points back - and a process-lifetime
	//schema never lets go of it.  A fresh one per call does, so LeakSanitizer reported every set at exit (736 allocations across
	//the suite).  Kept reachable here, exactly as the real one is, so the report matches the design rather than the scaffolding.
	Ξ keepAlive( vector<sp<DB::AppSchema>> schemas )ι->vector<sp<DB::AppSchema>>{
		static auto& built = *new vector<vector<sp<DB::AppSchema>>>{}; //never destroyed:  a plain static is torn down before LeakSanitizer looks, which is the one moment that matters.
		built.push_back( schemas );
		return schemas;
	}
	Ξ schemas()ε->vector<sp<DB::AppSchema>>{
		flat_map<string,sp<DB::Table>> tables{
			table( "provider_types", R"({"columns":{"provider_type_id":{"sk":0,"i":0},"name":{"i":1}}})" ),
			table( "providers", R"({"columns":{"provider_id":{"sk":0,"i":0},"provider_type_id":{"pkTable":"provider_types","i":1},"name":{"i":2}}})" ),
			//#40: varbinary has no graphql spelling (users.password) and must be left out of __type rather than fail it; a guid
			//is a String (hist_groups.guid), there beside it.
			table( "nodes", R"({"columns":{"node_id":{"sk":0,"i":0},"name":{"i":1},"guid":{"type":"guid","i":2},"secret":{"type":"varbinary","i":3}}})" ),
			//#42: a table whose json name begins with a mutation verb ("start") - IsMutation used to claim it and the query was refused.
			table( "startups", R"({"columns":{"startup_id":{"sk":0,"i":0},"name":{"i":1}}})" ),
			//soft-deleted rows, as every slugColumns table has
			table( "widgets", R"({"columns":{"widget_id":{"sk":0,"i":0},"name":{"i":1},"deleted":{"type":"DateTime","nullable":true,"i":2}}})" )
		};
		auto db = ms<DB::DBSchema>( "_qlTests", move(tables), "" );
		DB::DBSchema::Initialize( {}, db );
		return keepAlive( { db->AppSchemas.begin()->second } );
	}
}
