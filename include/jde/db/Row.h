#pragma once
#include "Value.h"
#include <jde/fwk/str.h>
#include "exports.h"

#define Φ ΓDB auto
namespace Jde::DB{
	struct Row final{
		Row( vector<Value>&& values )ι:_values{move(values)}{}
		α operator[]( uint i )Ι->const Value&{ return _values[i]; }
		α operator[]( uint i )ι->Value&{ return _values[i]; }
		Ŧ Get( uint i )Ι->T;                //string, DBTimePoint or any arithmetic T - Value::Get converts across the numeric alternatives.
		Ŧ GetOpt( uint i )Ι->optional<T>; //nullopt for a NULL cell, else Get<T>.
		Φ GetBit( uint i )Ι->bool;
		Φ GetBitOpt( uint i )Ι->optional<bool>;
		α GetBytes( uint i )Ι->const vector<uint8_t>&{ return _values[i].get_bytes(); }
		α GetGuid( uint i )Ι->boost::uuids::uuid{ return _values[i].get_guid(); }
		α GetString( uint i )Ι->const string&{ return IsNull(i) ? Str::Empty() : _values[i].get_string(); }
		α TakeString( uint i )ι->string{ return IsNull(i) ? string{} : move( _values[i].get_string() ); } //the consuming read: moves the cell out and leaves it empty; a NULL answers empty and *stays* NULL.  (B6: GetString's old mutable twin was this in disguise, and turned a NULL cell into a string as a side effect of reading it.)
		Φ IsNull( uint i )Ι->bool;
		α Size()Ι->uint{ return _values.size(); }
	private:
		vector<Value> _values;
	};
	template<> Ξ Row::Get<string>( uint i )Ι->string{ return GetString(i); }
	Ŧ Row::Get( uint i )Ι->T{ return _values[i].Get<T>(); }
	Ŧ Row::GetOpt( uint i )Ι->optional<T>{ return IsNull(i) ? optional<T>() : Get<T>(i); }
}
#undef Φ