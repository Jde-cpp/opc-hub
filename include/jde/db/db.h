#pragma once
#include <jde/fwk/io/json.h>
#include <jde/db/exports.h>

#define Φ ΓDB α

namespace Jde::QL{ struct IQL; }
namespace Jde::Access{ struct IAcl; }
namespace Jde::DB{
	struct Cluster; struct IDataSource; struct AppSchema;

	Φ DataSource( const jobject& config, SRCE )ε->sp<IDataSource>;
	Φ GetCluster( sv configName, sp<Access::IAcl>, SRCE )ε->sp<Cluster>; //resolve a specific cluster by its dbServers key (e.g. two backends configured side by side).
	Φ GetAppSchema( str name, sp<Access::IAcl>, optional<jobject> dbSettings=nullopt )ε->sp<AppSchema>;
	Φ SyncSchema( const AppSchema& schema, sp<QL::IQL> ql )ε->void;
	Φ SyncData( const AppSchema& schema, sp<QL::IQL> ql, sv extension, bool skipUnchanged=false )ε->void;//one more seed pass over /dbServers/dataPaths, files of `extension` - for what SyncSchema's ".mutation" pass runs too early for (the ".roles" files need the access server's role mutations).
	Φ SeedFile( const AppSchema& schema, string name, str text, sp<QL::IQL> ql, bool skipUnchanged )ε->bool;//one seed file's text through `ql`;  skipUnchanged:  a schema with a `seeds` table skips a file whose text it last applied, and records one it applies (reviews/m3-closing.md #12).  False when skipped.
#ifndef PROD
	namespace NonProd{
		Φ Recreate( const AppSchema& schema, sp<QL::IQL> ql )ε->void;
	}
#endif
}
#undef Φ