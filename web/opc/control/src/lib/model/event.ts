import { ProtoUtils, Timestamp } from 'jde-framework';
import { hexOf, isPlain, jsonShape, Value, valueString } from './value';

//An event as an events subscription delivers one (clients spec *Events*, #424; Opc.FromServer.proto EventValues):  one field per
//clause of the subscription's list, in its order - BaseEventType's eight base fields first, then the browse paths the tab selected -
//each a Value, or undefined where the server left the field empty.  `historyEvents` answers the same lists.
export const baseEventFields = ['EventId', 'EventType', 'SourceNode', 'SourceName', 'Time', 'ReceiveTime', 'Message', 'Severity'] as const;
export const enum EBaseField{ EventId, EventType, SourceNode, SourceName, Time, ReceiveTime, Message, Severity }
export type EventFields = (Value|undefined)[];
//A row of the Events table:  its fields, the EventId as hex - the row's key, which a history fill dedupes on - and the Time as a Date,
//what the table orders by;  `received` is when this browser got it, for a row whose Time the server left out.
export type EventRow = { key:string; time:Date|undefined; received:Date; fields:EventFields };
let anonymous = 0;//a row with no EventId gets a key of its own:  never deduped
export function toEventRow( fields:EventFields, received:Date=new Date() ):EventRow{
	const id = fields[EBaseField.EventId];
	const key = id instanceof Uint8Array && id.length ? hexOf( id ) : typeof id=='string' && id ? id : `anonymous/${++anonymous}`;//REST's json spells a ByteString as hex
	return { key, time: eventTime( fields[EBaseField.Time] ), received, fields };
}
//a Time field as a Date:  the {seconds, nanos} the socket's push and REST's json both carry (Gateway.toValue), as valueString reads one
export function eventTime( v:Value|undefined ):Date|undefined{
	if( v instanceof Date )
		return v;
	if( v && typeof v=='object' && Object.hasOwn( v, 'seconds' ) && Object.hasOwn( v, 'nanos' ) && (!isPlain( v ) || jsonShape( v )=='timestamp') )
		return ProtoUtils.toDate( <Timestamp>v ) ?? undefined;
	return undefined;
}
//a LocalizedText as both paths give one, {text, locale?} (Gateway.toValue, Variant.cpp):  text, not a structure of two members
export function isLocalizedText( v:unknown ):v is {text:string; locale?:string}{
	return isPlain( v ) && typeof v['text']=='string' && Object.keys( v ).every( k=>k=='text' || k=='locale' );
}
//a field as a cell shows it:  a LocalizedText its text, the rest as any value
export function eventFieldText( v:Value|undefined ):string{
	return isLocalizedText( v ) ? v.text : valueString( v );
}
//A selected field's browse path below the event type, as the gateway reads one (EventClause::Parse):  segments split on `/`, each
//`name`, `2:name` or `2~name`, the namespace index a UInt16.  Why a path is refused, or nothing.
export function selectProblem( path:string ):string|undefined{
	const t = path.trim();
	if( !t )
		return "A field is a browse path, such as Rpm or 2:Rpm.";
	for( const segment of t.split( '/' ) ){
		const pos = segment.search( /[:~]/ );
		const digits = pos==-1 ? undefined : segment.slice( 0, pos );
		if( digits!==undefined && !/^\d+$/.test( digits ) )
			return `'${path}' has a namespace index that is no number - '${digits}'.`;
		if( digits!==undefined && +digits>65535 )
			return `'${path}' names a namespace index over 65535.`;
		if( !segment.slice( pos+1 ).trim() )
			return `'${path}' has an empty segment.`;
	}
	return undefined;
}
//A valid path as the gateway compares one (EventClause::Key):  namespace 0 left out, any other index before a `:`.
function canonicalPath( path:string ):string{
	return path.trim().split( '/' ).map( segment=>{
		const pos = segment.search( /[:~]/ );
		if( pos==-1 )
			return segment;
		const ns = +segment.slice( 0, pos ), name = segment.slice( pos+1 );
		return ns ? `${ns}:${name}` : name;
	} ).join( '/' );
}
//Whether a path names a base field or one of `select`:  the gateway delivers a field once, so a repeat would shift the columns after it.
export function repeatsField( path:string, select:string[] ):boolean{
	const key = canonicalPath( path );
	return (<readonly string[]>baseEventFields).includes( key ) || select.some( s=>canonicalPath( s )==key );
}
//A field list kept on this browser, less what the gateway would refuse and what repeats a field.
export function keptSelect( kept:unknown ):string[]{
	const y:string[] = [];
	if( Array.isArray( kept ) ){
		for( const path of kept ){
			if( typeof path=='string' && !selectProblem( path ) && !repeatsField( path, y ) )
				y.push( path );
		}
	}
	return y;
}
//The table's columns for a select list:  the base fields but the EventId, which is the row's key, then the selected paths, each
//keyed by its place in the field list.
export type EventColumn = { key:string; label:string; index:number };
export function eventColumns( select:string[] ):EventColumn[]{
	const y:EventColumn[] = baseEventFields.map( ( label, index )=>({ key: label, label, index }) ).filter( c=>c.index!=EBaseField.EventId );
	select.forEach( ( path, i )=>y.push( { key: `select/${i}`, label: path, index: baseEventFields.length+i } ) );
	return y;
}