#include <jde/db/meta/Column.h>
#include <jde/db/meta/Table.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/fwk/str.h>

#define let const auto

namespace Jde::DB{
	α Column::Count()ι->sp<Column>{ return ms<Column>( "count(*)" ); }

	α getDefault( const jobject& j, bool isNullable, EType type )ε->optional<Value>{
		optional<Value> obj;
		if( j.contains("default") )
			obj = Value{ type, j.at("default") };
		else if( isNullable )
			obj = type == EType::VarBinary ? Value{vector<uint8_t>()} : Value{};  //odbc handles varbinary nulls differently.
		return obj;
	}

	α getCriteria( const jobject& config )ε->optional<DB::Criteria> {
		optional<DB::Criteria> criteria;
		if( auto j = Json::FindObject( config, "criteria" ); j )
			criteria = { ms<DB::Column>( Json::AsSV(*j, "columnName") ),  Value{serialize(Json::AsValue(*j, "value"))} };
		return criteria;
	}


	α getPK( const jobject& j )ε->sp<Table>{
		sp<Table> table;
		if( auto name = Json::FindString(j, "pkTable"); name )
			table = ms<Table>( *name );
		else if( let pk = Json::FindObject(j, "pkTable"); pk )
			table = ms<Table>( Json::AsString(*pk,"name") );
		return table;
	}

	Column::Column( sv name )ι:
		Name{ name }
	{}
	using namespace Json;
	Column::Column( sv name, const jobject& j )ε:
		Name{ name },
		Criteria{ getCriteria(j) },
		IsNullable{ FindBool(j, "nullable").value_or(false) },
		CaseSensitive{ FindBool(j, "caseSensitive").value_or(false) },
		MaxLength{ FindNumber<uint16>(j, "length") },
		PKTable{ getPK(j) },
		IsSequence{ PKTable ? false : FindBool(j, "sequence").value_or(false) },
		Insertable{ FindBool(j, "insertable").value_or(!IsSequence) },
		SKIndex{ IsSequence ? optional<uint8>{0} : FindNumber<uint8>(j, "sk") },
		QLAppend{ FindDefaultSV(j, "qlAppend") },
		Type{ ToType(FindSV(j, "type").value_or("VarChar")) },
		Updateable{ FindBool(j, "updateable").value_or(true) },
		Default{ getDefault(j, IsNullable, Type) }
	{}

	α Column::Initialize( sp<DB::Table> table )ε->void{
		Table = table;
		if( PKTable )
			PKTable = table->Schema->GetTablePtr( PKTable->Name );
		if( Criteria && table->Extends ){
			Criteria->Column = table->Extends->GetColumnPtr( Criteria->Column->Name );
			if( Criteria->Value.Type()==EValue::String )//physical table will have config value, not json from constructor.
				Criteria->Value = { Criteria->Column->Type, Json::ParseValue(move(Criteria->Value.get_string())) };
		}
	}
	α Column::operator==( const Column& b )Ι->bool{
		return Name==b.Name && Table!=nullptr && b.Table!=nullptr && Table->Name==b.Table->Name;
	}

	α Column::FQName()Ι->string{
		return Ƒ( "{}.{}", Table->SqlName(), Name );
	}

	α Column::IsEnum()Ι->bool{
		return PKTable && PKTable->IsEnum();
	}

	α Column::IsFlags()Ι->bool{
		return PKTable && PKTable->IsFlags;
	}
	α Column::IsPK()Ι->bool{
		return Table && Table->SurrogateKeys.size()==1 && Table->SurrogateKeys[0].get()==this;
	}
}