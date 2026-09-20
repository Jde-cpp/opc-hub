import Long from "long";
import { Duration, Guid, ProtoUtils, Timestamp  } from "jde-framework";
import { NodeId } from "./node-id";
import { ExNodeId } from "./ex-node-id";
import {OpcError} from "./opc-error";
import { StatusCode } from "./types";

export type Value = boolean | Duration | OpcError | ExNodeId | Guid | Long | NodeId | number | string | Timestamp | Uint8Array | Value[];

export function valueJson( value: Value ):any/*:NodeIdJson*/{
	if( value instanceof ExNodeId )
		return value.toJson();
	else if( value instanceof NodeId )
		return value.id;
	else if( value instanceof Uint8Array )
		return {b: btoa(value.reduce((acc, current) => acc + String.fromCharCode(current), "")) };
	else if( value instanceof OpcError )
		return {sc: value.sc };
	else if( Array.isArray(value) )
		return value.map( x=>valueJson(x) );
	else
		return value;
}

export function valueString( value: Value|undefined ):string{
	if( value===undefined || value===null )
		return "";//a Variable the browse could not read has no value, and the Object.hasOwn test below throws on null/undefined - so rendering that row crashed the page
	else if( typeof value === "string" )
		return value;
	else if( typeof value === "number" )
		return value.toString();
	else if( typeof value === "boolean" )
		return value.toString();
	else if( value instanceof Long )
		return value.toString();
	else if( value instanceof Guid )
		return value.toString();
	else if( value instanceof Uint8Array )
		return btoa( value.reduce((acc, current) => acc + String.fromCharCode(current), "") );
	else if( Object.hasOwn(value, "seconds") && Object.hasOwn(value, "nanos") ){
		const date = ProtoUtils.toDate( <Timestamp>value );
		return date ? date.toISOString() : "";//unset is seconds==0
	}
	else if( value instanceof ExNodeId )
		return JSON.stringify(value.toJson());
	else if( value instanceof NodeId )
		return value.id.toString();
	else if( value instanceof OpcError )
		return value.toString();
	else if( Array.isArray(value) )
		return value.map( x=>valueString(x) ).join( ", " );//`this.` was stale — these are free functions now
	else
		return `unknown type ${typeof value}`;
}

export function toValue( json:any ):Value{
	let value = json;
	if( Array.isArray(value) )//per element: ToJson emits one entry per array element, and a Long, a {v,sc} or a NodeId
		value = value.map( x=>toValue(x) );//element needs the same unwrapping a scalar does — they arrived as raw objects.
	else if( value?.hasOwnProperty('v') )//{v,sc} — a reading with quality attached.  Before the 'sc' test on purpose: sc-first turned it into an OpcError and discarded the reading.  Recurse for a Long payload.
		value = toValue( json.v );
	else if( value?.hasOwnProperty('sc') )
		value = new OpcError( json.sc, "OpcError", "", undefined );//was `new Error(sc)` — a plain Error isn't `instanceof OpcError`, so valueString rendered it as "unknown type object"
	else if( value?.hasOwnProperty('unsigned') )
		value = new Long( json.low, json.high, json.unsigned );
	return value;
}

//The quality beside the reading.  toValue() returns the reading alone, so an Uncertain {v,sc} lost its sc over REST
//while the socket carried it in SubscriptionResult.sc.  undefined/0 = Good; a Bad reading carries no `v` and toValue
//already turns it into an OpcError, which holds the same code.
export function valueSc( json:any ):StatusCode|undefined{
	return json?.hasOwnProperty('sc') ? <StatusCode>json.sc : undefined;
}

//A reading whole - the value and its quality together, which is how every path hands one to a row (Variable.setReading).
//sc 0/undefined = Good.  No `value` = the server sent none:  REST drops it on Bad (`{sc}`), where the socket still carries
//the one the server holds.
export type Reading = { value?:Value, sc?:StatusCode };
export function toReading( json:any ):Reading{
	const sc = valueSc( json ) ?? 0;
	return !Array.isArray(json) && json?.hasOwnProperty('sc') && !json.hasOwnProperty('v') ? {sc} : { value: toValue(json), sc };
}