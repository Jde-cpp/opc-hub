#pragma once
#include <jde/db/sqlite_api.h>

//Twins of the *generated* insert procs of the qlTests schema's two sequenced tables (../../qlTests-meta.jsonnet) - the other
//dialects generate theirs (TableDdl::InsertProcCreateStatement); sqlite has no procs.  One <proc>.cpp per proc, registered by
//RegisterProcs (sqlite_api.cpp) through the driver's IProcs.
namespace Jde::DB::Sqlite::QLTestsProcs{
	α RegisterQltItemInsert( IProcs& procs )ι->void;
	α RegisterQltLabelInsert( IProcs& procs )ι->void;
}