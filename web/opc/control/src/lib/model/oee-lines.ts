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
const memberOrder = ( a:OeeLineMember, b:OeeLineMember )=>a.sort-b.sort || a.id-b.id;
export function oeeMembersOf( plant:OeePlant, lineId:number ):OeeLineMember[]{
	return plant.members.filter( m=>m.lineId==lineId ).sort( memberOrder );
}
//The plant's graph, built once for the reads of it:  each line's members in their order, what each live line reaches - each
//machine once, in member order with a member line's where it stands, the order of `oeeTimeline` - and the live lines above
//one, through any path.  A loop, which the gateway refuses, ends the walk;  a reach cut short by one isn't kept.
type OeeGraph = { members:( lineId:number )=>OeeLineMember[]; reach:( lineId:number )=>OeeMachineRef[]; above:( lineId:number )=>Set<number> };
function oeeGraph( plant:OeePlant ):OeeGraph{
	const live = new Set( plant.lines.map( l=>l.id ) ), machines = new Map( plant.machines.map( m=>[m.id, m] ) );
	const byLine = new Map<number,OeeLineMember[]>(), parents = new Map<number,number[]>();
	for( const m of plant.members ){
		( byLine.get( m.lineId ) ?? byLine.set( m.lineId, [] ).get( m.lineId )! ).push( m );
		if( m.memberLineId!=undefined && live.has( m.lineId ) )
			( parents.get( m.memberLineId ) ?? parents.set( m.memberLineId, [] ).get( m.memberLineId )! ).push( m.lineId );
	}
	for( const list of byLine.values() )
		list.sort( memberOrder );
	const members = ( lineId:number )=>byLine.get( lineId ) ?? [];
	const reached = new Map<number,OeeMachineRef[]>(), walking = new Set<number>();
	let cut = false;
	const reach = ( lineId:number ):OeeMachineRef[]=>{
		const known = reached.get( lineId );
		if( known )
			return known;
		if( !live.has( lineId ) )
			return [];
		if( walking.has( lineId ) ){
			cut = true;
			return [];
		}
		const outer = cut, seen = new Set<number>(), y:OeeMachineRef[] = [];
		cut = false;
		walking.add( lineId );
		for( const m of members( lineId ) ){
			const machine = m.machineId==undefined ? undefined : machines.get( m.machineId );
			for( const x of machine ? [machine] : m.memberLineId!=undefined ? reach( m.memberLineId ) : [] ){
				if( !seen.has( x.id ) ){
					seen.add( x.id );
					y.push( x );
				}
			}
		}
		walking.delete( lineId );
		if( !cut )
			reached.set( lineId, y );
		cut ||= outer;
		return y;
	};
	const above = ( lineId:number ):Set<number>=>{
		const y = new Set<number>(), next = [lineId];
		for( let id = next.pop(); id!=undefined; id = next.pop() ){
			for( const parent of parents.get( id ) ?? [] ){
				if( parent!=lineId && !y.has( parent ) ){
					y.add( parent );
					next.push( parent );
				}
			}
		}
		return y;
	};
	return { members, reach, above };
}
//The machines a line reaches, each once, in member order with a member line's where it stands:  the order of `oeeTimeline`.
export function oeeLineReach( plant:OeePlant, lineId:number ):OeeMachineRef[]{ return oeeGraph( plant ).reach( lineId ); }

//What a line's Members picker offers (spec *Web*, *Lines*):  each machine is once within a line, this one and every line above
//it, so it offers the machines none of those reaches yet, and the lines of a level no higher than its own that aren't its
//members, don't hold it, and reach no machine any of those reaches.
export function oeeMemberCandidates( plant:OeePlant, lineId:number ):{machines:OeeMachineRef[]; lines:OeeLineRef[]}{
	const line = plant.lines.find( l=>l.id==lineId );
	if( !line )
		return { machines: [], lines: [] };
	const graph = oeeGraph( plant ), above = graph.above( lineId ), rank = oeeLevelRank( line.level );
	const taken = new Set( [lineId, ...above].flatMap( id=>graph.reach( id ).map( m=>m.id ) ) );
	const direct = new Set( graph.members( lineId ).filter( m=>m.memberLineId!=undefined ).map( m=>m.memberLineId! ) );
	return {
		machines: plant.machines.filter( m=>!taken.has( m.id ) ),
		lines: plant.lines.filter( l=>l.id!=lineId && !direct.has( l.id ) && !above.has( l.id ) && oeeLevelRank( l.level )>=rank && !graph.reach( l.id ).some( m=>taken.has( m.id ) ) )
	};
}

//The dashboard's order (spec *Web*, *The Gantt chart*):  the lines that are in no line, by level so the site or the areas come
//first, then by name, each with the machines it reaches, and after them the machines in no line.  A machine in a line has no
//tile of its own:  it is in its line's.
export type OeeLineGroup = { line:OeeLineRef; machines:OeeMachineRef[] };
export function oeeDashboardGroups( plant:OeePlant ):{groups:OeeLineGroup[]; loose:OeeMachineRef[]}{
	const live = new Set( plant.lines.map( l=>l.id ) ), held = plant.members.filter( m=>live.has( m.lineId ) ), graph = oeeGraph( plant );
	const memberLines = new Set( held.filter( m=>m.memberLineId!=undefined ).map( m=>m.memberLineId! ) ), inLine = new Set( held.filter( m=>m.machineId!=undefined ).map( m=>m.machineId! ) );
	const top = plant.lines.filter( l=>!memberLines.has( l.id ) ).sort( ( a, b )=>oeeLevelRank( a.level )-oeeLevelRank( b.level ) || a.name.localeCompare( b.name ) );
	return { groups: top.map( line=>({ line, machines: graph.reach( line.id ) }) ), loose: plant.machines.filter( m=>!inLine.has( m.id ) ) };
}
//a line as its pages head it:  its level and its name
export function oeeLineTitle( line:{name:string; level?:string} ):string{ return line.level ? `${line.level} ${line.name}` : line.name; }