#pragma once
#include <jde/db/Value.h>
#include <jde/db/generators/FromClause.h>
#include <jde/db/exports.h>

#define Φ ΓDB α
namespace Jde::DB{
	struct Column; struct FromClause; struct Table; struct WhereClause;
	struct ΓDB Sql final{ //the drivers are separate modules: EmbedParams is out-of-line in Jde.DB.
		string Text;
		vector<Value> Params;
		bool IsProc{ false };
		α EmbedParams()Ι->string;
	};

}
#undef Φ