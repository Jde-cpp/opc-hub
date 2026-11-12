import { ISlugRow, Mutation, MutationType, SlugRow, SlugRowProps } from 'jde-framework';
import { HistGroup } from './hist-admin';
import type { OeeMachineRef } from './oee-live-view';

//OEE's lines as the gateway's pages hold them (#409, docs/oee/spec.md *Lines*, *Web*):  oee_lines as a row its page saves
//whole, oee_line_members as rows edited one at a time, and the graph the two make - what a line reaches, what its Members
//picker may offer, and how the dashboard groups the machines under the lines that are in no line.

//ISA-95's levels below the enterprise, highest first:  a member line's is no higher than its parent's
export const oeeLevels = ['Site', 'Area', 'Line', 'Cell'] as const;
export type OeeLevel = typeof oeeLevels[number];
//a level's place, 0 the highest;  one that is none of the four is below them all
export function oeeLevelRank( level:string|undefined ):number{
	const i = oeeLevels.indexOf( <OeeLevel>level );
	return i<0 ? oeeLevels.length : i;
}

//A line:  a named set of machines and of lines at a level, with the calendar its buckets and its shift are by.
export class OeeLine extends SlugRow<OeeLine>{
	constructor( obj:OeeLineProps ){
		super( OeeLine.typeName, <SlugRowProps>obj );
		this.level = obj.level ?? undefined;
		this.calendarId = HistGroup.toId( obj.calendarId );
	}
	override get canSave():boolean{ return super.canSave && (<readonly string[]>oeeLevels).includes( this.level ?? '' ); }
	override equals( row:ISlugRow ):boolean{
		const o = <OeeLine>row;
		return super.equals( row ) && this.level==o.level && this.calendarId==o.calendarId;
	}
	//a create states the level, and the calendar when it has one;  an update, what changed, null for a calendar cleared
	override mutation( original:OeeLine ):Mutation[]{
		console.assert( this.canSave );
		const args:Record<string,unknown> = super.mutationArgs( original );
		const create = !original?.id;
		if( this.level!=original?.level )
			args['level'] = this.level;
		if( create ? this.calendarId!=undefined : this.calendarId!=original.calendarId )
			args['calendarId'] = this.calendarId ?? null;
		return Object.keys( args ).length ? [new Mutation( this.type, this.id, args, create ? MutationType.Create : MutationType.Update )] : [];
	}
	get properties():OeeLine{ return new OeeLine( this ); }
	level?:string;
	calendarId?:number;
	static typeName = 'OeeLine';
}
export type OeeLineProps = Partial<SlugRowProps> & { level?:string|null; calendarId?:number|string|null };

export type OeeLineRef = { id:number; name:string; slug:string; level:string };
export function toOeeLineRef( json:any ):OeeLineRef{
	return { id: Number( json.id ), name: String( json.name ?? json.slug ), slug: String( json.slug ?? '' ), level: String( json.level ?? '' ) };
}
//a member:  a machine or a line, one of the two, at its place in the line's order
export type OeeLineMember = { id:number; lineId:number; machineId?:number; memberLineId?:number; sort:number };
export function toOeeLineMember( json:any ):OeeLineMember{
	return { id: Number( json.id ), lineId: Number( json.oeeLineId ), machineId: json.oeeMachineId==null ? undefined : Number( json.oeeMachineId ), memberLineId: json.memberLineId==null ? undefined : Number( json.memberLineId ), sort: Number( json.sort ?? 0 ) };
}
//The gateway's live machines and lines, and every membership:  a deleted line keeps its members until it is purged, so a member
//may name a machine or a line that is in neither list, and is passed over.
export type OeePlant = { machines:OeeMachineRef[]; lines:OeeLineRef[]; members:OeeLineMember[] };

//a line's direct members in their order:  by `sort`, then as they were added
export function oeeMembersOf( plant:OeePlant, lineId:number ):OeeLineMember[]{
	return plant.members.filter( m=>m.lineId==lineId ).sort( ( a, b )=>a.sort-b.sort || a.id-b.id );
}
//`visit` each live line from `lineId` down, depth first in member order, each once, with its direct machines
function walk( plant:OeePlant, lineId:number, visit:( line:number, machines:number[] )=>void ):void{
	const live = new Set( plant.lines.map( l=>l.id ) ), seen = new Set<number>();
	const step = ( id:number )=>{
		if( seen.has( id ) || !live.has( id ) )
			return;
		seen.add( id );
		const members = oeeMembersOf( plant, id );
		visit( id, members.filter( m=>m.machineId!=undefined ).map( m=>m.machineId! ) );
		for( const m of members ){
			if( m.memberLineId!=undefined )
				step( m.memberLineId );
		}
	};
	step( lineId );
}
//The machines a line reaches, each once, in member order with a member line's where it stands:  the order of `oeeTimeline`.
export function oeeLineReach( plant:OeePlant, lineId:number ):OeeMachineRef[]{
	const byId = new Map( plant.machines.map( m=>[m.id, m] ) ), live = new Set( plant.lines.map( l=>l.id ) ), reached:OeeMachineRef[] = [], seen = new Set<number>();
	const step = ( id:number, path:Set<number> )=>{
		if( path.has( id ) || !live.has( id ) )
			return;
		for( const m of oeeMembersOf( plant, id ) ){
			const machine = m.machineId==undefined ? undefined : byId.get( m.machineId );
			if( machine && !seen.has( machine.id ) ){
				seen.add( machine.id );
				reached.push( machine );
			}
			else if( m.memberLineId!=undefined )
				step( m.memberLineId, new Set( [...path, id] ) );
		}
	};
	step( lineId, new Set() );
	return reached;
}
//the lines a line reaches, itself among them
function linesUnder( plant:OeePlant, lineId:number ):Set<number>{
	const lines = new Set<number>();
	walk( plant, lineId, id=>lines.add( id ) );
	return lines;
}

//What a line's Members picker offers (spec *Web*, *Lines*):  the machines it doesn't reach yet, and the lines of a level no
//higher than its own that aren't its members, don't reach it, and reach no machine it reaches - each machine is once within a
//line.  A line above this one may still reach a pick by another path:  that is the gateway's refusal.
export function oeeMemberCandidates( plant:OeePlant, lineId:number ):{machines:OeeMachineRef[]; lines:OeeLineRef[]}{
	const line = plant.lines.find( l=>l.id==lineId );
	if( !line )
		return { machines: [], lines: [] };
	const reached = new Set( oeeLineReach( plant, lineId ).map( m=>m.id ) ), rank = oeeLevelRank( line.level );
	const direct = new Set( plant.members.filter( m=>m.lineId==lineId && m.memberLineId!=undefined ).map( m=>m.memberLineId! ) );
	return {
		machines: plant.machines.filter( m=>!reached.has( m.id ) ),
		lines: plant.lines.filter( l=>l.id!=lineId && !direct.has( l.id ) && oeeLevelRank( l.level )>=rank && !linesUnder( plant, l.id ).has( lineId ) && !oeeLineReach( plant, l.id ).some( m=>reached.has( m.id ) ) )
	};
}

//The dashboard's order (spec *Web*, *The Gantt chart*):  the lines that are in no line, by level so the site or the areas come
//first, then by name, each with the machines it reaches, and after them the machines in no line.  A machine in a line has no
//tile of its own:  it is in its line's.
export type OeeLineGroup = { line:OeeLineRef; machines:OeeMachineRef[] };
export function oeeDashboardGroups( plant:OeePlant ):{groups:OeeLineGroup[]; loose:OeeMachineRef[]}{
	const live = new Set( plant.lines.map( l=>l.id ) ), held = plant.members.filter( m=>live.has( m.lineId ) );
	const memberLines = new Set( held.filter( m=>m.memberLineId!=undefined ).map( m=>m.memberLineId! ) ), inLine = new Set( held.filter( m=>m.machineId!=undefined ).map( m=>m.machineId! ) );
	const top = plant.lines.filter( l=>!memberLines.has( l.id ) ).sort( ( a, b )=>oeeLevelRank( a.level )-oeeLevelRank( b.level ) || a.name.localeCompare( b.name ) );
	return { groups: top.map( line=>({ line, machines: oeeLineReach( plant, line.id ) }) ), loose: plant.machines.filter( m=>!inLine.has( m.id ) ) };
}
//a line as its pages head it:  its level and its name
export function oeeLineTitle( line:{name:string; level?:string} ):string{ return line.level ? `${line.level} ${line.name}` : line.name; }