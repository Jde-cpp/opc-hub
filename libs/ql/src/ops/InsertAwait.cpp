#include "InsertAwait.h"
#include <jde/fwk/co/AnyAwait.h>
#include <jde/db/IDataSource.h>
#include <jde/db/generators/Functions.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/names.h>
#include <jde/db/meta/Table.h>
#include "../types/JsonColumn.h"
#include "../qlInternal.h"

#define let const auto

namespace Jde::QL{
	using DB::Value;
	constexpr ELogTags _tags{ ELogTags::QL };
	Ω getEnumValue( const DB::Column& c, const JsonColumn& qlCol, const jvalue& v )->Value;

	InsertAwait::InsertAwait( sp<DB::Table> table, MutationQL m, UserPK executer, bool identityInsert, SL sl )ι:
		base{ move(table), move(m), executer, sl },
		_identityInsert{ identityInsert }
	{}

	α InsertAwait::await_ready()ι->bool{
		try{
			_table->Authorize( Access::ERights::Create, _userPK, _sl );
			CreateQuery( *_table, _mutation.ExtrapolateVariables() );
			if( _statements.empty() && !_table->HasCustomInsertProc )
				_result = jvalue{};//nothing to insert - finished here, and nothing is published.
		}
		catch( Exception& e ){
			Refuse( move(e) );
		}
		return Refused() || _result.has_value();
	}

	α InsertAwait::CreateQuery( const DB::Table& table, jobject input, bool nested )ε->void{
		for( let& [key,value] : input ){ //look for nested tables.
			if( auto nestedTable = value.is_object() ? table.Schema->FindTable(DB::Names::FromJson(DB::Names::ToPlural(key))) : nullptr; nestedTable ){
				let& o = value.get_object();
				if( o.size()==1 && o.contains("id") && nestedTable->SurrogateKeys.size() ){
					nestedTable->Authorize( Access::ERights::Read, _userPK, _sl );
					//#24: the same rule as the pk branch in AddStatement - `identity:{id:N}` on a table that *extends* identities
					//would pre-seed the back-fill and bind the new row to N, so the id this insert is about to create is dropped.
					if( table.Extends && table.Extends->Name==nestedTable->Name ){
						WARNT( ELogTags::QL, "[{}]ignoring the supplied '{}' id - an extension row takes its pk from the row it extends.", table.Name, string{key} );
					}
					else
						_nestedIds.emplace( nestedTable->SurrogateKeys[0]->Name, DB::Value{Json::AsNumber<uint>(o, "id")} );
				}
				else{
					nestedTable->Authorize( Access::ERights::Create, _userPK, _sl );
					CreateQuery( *nestedTable, o, true );
				}
			}
		}
		if( table.Extends && !nested )
			AddStatement( *table.Extends, input, table.GetSK0()->Criteria );
		AddStatement( table, input );
	}

	α InsertAwait::AddStatement( const DB::Table& table, const jobject& input, optional<DB::Criteria> criteria )ε->void{
		uint cNonDefaultArgs{};
		string missingColumnsError{};
		DB::InsertClause statement;
		vector<sp<DB::Column>> missingColumns;
		for( let& c : table.Columns ){
			if( !c->Insertable && (!_identityInsert || !c->IsPK()) ){
				//What the meta says no create sets, a caller naming it is refused:  it was dropped without a word.  A key keeps its own
				//rules - an extension's, sent, is ignored with a warning (#24, below).
				if( let name = JsonColumn{c}.MemberName(); !c->IsPK() && input.contains(name) )
					throw Exception{ _sl, ExceptionArgs{EHttpStatus::BadRequest}, "'{}' can't be set on a create:  {}.{} isn't insertable.", name, table.Name, c->Name };
				continue;
			}
			const JsonColumn qlCol{ c };
			Value value;
			let memberName = qlCol.MemberName();
			//#24: an extension's pk is the parent row's id - it comes from the insert a few lines up, never from the client.
			//Honouring `identityId:N` (or `id:N`) bound the new users row to an existing identity and left the identities row
			//this mutation just created an orphan;  ignoring it puts the column in _missingColumns, where Execute fills it in.
			let isExtensionKey = !_identityInsert && c->IsPK() && table.Extends;
			if( isExtensionKey && (input.if_contains(memberName) || input.if_contains("id")) )
				WARNT( ELogTags::QL, "[{}.{}]ignoring the supplied key - an extension row takes its pk from the row it extends.", table.Name, c->Name );
			if( let jvalue = isExtensionKey ? nullptr : input.if_contains(memberName); jvalue ){// calling a stored proc, so need all columns.
				++cNonDefaultArgs;
				value = c->IsEnum() && (jvalue->is_string() || jvalue->is_array())
					? getEnumValue( *c, qlCol, *jvalue )
					: qlCol.ToValue( *jvalue );
			}
			//#24: still ungated beyond the extension check above - the schema seed inserts its enum rows through this branch
			//(`createRights( name:"None", id:0 )`, which LocalQL::Upsert routes here *without* _identityInsert), so gating it on
			//_identityInsert as the fix note suggests fails startup.  The extension case, which is the one that binds a new row
			//to someone else's identity, is handled by isExtensionKey.
			else if( let id = !isExtensionKey && c->IsPK() ? input.if_contains("id") : nullptr; id )
				value = Value{ c->Type, *id };
			//#238: a key-spelled fk (`histTemplate:{ id:N }`) was read by CreateQuery, so it is an arg now - the missing-column fill
			//below never reaches a nullable fk, whose default is null.
			else if( auto nested = !isExtensionKey && c->PKTable ? _nestedIds.find(c->Name) : _nestedIds.end(); nested!=_nestedIds.end() ){
				++cNonDefaultArgs;
				value = nested->second;
			}
			else if( !c->Default && c->Insertable ){ //insertable=not populated by stored proc, may [not] be an extension record.
				THROW_IF( !c->PKTable, "No default for '{}' in '{}'. mutation='{}'", c->Name, table.Name, _mutation.ToString() );
				++cNonDefaultArgs;
				missingColumns.emplace_back( c );//also needs to be inserted, insert null for now.
			}
			else if( criteria && c->Name==criteria->Column->Name )
				value = criteria->Value;
			else
				value = *c->Default;
			statement.Add( c, value.Variant );
		}
		//`missingColumns` only ever grows next to a `++cNonDefaultArgs`, so its size can never exceed the count.  The guard is
		//therefore true only when cNonDefaultArgs>missingColumns.size()>=0, i.e. cNonDefaultArgs>0 - which is why the old
		//`(cNonDefaultArgs || missingColumns.size())` disjunct and the empty-InsertClause arm below could never do anything.
		if( cNonDefaultArgs!=missingColumns.size() ){//don't want to insert just identity_id in users table.
			if( /*table.SequenceColumn() &&*/ !_identityInsert ) //role does not have a seq column, but has a return param.
				statement.Add( table.SequenceColumn(), (uint)0ul );
			_statements.emplace_back( move(statement) );
			_missingColumns.emplace_back( move(missingColumns) );
		}
	}

	//One coroutine, the statements' QueryAwaits through Any().  A db failure goes out through ResumeExp, whose dynamic type survives
	//(#28: a duplicate key stays a DBException, and a 409).  A table with rules or a live side of its own wraps this from its
	//IQL's CustomMutation (the gateway's ServerCnnctnMAwait), which is where the hooks that used to run here went.
	α InsertAwait::Execute()ι->TAwait<jvalue>::Task{
		jarray y;
		try{
			auto& ds = *_table->Schema->DS();
			for( uint i=0; i<_statements.size(); ++i ){
				auto& statement = _statements[i];
				for( auto&& missingCol : _missingColumns[i] ){
					if( auto missingValue = _nestedIds.find(missingCol->Name); missingValue!=_nestedIds.end() )
						statement.SetValue( missingCol, move(missingValue->second) );
				}

				uint id{};
				if( _identityInsert )
					statement.IsStoredProc = false;
				auto sql = statement.Move();
				if( statement.IsStoredProc ){
					let result = co_await Any( ds.Query(move(sql), true, _sl) );
					for( let& row : result.Rows ){
						ASSERT( row.Size() );
						id = row.Size() ? row.Get<int32_t>( 0 ) : 0;
					}
					y.push_back( jobject{ {"id", id}, {"rowCount",result.RowsAffected} } );
				}else{
					if( _identityInsert && ds.Syntax().NeedsIdentityInsert() )
						sql.Text = Ƒ("SET IDENTITY_INSERT {0} ON;{1};SET IDENTITY_INSERT {0} OFF;", _table->SqlName(), sql.Text );
					let rowCount = ( co_await Any(ds.Query(move(sql), false, _sl)) ).RowsAffected;
					y.push_back( jobject{ {"rowCount",rowCount} } );
				}

				auto table = statement.Values.size() ? statement.Values.begin()->first->Table : nullptr;
				if( auto sequence = statement.Values.size() && table->SurrogateKeys.size() ? table->SurrogateKeys[0] : nullptr; sequence )
					_nestedIds.emplace( sequence->Name, id );
			}
			TRACE( "InsertAwait::Execute: {}", serialize(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
			co_return;
		}
		Resume( move(y) );
	}
	α getEnumValue( const DB::Column& c, const JsonColumn& qlCol, const jvalue& v )->Value{
		Value y;
		let values = GetEnumValues( qlCol.Table() );
		if( v.is_string() ){
			let enum_ = FindKey( values, string{v.get_string()} ); THROW_IF( !enum_, "Could not find '{}' for {}", string{v.get_string()}, qlCol.MemberName() );
			y = *enum_;
		}
		else
			y = Value{ c.Type, ToFlags(values, v.get_array(), qlCol.MemberName()) };
		return y;
	}
}