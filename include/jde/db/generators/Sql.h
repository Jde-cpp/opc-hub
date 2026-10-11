#pragma once
#include <jde/db/Value.h>
#include <jde/db/exports.h>

namespace Jde::DB{
	struct ΓDB Sql final{ //the drivers are separate modules: EmbedParams is out-of-line in Jde.DB.
		string Text;
		vector<Value> Params;
		bool IsProc{ false };
		α EmbedParams()Ι->string;
	};

}