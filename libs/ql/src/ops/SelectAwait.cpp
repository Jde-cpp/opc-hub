#include "SelectAwait.h"
#include <jde/fwk/chrono.h>
#include <jde/db/IDataSource.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/ql.h>
#include <jde/ql/QLHook.h>
#include "../qlInternal.h"

#define let const auto

namespace Jde::QL{
	using namespace DB::Names;

	//#48: the one flags-array parser.  Insert had its own, which `continue`d past a non-string element instead of refusing it -
	//so `allowed:[1,2]`, which no name lookup could ever match, produced flags==0 and a silently unusable row, while the same
	//array through the update path threw.  Both now come here, so they cannot drift again.
	α ToFlags( const flat_map<uint,string>& values, const jarray& flags, sv memberName, SL sl )ε->uint{
		uint y{};
		for( let& jflag : flags ){
			let name = Json::AsString( jflag );//throws for anything that is not a flag name.
			let flag = FindKey( values, name ); THROW_IFSL( !flag, "Could not find '{}' for {}", name, memberName );
			y |= *flag;
		}
		return y;
	}
	α GetEnumValues( const DB::Table& table, SL sl )ε->flat_map<uint,string>{
		return table.Schema->DS()->SelectEnumSync<uint,string>( table, Cache::DefaultDuration(), sl );
	}
	α numberToJson( const DB::Value& dbValue, const DB::Column& c )ι->jvalue{
		jvalue y;
		if( c.Type==DB::EType::DateTime ){
			let v = dbValue.get_number<uint>();
			y = ToIsoString( TimePoint{std::chrono::seconds(v)} );//ToIsoString already ends in 'Z'.
		}
		else if( c.PKTable && (c.IsEnum() || c.IsFlags()) ){
			flat_map<uint,string> values;
			try{
				values = GetEnumValues( *c.PKTable );
			}
			catch( const runtime_error& e ){
				WARNT( ELogTags::QL, "[{}]enum values could not be loaded - rendering the numeric id: {}", c.PKTable->Name, e.what() );
			}
			let value = dbValue.Get<uint>();
			if( c.IsFlags() ){
				jarray flags;
				auto remainingFlags = value;
				for( uint iFlag=0x1; remainingFlags!=0; iFlag <<= 1 ){
					if( (remainingFlags & iFlag)==0 )
						continue;
					if( let flag = values.find(iFlag); flag!=values.end() )
						flags.emplace_back( flag->second );
					else
						flags.emplace_back( std::to_string(iFlag) );
					remainingFlags -= iFlag;
				}
				y = flags;
			}
			else //enum but not flags
				y = Find( values, value ).value_or( std::to_string(value) );
		}
		else if( c.Type==DB::EType::Bit )
			y = dbValue.ToUInt()!=0;
		else
			y = dbValue.ToJson();
		return y;
	}

	α ValueToJson( DB::Value&& dbValue, const ColumnQL* pMember )ι->jvalue{
		using enum DB::EValue;
		jvalue json;
		switch( dbValue.Type() ){
			case UInt64: case Int32: case Int64: json = pMember && pMember->DBColumn ? numberToJson( dbValue, *pMember->DBColumn ) : dbValue.ToJson(); break;
			default: json = dbValue.Move();
		}
		return json;
	};

	α SelectAwait::await_ready()ι->bool{
		if( _log )
			LOGSL( ELogLevel::Trace, _sl, ELogTags::QL, "{}.", _qlTable.ToString() );
		try{
			if( _qlTable.JsonName=="__type" )
				_result = QueryType( _qlTable, _executer, _sl );
			else if( _qlTable.JsonName=="__schema" )
				_result = QuerySchema( _qlTable );
		}
		catch( Exception& e ){ _error = e.Move(); }//#28: the dynamic type, for the awaiter's rethrow.
		catch( runtime_error& e ){ _error = mu<Exception>( move(e) ); }
		return _result || _error;
	}
	α SelectAwait::Execute()ι->VoidTask{
		try{
			if( auto j = _statement ? optional<jvalue>{} : co_await QL::Hook::Select( _qlTable, _executer, _sl ); j.has_value() )
			  Resume( move(*j) );
			else
				Query();
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	//The name guesses come first - they pick the right column when a table has two fks to the same parent, which FindFK
	//(first match by pk table) can not.  But a guess is only an fk if the column it lands on has a pk table:  nesting a table
	//under itself spells its own pk ("provider_types" -> provider_type_id), whose PKTable is null.  FindFK then covers the
	//names the guess can not spell - an extension whose base pluralises irregularly (objects extends node_ids -> node_id_id).
	α findFK( const DB::Table& dbTable, string qlName )ε->sp<DB::Column>{
		auto fk = dbTable.FindColumn( qlName ); //members.
		if( !fk )
			fk = dbTable.FindColumn( ToSingular(qlName)+"_id" );//member_id
		return fk && fk->PKTable ? fk : dbTable.FindFK( qlName );
	}

	Ω addColumn( const ColumnQL& c, const TableQL& qlTable, const DB::Table& dbTable, DB::Statement& statement, bool excludeId )ε->void{
		auto pk = dbTable.FindPK();
		let isPK = c.JsonName=="id";
		if( isPK && dbTable.Extends )
			pk = dbTable.Extends->GetPK();
	 	THROW_IF( isPK && !pk, "[{}]No id column.", qlTable.JsonName );

		auto columnName = isPK ? pk->Name : FromJson( c.JsonName );
		if( excludeId && isPK )
			return;
		auto dbColumn = isPK ? pk : dbTable.FindColumn( columnName );//want main table's pk, not extension's fk.  group maybe empty
		if( dbColumn && dbColumn->Table->Name!=dbTable.Name ){ //extension table.
			auto fk = findFK( dbTable, dbColumn->Table->Name ); THROW_IF( !fk, "[{}]Could not find the column joining it to '{}'.", dbTable.Name, dbColumn->Table->Name ); //a null To can not be joined - say so instead of building the statement around it.
			statement.From.TryAdd( {dbColumn->Table->GetPK(), fk, false} );//extension table & main table are joined on same name.
		}
		else if( !dbColumn ){
			if( let pEnum = dbTable.FindColumn(columnName+"_id"); pEnum && pEnum->PKTable ){//enumeration dbColumn==provider_id
				auto pFKTable = pEnum->PKTable->QLView
					? pEnum->PKTable->QLView //prvoiders_ql
					: pEnum->PKTable; //another enum
				statement.From.TryAdd( {pEnum, pFKTable->GetPK(), !pEnum->IsNullable} );
				dbColumn = pFKTable->GetColumnPtr( "name" );
			}
			else if( columnName== "count" )
				dbColumn = DB::Column::Count();
			THROW_IF( !dbColumn, "Could not find column '{}.{}'", dbTable.Name, columnName );
		}

		statement.Select.TryAdd( dbColumn );
		c.DBColumn = dbColumn;
	}

	Ω columnSql( const TableQL& qlTable, const DB::Table& dbTable, bool excludeId, DB::Statement& statement, optional<bool> includeDeleted=nullopt, bool includeWhere=true )ε->void{
		for( let& c : qlTable.Columns )
			addColumn( c, qlTable, dbTable, statement, excludeId );

		if( includeWhere )
			statement.Where += QL::ToWhereClause( qlTable, dbTable, includeDeleted.value_or(statement.Select.FindColumn("deleted")!=nullptr) );
		for( let& qlChild : qlTable.Tables ){
			THROW_IF( !qlChild.DBTable(), "[{}]Unknown sub-table '{}'.", dbTable.Name, qlChild.JsonName ); //only a system-named child of a system parent resolves to no view, and none of those reach here.
			auto pFK = findFK( dbTable, qlChild.DBTable()->Name ); //members.
			if( pFK ){
				auto pkTable = pFK->PKTable;
				if( pkTable->QLView )
					pkTable = pkTable->QLView;
				statement.From.TryAdd( {pFK, pkTable->GetPK(), !pFK->IsNullable} );
				columnSql( qlChild, *pkTable, false, statement, includeDeleted, includeWhere );
			}
		}
	}

	α findMap( const DB::Table& dbTable, string qlName )ε->optional<DB::Table::ParentChildMap>{
		optional<DB::Table::ParentChildMap> map;
		if( auto mapTable = find_if(dbTable.Children, [&](auto& c){return c->Map->Child->PKTable->Name==qlName;}); mapTable!=dbTable.Children.end() ) //role_members
			map = (*mapTable)->Map;//permissionId
		return map;
	}

	α addSubTables( const TableQL& parentQL, const SelectAwait::SubTables& subTables, jobject& parent, uint parentId )ι->void{
		for( let& qlTable : parentQL.Tables ){
			let subPlural = qlTable.JsonName.ends_with( "s" );
			if( subPlural )
				parent[qlTable.JsonName] = jarray{};
			let pResultTable = subTables.find( qlTable.JsonName );
			if( pResultTable==subTables.end() )
				continue;
			let& subResults = pResultTable->second;
			//#22: no fallback.  This used to attach the lowest-keyed parent's children to a row whose id it could not read,
			//which is every row when the pk was not column 0 - wrong children rather than none.
			auto range = subResults.equal_range( parentId );
			for( auto pRow = range.first; pRow!=range.second; ++pRow ){
				if( subPlural )
					parent[qlTable.JsonName].get_array().emplace_back( pRow->second );
				else
					parent[qlTable.JsonName] = pRow->second;
			}
		};
	}

	α SelectAwait::SelectSubTables( DB::Statement parentSql, vector<TableQL> tables, sp<DB::Table> parentTable, DB::WhereClause where )ε->DB::SelectAwait::Task{
		SubTables subTables;
		try{
			for( auto& qlTable : tables ){//members
				THROW_IF( !qlTable.DBTable(), "[{}]Unknown sub-table '{}'.", parentTable->Name, qlTable.JsonName );
				auto fk = findFK( *parentTable, qlTable.DBTable()->Name );
				DB::Statement statement;
				if( auto map = fk ? fk->Table->Map : nullopt; map ){ //members.member_id  if not a map, get it in main table.
					statement.Select.TryAdd( fk->Table->SurrogateKeys[0] );//add identity_id of members for result.
					columnSql( qlTable, *fk->PKTable, false, statement );
					statement.From.TryAdd( {fk->PKTable->GetPK(), fk, true} ); //identities join members
				}
				else if( auto map = findMap(*parentTable, qlTable.DBTable()->Name); map ){ //role_members
					auto parent = map->Parent; //role_id
					auto child = map->Child; //permission_id
					statement.Select.TryAdd( parent );
					columnSql( qlTable, *child->PKTable, false, statement ); //select id, allowed, denied
					statement.From.TryAdd( {parent->PKTable->GetPK(), parent, true} ); //from roles join role_members
					statement.From.TryAdd( {child, child->PKTable->GetPK(), true} ); //join permissions
				}
				else
					continue; //THROW_IF( !fk, "Could not find fk for {}->{}", parentTable.Name, qlTable.DBTable->Name );

				statement.Where += where;
				auto& jrow = subTables.emplace( qlTable.JsonName, flat_multimap<uint,jobject>{} ).first->second;
				auto sql = statement.Move();
				auto rows = co_await DS().SelectAsync( move(sql) );
				for( auto&& row : rows ){
					jobject jSubRow;
					let rowToJson2 = [&row]( const vector<ColumnQL>& columns, jobject& toRow ){
						int i = 1;//first should be pk of parent table.
						for( let& c : columns )
							toRow[c.JsonName] = ValueToJson( move(row[i++]), &c );
					};
					rowToJson2( qlTable.Columns, jSubRow );
					if( fk ){
						for( let& childTable : qlTable.Tables ){
							jobject jChildTable;
							rowToJson2( childTable.Columns, jChildTable );
							jSubRow[childTable.JsonName] = jChildTable;
						}
					}
					jrow.emplace( row.Get<uint>(0), jSubRow );
				}
			}
			Query( move(parentSql), move(subTables) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	//#10: every table the client named is a read, not just the root - columnSql joins the fk children into this statement and
	//SelectSubTables selects the rest, and neither authorized anything.  One walk covers both, before the data source is touched.
	α SelectAwait::Authorize( const TableQL& qlTable )ε->void{
		if( let dbTable = qlTable.DBTable(); dbTable )
			dbTable->Authorize( Access::ERights::Read, _executer, _sl );
		for( let& child : qlTable.Tables )
			Authorize( child );
	}
	//#22: sub-table rows are keyed by the parent's pk, and columnSql emits the select in the order the *client* asked for its
	//columns - so the pk is at 0 only by luck.  Find it, and add it if the client did not ask for it at all: without a key
	//there is nothing to attach the children to.  ToJson skips a column no ColumnQL claims, so the extra one does not surface.
	Ω parentKeyIndex( DB::Statement& statement, const DB::Table& dbTable )ε->uint{
		let pk = dbTable.Extends ? dbTable.Extends->GetPK() : dbTable.GetPK();
		for( uint i=0; i<statement.Select.Columns.size(); ++i ){
			if( let col = get_if<DB::AliasCol>(&statement.Select.Columns[i]); col && col->Column && *col->Column==*pk )
				return i;
		}
		statement.Select.TryAdd( pk );
		return statement.Select.Columns.size()-1;
	}
	α SelectAwait::Query()ι->void{
		try{
			let dbTable = _qlTable.DBTable();
			THROW_IF( !dbTable, "No DB table for '{}'", _qlTable.JsonName );
			Authorize( _qlTable );
			_ds = dbTable->Schema->DS();
			auto statement = _statement ? move(*_statement) : SelectStatement( _qlTable );
			if( _qlTable.Tables.size() )
				_parentKeyIndex = parentKeyIndex( statement, *dbTable );
			auto where = statement.Where;//copied before the move below - argument evaluation order is unspecified.
			SelectSubTables( move(statement), _qlTable.Tables, dbTable, move(where) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	α SelectAwait::Query( DB::Statement statement, SubTables subTables )ε->DB::SelectAwait::Task{
		jvalue y;
		if( _qlTable.IsPlural() )
			y = jarray{};
		try{
			auto rows = co_await DS().SelectAsync( statement.Move(), _sl );
			for( auto&& row : rows ){
				auto jrow = _qlTable.ToJson( row, statement.Select.Columns );
				if( subTables.size() )
					addSubTables( _qlTable, subTables, jrow, row.Get<uint>(_parentKeyIndex) );
				if( _qlTable.IsPlural() )
					y.get_array().emplace_back( move(jrow) );
				else
					y = move( jrow );
			}
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	α SelectAwait::await_resume()ε->jvalue{
		auto y = base::await_resume();
		if( _log )
			LOGSL( ELogLevel::Trace, _sl, ELogTags::QL, "SelectAwaitResult: {}", serialize(y) );
		return y;
	}
}
namespace Jde{
	α QL::SelectStatement( const TableQL& qlTable, optional<bool> includeDeleted, bool includeWhere )ε->DB::Statement{
		let dbView = qlTable.DBTable();
		DB::Statement statement;
		columnSql( qlTable, *dbView, false, statement, includeDeleted, includeWhere );
		if( statement.From.Empty() )
			statement.From += { dbView->Columns[0] };
		if( optional<DB::Criteria> criteria = dbView->Extends ? dbView->SurrogateKeys[0]->Criteria : nullopt; criteria ) //identities is_group
			statement.Where.Add( *criteria );//group with no members.
		statement.OrderBy = qlTable.OrderBy();
		statement.Limit( qlTable.Limit() );
		statement.Skip( qlTable.Offset() );

		return statement;
	}
}