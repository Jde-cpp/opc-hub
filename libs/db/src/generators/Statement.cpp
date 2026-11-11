#include <jde/db/generators/Statement.h>
#include <jde/db/generators/Functions.h>
#include <jde/db/meta/Table.h>

namespace Jde::DB{
	Statement::Statement( SelectClause select, FromClause&& from, WhereClause&& where, string orderBy )ι:
		Select{move(select)},
		From{move(from)},
		Where{move(where)},
		OrderBy{move(orderBy)}
	{}

	α Statement::Empty()ι->bool{
		return Select.Columns.empty();
	}
	α Statement::Move()ε->Sql{
		Sql sql; sql.Text.reserve( 512 );
		sql.Text += Select.ToString( From.HasJoin() );
		sql.Text += '\n'+From.ToString();
		sql.Text += '\n'+Where.Move();
		if( !GroupBy.empty() )
			sql.Text += "\ngroup by "+GroupBy;
		if( !OrderBy.empty() )
			sql.Text += "\norder by "+OrderBy;
		if( _limit || _skip )
			sql.Text = From.GetFirstTable()->Syntax().Limit( sql.Text, _limit, _skip );
		sql.Params = move( Where.Params() );
		return sql;
	}
}