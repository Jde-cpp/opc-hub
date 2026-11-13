#pragma once
#include <quickjs.h>

//A message's values across the sandbox's edge (docs/flows/spec.md *The Function node*):  as values, never through text.
//An integer within JavaScript's safe range, 2^53 less one either side of zero, is a number, and one past it a BigInt,
//since the json keeps no type to tell an Int64 by;  a BigInt comes back as an integer where 64 bits hold it.
namespace Jde::Opc::Flows::Js{
	//Json as a value of the context, the caller's to free:  JS_EXCEPTION, with the context's exception pending, when the
	//heap has no room for it.
	α ToJs( JSContext* ctx, const jvalue& v )ι->JSValue;
	α ToJs( JSContext* ctx, const jobject& o )ι->JSValue;
	//A value as json, by JSON.stringify's rules:  an object's own enumerable properties, its `toJSON` where it has one, so
	//a Date is its ISO text, a function, a symbol and `undefined` left out of an object and null in an array, a number
	//that is not finite null.  False, with the context's exception pending:  what a getter threw, a value nested past 64
	//levels, which one that holds itself is, a BigInt past 64 bits, and json past `budget` bytes, which bounds what a
	//body builds outside its heap.  In an entry:  it runs the value's getters.
	α ToJson( JSContext* ctx, JSValueConst v, jvalue& y, uint budget )ι->bool;
}