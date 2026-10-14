#include "ColumnDdl.h"
#include <jde/db/generators/Syntax.h>
#include <jde/db/meta/Table.h>

#define let const auto
namespace Jde::DB{
	ColumnDdl::ColumnDdl( sv name, sv defaultSql, bool isNullable, EType type, optional<uint> maxLength, bool isSequence, optional<uint8> skIndex, optional<uint> numericPrecision, optional<uint> numericScale, sv defaultName )ι:
		Column{ name },
		DefaultSql{ defaultSql },
		DefaultName{ defaultName }
	{
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
	ColumnDdl::~ColumnDdl()=default;

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
		let defaultLiteral = config.Default ? syntax.DefaultLiteral( *config.Default ) : string{};
		let defaultClause = defaultLiteral.size() ? " default "+defaultLiteral : string{};
		return Ƒ( "{} {} {}{}{}", config.Name, DataTypeString(config), null, sequence, defaultClause );
	}
}