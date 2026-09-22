#pragma once
#include <jde/db/Key.h>
#include "TableQL.h"
#include "../usings.h"

namespace Jde::DB{ struct AppSchema; }
namespace Jde::QL{
	struct TableQL;
	struct MutationQL final : Input{
		MutationQL( string commandName, jobject&& args, sp<jobject> variables, optional<TableQL>&& resultRequest, bool returnRaw, const vector<sp<DB::AppSchema>>& schemas, bool system )ε;
		Ω IsMutation( sv name )ι->bool;
		Ω ParseCommand( sv name, SRCE )ε->tuple<string,EMutationQL>;
		α TableName()Ι->string; //json name=user returns users
		α JTableName()Ι->string override{ return JsonTableName; }
		α ToString()Ι->string;

		string CommandName;
		sp<DB::Table> DBTable;
		string JsonTableName;
		optional<TableQL> ResultRequest;
		bool ReturnRaw;
		EMutationQL Type;
		bool AddIfMissing{};//LocalQL::Upsert's adds - the seeds':  an add whose target already holds it changes nothing, so an admin's edit to a seeded grant survives the next start (reviews/m3-closing.md #12).  Only RoleMAwait's permission add reads it; a member add skips an existing member anyway.
	};
}