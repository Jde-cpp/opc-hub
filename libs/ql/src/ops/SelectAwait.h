#pragma once
#include <jde/fwk/co/AnyAwait.h>
#include <jde/db/awaits/SelectAwait.h>
#include <jde/db/generators/Statement.h>
#include <jde/ql/types/TableQL.h>

namespace Jde::DB{ struct IDataSource; struct Statement; }
namespace Jde::QL{
	struct SelectAwait final: AnyAwait<jvalue>{
		using base = AnyAwait<jvalue>;
		using SubTables=flat_map<string,flat_multimap<uint,jobject>>;//tableName,pk, row
		SelectAwait( TableQL qlTable, UserPK executer, bool log, SL sl )ι: base{ sl }, _executer{executer}, _log{log}, _qlTable{move(qlTable)}{}
		SelectAwait( TableQL qlTable, DB::Statement sql, UserPK executer, bool log, SL sl )ι: base{ sl }, _executer{executer}, _log{log}, _qlTable{move(qlTable)}, _statement{move(sql)}{}
		α await_ready()ι->bool override;//__type and __schema are answered here, from the introspection documents - no db, no suspension.
		α await_resume()ε->jvalue;//the base's, plus the result trace when asked for.
	private:
		α Suspend()ι->void override{ Execute(); }
		α Execute()ι->VoidTask;//Hook::Select is an AnyAwait, so any task type serves; Query() then hands off to the DB::SelectAwait chain.
		α Query()ι->void;
		α Query( DB::Statement statement, SubTables subTables )ε->DB::SelectAwait::Task;
		α SelectSubTables( DB::Statement parentSql, vector<TableQL> tables, sp<DB::Table> parentTable, DB::WhereClause where )ε->DB::SelectAwait::Task;

		α DS()->DB::IDataSource&{ return *_ds; }
		sp<DB::IDataSource> _ds;
		uint _parentKeyIndex{};//#22: where the parent pk sits in the select - the client chooses the column order, so it is not always 0.
		UserPK _executer;
		bool _log;
		TableQL _qlTable;
		optional<DB::Statement> _statement;
	};
}