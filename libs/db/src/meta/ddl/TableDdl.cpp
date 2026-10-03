#include "TableDdl.h"
#include "ColumnDdl.h"
#include <jde/db/generators/Syntax.h>
#include "Index.h"
#include "SchemaDdl.h"
#include <jde/db/meta/Table.h>

#define let const auto

namespace Jde::DB{
	TableDdl::~TableDdl()=default;

	α TableDdl::CreateStatement()Ε->string{
		let& syntax = Syntax();
		string y = "create table "+SqlName()+"(";
		string suffix;
		for( let& c : Columns ){
			y += suffix+"\n\t"+ColumnDdl::CreateStatement( *c );
			suffix = ",";
		}
		//SurrogateKeys is the (possibly composite) primary key, ordered by SKIndex; a sequence column has SKIndex 0 so it is included here.
		if( SurrogateKeys.size() && !ColumnDdl::DeclaresKey(*SurrogateKeys[0]) ){ //sqlite's sequence column declared the key itself, and a second declaration is an error.
			vector<string> columns;
			for( let& c : SurrogateKeys )
				columns.push_back( c->Name );
			y += suffix+"\n\t"+syntax.CreatePrimaryKey( Name, Str::Join(columns, ", ") )+"\n";
		}
		//When the syntax can't 'alter table add constraint' (sqlite), SyncFKs is skipped, so declare fks inline here -
		//it's the only place they get enforced. Column::NeedsFK is the selection rule shared with SchemaDdl::SyncFKs.
		if( !syntax.CanAddForeignKeys() ){
			for( let& c : Columns ){
				if( !c->NeedsFK() )
					continue;
				let& pk = *c->PKTable;
				y += Ƒ( "{}\n\tforeign key({}) references {}({})", suffix, c->Name, pk.SqlName(), pk.GetPK()->Name );
				suffix = ",";
			}
		}
		return y+")";
	}

	α TableDdl::InsertProcCreateStatement( const Table& config )Ι->string{
		let& syntax = Syntax();
		let procName = UnqualifiedProcName( InsertProcName() );
		string create = Ƒ( "{} {}.{}(", syntax.CreateProcSql(), Schema->DBSchema->Name, syntax.EscapeDdl(procName) );
		string insert = Ƒ( "\tinsert into {}(", SqlName() );
		string values{ "\t\tvalues(" };
		let prefix = syntax.ProcParameterPrefix().empty() ? "_" : syntax.ProcParameterPrefix();
		char delimiter = ' ';
		for( let& c : config.Columns ){
			auto value{ Ƒ("{}{}"sv, prefix, c->Name) };
			if( c->Insertable )
				create += Ƒ( "{}{}{} {}", delimiter, prefix, c->Name, ColumnDdl::DataTypeString(*c) );
			else{
				if( c->IsNullable || !c->Default )
					continue;
				if( c->Default->is_string() && c->Default->get_string()=="$now" )
					value = syntax.UtcNow();
			}
			insert += Ƒ( "{}{}", delimiter, c->Name );
			values += Ƒ( "{}{}", delimiter, value );
			delimiter = ',';
		}
		insert += " )\n";
		values += " );\n";
		let seqCol = SequenceColumn(); //C10: the commented-out DriverReturnsLastInsertId() guard went with the virtual - it had been dead since the file was written, and every dialect answered true.
		if( seqCol ) //the OUT param: `out` before it on the dialects that spell it that way, `output` after on the ones that don't.
			create += Ƒ( "{}{} {}{} {}{}", delimiter, syntax.PrefixOut() ? " out" : "", prefix, seqCol->Name, ColumnDdl::DataTypeString(*seqCol), syntax.PrefixOut() ? "" : " output" );
		create += Ƒ( " )\n{}\n{}{}", syntax.ProcStart(), insert, values );
		if( seqCol )
			create += Ƒ( "\tset {}{} = {};\n", prefix, seqCol->Name, syntax.IdentitySelect() );
		create += syntax.ProcEnd();
		return create;
	}
}