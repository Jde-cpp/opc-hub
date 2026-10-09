#pragma once

namespace Jde::DB{ struct Value; struct Table; }
namespace Jde::QL{
	struct ColumnQL; struct TableQL;
	//The library's cross-file helpers, declared once.  Each used to be re-declared by hand at every caller (ql-refactor B3).

	//ops/SelectAwait.cpp - the id->name map an enum/flags column renders through.  SelectEnumSync: cached, but a miss blocks -
	//LoadEnum (ql.h) fills the cache at startup so no request path takes that miss.
	α GetEnumValues( const DB::Table& table, SRCE )ε->flat_map<uint,string>;
	//ops/SelectAwait.cpp - #48: the one flags-array parser, shared by the insert and update paths so they cannot drift again.
	α ToFlags( const flat_map<uint,string>& values, const jarray& flags, sv memberName, SRCE )ε->uint;
	//ops/SelectAwait.cpp - a db value as json, rendered through the column's enum/flags/DateTime/Bit type when a column is given.
	α ValueToJson( DB::Value&& dbValue, const ColumnQL* pMember=nullptr )ι->jvalue;
	//types/Introspection.cpp - the __type and __schema documents SelectAwait answers from await_ready.
	α QueryType( const TableQL& typeTable, UserPK executer, SL sl )ε->jobject;
	α QuerySchema( const TableQL& schemaTable )ε->jobject;
	//types/Parser.cpp - a command SetSystemMutations registered.
	α IsSystemMutation( sv command )ι->bool;
}