#pragma once
#include "../exports.h"
#include <jde/access/usings.h>

namespace Jde::DB{
	struct Column; struct AppSchema; struct Syntax;

	//db-refactor B4: there is only Table.  A `views:` schema entry builds the same type as a `tables:` entry - AppSchema
	//keeps the two in separate maps (Tables/Views) and map membership is the whole distinction: SyncTables walks Tables,
	//FindView falls back to Tables, and nothing asks a Table whether it is a view.
	struct ΓDB Table{
		Table( string name )ι:Name{name},DBName{move(name)}{}  //placeholder - a name Initialize resolves (qlView, extends, pkTable); columns populated then.
		Table( sv name, const jobject& j )ε;
		virtual ~Table(); //out-of-line: the key function that anchors the vtable in Jde.DB - TableDdl is dynamic_pointer_cast'ed from a -fvisibility=hidden driver.
		α Initialize( sp<DB::AppSchema> schema, sp<Table> self )ε->void;

		α Authorize( Access::ERights rights, UserPK userPK, SL sl )Ε->void;
		α FindOwnColumn( sv name )Ι->sp<Column>; //this table's columns only: `id` resolves to a lone surrogate key, then Columns by name; never consults Extends.
		α FindColumn( sv name )Ι->sp<Column>; //FindOwnColumn, then the extended table's.
		α GetColumn( sv name, SRCE )Ε->const Column&;
		α GetColumnPtr( sv name, SRCE )Ε->sp<Column>;
		α GetColumns( vector<string> names, SRCE )Ε->vector<sp<Column>>;
		α FindPK()Ι->sp<Column>;
		α FindFK( sv pkTableName )Ι->sp<Column>;
		α GetPK( SRCE )Ε->sp<Column>;
		α GetSK0(SRCE)Ε->sp<Column>;
		α InsertProcName()Ι->string;
		α DdlInsertProcName()Ι->string; //InsertProcName, but empty when there is no server object to create/drop.
		α UpsertProcName()Ι->string;
		α IsEnum()Ι->bool;
		α JsonName()Ι->string;
		α SequenceColumn()Ι->sp<Column>;
		α SqlName()Ι->string; //DBName, quoted when it is a reserved word (e.g. an unprefixed `groups`) - DBName stays raw for metadata lookups.
		α Syntax()Ι->const DB::Syntax&;

		string Name; //provider_id
		vector<sp<Column>> Columns;
		string DBName; //[schema.][um_]Name
		bool HasCustomInsertProc{};
		string AddProc;
		string RemoveProc;
		bool IsFlags{}; //e.g. read=1, update=2, purge=4, execute=8, rights=16
		vector<sp<Column>> SurrogateKeys;//before Map
		struct ParentChildMap{ sp<Column> Parent; sp<Column> Child; };
		optional<ParentChildMap> Map;//members: identity_id, member_id
		sp<Table> QLView;
		//The table that declared this one as its qlView, set in Table::Initialize.  Weak, or the pair would keep each other
		//alive.  Authorize tests the owner's name:  a resource exists per table ("users"), never per ql view ("usersQl").
		wp<Table> Owner;
		sp<DB::AppSchema> Schema;
		Access::ERights Operations{}; //the grantable rights - the resource row's `allowed`; a write outside them is the system's alone (Authorize).
		vector<sp<Table>> Children;
		vector<vector<string>> NaturalKeys;
		string PurgeProcName;
		sp<Table> Extends;
	};
}
