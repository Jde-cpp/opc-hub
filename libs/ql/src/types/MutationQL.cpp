#include <jde/ql/types/MutationQL.h>
#include <jde/db/names.h>
#include <jde/db/generators/Functions.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/DBSchema.h>
#include <jde/ql/ql.h>
#include <jde/ql/types/Parser.h>
#include "../qlInternal.h"

#define let const auto

namespace Jde::QL{
	α MutationQL::ParseCommand( sv commandName, SL _sl )ε->tuple<string,EMutationQL>{
		if( IsSystemMutation(commandName) )
			return { string{commandName}, EMutationQL::Execute };
		uint iType=0;
		for( ;iType<MutationQLNames.size() && !commandName.starts_with(MutationQLNames[iType].Verb); ++iType );
		if( iType==MutationQLNames.size() )
			throw Exception{ _sl, {ELogTags::QL}, "Could not find mutation {}", commandName };

		auto tableJsonName = string{ commandName.substr(MutationQLNames[iType].Verb.size()) };
		tableJsonName[0] = (char)tolower( tableJsonName[0] );

		return { move(tableJsonName), (EMutationQL)iType };
	}

	MutationQL::MutationQL( string commandName, jobject&& args, sp<jobject> variables, optional<TableQL>&& resultRequest, bool returnRaw, const vector<sp<DB::AppSchema>>& schemas, bool system )ε:
		Input{ move(args), move(variables) }, CommandName{move(commandName)}, ResultRequest{move(resultRequest)}, ReturnRaw{returnRaw}{
		std::tie(JsonTableName,Type) = ParseCommand( CommandName );
		DBTable = system || JsonTableName.empty() ? nullptr : DB::AppSchema::GetTablePtr( schemas, DB::Names::ToPlural(DB::Names::FromJson(JsonTableName)) );
	}

	//System mutations (updateLogSetting, ...) resolve no table, and the ASSERT this used to make did not stop the null
	//deref that followed - Access::Server::CustomMutation asks every mutation for its table name before anything else,
	//so `mutation updateLogSetting(...)` segfaulted the app server.  Callers all compare against a literal, and "" matches none of them.
	α MutationQL::TableName()Ι->string{ return DBTable ? DBTable->Name : string{}; }
	α MutationQL::ToString()Ι->string{
		auto args = serialize(Args);
		if( args.size()>3 && args[0]=='{' )
			args = args.substr(1, args.size()-2);
		return Ƒ( "{}({}){}", CommandName, move(args), ResultRequest ? ResultRequest->ToString() : "" );
	}

	α MutationQL::IsMutation( sv name )ι->bool{
		bool isMutation{ name=="mutation" || IsSystemMutation(name) };
		for( uint i=0; !isMutation && i<MutationQLNames.size(); ++i ){
			let& verb = MutationQLNames[i].Verb;
			isMutation = name.size()>verb.size() && name.starts_with( verb ) && isupper( (unsigned char)name[verb.size()] );
		}
		return isMutation;
	}
}