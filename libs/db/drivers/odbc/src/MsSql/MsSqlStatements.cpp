#include "MsSqlStatements.h"
#include "../../../../src/meta/InformationSchemaSql.h"

namespace Jde::DB::MsSql::Sql
{
	using std::endl;
	α ColumnSql( bool addTable )ι->string{
		std::ostringstream os;
		os << "select tables.name table_name, columns.name column_name, columns.column_id, constraints.definition, columns.is_nullable, types.name, columns.max_length, is_identity, case when pk_columns.column_id is null then 0 else 1 end, columns.precision, columns.scale, constraints.name" << endl //constraints.name last (#241): what a changed default drops first.
			<< "from sys.schemas" << endl
			<< "join sys.objects tables on tables.schema_id=schemas.schema_id and tables.type in ('U','V') and tables.is_ms_shipped=0" << endl
			<< "join sys.columns on tables.object_id=columns.object_id" << endl
			<< "join sys.types on columns.user_type_id=types.user_type_id" << endl//user_type_id, not system_type_id: the latter is shared by base+alias types (nvarchar/sysname) and duplicates every such column.
			<< "left join sys.indexes pk on tables.object_id=pk.object_id and pk.is_primary_key=1" << endl
			<< "left join sys.index_columns pk_columns on pk_columns.object_id=pk.object_id and pk_columns.index_id=pk.index_id and pk_columns.column_id=columns.column_id" << endl//column-specific, so is_id marks only PK members, not every column of a PK table.
			<< "left join sys.default_constraints constraints on columns.default_object_id=constraints.object_id" << endl
			<< "where schemas.name=?" << endl;
		if( addTable )
			os << "\tand tables.name like ?" << endl;
		os << "order by tables.name, columns.column_id" << endl;
		return os.str();
	}

	α IndexSql( bool addTable, bool addPrefix )ι->string{
		std::ostringstream os;
		os << "select tables.name table_name, indexes.name index_name, columns.name column_name, case when indexes.is_unique=0 then CAST(1 AS BIT) else CAST(0 AS BIT) end non_unique, index_columns.key_ordinal" << endl
			<< "from sys.indexes" << endl
			<< "\tjoin sys.index_columns ON  indexes.object_id = index_columns.object_id and indexes.index_id = index_columns.index_id" << endl
			<< "\tjoin sys.columns ON index_columns.object_id = columns.object_id and index_columns.column_id = columns.column_id" << endl
			<< "\tjoin sys.tables ON indexes.object_id = tables.object_id" << endl
			<< "\tjoin sys.schemas on tables.schema_id=schemas.schema_id" << endl
			<< "WHERE tables.is_ms_shipped = 0" << endl

			<< "\t\tand schemas.name=?" << endl;
		if( addTable )
			os << " and tables.name=?" << endl;
		else if( addPrefix )
			os << " and tables.name like ?" << endl;
		os << "ORDER BY tables.name, indexes.name, index_columns.key_ordinal" << endl;
		return os.str();
	}

	α ForeignKeySql( bool addSchema )ι->string{ //both con joins carry the schema - see InformationSchemaSql.h.
		return InformationSchema::ForeignKeySql(
			"con.CONSTRAINT_SCHEMA=fk.CONSTRAINT_SCHEMA and con.CONSTRAINT_NAME=fk.CONSTRAINT_NAME",
			"con.UNIQUE_CONSTRAINT_SCHEMA=pk.CONSTRAINT_SCHEMA and con.UNIQUE_CONSTRAINT_NAME=pk.CONSTRAINT_NAME and fk.TABLE_SCHEMA=pk.TABLE_SCHEMA and fk.ORDINAL_POSITION=pk.ORDINAL_POSITION",
			addSchema ? "where pk.TABLE_SCHEMA=?\n" : "" );
	}
	α ProcSql( bool addSchema )ι->string{ return InformationSchema::ProcSql( addSchema ); }
}