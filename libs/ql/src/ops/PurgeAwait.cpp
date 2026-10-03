#include "PurgeAwait.h"
#include <jde/fwk/co/AnyAwait.h>
#include <jde/db/IDataSource.h>
#include <jde/db/generators/Functions.h>
#include <jde/db/meta/Column.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/QLHook.h>
#include <jde/ql/types/MutationQL.h>

#define let const auto

namespace Jde::QL{
	//#25: authorize before the hook, not after it.  Execute() co_awaits Hook::PurgeBefore first and the only Authorize was
	//inside Statements(), which runs after - so on the gateway a purge hook had already removed the access provider (and its
	//PurgeFailure counterpart re-created it under a new id) before the caller was refused.  Mirrors UpdateAwait::await_ready:
	//an in-memory acl check needs no suspension, so an unauthorized purge never starts.
	α PurgeAwait::await_ready()ι->bool{
		try{
			THROW_IF( !_table, "Table not found for mutation '{}'.", _mutation.ToString() );
			_table->Authorize( Access::ERights::Purge, _userPK, _sl );
		}
		catch( Exception& e ){
			Refuse( move(e) );
		}
		return Refused();
	}
	α PurgeAwait::Statements( const DB::Table& table )ε->vector<DB::Sql>{
		table.Authorize( Access::ERights::Purge, _userPK, _sl );

		auto pk = table.Extends ? table.SurrogateKeys[0] : table.GetPK();
		DB::Sql sql{
			table.PurgeProcName.size() ? Ƒ( "{}( ? )", table.Schema->Prefix+table.PurgeProcName ) : Ƒ( "delete from {} where {}=?", table.SqlName(), pk->Name ),
			{ DB::Value{_mutation.AsNumber<uint>("id", _sl)} },
			!table.PurgeProcName.empty()
		};
		vector<DB::Sql> statements{ move(sql) };

		if( table.Extends ){
			let extendedPurge = Statements( *table.Extends );
			statements.insert( end(statements), begin(extendedPurge), end(extendedPurge) );
		}
		return statements;
	}

	//One coroutine - see InsertAwait::Execute for the shape and why:  the db failure is parked, its hook awaited after the
	//handler, and the failure rethrown through the up<runtime_error> so its dynamic type survives (#28).
	α PurgeAwait::Execute()ι->TAwait<jvalue>::Task{
		jvalue y; up<runtime_error> failure;
		try{
			auto before = co_await Hook::PurgeBefore( _mutation, _userPK );
			auto result0 = before ? before->if_contains( 0 ) : nullptr;
			if( result0 && result0->is_object() && Json::FindDefaultBool(result0->get_object(), "complete") ){//the hook did the purge.
				result0->get_object().erase( "complete" );
				y = jarray{ move(*result0) };
			}
			else{
				try{
					uint rowCount{};
					auto& ds = *_table->Schema->DS();
					for( auto& statement : Statements(*_table) )//TODO for mysql allow CLIENT_MULTI_STATEMENTS return ds->Execute( Str::AddSeparators(statements, ";"), parameters, sl );
						rowCount += co_await Any( ds.Execute(move(statement), _sl) );
					y = rowCount;//only a count that is not 0 is published (#47), and mysql counts a call by its last statement - so a purge proc ends on the delete of the row itself.
				}
				catch( runtime_error& e ){
					failure = ToExceptionPtr( move(e) );
				}
				if( failure )
					co_await Hook::PurgeFailure( _mutation, _userPK );//the db error is the one reported; a failure hook that itself throws replaces it.
				else
					co_await Hook::PurgeAfter( _mutation, _userPK );
			}
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
			co_return;
		}
		if( failure )
			ResumeExp( move(*failure) );
		else
			Resume( move(y) );
	}
}