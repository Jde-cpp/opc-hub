import Long from "long";
import { DateUtils, Duration, Guid, ProtoUtils, Timestamp  } from "jde-framework";
import { NodeId } from "./node-id";
import { ExNodeId } from "./ex-node-id";
import {OpcError} from "./opc-error";
import { ETypes, StatusCode } from "./types";

export type Value = boolean | Duration | ExtensionObject | OpcError | ExNodeId | Guid | Long | NodeId | number | string | Structure | Timestamp | Uint8Array | Value[];

//A structure the gateway decoded by member (clients spec *Structures*, #422):  its members by name, in the type's order, each a Value - a
//nested structure one of these, an array member an array, an absent optional member null.  The same shape off the socket's `structure`
//(Gateway.toValue) and off the REST json, so one renderer (StructureTable) shows both.
export type Structure = { [member:string]:Value|null };
//A json object, not a class instance, an array or null:  what REST's values travel as, and a structure off the socket.
export const isPlain = ( v:unknown ):v is Record<string,unknown>=>typeof v=='object' && v!==null && !Array.isArray( v ) && Object.getPrototypeOf( v )===Object.prototype;
const within = ( o:object, names:readonly string[] )=>Object.keys( o ).every( k=>names.includes(k) );
const identifiers = ['i', 's', 'g', 'b'];
const nodeIdKeys = ( o:Record<string,unknown>, extra:string[] = [] )=>within( o, ['ns', ...identifiers, ...extra] ) && identifiers.filter( k=>k in o ).length==1;
//The kinds REST's json gives as plain objects besides a structure, each known by its whole key set (Variant::ElementToJson):  a DateTime,
//{seconds,nanos};  the Long form, {low,high,unsigned};  a QualifiedName, {ns,name};  a NodeId, {ns,i|s|g|b}, and an ExpandedNodeId with
//its nsu or serverIndex;  an opaque body, {typeId,body};  and {}, an empty QualifiedName or a union with none selected.  A structure with
//a member of one of those names stays a structure;  one whose members are exactly one of those sets reads as that kind, since the json
//can't tell them apart.
export type JsonShape = 'timestamp'|'long'|'qualifiedName'|'nodeId'|'expandedNodeId'|'extensionObject'|'empty';
export function jsonShape( o:Record<string,unknown> ):JsonShape|undefined{
	if( !Object.keys(o).length )
		return 'empty';
	if( 'seconds' in o && 'nanos' in o && within(o, ['seconds', 'nanos']) )
		return 'timestamp';
	if( 'low' in o && 'high' in o && 'unsigned' in o && within(o, ['low', 'high', 'unsigned']) )
		return 'long';
	if( typeof o['name']=='string' && within(o, ['ns', 'name']) )
		return 'qualifiedName';
	if( nodeIdKeys(o) )
		return 'nodeId';
	if( ('nsu' in o || 'serverIndex' in o || 'serverindex' in o) && nodeIdKeys(o, ['nsu', 'serverIndex', 'serverindex']) )//the gateway spells the index `serverindex` (ExNodeId.cpp)
		return 'expandedNodeId';
	const typeId = o['typeId'];
	if( isPlain(typeId) && nodeIdKeys(typeId) && within(o, ['typeId', 'body']) )
		return 'extensionObject';
	return undefined;
}
//A plain object of members, of no other kind's shape (jsonShape).
export function isStructure( value:unknown ):value is Structure{ return isPlain( value ) && !jsonShape( value ); }
//An array of them, which a cell shows as a table each.
export function isStructures( value:unknown ):value is Structure[]{ return Array.isArray( value ) && value.length>0 && value.every( isStructure ); }

//A structure no type explains, as it travels - the encoding's type id and the encoded body:  a type the server gives no DataTypeDefinition
//for, a pre-1.04 server's say, which the gateway's registry cannot build (clients spec *Structures*).  The cell says so rather than showing
//the bytes.
export class ExtensionObject{
	constructor( public typeId:NodeId, public body?:Uint8Array|string ){}
	toString():string{ return `undecoded ${this.typeId.uaString()}`; }
	get tooltip():string{ return `The server gives no definition for the type ${this.typeId.uaString()}, so the value cannot be decoded.`; }
}

//A value as a write sends it - the json Value::Set reads for its type (clients spec *Values and writes*):  a NodeId as NodeId::ToJson
//spells one, an ExpandedNodeId with its uri and server index, a Guid its text, a ByteString {b: base64}, a status {sc};  a structure as
//its members, each this way.  A body no type explains has no json to write.
export function valueJson( value: Value ):any{
	if( value instanceof ExNodeId )
		return { ...value.toJson(), ...(value.nsu ? {nsu: value.nsu} : {}), ...(value.serverIndex ? {serverindex: value.serverIndex} : {}) };
	else if( value instanceof NodeId )
		return value.toJson();
	else if( value instanceof Guid )
		return value.toString();
	else if( value instanceof Uint8Array )
		return {b: btoa(value.reduce((acc, current) => acc + String.fromCharCode(current), "")) };
	else if( value instanceof OpcError )
		return {sc: value.sc };
	else if( value instanceof ExtensionObject )//the body no type explains:  Value::Set builds a structure from its members, which this has none of
		throw new Error( `The value is undecoded, so it cannot be written - ${value.typeId.uaString()}.` );
	else if( Array.isArray(value) )
		return value.map( x=>valueJson(x) );
	else if( isStructure(value) )
		return Object.fromEntries( Object.entries(value).map( ([name, member])=>[name, member===null ? null : valueJson(member)] ) );
	else
		return value;
}
//A ByteString's text, the hex REST's json gives (ByteStringToJson), and the bytes of such text:  none for an odd count of digits or
//a character that is no hex digit.  Blank is no bytes.
export function hexOf( bytes:Uint8Array ):string{ return [...bytes].map( b=>b.toString( 16 ).padStart( 2, '0' ) ).join( '' ); }
export function bytesOfHex( text:string ):Uint8Array|undefined{
	const t = text.trim();
	if( t.length%2 || !/^[0-9a-fA-F]*$/.test( t ) )
		return undefined;
	return Uint8Array.from( t.match( /../g ) ?? [], pair=>parseInt( pair, 16 ) );
}
//A Guid from its text, 8-4-4-4-12 hex digits as a read spells one;  none for any other text.
export function parseGuid( text:string ):Guid|undefined{
	const t = text.trim();
	return /^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$/.test( t ) ? new Guid( t.toLowerCase() ) : undefined;
}

//A Float reading at the fewest digits that are still that float.  The proto's float decodes widened to a double, 21.3 as
//21.299999237060547, which every screen showed;  the gateway's json writes a Float this way already (Value::ToJson).
//a number box's text as a write sends it.  A 64-bit integer goes as a Long built from the text, since a JS number holds 53
//bits and the number input hands the digits over whole.  Blank text, text that is not a number, and for a 64-bit type text
//that is not a whole number in its range, is no value.
export function numberValue( text:string, dataType:ETypes|undefined ):number|Long|undefined{
	const t = text.trim();
	if( dataType!=ETypes.Int64 && dataType!=ETypes.UInt64 )
		return t=="" || isNaN( +t ) ? undefined : +t;
	const digits = t.replace( /^\+/, "" );
	if( !/^-?\d+$/.test(digits) )
		return undefined;
	const value = Long.fromString( digits, dataType==ETypes.UInt64 );
	return value.toString()==digits.replace( /^(-?)0+(?=\d)/, "$1" ) ? value : undefined;//fromString wraps silently past the type's range, so what reads back has to be what was typed
}

export function shortestFloat( x:number ):number{
	if( !Number.isFinite(x) )
		return x;
	const f = Math.fround( x );
	for( let digits=1; digits<9; ++digits ){
		const y = Number( x.toPrecision(digits) );
		if( Math.fround(y)===f )
			return y;
	}
	return Number( x.toPrecision(9) );//always the float again
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
	else if( Object.hasOwn(value, "seconds") && Object.hasOwn(value, "nanos") && (!isPlain(value) || jsonShape(value)=='timestamp') ){//local time to the millisecond, as every timestamp column and DateTime editor shows one (NodeView.time) - not UTC (reviews-issue/417-clients-attributes.md W1)
		const date = ProtoUtils.toDate( <Timestamp>value );
		return date ? DateUtils.toLocalInput( date ).replace( "T", " " ) : "";//unset is seconds==0
	}
	else if( isPlain(value) )
		return plainText( value );
	else if( value instanceof ExtensionObject )
		return value.toString();
	else if( value instanceof ExNodeId )
		return JSON.stringify(value.toJson());
	else if( value instanceof NodeId )
		return value.uaString();//as REST's {ns,i} spells, so a push doesn't change a cell's text
	else if( value instanceof OpcError )
		return value.toString();
	else if( Array.isArray(value) )
		return value.map( x=>valueString(x) ).join( ", " );//`this.` was stale — these are free functions now
	else if( typeof value === "object" )
		return JSON.stringify( value, jsonText );
	else
		return `unknown type ${typeof value}`;
}
//A member's Long, Guid or undecoded body as its text, and the Long form too, should one reach here unconverted.
const jsonText = ( _:string, v:any )=>v instanceof Long || v instanceof Guid || v instanceof ExtensionObject ? v.toString() : isPlain( v ) && jsonShape( v )=='long' ? new Long( <number>v['low'], <number>v['high'], <boolean>v['unsigned'] ).toString() : v;
//A plain object as its kind spells it (jsonShape):  a QualifiedName ns:name, a NodeId as the standard writes one, {} as nothing, and a
//structure - or an ExpandedNodeId, which keeps its uri - as its json.
function plainText( v:Record<string,unknown> ):string{
	switch( jsonShape(v) ){
	case 'qualifiedName': return `${v['ns'] ?? 0}:${v['name']}`;
	case 'nodeId': return new NodeId( v ).uaString();
	case 'long': return new Long( <number>v['low'], <number>v['high'], <boolean>v['unsigned'] ).toString();
	case 'extensionObject': return new ExtensionObject( new NodeId(v['typeId']) ).toString();
	case 'empty': return "";
	default: return JSON.stringify( v, jsonText );
	}
}

//A reading's wrapper, by its whole key set (Value::ToJson, DataValueJson):  {v,sc} the quality beside the value, {sc} a Bad one with
//none, `source`/`server` a dataValue read's timestamps.  A structure whose members are exactly some of those reads as one.
const readingKeys = ['v', 'sc', 'source', 'server'];
const isReading = ( json:unknown ):json is { v?:any, sc?:StatusCode, source?:any, server?:any }=>isPlain( json ) && ('v' in json || 'sc' in json) && within( json, readingKeys );
export function toValue( json:any ):Value{
	if( isReading(json) )//the value before the 'sc' test on purpose: sc-first turned an Uncertain reading into an OpcError and discarded it
		return 'v' in json ? toValue( json.v ) : new OpcError( json.sc, "OpcError", "", undefined );//an OpcError, so valueString spells it
	if( Array.isArray(json) )//per element: ToJson emits one entry per array element, each unwrapped as a scalar is
		return json.map( x=>toValue(x) );
	return plainValue( json );
}
//A value's json inside the wrapper (jsonShape):  the Long form a Long, an opaque body - one no type explains (Variant.cpp
//extensionObjectToJson) - an ExtensionObject, and a structure member by member, so it has the shape the socket's has
//(Gateway.toStructure);  a NodeId, a QualifiedName, a DateTime as it came, which valueString spells by its shape.
function plainValue( json:any ):Value{
	if( !isPlain(json) )
		return json;
	switch( jsonShape(json) ){
	case 'long': return new Long( <number>json['low'], <number>json['high'], <boolean>json['unsigned'] );
	case 'extensionObject': return new ExtensionObject( new NodeId(json['typeId']), <Uint8Array|string|undefined>json['body'] );
	case undefined: return <Structure>Object.fromEntries( Object.entries(json).map( ([name, member])=>[name, memberValue(member)] ) );
	default: return <Value><unknown>json;
	}
}
const memberValue = ( json:unknown ):Value|null=>json===null ? null : Array.isArray( json ) ? json.map( x=><Value>memberValue(x) ) : plainValue( json );

//The quality beside the reading.  toValue() returns the reading alone, so an Uncertain {v,sc} lost its sc over REST
//while the socket carried it in SubscriptionResult.sc.  undefined/0 = Good; a Bad reading carries no `v` and toValue
//already turns it into an OpcError, which holds the same code.
export function valueSc( json:any ):StatusCode|undefined{
	return isReading( json ) && 'sc' in json ? <StatusCode>json.sc : undefined;
}

//A reading whole - the value and its quality together, which is how every path hands one to a row (Variable.setReading).
//sc 0/undefined = Good.  No `value` = the server sent none:  REST drops it on Bad (`{sc}`), where the socket still carries
//the one the server holds.  `source`/`server` are the reading's timestamps when the server sent them - a push carries both,
//REST none.
export type Reading = { value?:Value, sc?:StatusCode, source?:Date, server?:Date };
export function toReading( json:any ):Reading{
	const sc = valueSc( json ) ?? 0;
	const y:Reading = isReading(json) && !('v' in json) ? {sc} : { value: toValue(json), sc };
	//the timestamps a `dataValue` read carries (Value::DataValueJson), as a push does:  the Attributes tab's Value row.  Off the DataValue's
	//shape alone, which always has `sc`:  a structure's `source` member is the value's (reviews-issue/417-clients-attributes.md #5)
	if( isReading(json) ){
		if( json.source )
			y.source = ProtoUtils.toDate( json.source ) ?? undefined;
		if( json.server )
			y.server = ProtoUtils.toDate( json.server ) ?? undefined;
	}
	return y;
}