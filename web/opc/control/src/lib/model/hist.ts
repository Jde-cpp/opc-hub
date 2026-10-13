import { ProtoUtils, Timestamp } from 'jde-framework';
import { NodeId, NodeKey } from './node-id';
import { OpcError } from './opc-error';
import { ENodeClass, Variable } from './node';
import { CnnctnSlug } from './server-cnnctn';
import { isBad, nameKey } from './status-code';
import { Browse, StatusCode, toLocalizedText } from './types';
import { toValue, Value, valueJson } from './value';

//Where a read comes from:  a server's own history over its connection, read through the gateway (spec *Pass-through*), or a
//group the gateway's historian keeps, once it keeps them.  One result shape for both.
export type HistSource = { opc:CnnctnSlug; group?:undefined } | { group:number; opc?:undefined };
//one key per source, for what belongs to one:  the aggregates listed, the aggregate picked, a group's capabilities
export function histSourceKey( s:HistSource ):string{ return s.group!=undefined ? `group/${s.group}` : `opc/${s.opc}`; }
//A group the gateway's historian keeps, as historyGroups answers it for a page's nodes (spec *Reads*):  its id, the name it is
//shown by, and those of the nodes asked that it holds, spelled as a read spells them back.  A node is in one group per connection
//at most, so this says where the gateway keeps each node's history, beside what its server keeps.
export type HistGroup = { id:number; name:string; nodes:NodeId[] };
//A group's members among a page's nodes:  a History tab's source beside the server's own history.
export type HistGroupMembers = { id:number; name:string; nodes:Variable[] };
//What this user may do with a group's history, as historyCapabilities answers it:  the aggregates the historian computes, which an
//aggregate read with `group` names, and the edits - Update for an insert, replace or update, Delete for a purge, each a grant on
//an enforced resource, which the gateway refuses the edits without (spec *Authorization*).  A server's node says both with
//userAccessLevel's HistoryWrite bit;  a group has nothing like it, so the gateway is asked.
export type HistRights = { update:boolean; delete:boolean };
export type HistCapabilities = HistRights & { aggregates:HistAggregate[] };

//ModificationInfo, with `modified: true`:  what an edit did, when and who (spec *Reads*).
export type HistModification = { time:Date|null; type:string; user:string };
//A time as the gateway's json carries it, UADateTime's (libs/opc/src/uatypes/DateTime.cpp):  to the 100 ns tick, which a Date
//cuts to the millisecond.  An edit names a stored record by it.
export type HistTime = { seconds:number; nanos:number };
//One value of a `history` page (apps/OpcGateway/config/introspection/hist.jsonnet), its times as Dates.
export type HistValue = {
	node:NodeId;
	source:Date|null;//with no source time the server's, as the gateway's merge orders it;  null with neither:  no place on a time axis
	server:Date|null;
	sourceTime?:HistTime;//the source time to the tick, what an edit of the record names;  none for a push, which ts-proto cuts to a Date
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
//query the trend opens with;  a continuation pages the read before.  `aggregate` with `interval` makes it an aggregate read,
//of both ends:  one value a node per interval, Part 11's ReadProcessed.
export type HistReadArgs = { nodes:NodeId[]; start?:Date; end?:Date; limit?:number; bounds?:boolean; modified?:boolean; interval?:number; aggregate?:string; continuation?:string };
//An aggregate read's shape (spec *Reads*):  the function by the browse name of its AggregateFunction object, Part 13's or one
//the server lists, over intervals of `interval` milliseconds, as Part 11's processingInterval is.
export type HistAggregation = { aggregate:string; interval:number };
//An aggregate the server lists:  an object of its HistoryServerCapabilities/AggregateFunctions folder, which an aggregate read
//names by its browse name (spec *Pass-through*), shown by its display name.
export type HistAggregate = { name:string; browse:string };
export const aggregateFunctionsFolder = new NodeId( {ns: 0, i: 11201} );//HistoryServerCapabilities/AggregateFunctions
//Bad_NoData:  Part 13's answer for an interval with no record, and a node's for a range with none.
export const BadNoData:StatusCode = 0x809B0000;
export function isNoData( sc:StatusCode ):boolean{ return nameKey( sc )==BadNoData; }
//Bad_BoundNotFound:  a bound where the node has no record, as a subscription's snapshot opens a node with none before fromTime
export const BadBoundNotFound:StatusCode = 0x80D70000;
export function isBoundNotFound( sc:StatusCode ):boolean{ return nameKey( sc )==BadBoundNotFound; }

function toDate( json:any ):Date|null{ return json ? ProtoUtils.toDate( <Timestamp>json ) : null; }
function toHistTime( json:any ):HistTime|undefined{ return json ? { seconds: Number( json.seconds ), nanos: Number( json.nanos ?? 0 ) } : undefined; }
export function toHistValue( json:any ):HistValue{
	const m = json.modification;
	const value = json.value==null ? undefined : toValue( json.value );
	return {
		node: new NodeId( json.node ),
		source: toDate( json.source ) ?? toDate( json.server ),
		server: toDate( json.server ),
		sourceTime: toHistTime( json.source ),
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
export function toHistGroups( json:any ):HistGroup[]{
	return ( <any[]>(json?.groups ?? []) ).map( g=>({ id: Number( g.id ), name: String( g.name ?? '' ), nodes: ( <any[]>(g.nodes ?? []) ).map( n=>new NodeId( n ) ) }) );
}
//a group's aggregates are the library's, named alone:  the name is the browse name, as a read takes it
export function toHistCapabilities( json:any ):HistCapabilities{
	return { aggregates: ( <any[]>(json?.aggregates ?? []) ).map( a=>({ name: String( a ), browse: String( a ) }) ), update: !!json?.update, delete: !!json?.delete };
}
//the folder's objects as node{ children{ name browse nodeClass } } lists them, in the server's order:  an aggregate is an
//object, by Part 13's AggregateFunctionType, and nothing else there is one.  Labelled by its display name, or its browse name
//for one with no text.
export function toHistAggregates( json:any ):HistAggregate[]{
	return ( <any[]>(json?.children ?? []) ).filter( c=>c.nodeClass==ENodeClass.Object && (<Browse|undefined>c.browse)?.name )
		.map( c=>({ name: String( toLocalizedText( c.name )?.text || c.browse.name ), browse: String( c.browse.name ) }) );
}
//`ms` rounded up to a multiple of `interval` on the local clock, so an aggregate read's intervals sit on its boundaries:  the
//minute, the hour, local midnight.
export function alignUp( ms:number, interval:number ):number{
	const offset = new Date( ms ).getTimezoneOffset()*60_000;
	return Math.ceil( (ms-offset)/interval )*interval+offset;
}
//A time as hist's DateTime arguments take it, UADateTime's json (libs/opc/src/uatypes/DateTime.cpp):  plain numbers, not the
//Long ProtoUtils.fromDate builds, which JSON would write as {low,high,unsigned}.
export function qlTime( d:Date|HistTime ):HistTime{
	if( !(d instanceof Date) )
		return { seconds: d.seconds, nanos: d.nanos };
	const ms = d.getTime();
	const seconds = Math.floor( ms/1000 );
	return { seconds, nanos: (ms-seconds*1000)*1_000_000 };
}
const time = ( v:HistValue )=>v.source ? v.source.getTime() : Number.POSITIVE_INFINITY;
//the same record, among those at one source time:  the node's, and in a modified read the same edit's - a time can hold
//several modifications of one node, an Insert and the Delete that took it back, told apart by when and how they were made
function sameRecord( a:HistValue, b:HistValue ):boolean{
	return a.node.key==b.node.key && a.modification?.type==b.modification?.type && a.modification?.time?.getTime()==b.modification?.time?.getTime();
}
//the first of `values`, in time order, at `t` or after it:  values.length when none is
function lowerBound( values:HistValue[], t:number ):number{
	let lo = 0, hi = values.length;
	while( lo<hi ){
		const mid = (lo+hi)>>>1;
		if( time(values[mid])<t ) lo = mid+1; else hi = mid;
	}
	return lo;
}
//where `values`, in time order, holds v's record, or -1:  a binary search to the time, then its few records
function find( values:HistValue[], v:HistValue ):number{
	const t = time( v );
	for( let i=lowerBound( values, t ); i<values.length && time(values[i])==t; ++i ){
		if( sameRecord(values[i], v) )
			return i;
	}
	return -1;
}
//`existing` with what `incoming` adds, in source-time order, values with no time last;  the same array back when nothing was
//new, so a signal holding it stays quiet.  One record per node and source time:  a subscription's first push is the value the
//read already holds.  Millisecond precision, a Date's:  two records of one node inside a millisecond fold, which no trend could
//draw apart anyway.  `existing` must be in that order already, as every merge leaves it.  A live push, newest, is an append.
//A read's record takes the place of its push, whose time is a Date's, so an edit can name it.
export function mergeHistValues( existing:HistValue[], incoming:HistValue[] ):HistValue[]{
	const added:HistValue[] = [];
	let upgraded:HistValue[]|undefined;
	for( const v of incoming.length>1 ? [...incoming].sort( (a,b)=>time(a)-time(b) ) : incoming ){
		const t = time( v );
		let twin = false;//the same record earlier in `incoming`:  sorted, so among the last added
		for( let i=added.length-1; i>=0 && time(added[i])==t && !twin; --i )
			twin = sameRecord( added[i], v );
		if( twin )
			continue;
		const at = find( existing, v );
		if( at<0 )
			added.push( v );
		else if( v.sourceTime && !existing[at].sourceTime )
			( upgraded ??= [...existing] )[at] = v;
	}
	existing = upgraded ?? existing;
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
//Where a group's subscription resumes after a break:  the oldest of its nodes' latest source times, so a node whose source clock
//runs behind another's keeps what was stored meanwhile - from the newest of them, it got its bound alone (historian-live-trend
//#5) - but no further back than histResumeReach before the newest, since a node quiet that long has its value as the bound.
//`latest`:  each node's latest source time held.  Undefined for none.
export function histResumeTime( latest:Iterable<Date> ):Date|undefined{
	let oldest = Number.POSITIVE_INFINITY, newest = Number.NEGATIVE_INFINITY;
	for( const t of latest ){
		oldest = Math.min( oldest, t.getTime() );
		newest = Math.max( newest, t.getTime() );
	}
	return newest==Number.NEGATIVE_INFINITY ? undefined : new Date( Math.max(oldest, newest-histResumeReach) );
}
export const histResumeReach = 10*60_000;//ms
//each node's latest source time among `values`, for histResumeTime
export function latestHistTimes( values:HistValue[] ):Map<NodeKey,Date>{
	const y = new Map<NodeKey,Date>();
	for( const v of values ){
		const held = y.get( v.node.key );
		if( v.source && (!held || v.source>held) )
			y.set( v.node.key, v.source );
	}
	return y;
}
//A modification pushed on a group's subscription (spec *Subscriptions*):  what an edit did at `target`, the value it put there,
//none for a delete, and when and who made it.  What it replaced isn't pushed:  the client holds it.
export type HistPushKind = 'insert'|'replace'|'update'|'delete';
export type HistPushedModification = { node:NodeId; target:Date; kind:HistPushKind; value?:HistValue; time:Date|null; user:string };
//`values` with a pushed modification applied, in source-time order, so the trend redraws the point it targets:  a delete takes
//every record of the node at the target, as the edit did (spec *Edits*), and the others put the value pushed in place of the
//node's last record there, or among the values where it falls when none is.  One before the earliest value held is left to
//Load earlier, which reads it - unless `whole`, the values back to the start of the history, when nothing will read it and it
//goes first (historian-live-trend #4).  The same array back when nothing changed.  A Date's millisecond, as the merge folds
//records.
export function applyHistModification( values:HistValue[], m:HistPushedModification, whole=false ):HistValue[]{
	const t = m.target.getTime();
	const put = m.kind=='delete' || !m.value ? undefined : {...m.value, source: m.target};
	if( (m.kind!='delete' && !put) || (!whole && (!values.length || t<time(values[0]))) )
		return values;
	const mine:number[] = [];
	let end = lowerBound( values, t );
	for( ; end<values.length && time(values[end])==t; ++end ){
		if( values[end].node.key==m.node.key )
			mine.push( end );
	}
	if( !put )
		return mine.length ? values.filter( (_, i)=>!mine.includes(i) ) : values;
	const y = [...values];
	if( mine.length )
		y[mine[mine.length-1]] = put;
	else
		y.splice( end, 0, put );
	return y;
}
//A records event's modifications applied in order, as applyHistModification applies each, but a run of deletes in one pass:  a
//range purge pushes a delete per record it took, and a filter of all held for each stalled the page (historian-live-trend #12).
export function applyHistModifications( values:HistValue[], ms:HistPushedModification[], whole=false ):HistValue[]{
	let deletes = new Map<number,Set<NodeKey>>();//target time -> nodes
	const flush = ()=>{
		if( !deletes.size )
			return;
		const kept = values.filter( v=>!deletes.get( time(v) )?.has( v.node.key ) );
		if( kept.length<values.length )
			values = kept;
		deletes = new Map();
	};
	for( const m of ms ){
		if( m.kind!='delete' ){
			flush();
			values = applyHistModification( values, m, whole );
		}
		else if( deletes.has( m.target.getTime() ) )
			deletes.get( m.target.getTime() )!.add( m.node.key );
		else
			deletes.set( m.target.getTime(), new Set( [m.node.key] ) );
	}
	flush();
	return values;
}

//The edits (spec *Edits*):  Part 11's UpdateData as Insert, a value at a time that holds none, Replace, one at a time that
//holds one, and Update, either;  and a purge, of a range or at times.  Each is a QL mutation by its whole name.
export type HistEditKind = 'insert'|'replace'|'update'|'purgeRange'|'purgeTimes';
export const histEditCommands:Record<HistEditKind,string> = { insert: 'createHistory', replace: 'updateHistory', update: 'upsertHistory', purgeRange: 'purgeHistory', purgeTimes: 'purgeHistory' };
//A value an UpdateData writes, in the shape a read returns one.  No status is Good, and with no server time the server stamps
//its own.  The gateway types the value by the node's DataType and refuses one that can't take it, naming the node and value.
export type HistEditValue = { node:NodeId; source:Date|HistTime; server?:Date; status?:StatusCode; value:Value };
export type HistEditArgs =
	{ kind:'insert'|'replace'|'update'; values:HistEditValue[] } |
	{ kind:'purgeRange'; nodes:NodeId[]; start:Date|HistTime; end:Date|HistTime } |//the range is the server's:  OpcServer leaves out its end, and start equal to end is the one value there
	{ kind:'purgeTimes'; nodes:NodeId[]; times:Date[] };//DeleteAtTime, which OpcServer refuses with Bad_NotSupported
//What an edit answers:  a row per value, or per node and time for a purge at times, with the server's operation result or
//its node's entry status, beside each node's entry status.  A range purge answers per node alone.
export type HistEditValueResult = { node:NodeId; source:Date|null; status:StatusCode };
export type HistEditResult = { values:HistEditValueResult[]; nodes:HistNodeStatus[] };
export function toHistEditResult( json:any ):HistEditResult{
	return {
		values: ( <any[]>(json?.values ?? []) ).map( v=>({ node: new NodeId(v.node), source: toDate(v.source), status: <StatusCode>(v.status ?? 0) }) ),
		nodes: ( <any[]>(json?.nodes ?? []) ).map( n=>({ node: new NodeId(n.node), status: <StatusCode>(n.status ?? 0) }) )
	};
}
//whether the server refused any of it:  a Bad operation result, or a Bad entry status, which a range purge answers with alone
export function editRefused( r:HistEditResult ):boolean{
	return r.values.some( v=>isBad(v.status) ) || r.nodes.some( n=>isBad(n.status) );
}
//whether it refused all of it:  every value Bad, or for a range purge, every node.  Any part taken changed the history.
export function editRefusedAll( r:HistEditResult ):boolean{
	const parts = r.values.length ? r.values : r.nodes;
	return parts.length>0 && parts.every( p=>isBad(p.status) );
}
//an UpdateData's value as the mutation's `values` argument carries it:  only the parts given, as the read's arguments go
export function editValueJson( v:HistEditValue ):Record<string,unknown>{
	const y:Record<string,unknown> = { node: v.node.toJson(), source: qlTime(v.source), value: valueJson(v.value) };
	if( v.server )
		y["server"] = qlTime( v.server );
	if( v.status!==undefined )
		y["status"] = v.status;
	return y;
}