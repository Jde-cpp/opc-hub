#include <jde/ql/ops/TablesAwait.h>
#include <jde/ql/IQL.h>
#include "SelectAwait.h"

#define let const auto
namespace Jde::QL{
	α TablesAwait::Execute()ι->TAwait<jvalue>::Task{
		optional<jvalue> y;
		try{
			for( auto&& table : _tables ){
				ASSERT( !_statement || _statement->From.Joins.size() );
				//a count per value is a top-level collection's:  refused here, before any handler, as a custom query builds its children itself.
				if( let grouped = table.GroupedChild(); grouped )
					throw Exception{ _sl, {ELogTags::QL, 0, EHttpStatus::BadRequest}, "[{}]sub-table '{}' takes no groupBy:  a count per value is a top-level collection's.", table.JsonName, grouped->JsonName };
				optional<jvalue> result;
				let returnRaw = table.ReturnRaw && _tables.size()==1;
				auto returnName = table.ReturnName();
				if( _ql ){
					//route on the name _before_ handing the table over:  StatusQuery/LogSettingsQuery/LogQuery take it by rvalue, so nothing
					//after the call may read `table` (or a reference into it) again - hence the branches that move are terminal.
					let& jsonName = table.JsonName;
					let isStatus = jsonName=="status";
					//#44: the exact names, not a prefix.  `starts_with("log")` had already had to carve `logLevel` out by hand, and the next
					//`log*` table would have been handed to LogQuery the same way - which takes the table by rvalue, so there is no recovering
					//from it.  These four are every spelling any caller sends; a real table named `log*` is now simply not one of them.
					let isLogSettings = jsonName=="logSetting" || jsonName=="logSettings";
					let isLog = jsonName=="log" || jsonName=="logs";
					if( isStatus )
						result = _ql->StatusQuery( move(table), _creds, _sl );
					else if( isLogSettings || isLog ){
						auto await = isLogSettings ? _ql->LogSettingsQuery( move(table), _creds, _sl ) : _ql->LogQuery( move(table), _creds, _sl );
						THROW_IF( !await, "[{}]{} returned null.", returnName, isLogSettings ? "LogSettingsQuery" : "LogQuery" ); //the table is gone - there is nothing left to fall back on.
						result = co_await *await;
					}
					else if( auto await = jsonName=="__schema" ? nullptr : _ql->CustomQuery( table, _creds, _sl ); await )
						result = co_await *await;
				}
				if( !result ){
					if( _statement )
						result = co_await SelectAwait{ move(table), *_statement, _creds.UserPK(), true, _sl };
					else
						result = co_await SelectAwait{ move(table), _creds.UserPK(), true, _sl };
				}
				if( returnRaw )
					y = move( *result );
				else{
					if( !y )
						y = jobject{};
					y->get_object()[move(returnName)] = move( *result );
				}
			}
			Resume( y.value_or(jvalue{}) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}