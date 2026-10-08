import { ProtoUtils, Timestamp } from 'jde-framework';
import { NodeId } from './node-id';
import { OpcError } from './opc-error';
import { CnnctnSlug } from './server-cnnctn';
import { StatusCode } from './types';
import { toValue, Value } from './value';

//Where a read comes from:  a server's own history over its connection, read through the gateway (spec *Pass-through*), or a
//group the gateway's historian keeps, once it keeps them.  One result shape for both.
export type HistSource = { opc:CnnctnSlug; group?:undefined } | { group:number; opc?:undefined };

//ModificationInfo, with `modified: true`:  what an edit did, when and who (spec *Reads*).
export type HistModification = { time:Date|null; type:string; user:string };
//One value of a `hist` page (apps/OpcGateway/config/introspection/hist.jsonnet), its times as Dates.
export type HistValue = {
	node:NodeId;
	source:Date|null;//with no source time the server's, as the gateway's merge orders it;  null with neither:  no place on a time axis
	server:Date|null;
	status:StatusCode;//0 = Good
	value?:Value;//the server sent none, or only its status:  a Bad reading's shape
	bound:boolean;//one of the bounds returnBounds asked for
	heartbeat:boolean;//a group's stored repeat of its last value;  never a server's
	modification?:HistModification;
};
export type HistNodeStatus = { node:NodeId; status:StatusCode };
//A page:  the values in source-time order, later first in a reverse read, the continuation for the next page, null on the
//last, and each node's status as the server answered it, Good_NoData for one with nothing in the range.
export type HistPage = { values:HistValue[]; continuation:string|null; nodes:HistNodeStatus[] };
//hist's arguments (spec *Reads*):  a start after end reads in reverse;  an end alone reads backward from it, the "last N"
//query the trend opens with;  a continuation pages the read before.
export type HistReadArgs = { nodes:NodeId[]; start?:Date; end?:Date; limit?:number; bounds?:boolean; modified?:boolean; continuation?:string };

function toDate( json:any ):Date|null{ return json ? ProtoUtils.toDate( <Timestamp>json ) : null; }
export function toHistValue( json:any ):HistValue{
	const m = json.modification;
	const value = json.value==null ? undefined : toValue( json.value );
	return {
		node: new NodeId( json.node ),
		source: toDate( json.source ) ?? toDate( json.server ),
		server: toDate( json.server ),
		status: <StatusCode>( json.status ?? 0 ),
		value: value instanceof OpcError ? undefined : value,//a Bad reading's value is its status alone (Value::ToJson), which `status` holds
		bound: !!json.bound,
		heartbeat: !!json.heartbeat,
		modification: m ? { time: toDate(m.time), type: String(m.type ?? "Unknown"), user: String(m.user ?? "") } : undefined
	};
}
export function toHistPage( json:any ):HistPage{
	return {
		values: ( <any[]>(json?.values ?? []) ).map( toHistValue ),
		continuation: json?.continuation ?? null,
		nodes: ( <any[]>(json?.nodes ?? []) ).map( n=>({ node: new NodeId(n.node), status: <StatusCode>(n.status ?? 0) }) )
	};
}
//A time as hist's DateTime arguments take it, UADateTime's json (libs/opc/src/uatypes/DateTime.cpp):  plain numbers, not the
//Long ProtoUtils.fromDate builds, which JSON would write as {low,high,unsigned}.
export function qlTime( d:Date ):{seconds:number; nanos:number}{
	const ms = d.getTime();
	const seconds = Math.floor( ms/1000 );
	return { seconds, nanos: (ms-seconds*1000)*1_000_000 };
}
const time = ( v:HistValue )=>v.source ? v.source.getTime() : Number.POSITIVE_INFINITY;
//whether `values`, in time order, holds a record of v's node at v's time:  a binary search to the time, then its few records
function holds( values:HistValue[], v:HistValue ):boolean{
	const t = time( v );
	let lo = 0, hi = values.length;
	while( lo<hi ){
		const mid = (lo+hi)>>>1;
		if( time(values[mid])<t ) lo = mid+1; else hi = mid;
	}
	for( let i=lo; i<values.length && time(values[i])==t; ++i ){
		if( values[i].node.key==v.node.key )
			return true;
	}
	return false;
}
//`existing` with what `incoming` adds, in source-time order, values with no time last;  the same array back when nothing was
//new, so a signal holding it stays quiet.  One record per node and source time:  a subscription's first push is the value the
//read already holds.  Millisecond precision, a Date's:  two records of one node inside a millisecond fold, which no trend could
//draw apart anyway.  `existing` must be in that order already, as every merge leaves it.  A live push, newest, is an append.
export function mergeHistValues( existing:HistValue[], incoming:HistValue[] ):HistValue[]{
	const added:HistValue[] = [];
	for( const v of incoming.length>1 ? [...incoming].sort( (a,b)=>time(a)-time(b) ) : incoming ){
		const t = time( v );
		let twin = false;//the same record earlier in `incoming`:  sorted, so among the last added
		for( let i=added.length-1; i>=0 && time(added[i])==t && !twin; --i )
			twin = added[i].node.key==v.node.key;
		if( !twin && !holds(existing, v) )
			added.push( v );
	}
	if( !added.length )
		return existing;
	if( !existing.length || time(added[0])>=time(existing[existing.length-1]) )
		return existing.concat( added );
	const merged = new Array<HistValue>( existing.length+added.length );
	let i = 0, j = 0, k = 0;
	while( i<existing.length && j<added.length )
		merged[k++] = time(added[j])<time(existing[i]) ? added[j++] : existing[i++];
	while( i<existing.length )
		merged[k++] = existing[i++];
	while( j<added.length )
		merged[k++] = added[j++];
	return merged;
}
//A /opc push as a history value (the live tail).  One with no source time takes the server's, as the gateway's
//own merge does (apps/OpcGateway/src/ql/HistQLAwait.h, Time), and failing that the moment it arrived.  A subscribe that
//failed rides in as an OpcError value - a refusal, not a reading - and is not a value here.
export function pushValue( node:NodeId, r:{value?:Value; sc?:StatusCode; source?:Date; server?:Date}, now:Date ):HistValue{
	return { node, source: r.source ?? r.server ?? now, server: r.server ?? null, status: r.sc ?? 0, value: r.value instanceof OpcError ? undefined : r.value, bound: false, heartbeat: false };
}