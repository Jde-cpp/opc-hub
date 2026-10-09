#include <jde/db/meta/Table.h>
#include <jde/fwk/str.h>
#include <jde/db/IDataSource.h>
#include <jde/db/names.h>
#include <jde/db/generators/Functions.h>
#include <jde/db/generators/Syntax.h>
#include <jde/db/meta/Cluster.h>
#include <jde/db/meta/Catalog.h>
#include <jde/db/meta/Column.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/DBSchema.h>
#include <jde/access/IAcl.h>

#define let const auto

namespace Jde::DB{
	α toColumns( const jobject& j )ε->vector<sp<Column>>{
		flat_map<uint16,sp<Column>> ordered;
		if( auto kv = j.find("columns"); kv!=j.end() ){
			uint16 defaultIndex = 128;
			for( let& nameCol : Json::AsObject(kv->value()) ){
				let name{ nameCol.key() };
				let& column{ Json::AsObject(nameCol.value()) };
				auto i = Json::FindNumber<uint16>( column, "i" ).value_or( defaultIndex++ );
				while( ordered.find(i)!=ordered.end() )
					i = defaultIndex++;
				ordered.emplace( i, ms<Column>(Names::FromJson(name), column) );
			}
		}
		vector<sp<Column>> columns;
		for( let& [_,c] : ordered )
			columns.push_back( c );
		return columns;
	}

	α getMap( const jobject& j, const Table& table )ε->optional<Table::ParentChildMap>{
		auto jmap = Json::FindObject( j, "map" );
		return jmap
			? Table::ParentChildMap{table.GetColumnPtr(Json::AsSV(*jmap, "parentId")), table.GetColumnPtr(Json::AsSV(*jmap, "childId")) }
			: optional<Table::ParentChildMap>{};
	}

	α getSurrogateKeys( sv tableName, const vector<sp<Column>>& columns )ε->vector<sp<Column>>{
		flat_map<uint8,sp<Column>> skColumns;
		for( let& c : columns ){
			if( !c->SKIndex.has_value() )
				continue;
			let [existing, added] = skColumns.emplace( *c->SKIndex, c );
			THROW_IF( !added, "[{}]Columns '{}' and '{}' both declare sk:{}.  Clear it on the one that is a foreign key.", tableName, existing->second->Name, c->Name, (uint)*c->SKIndex );//(uint): SKIndex is uint8, which formats as a character - sk:0 would print as a NUL.
		}
		vector<sp<Column>> y;
		for( let& [_,c] : skColumns )
			y.push_back( c );
		return y;
	}

	α getNaturalKeys( const jobject& j )ε->vector<vector<string>>{
		vector<vector<string>> naturalKeys;
		if( auto kv = j.find("naturalKeys"); kv!=j.end() ){
			for( let& nk : kv->value().as_array() ){
				vector<string> columns;
				for( let& col : nk.as_array() )
					columns.emplace_back( col.as_string() );
				naturalKeys.push_back( move(columns) );
			}
		}
		return naturalKeys;
	}

	constexpr Access::ERights DefaultOps{ Access::ERights::Create | Access::ERights::Read | Access::ERights::Update | Access::ERights::Delete | Access::ERights::Purge | Access::ERights::Administer };

	Table::Table( sv name, const jobject& o )ε:
		Name{name},
		Columns{ toColumns(o) },
		HasCustomInsertProc{ Json::FindBool(o, "customInsertProc").value_or(false) },
		AddProc{ string{Json::FindDefaultSV(o, "addProc")} },
		RemoveProc{ string{Json::FindDefaultSV(o, "removeProc")} },
		IsFlags{ Json::FindBool(o, "isFlags").value_or(false) },
		SurrogateKeys{ getSurrogateKeys(name, Columns) },//before Map
		Map{ getMap(o,*this) },
		QLView{ o.contains("qlView") ? ms<Table>( Json::AsString(o,"qlView")) : nullptr },
		Operations{ Json::FindArray(o, "ops") ? Access::ToRights(Json::AsArray(o, "ops")) : DefaultOps },
		NaturalKeys{ getNaturalKeys(o) },
		PurgeProcName{ o.contains("purgeProc") ? string{o.at("purgeProc").as_string()} : string{} },
		Extends{ o.contains("extends") ? ms<Table>(Json::AsString(o,"extends")) : nullptr }
	{}
	Table::~Table()=default;

	α Table::Initialize( sp<DB::AppSchema> schema, sp<Table> self )ε->void{
		if( Extends ) //first: Column::Initialize rewrites Criteria through the extended table.
			Extends = schema->GetTablePtr( Extends->Name );
		self->Schema = schema;
		for( let& c : Columns )
			c->Initialize( self );

		if( QLView ){
			QLView = schema->GetViewPtr( QLView->Name );
			QLView->Owner = self;//idempotent: SyncTables re-initializes, and re-assigning the same owner is a no-op.
		}

		if( Map ){
			let& pkTable = Map->Parent->PKTable;
			THROW_IF( !pkTable, "[{}]Map parent column '{}' has no pkTable.", Name, Map->Parent->Name );
			if( find(pkTable->Children, self)==pkTable->Children.end() ) //idempotent: SyncTables re-initializes tables (ctor Initialize + SyncTables), so don't duplicate Children.
				pkTable->Children.emplace_back( self );
		}

		//if mssql & schema is not default & ds schema!=config schema.
		let& dbSchema = *Schema->DBSchema;
		DBName.clear();
		if( let isPhysical = dbSchema.IsPhysical(); !isPhysical && Schema->Prefix.size() ) //vs config.
			DBName += Schema->Prefix;

		DBName+=Name;
	}
	α Table::Authorize( Access::ERights rights, UserPK userPK, SL sl )Ε->void{
		if( let p=Schema->Authorizer; p ){
			using enum Access::ERights;
			let owner = Owner.lock();//a ql view has no resource of its own - ResourceLoadAwait creates one per table.
			let name = Names::ToJson( owner ? owner->Name : Name );
			if( !empty(rights & Operations) ) // only test if the requested rights intersect with the table's enforced operations
				p->Test( Schema->Name, name, rights, userPK, sl );
			else if( !empty(rights & (Create|Update|Delete|Purge)) )//no resource row can grant a write outside the ops, nor enforce a read.
				p->TestSystem( Schema->Name, name, rights, userPK, sl );
			else
				p->TestUser( userPK, sl );
		}
	}

	α Table::FindOwnColumn( sv name )Ι->sp<Column>{
		if( name=="id" && SurrogateKeys.size()==1 )
			return SurrogateKeys[0];
		auto pColumn = find_if( Columns, [&name](let& c){return c->Name==name;} );
		return pColumn==Columns.end() ? sp<Column>{} : *pColumn;
	}
	α Table::FindColumn( sv name )Ι->sp<Column>{
		auto pColumn = FindOwnColumn( name );
		if( !pColumn && Extends )
			pColumn = Extends->FindColumn( name );
		return pColumn;
	}
	α Table::GetColumn( sv name, SL sl )Ε->const Column&{
		return *GetColumnPtr( name, sl );
	}
	α Table::GetColumnPtr( sv name, SL sl )Ε->sp<Column>{
		auto pColumn = FindColumn( name ); THROW_IFSL( !pColumn, "[{}.{}]Could not find column.", Name, name );
		return pColumn;
	}

	α Table::GetColumns( vector<string> names, SL sl )Ε->vector<sp<Column>>{
		vector<sp<Column>> columns;
		for( let& name : names )
			columns.push_back( GetColumnPtr(name, sl) );
		return columns;
	}

	α Table::GetPK( SL sl )Ε->sp<Column>{
		auto p = FindPK(); THROW_IFSL( SurrogateKeys.size()!=1, "[{}]Expected 1 surrogate column, found: {}.", Name, SurrogateKeys.size() );
		return p;
	}
	α Table::GetSK0(SL sl)Ε->sp<Column>{
		THROW_IFSL( SurrogateKeys.size()==0, "No surrogate keys for '{}'.", Name );
		return SurrogateKeys[0];
	}

	α Table::FindFK( sv pkTableName )Ι->sp<Column>{
		auto p = find_if( Columns, [&pkTableName](let& c){return c->PKTable && c->PKTable->Name==pkTableName;} );
		return p==Columns.end() ? sp<Column>{} : *p;
	}
	α Table::FindPK()Ι->sp<Column>{
		return SurrogateKeys.size()==1 ? SurrogateKeys.front() : sp<Column>{};
	}

	α Table::SequenceColumn()Ι->sp<Column>{
		auto p = find_if( Columns, [](let& c){return c->IsSequence;} );
		return p==Columns.end() ? nullptr : *p;
	}

	α Table::InsertProcName()Ι->string{
		return !SequenceColumn() && !HasCustomInsertProc ? string{} : Ƒ( "{}_insert", Names::ToSingular(DBName) );
	}
	//The insert proc as a *server object*.  Empty when the dialect has no procs (sqlite), where the insert exists as
	//a native twin registered through IProcs and there is nothing for DDL sync to create or drop.  Keeping the rule
	//here means the DDL paths call one function instead of each re-deriving `!Syntax().HasProcs()`.
	α Table::DdlInsertProcName()Ι->string{
		return Syntax().HasProcs() ? InsertProcName() : string{};
	}
	α Table::UpsertProcName()Ι->string{
		return Ƒ( "{}_upsert", Names::ToSingular(DBName) );
	}

	α Table::SqlName()Ι->string{
		let& syntax = Syntax();
		return syntax.IsReservedWord( DBName ) ? syntax.EscapeDdl( DBName ) : DBName;
	}

	α Table::IsEnum()Ι->bool{
		return QLView ? QLView->IsEnum() : Columns.size()==2 && Columns[1]->Name=="name";
	}
	α Table::JsonName()Ι->string{
		auto name = Names::ToJson( Names::ToSingular(Name) );
		if( name.size() )
			name[0] = (char)std::toupper( name[0] );
		return name;
	}

	α Table::Syntax()Ι->const DB::Syntax&{ return Schema->Syntax(); }
}
