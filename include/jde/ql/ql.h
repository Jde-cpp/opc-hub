#pragma once
#include <jde/fwk/co/Await.h>
#include <jde/db/generators/Functions.h>
#include <jde/db/generators/Statement.h>
#include "types/MutationQL.h"
#include "types/RequestQL.h"
#include "types/TableQL.h"
#include "types/Subscription.h"

namespace Jde::Access{ struct Authorize; }
namespace Jde::DB{ struct AppSchema; struct Table; }
namespace Jde::QL{
	struct Introspection; struct Object; struct IQL; struct LocalQL;
	α AddIntrospection( Introspection&& x )ι->void;
	α FindIntrospection( sv typeName )ι->const Object*; //config-declared type, nullptr if none.
	Ŧ AsId( const jobject& j, SRCE )ε->T;
	Ŧ AsId( const jvalue& j, SRCE )ε->T;
	α SetSystemTables( flat_set<string>&& jsonNames )ι->void;
	//Mutations named by a whole command that starts with no verb (the gateway's histInsert):  parsed as Execute, with the
	//command as their json name, and resolving no table, so only a CustomMutation answers them.
	α SetSystemMutations( flat_set<string>&& commands )ι->void;

	α Configure( const vector<sp<DB::AppSchema>>& schemas )ε->void;
	//Caches a lookup table's id->name map - what SelectAwait renders an enum/flags column from - with no expiry, so no
	//request path takes SelectEnumSync's blocking miss.  On MySQL that miss parks the caller on the very executor pool that
	//has to deliver its query (appserver-review2 r2.24).  Call from startup, off that pool, and after the schema exists;
	//whoever writes a loaded table calls LoadEnum again - MutationAwait clears the entry, and a non-QL writer re-loads.
	α LoadEnum( const DB::Table& table, SRCE )ι->bool;
	α LoadEnums( const vector<sp<DB::AppSchema>>& schemas, SRCE )ι->uint;//every IsEnum/IsFlags table in each schema; returns how many loaded.
	α Parse( string query, jobject variables, const vector<sp<DB::AppSchema>>& schemas, bool returnRaw=true, SRCE )ε->RequestQL;
	α ParseM( string query, jobject variables, const vector<sp<DB::AppSchema>>& schemas, bool returnRaw=true, SRCE )ε->MutationQL;
	α ParseQuery( string query, jobject variables, const vector<sp<DB::AppSchema>>& schemas, bool returnRaw=true, SRCE )ε->TableQL;
	α ParseSubscriptions( string query, jobject variables, const vector<sp<DB::AppSchema>>& schemas, SRCE )ε->vector<Subscription>;
	α SelectStatement( const TableQL& qlTable, optional<bool> includeDeleted=nullopt, bool includeWhere=true )ε->DB::Statement;
}
namespace Jde{
	Ŧ QL::AsId( const jobject& o, SL sl )ε->T{
		return Json::AsNumber<T>( o, "id", sl );
	}
	Ŧ QL::AsId( const jvalue& v, SL sl )ε->T{
		return AsId<T>( Json::AsObject(v, sl), sl );
	}
}