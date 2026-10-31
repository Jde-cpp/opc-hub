#pragma once

namespace Jde::DB{ struct Column; struct Table; struct Value; }
namespace Jde::QL{
	//A db column as json spells it:  the member name a client's mutation uses for it, and the view its value lives in - the enum or
	//flags table for a lookup column, else its own.  The write side's mirror of ColumnQL (include/jde/ql/types/TableQL.h).
	struct JsonColumn final{
		JsonColumn( sp<DB::Column> c )ι: Column{c}{}
		α Table()Ι->const DB::Table&;
		α MemberName()Ι->string;
		//The value a mutation sends for the column.  An enum fk's member is the enum table's singular (`itemType`), which is also the
		//nested-key spelling's name, so `itemType:{ id:2 }` lands here and its id is the value (#238).  Any other object is refused.
		//An empty string is null in a nullable text column (.claude/rules/db.md).
		α ToValue( const jvalue& v )Ε->DB::Value;
		sp<DB::Column> Column;
	};
}