#include "ColumnDdl.h"
#include <jde/db/generators/Syntax.h>
#include <jde/db/meta/Table.h>

#define let const auto
namespace Jde::DB{
	ColumnDdl::ColumnDdl( sv name, optional<Value> dflt, bool isNullable, EType type, optional<uint> maxLength, bool isSequence, optional<uint8> skIndex, optional<uint> numericPrecision, optional<uint> numericScale )ι:
		Column{ name }
	{
		Default = move( dflt );
		IsNullable = isNullable;
		Type = type;
		MaxLength = maxLength;
		NumericPrecision = numericPrecision;
		NumericScale = numericScale;
		IsSequence = isSequence;
		SKIndex = skIndex;
		Insertable = !isSequence; //the configured column's defaults (Column's json ctor); SyncTables copies the configured Insertable over this anyway.
		Updateable = true;
	}

	α ColumnDdl::DataTypeString( const Column& config )ι->string{
		let& syntax = config.Table->Syntax();
		let useMaxLength = config.MaxLength && syntax.HasLength( config.Type );
		return useMaxLength ? Ƒ( "{}({})", syntax.ToString(config.Type), *config.MaxLength ) : syntax.ToString( config.Type );
	}

	//It can only stand for the whole key:  a sequence in a composite key keeps the table-level constraint, and gets no identity syntax there.
	α ColumnDdl::DeclaresKey( const Column& config )ι->bool{
		return config.IsSequence && config.Table->Syntax().IdentityIsPrimaryKey() && config.Table->SurrogateKeys.size()==1;
	}

	α ColumnDdl::CreateStatement( const Column& config )ε->string{
		let& syntax = config.Table->Syntax();
		let null = config.IsNullable ? "null"sv : "not null"sv;
		const string sequence = config.IsSequence && ( DeclaresKey(config) || !syntax.IdentityIsPrimaryKey() ) ? " "+string{syntax.IdentityColumnSyntax()} : string{};
		string defaultClause;
		let& dflt = config.Default;
		if( dflt && !dflt->is_null() ){
			if( dflt->is_bool() )
				defaultClause = Ƒ( " default {}", dflt->get_bool() ? 1 : 0 );
			else if( string s = dflt->is_string() ? dflt->get_string() : string{}; s.size() )
				defaultClause = Ƒ( " default {}", s=="$now" ? syntax.NowDefault() : Ƒ("'{}'", s) );
			else if( dflt->is_number() )
				defaultClause = Ƒ( " default {}", dflt->get_number<int>() );
			else if( config.Type!=EType::VarBinary )
				THROW( "({})Default type not implemented.", dflt->TypeName() );
		}
		return Ƒ( "{} {} {}{}{}", config.Name, DataTypeString(config), null, sequence, defaultClause );
	}
}