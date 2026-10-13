#include "IServerMeta.h"
#include <jde/db/IDataSource.h>
#include <jde/db/generators/Syntax.h>
#include <jde/db/meta/Table.h>
#include "ddl/ColumnDdl.h"

namespace Jde::DB{
	α IServerMeta::SetDefault( const Table& table, const ColumnDdl& column, const Value& dflt )Ε->void{
		for( auto&& statement : _ds.Syntax().SetDefault(table.SqlName(), column.Name, dflt, column.DefaultName) )
			_ds.ExecuteSync( {move(statement)} );
	}
}