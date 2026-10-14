#include "MySqlStatements.h"
#include "../../../src/meta/InformationSchemaSql.h"

namespace Jde::DB::MySql{
	using std::endl;
	α Ddl::ColumnSql( sv tablePrefix, bool addTable )ι->string{
		std::ostringstream os;
		os << "select	t.TABLE_NAME, COLUMN_NAME, ORDINAL_POSITION, COLUMN_DEFAULT, IS_NULLABLE, COLUMN_TYPE, CHARACTER_MAXIMUM_LENGTH, EXTRA='auto_increment' is_identity, 0 is_id, NUMERIC_PRECISION, NUMERIC_SCALE, EXTRA" << endl //EXTRA last (#241): DEFAULT_GENERATED marks an expression default.
			<< "from		INFORMATION_SCHEMA.TABLES t" << endl
			<< "\tjoin INFORMATION_SCHEMA.COLUMNS c using(TABLE_CATALOG, TABLE_SCHEMA, TABLE_NAME)" << endl
			<< "\twhere t.TABLE_SCHEMA=?" << endl
			<< "\tand\tt.TABLE_TYPE in ('BASE TABLE', 'VIEW')" << endl;
		if( addTable )
			os << "\tand t.TABLE_NAME=?" << endl;
		else if( tablePrefix.size() )
			os << "\tand t.TABLE_NAME like ?" << endl;
		os << "order by t.TABLE_NAME, ORDINAL_POSITION" << endl;
		return os.str();
	}

	α Ddl::ForeignKeySql( bool addSchema )ι->string{
		//Both con joins carry the schema (see InformationSchemaSql.h); the pk side needs the COLLATE - the
		//REFERENTIAL_CONSTRAINTS and KEY_COLUMN_USAGE name columns are collated differently on mysql.
		return InformationSchema::ForeignKeySql(
			"con.CONSTRAINT_SCHEMA=fk.CONSTRAINT_SCHEMA and con.CONSTRAINT_NAME=fk.CONSTRAINT_NAME and con.TABLE_NAME=fk.TABLE_NAME",
			"pk.CONSTRAINT_SCHEMA COLLATE utf8_general_ci=con.UNIQUE_CONSTRAINT_SCHEMA and pk.CONSTRAINT_NAME COLLATE utf8_general_ci=con.UNIQUE_CONSTRAINT_NAME and pk.ORDINAL_POSITION=fk.ORDINAL_POSITION and pk.TABLE_NAME=con.REFERENCED_TABLE_NAME",
			addSchema ? "where pk.TABLE_SCHEMA=?\n  and fk.TABLE_SCHEMA=?\n" : "" );
	}

	α Ddl::IndexSql( sv tablePrefix, bool addTable )ι->string{
		std::ostringstream os;
		os << "SELECT TABLE_NAME, INDEX_NAME, COLUMN_NAME, NON_UNIQUE, SEQ_IN_INDEX FROM INFORMATION_SCHEMA.STATISTICS WHERE TABLE_SCHEMA = ?";
		if( addTable )
			os << " and TABLE_NAME=?";
		else if( tablePrefix.size() )
			os << " and TABLE_NAME like ?";
		os << " order by TABLE_NAME, INDEX_NAME, SEQ_IN_INDEX";
		return os.str();
	}

	α Ddl::ProcSql( bool addSchema )ι->string{ return InformationSchema::ProcSql( addSchema ); }
}