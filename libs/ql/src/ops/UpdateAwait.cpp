#include "UpdateAwait.h"
#include <jde/fwk/co/AnyAwait.h>
#include <jde/ql/QLHook.h>
#include <jde/db/IDataSource.h>
#include <jde/db/names.h>
#include <jde/db/generators/Functions.h>
#include <jde/db/generators/UpdateClause.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Column.h>
#include <jde/db/meta/Table.h>
#include <jde/access/IAcl.h>
#include "../types/JsonColumn.h"
#include "../qlInternal.h"

#define let const auto

namespace Jde::QL{
	using DB::Value;
	using Enums=flat_map<string,flat_map<uint,string>>;//enum table name -> its values, prefetched by Execute so the clause build never waits on the db.

	//Authorization only - it is an in-memory acl check, so an unauthorized mutation is still refused without suspending.  The
	//clauses are built in Execute() instead: resolving a flags column's names needs the enum table, and await_ready can not
	//co_await, which is what forced the old BlockAwait onto whatever thread happened to be running the mutation.
	α UpdateAwait::await_ready()ι->bool{
		try{
			THROW_IF( !_table, "Table not found for mutation '{}'.", _mutation.ToString() );
			_table->Authorize( _mutation.Type==EMutationQL::Update ? Access::ERights::Update : Access::ERights::Delete, _userPK, _sl );
		}
		catch( Exception& e ){
			Refuse( move(e) );
		}
		return Refused();
	}

	//The flags columns this mutation actually sets, over the same extension chain and the same arg predicate CreateUpdate uses.
	Ω flagTables( const DB::Table& table, const jobject& input, vector<sp<DB::Table>>& y )ι->void{
		if( table.Extends )
			flagTables( *table.Extends, input, y );
		for( let& c : table.Columns ){
			if( !c->Updateable || c->IsPK() || c->SKIndex || !c->IsFlags() )//#46: the same predicate createUpdate uses, so the two walks stay in step.
				continue;
			let jvalue = input.if_contains( JsonColumn{c}.MemberName() );
			if( let flags = jvalue ? jvalue->if_array() : nullptr; flags && flags->size() )
				y.push_back( JsonColumn{c}.Column->PKTable );
		}
	}

	//Prefetched by Execute().  The fallback is the old path (cached, but it blocks); it only runs if the two walks ever
	//disagree, so a future divergence costs a stall instead of a failed mutation.
	Ω enumValues( Enums& enums, const DB::Table& enumTable )ε->const flat_map<uint,string>&{
		auto p = enums.find( enumTable.Name );
		if( p==enums.end() ){
			WARNT( ELogTags::QL, "[{}]enum values were not prefetched - falling back to a blocking lookup.", enumTable.Name );
			p = enums.emplace( enumTable.Name, GetEnumValues(enumTable) ).first;
		}
		return p->second;
	}

	//kind:  the extension's criteria on the row it extends - users and groups share identities, and is_group says whose a row is.
	Ω createUpdate( const DB::Table& table, const jobject& input, Enums& enums, vector<DB::UpdateClause>& updates, const optional<DB::Criteria>& kind={} )ε->DB::Value{
		let pExtendedFromTable = table.Extends;
		DB::Value rowKey = pExtendedFromTable  ? createUpdate(*pExtendedFromTable, input, enums, updates, table.GetSK0()->Criteria) : DB::Value{};

		DB::UpdateClause update;
		if( pExtendedFromTable )
			update.Where.Add( table.SurrogateKeys[0], rowKey );
		else{
			let& args = input;
			if( let id = table.FindPK() ? Json::FindNumber<uint>(args, "id") : optional<uint>{}; id )
				update.Where.Add( table.FindPK(), DB::Value{*id} );
			else if( let name = table.FindColumn("name") ? Json::FindSV(args, "name") : optional<sv>{}; name )
				update.Where.Add( table.FindColumn("name"), DB::Value{string{*name}} );
			else if( let slug = table.FindColumn("slug") ? Json::FindSV(args, "slug") : optional<sv>{}; slug )
				update.Where.Add( table.FindColumn("slug"), DB::Value{string{*slug}} );
			else
				THROW( "Could not get criteria from {}", serialize(args) );
			rowKey = update.Where.Params()[0];
			if( kind )
				update.Where.Add( *kind );
		}

		for( let& c : table.Columns ){
			//#46: a key column is not a settable one.  `updateable` defaults to true and none of common-meta's sequenced pk
			//shapes says otherwise, so `updateResource( id:6, resourceId:5006 )` emitted `set resource_id = 5006` and renumbered
			//the row out from under Authorize's in-memory map;  only sqlite's FKs (created without ON UPDATE CASCADE) stopped
			//the identities case, and then only where a child row happened to exist.
			if( !c->Updateable || c->IsPK() || c->SKIndex )
				continue;

			const JsonColumn qlColumn{ c };
			let jvalue = input.if_contains( qlColumn.MemberName() );
			if( !jvalue )
				continue;
			if( !c->IsFlags() )
				update.Add( c, DB::Value{c->Type, *jvalue} );
			else{
				uint value = 0;
				if( let flags = jvalue->if_array(); flags && flags->size() )
					value = ToFlags( enumValues(enums, qlColumn.Table()), *flags, qlColumn.MemberName() );
				else if( jvalue->is_number() )
					value = Json::AsNumber<uint>( *jvalue );
				update.Add( c, {value} );
			}
		}
		THROW_IF( update.Where.Empty(), "There is no where clause." );
		if( update.Values.size() ){
			//#45: an extension row is keyed by whatever the parent's where clause was keyed by, and on the name/slug branches that
			//is the literal, not the row - `update access_users set … where access_users.identity_id='bob'` matched nothing and the
			//parent's row count was reported as success.  Resolving the parent pk first would mean a blocking select on the mutation
			//path, so the shape is refused.  Only when this statement has something to set:  `updateGroup( name:…, description:… )`
			//touches parent columns only, so no extension statement is emitted and the natural key is still the right way to say it.
			THROW_IF( pExtendedFromTable && !Json::FindNumber<uint>(input, "id"), "'{}' extends '{}', so setting one of its own columns needs an id - name and slug key the parent, not its extension.", table.Name, pExtendedFromTable->Name );
			updates.push_back( move(update) );
		}
		return rowKey;
	}
	α UpdateAwait::CreateDeleteRestore( const DB::Table& table, const jobject& input )ε->DB::UpdateClause{
		DB::UpdateClause update;
		auto deleted = table.GetColumnPtr( "deleted" );
		let value = _mutation.Type==EMutationQL::Delete ? DB::Value{"$now"} : DB::Value{};
		update.Add( deleted, value );
		auto key = _mutation.GetKey();
		update.Where.Add( key.IsPK() ? deleted->Table->GetPK() : deleted->Table->GetColumnPtr("slug"), DB::Value::FromKey(key) );//deleted=main table, table=possibly extension table.
		if( auto kind = table.Extends ? table.GetSK0()->Criteria : nullopt; kind )//else deleteGroup with a user's id deletes the user (access-refactor B5).
			update.Where.Add( *kind );
		for( let& arg : input ){
			if( arg.key()=="id" || arg.key()=="slug" )
				continue;
			if( let column = table.FindColumn( arg.key() ); column )
				update.Where.Add( column, DB::Value{column->Type, arg.value()} );
		}
		return update;
	}

	//One coroutine.  The enum prefetch is a CacheAwait, the hooks MutationAwaits, the statements ExecuteAwaits - three task types,
	//which is what used to split this into Build → UpdateBefore → Execute → UpdateAfter with the clauses parked on members.  The
	//hooks are AnyAwaits and Any() wraps the other two, so one frame awaits all three, and the clauses are locals again.
	α UpdateAwait::Execute()ι->TAwait<jvalue>::Task{
		uint rowCount{};
		try{
			let input = _mutation.ExtrapolateVariables();
			vector<DB::UpdateClause> updates;
			if( _mutation.Type==EMutationQL::Update ){
				vector<sp<DB::Table>> enumTables; Enums enums;//a flags column resolves its names through the enum table - fetched here, where waiting on the db is free.
				flagTables( *_table, input, enumTables );
				for( let& enumTable : enumTables ){
					if( enums.contains(enumTable->Name) )
						continue;
					auto values = enumTable->Schema->DS()->SelectEnum<uint,string>( *enumTable, Cache::DefaultDuration(), _sl );//an lvalue: CacheAwait is not movable, so Any() references it.
					enums.emplace( enumTable->Name, co_await Any(values) );
				}
				createUpdate( *_table, input, enums, updates );
				THROW_IF( updates.empty(), "There is nothing to update." );
			}
			else
				updates.push_back( CreateDeleteRestore(*_table, input) );
			co_await Hook::UpdateBefore( _mutation, _userPK );
			auto& ds = *_table->Schema->DS();
			for( auto& update : updates )
				rowCount += co_await Any( ds.Execute(update.Move()) );
			co_await Hook::UpdateAfter( _mutation, _userPK );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
			co_return;
		}
		Resume( jvalue{rowCount} );
	}
}