#pragma once
#include "../exports.h"
#include "FromClause.h"
#include "SelectClause.h"
#include "Sql.h"
#include "WhereClause.h"

namespace Jde::DB{
	struct ΓDB Statement final{
		Statement()ι=default;
		Statement( SelectClause select, FromClause&& from, WhereClause&& where, string orderBy={} )ι;
		α Empty()ι->bool;
		α Limit( uint limit )ι->void{ _limit=limit; }
		α Skip( uint skip )ι->void{ _skip=skip; }
		α Move()ε->Sql;
		SelectClause Select;
		FromClause From;
		WhereClause Where;
		string GroupBy;
		string OrderBy;
	private:
		uint _limit{};
		uint _skip{};
	};
}