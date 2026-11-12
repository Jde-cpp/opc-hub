#pragma once
#include <jde/flows/Node.h>

namespace Jde::Opc::Flows{
	α AsNumber( const jvalue& v )ι->optional<double>;
	//-1, 0 or 1 for two numbers, whatever their kinds and two integers exactly, two strings or two Booleans;  none for any other pair.
	α Order( const jvalue& a, const jvalue& b )ι->optional<int>;
	α Equal( const jvalue& a, const jvalue& b )ι->bool;//numbers by value;  the rest as json compares.

	//Where a rule's value is (spec *Nodes*):  in the rule, a literal, in the message, or in the flow's or the global context.
	enum class EScope : uint8{ Literal, Msg, Flow, Global };
	//The rule's `typeKey`, Node-RED's `vt`, `pt` and `tot`:  "msg", "flow" or "global", and `dflt` without one.
	α ToScope( const jobject& rule, sv typeKey, EScope dflt )ε->EScope;
	struct Operand final{
		Operand( const jobject& rule, sv key, sv typeKey )ε;//the rule's `key` as its `typeKey` says:  the json itself, a property's path or a context key.
		//Null when the message or the context lacks it.  `scratch` holds a global's copy.
		α Find( const Message& m, const Store& flow, const GlobalStore& global, jvalue& scratch )Ι->const jvalue*;
	private:
		EScope _scope;
		jvalue _literal;
		optional<Path> _property;
		string _key;
	};
}