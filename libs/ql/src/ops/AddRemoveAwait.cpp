#include "AddRemoveAwait.h"
#include <jde/fwk/co/AnyAwait.h>
#include <jde/db/IDataSource.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/names.h>
#include <jde/db/generators/Functions.h>
#include <jde/db/meta/Column.h>
#include <jde/db/meta/Table.h>

#define let const auto

namespace Jde::QL{
	struct ChildParentParams final{ sp<DB::Column> ParentCol; sp<DB::Column> ChildColumn; DB::Value ParentParam; vector<DB::Value> ChildParams; };

	Ω getChildParentParams( sp<DB::Column> parentCol, sp<DB::Column> childCol, const jobject& input )ε->ChildParentParams{
		ChildParentParams params{ parentCol, childCol };
		let parentColName = DB::Names::ToJson( parentCol->Name );
		if( let p = Json::FindNumber<uint>(input, parentColName); p )
			params.ParentParam = DB::Value{ parentCol->Type, *p };
		else if( let p = Json::FindNumber<uint>(input, "id"); p )
			params.ParentParam = DB::Value{ parentCol->Type, *p };
		else
			THROW( "Could not find '{}' or id in '{}'", parentColName, serialize(input) );
		let childColName = DB::Names::ToJson( childCol->Name );
		if( let p = Json::FindNumber<uint>(input, childColName); p )
			params.ChildParams.emplace_back( DB::Value{childCol->Type, *p} );
		else if( let p = Json::FindArray(input, childColName); p ){
			for( let& v : *p )
				params.ChildParams.emplace_back( DB::Value{childCol->Type, Json::AsNumber<uint>(v)} );
		}
		else
			THROW( "Could not find '{}' in '{}'", childColName, serialize(input) );

		return params;
	};

	//#49: the column list has to grow with the placeholders.  It was fixed at the two mapped columns while `input:{…}` appended
	//a `?` per extra value, so any use of the feature emitted `insert into t(a,b)values(?,?,?)` - a column-count error, every
	//time.  Nothing in tree sends `input:` on an add, which is why a branch that could never work went unnoticed.
	Ω addSql( const DB::Table& table, const jobject& input, vector<DB::Value>& extraParams )ε->string{
		let& parentId = table.Map->Parent->Name; let& childId = table.Map->Child->Name;
		string extraColumns;
		string extraParamsString;
		if( auto defParams = Json::FindObject(input, "input"); defParams ){
			for( let& [name,value] : *defParams ){
				if( name=="id" || name==parentId || name==childId )
					continue;
				auto pColumn = table.GetColumnPtr( DB::Names::FromJson(name) );
				extraColumns += ","+pColumn->Name;
				extraParamsString += ",?";
				extraParams.emplace_back( DB::Value{pColumn->Type, value} );
			}
		}
		return Ƒ( "insert into {}({},{}{})values(?,?{})", table.SqlName(), parentId, childId, extraColumns, extraParamsString );
	}

	//One coroutine, the statements through Any().  A proc-backed table (access `roles`, its add_proc/remove_proc) has no map and is
	//refused here:  its add and remove are its IQL's CustomMutation's (RoleMAwait).  A map table with a rule of its own (access
	//`groups`) is wrapped there too (GroupAddMAwait), the stock add running as this.
	α AddRemoveAwait::Execute()ι->TAwait<jvalue>::Task{
		uint rowCount{};
		try{
			_table->Authorize( Access::ERights::Update, _userPK, _sl );
			let isAdd = _mutation.Type==EMutationQL::Add;
			ASSERT( isAdd || _mutation.Type==EMutationQL::Remove );
			THROW_IF( !_table->Map, "'{}' does not support add/remove.", _table->Name );
			let& map = *_table->Map;
			let input = _mutation.ExtrapolateVariables();
			let params = getChildParentParams( map.Parent, map.Child, input );//a missing column is refused here, by name (access GroupTests).
			auto& ds = *_table->Schema->DS();
			if( isAdd ){
				vector<DB::Value> extraParams;
				let sql = addSql( *_table, input, extraParams );
				for( let& p : params.ChildParams ){
					vector<DB::Value> values{ params.ParentParam, p };
					values.insert( end(values), begin(extraParams), end(extraParams) );
					rowCount += co_await Any( ds.Execute({sql, move(values)}, _sl) );
				}
			}
			else{
				let sql = Ƒ( "delete from {} where {}=? and {}=?", _table->SqlName(), map.Parent->Name, map.Child->Name );
				for( let& p : params.ChildParams )
					rowCount += co_await Any( ds.Execute({sql, {params.ParentParam, p}}, _sl) );
			}
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
			co_return;
		}
		Resume( jvalue{rowCount} );
	}
}