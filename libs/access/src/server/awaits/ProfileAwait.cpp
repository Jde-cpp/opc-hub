#include <jde/access/server/awaits/ProfileAwait.h>
#include <jde/db/IDataSource.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/ql.h>
#include "../serverInternal.h"

#define let const auto
namespace Jde::Access::Server{
	α ProfileAwait::Suspend()ι->void{
		Execute();
	}
	α ProfileAwait::Execute()ι->DB::ExecuteAwait::Task{
		try{
			THROW_IF( !_executer || _executer.Value==UserPK::System || _guest, "profile mutations require an authenticated user." );
			using enum QL::EMutationQL;
			THROW_IF( _mutation.Type!=Create && _mutation.Type!=Update, "profile mutation '{}' is not supported.", _mutation.CommandName );
			let args = _mutation.ExtrapolateVariables();
			let url = Json::AsString( args, "url" );
			let value = Json::FindString( args, "value" );
			let& table = GetTable( "profiles" );
			let ds = table.Schema->DS();
			uint count{};
			if( !value )
				count = co_await ds->Execute( DB::Sql{ Ƒ("delete from {} where identity_id=? and url=?", table.DBName), vector<DB::Value>{{_executer.Value}, {url}} }, _sl );
			else if( let suffix = ds->Syntax().UpsertSuffix({"identity_id","url"}, {"value"}); suffix.size() ){
				//One statement, so re-saving an unchanged value cannot fall through to an insert the composite pk rejects
				//(MySQL counts rows *changed*, so that update answered 0 and the insert raised a 409), and two concurrent
				//first saves cannot race.  The count is stated rather than reported: MySQL answers 1 for an insert, 2 for
				//an update and 0 for a no-op, where sqlite and SQL Server always answer 1 - and the row does hold `value`.
				co_await ds->Execute( DB::Sql{ Ƒ("insert into {}(identity_id,url,value)values(?,?,?){}", table.DBName, suffix), vector<DB::Value>{{_executer.Value}, {url}, {*value}} }, _sl );
				count = 1;
			}
			else{//dialects with no upsert form (SQL Server), where rows-matched makes this branch correct.
				count = co_await ds->Execute( DB::Sql{ Ƒ("update {} set value=? where identity_id=? and url=?", table.DBName), vector<DB::Value>{{*value}, {_executer.Value}, {url}} }, _sl );
				if( !count )//2 concurrent first saves can both reach the insert - the composite pk fails one and the client's next save heals it.
					count = co_await ds->Execute( DB::Sql{ Ƒ("insert into {}(identity_id,url,value)values(?,?,?)", table.DBName), vector<DB::Value>{{_executer.Value}, {url}, {*value}} }, _sl );
			}
			jobject y;
			y["rowCount"] = count;
			//No notification:  profiles are per-user state, and the fan-out has no per-listener identity - it would hand this
			//url and value blob to every profileUpdated subscriber, i.e. any other logged-in user (access-review3 #16).
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}
