#include "PurgeAwait.h"
#include <jde/fwk/co/AnyAwait.h>
#include <jde/db/IDataSource.h>
#include <jde/db/generators/Functions.h>
#include <jde/db/meta/Column.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/types/MutationQL.h>

#define let const auto

namespace Jde::QL{
	//#25: authorize first.  The only Authorize used to be inside Statements(), after the purge's before-hook had run - so on the
	//gateway the access provider was removed (and re-created under a new id) before the caller was refused.  Mirrors
	//UpdateAwait::await_ready:  an in-memory acl check needs no suspension, so an unauthorized purge never starts.  A custom
	//mutation that wraps this (the gateway's ServerCnnctnMAwait) authorizes before its own before-step for the same reason.
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
	//An extension's purge proc checks the kind itself (access_user_purge, access_group_purge).  Only the mutation's own table is
	//authorized (await_ready):  the row it extends is the same entity, and the extended tables - identities, permissions - declare
	//no ops, so authorizing them here would refuse everyone but the system.
	α PurgeAwait::Statements( const DB::Table& table, const optional<DB::Criteria>& kind )ε->vector<DB::Sql>{
		auto pk = table.Extends ? table.SurrogateKeys[0] : table.GetPK();
		DB::Sql sql{
			table.PurgeProcName.size() ? Ƒ( "{}( ? )", table.Schema->Prefix+table.PurgeProcName ) : Ƒ( "delete from {} where {}=?", table.SqlName(), pk->Name ),
			{ DB::Value{_mutation.AsNumber<uint>("id", _sl)} },
			!table.PurgeProcName.empty()
		};
		if( kind && table.PurgeProcName.empty() ){
			sql.Text += Ƒ( " and {}=?", kind->Column->Name );
			sql.Params.push_back( kind->Value );
		}
		vector<DB::Sql> statements{ move(sql) };

		if( table.Extends ){
			let extendedPurge = Statements( *table.Extends, table.GetSK0()->Criteria );
			statements.insert( end(statements), begin(extendedPurge), end(extendedPurge) );
		}
		return statements;
	}

	//One coroutine - see InsertAwait::Execute for the shape:  a db failure goes out through ResumeExp with its dynamic type (#28).
	α PurgeAwait::Execute()ι->TAwait<jvalue>::Task{
		uint rowCount{};
		try{
			auto& ds = *_table->Schema->DS();
			for( auto& statement : Statements(*_table) )//TODO for mysql allow CLIENT_MULTI_STATEMENTS return ds->Execute( Str::AddSeparators(statements, ";"), parameters, sl );
				rowCount += co_await Any( ds.Execute(move(statement), _sl) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
			co_return;
		}
		Resume( jvalue{rowCount} );//only a count that is not 0 is published (#47), and mysql counts a call by its last statement - so a purge proc ends on the delete of the row itself.
	}
}