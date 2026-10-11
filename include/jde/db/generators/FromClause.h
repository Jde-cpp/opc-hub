#pragma once
#include "../exports.h"

namespace Jde::DB{
	struct Column; struct Table; struct WhereClause;

	struct ΓDB Join final{
		Join( sp<Column> from )ι:From{move(from)}{};
		Join( sp<Column> from, sp<Column> to, bool inner=false )ι:From{move(from)}, To{move(to)}, Inner{inner}{};
		Join( sp<Column> from, string fromAlias, sp<Column> to, string toAlias, bool inner={} )ι;
		sp<Column> From;
		string FromAlias;
		sp<Column> To;
		string ToAlias;
		bool Inner{};
	};
	struct ΓDB FromClause final{
		FromClause()=default;
		FromClause( const sp<Table>& t )ε;
		FromClause( vec<sp<Table>>& tables, SRCE )ε;
		FromClause( Join&& j )ι:Joins{move(j)}{};
		α ToString()Ε->string;
		α operator+=( Join&& join )ι->FromClause&;
		α Add( sp<Column> from, sp<Column> to, bool inner=true )ι->void;
		α TryAdd( Join&& join )ι->void;

		α Contains( sv tableName )ι->bool;
		α Empty()Ι->bool;
		α GetColumnPtr( sv name, SL sl )Ε->sp<Column>;
		α GetFirstTable( SRCE )Ε->sp<Table>;
		α HasJoin()Ι->bool{ return Joins.size() && Joins[0].To; }
		vector<Join> Joins;
	};
}