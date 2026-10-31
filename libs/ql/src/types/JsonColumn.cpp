#include "JsonColumn.h"
#include <jde/db/names.h>
#include <jde/db/meta/Column.h>
#include <jde/db/Value.h>
#include <jde/db/meta/DBSchema.h>

#define let const auto

namespace Jde::QL{
	Ω isEnumKey( const DB::Column& c )ι->bool{ return c.IsEnum() && !c.IsFlags() && c.Name.ends_with("_id"); }

	α JsonColumn::MemberName()Ι->string{
		string y;
		if( isEnumKey(*Column) ){
			y = DB::Names::ToJson( Table().Name );
			if( !Column->IsFlags() )
				y = DB::Names::ToSingular( y );
		}
		else
			y = DB::Names::ToJson( Column->Name );
		return y;
	}
	α JsonColumn::ToValue( const jvalue& v )Ε->DB::Value{
		let o = isEnumKey(*Column) ? v.if_object() : nullptr;
		THROW_IF( o && (o->size()!=1 || !o->contains("id")), "'{}' takes an existing {} row's key - {{ id:N }} - not a row to create.", MemberName(), Table().Name );
		THROW_IF( !o && v.is_object(), "'{}' takes a value, not an object.", MemberName() );
		DB::Value y{ Column->Type, o ? o->at("id") : v };
		return Column->IsNullable && y.is_string() && y.get_string().empty() ? DB::Value{} : y;
	}
	α JsonColumn::Table()Ι->const DB::Table&{ return Column->IsFlags() || Column->IsEnum() ? *Column->PKTable : *Column->Table; }
}