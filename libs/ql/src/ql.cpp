#include <jde/ql/ql.h>
#include <jde/fwk/settings.h>
#include <jde/db/generators/Functions.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/QLAwait.h>
#include <jde/ql/LocalSubscriptions.h>
#include <jde/ql/types/MutationQL.h>
#include <jde/ql/types/Introspection.h>
#include <jde/ql/LocalQL.h>
#include <jde/db/IDataSource.h>
#include <jde/db/meta/Table.h>

#define let const auto

namespace Jde{
	α QL::LoadEnum( const DB::Table& table, SL sl )ι->bool{
		try{
			table.Schema->DS()->ReloadEnumSync( table, {}, sl );
			return true;
		}
		catch( runtime_error& e ){
			WARNT( ELogTags::QL, "Could not load the '{}' enum: {}  Its next render falls back to the blocking path.", table.Name, e.what() );
			return false;
		}
	}
	α QL::LoadEnums( const vector<sp<DB::AppSchema>>& schemas, SL sl )ι->uint{
		vector<string> loaded;
		for( let& schema : schemas ){
			for( let& [name, table] : schema->Tables ){
				if( (table->IsEnum() || table->IsFlags) && LoadEnum(*table, sl) )
					loaded.push_back( table->Name );
			}
		}
		DBGT( ELogTags::QL, "Loaded {} enum/flags lookups: {}", loaded.size(), Str::Join(loaded) );
		return (uint)loaded.size();
	}

	α QL::Configure( const vector<sp<DB::AppSchema>>& schemas )ε->void{
		for( let& schema : schemas ){
			if( let path = Settings::FindSV(schema->ConfigPath()+"/ql"); path )
				AddIntrospection( {Json::ReadJsonNet(*path, {})} );
		}
	}
}