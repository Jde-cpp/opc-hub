#pragma once
#include "../exports.h"
#include "../Value.h"

namespace Jde::DB{
	struct Column; struct Table;
	enum class ECardinality : uint8{ Zero=0, One=1, Many=2 };
	struct Criteria {
		sp<DB::Column> Column;
		DB::Value Value;
	};

	struct ΓDB Column{
		Column( sv name )ι;  //placeholder
		Column( sv name, const jobject& j )ε;
		virtual ~Column()=default;
		α Initialize( sp<DB::Table> table )ε->void;
		α operator==( const Column& b )Ι->bool;

		Ω Count()ι->sp<Column>;
		α FQName()Ι->string;
		α IsEnum()Ι->bool; //cache=true
		α IsFlags()Ι->bool;
		α IsPK()Ι->bool;  //this column IS the table's single surrogate key - membership, not just "declares an sk".
		α NeedsFK()Ι->bool{ return PKTable && !IsFlags(); } //references a pk table and isn't a flags bitmask - the single fk-eligibility rule for ddl (inline sqlite fks + SyncFKs).

		string Name;

		optional<DB::Criteria> Criteria;//unUsers=not is_group
		bool IsNullable{};
		bool CaseSensitive{};//compared byte for byte, the dialect's binary collation:  a key the code holds case-sensitive.
		optional<uint> MaxLength;
		optional<uint> NumericPrecision; //currently for db schema columns
		optional<uint> NumericScale; //currently for db schema columns
		sp<DB::Table> PKTable; //pk table if any.

		bool IsSequence{}; //uses db sequence.  TODO look to move to ddl.
		bool Insertable{ true };
		optional<uint8> SKIndex; //Is part of the surrogate key for the table.
		string QLAppend; //also select this column in ql query TODO example
		sp<DB::Table> Table;
		EType Type{ EType::None };
		bool Updateable{ true };
		optional<Value> Default; //nullable=Value{}
	};
}