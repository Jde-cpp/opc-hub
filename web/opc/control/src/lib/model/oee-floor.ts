import { OeeBucket, oeeDuration, oeeLossSplit, oeePercent, OeeSplitRow } from './oee';
import { OeeFloor, OeeFloorKind, OeeFloorShape, OeePoint } from './oee-floor-rows';
import { OeeLiveFigures, OeeLiveStatus } from './oee-live';
import { OeeLineRef, oeeLineReach, oeeMembersOf, OeePlant } from './oee-lines';
import type { OeeMachineRef } from './oee-live-view';

//A line's floor as its page draws it (#481, docs/demo/spec.md *The floor*, *Drawing*):  what the map draws from the two tables'
//rows (oee-floor-rows.ts) over the plant's graph - each placed machine and line, the lines boxed around their machines, the
//fixtures, in drawing order - the state a shape's outline, dash and state line say from its live status, the text a shape keeps
//by its size, the list view's order and the plant's clock in the line's zone.  Pure functions, so each is a spec with no page.
//In the page's chunk alone:  the resolver, in the initial bundle, reads the rows through OeeFloorService.
export * from './oee-floor-rows';

export type OeeBox = { x:number; y:number; width:number; height:number };
export function pointsBox( points:OeePoint[] ):OeeBox{
	const xs = points.map( p=>p[0] ), ys = points.map( p=>p[1] );
	const x = Math.min( ...xs ), y = Math.min( ...ys );
	return { x, y, width: Math.max( ...xs )-x, height: Math.max( ...ys )-y };
}
export function boxPoints( b:OeeBox ):OeePoint[]{ return [[b.x, b.y], [b.x+b.width, b.y], [b.x+b.width, b.y+b.height], [b.x, b.y+b.height]]; }
function unionBox( boxes:OeeBox[] ):OeeBox{
	const x = Math.min( ...boxes.map( b=>b.x ) ), y = Math.min( ...boxes.map( b=>b.y ) );
	return { x, y, width: Math.max( ...boxes.map( b=>b.x+b.width ) )-x, height: Math.max( ...boxes.map( b=>b.y+b.height ) )-y };
}
//a box grown `by` on every side, held to the floor
function grownBox( b:OeeBox, by:number, floor:OeeFloor ):OeeBox{
	const x = Math.max( 0, b.x-by ), y = Math.max( 0, b.y-by );
	return { x, y, width: Math.min( floor.width, b.x+b.width+by )-x, height: Math.min( floor.height, b.y+b.height+by )-y };
}

//The lines a line reaches through its members, each once, in member order, itself left out:  the gateway's Lines::ReachLines
//over the live lines.  A deleted line's members aren't walked.
export function oeeReachedLines( plant:OeePlant, lineId:number ):OeeLineRef[]{
	const live = new Map( plant.lines.map( l=>[l.id, l] ) ), y:OeeLineRef[] = [], seen = new Set( [lineId] );
	const walk = ( id:number )=>{
		for( const m of oeeMembersOf( plant, id ) ){
			const line = m.memberLineId==undefined ? undefined : live.get( m.memberLineId );
			if( line && !seen.has( line.id ) ){
				seen.add( line.id );
				y.push( line );
				walk( line.id );
			}
		}
	};
	if( live.has( lineId ) )
		walk( lineId );
	return y;
}

//What the map draws, bottom to top:  zones, aisles, walls, the lines, the machines and the labels, each kind in its `sort`.
//A machine's shape is drawn where the floor's line reaches the machine and it stands;  a deleted machine's, or one the line no
//longer reaches, is left out (plan Phase 3).  A line's shape likewise;  a reached line with no shape is the box around its
//machines' shapes, half a grid out, ahead of the shaped lines, and one none of whose machines is placed isn't drawn.
export type FloorMachine = { key:string; shape:OeeFloorShape; machine:OeeMachineRef; name:string; points:OeePoint[]; box:OeeBox };
export type FloorLine = { key:string; shape?:OeeFloorShape; line:OeeLineRef; name:string; points:OeePoint[]; box:OeeBox; boxed:boolean };
export type FloorFixture = { shape:OeeFloorShape; label?:string; points:OeePoint[]; box:OeeBox };
export type FloorDrawing = { machines:FloorMachine[]; lines:FloorLine[]; zones:FloorFixture[]; walls:FloorFixture[]; aisles:FloorFixture[]; labels:FloorFixture[] };
export const floorMachineKey = ( id:number )=>`m${id}`, floorLineKey = ( id:number )=>`l${id}`;
export function floorDrawing( floor:OeeFloor, shapes:OeeFloorShape[], plant:OeePlant, lineId:number ):FloorDrawing{
	const machines = new Map( oeeLineReach( plant, lineId ).map( m=>[m.id, m] ) ), lines = new Map( oeeReachedLines( plant, lineId ).map( l=>[l.id, l] ) );
	const of = ( kind:OeeFloorKind )=>shapes.filter( s=>s.kind==kind && s.points.length ).sort( ( a, b )=>a.sort-b.sort || a.id-b.id );
	const placed = of( 'Machine' ).flatMap( ( shape ):FloorMachine[]=>{
		const machine = shape.machineId==undefined ? undefined : machines.get( shape.machineId );
		return machine ? [{ key: floorMachineKey( machine.id ), shape, machine, name: shape.label ?? machine.name, points: shape.points, box: pointsBox( shape.points ) }] : [];
	} );
	const shaped = of( 'Line' ).flatMap( ( shape ):FloorLine[]=>{
		const line = shape.lineId==undefined ? undefined : lines.get( shape.lineId );
		return line ? [{ key: floorLineKey( line.id ), shape, line, name: shape.label ?? line.name, points: shape.points, box: pointsBox( shape.points ), boxed: false }] : [];
	} );
	const shapedLines = new Set( shaped.map( l=>l.line.id ) ), byMachine = new Map( placed.map( p=>[p.machine.id, p] ) );
	const boxed = [...lines.values()].filter( l=>!shapedLines.has( l.id ) ).flatMap( ( line ):FloorLine[]=>{
		const boxes = oeeLineReach( plant, line.id ).flatMap( m=>{ const p = byMachine.get( m.id ); return p ? [p.box] : []; } );
		if( !boxes.length )
			return [];
		const box = grownBox( unionBox( boxes ), floor.grid/2, floor );
		return [{ key: floorLineKey( line.id ), line, name: line.name, points: boxPoints( box ), box, boxed: true }];
	} );
	const fixtures = ( kind:OeeFloorKind )=>of( kind ).map( ( shape ):FloorFixture=>({ shape, label: shape.label, points: shape.points, box: pointsBox( shape.points ) }) );
	return { machines: placed, lines: [...boxed, ...shaped], zones: fixtures( 'Zone' ), walls: fixtures( 'Wall' ), aisles: fixtures( 'Aisle' ), labels: fixtures( 'Label' ) };
}
//the list view's order:  the lines, then the machines, as the map draws them
export function floorListOrder( drawing:FloorDrawing ):( FloorLine|FloorMachine )[]{ return [...drawing.lines, ...drawing.machines]; }

//A shape's state (spec *Drawing*, the table):  the outline's colour token, one of the Gantt chart's kinds or neutral, its dash,
//and the state line.  No access, the subscription refused, outranks all;  then `planned` false, Not planned;  then the state:
//running, a stop in its reason's category or uncoded, a stop still small in the small-stop colour, unknown grey and dashed.
//A line's outline is always dashed, its colour its machines':  all running, some stopped, or all stopped.  `now` is the page's
//clock, ticking each second, so the "since" text moves with no read.
export type OeeFloorToken = 'running'|'Unplanned'|'Planned'|'Excluded'|'uncoded'|'small'|'unknown'|'neutral';
export type OeeFloorDash = 'solid'|'dashed'|'dotted';
export type OeeFloorLink = 'connecting'|'live'|'dropped'|'refused'|'ended';
export type OeeFloorState = { token:OeeFloorToken; dash:OeeFloorDash; label:string; line:string; since?:Date };
const sinceText = ( label:string, since:Date|undefined, now:Date )=>since ? `${label} ${oeeDuration( now.getTime()-since.getTime() )}` : label;
export function oeeFloorState( status:OeeLiveStatus|undefined, link:OeeFloorLink, now:Date ):OeeFloorState{
	if( link=='refused' )
		return { token: 'neutral', dash: 'dotted', label: 'No access', line: 'No access' };
	if( link=='ended' )
		return { token: 'neutral', dash: 'dotted', label: 'Ended', line: 'Ended' };
	if( !status ){
		const label = link=='dropped' ? 'Disconnected' : 'Connecting';
		return { token: 'neutral', dash: 'solid', label, line: label };
	}
	if( status.machines )
		return oeeFloorLineState( status );
	if( !status.planned )
		return { token: 'neutral', dash: 'solid', label: 'Not planned', line: 'Not planned' };
	const since = status.since;
	if( status.state=='running' )
		return { token: 'running', dash: 'solid', label: 'Running', line: sinceText( 'Running', since, now ), since };
	if( status.state=='unknown' )
		return { token: 'unknown', dash: 'dashed', label: 'No data', line: sinceText( 'No data', since, now ), since };
	const stop = status.stop, begun = stop?.start ?? since;
	if( stop?.small )
		return { token: 'small', dash: 'solid', label: 'Stopped', line: sinceText( 'Stopped', begun, now ), since: begun };
	const category = stop?.reason?.category, token:OeeFloorToken = category=='Unplanned' || category=='Planned' || category=='Excluded' ? category : 'uncoded';
	const label = stop?.reason?.name ?? 'Uncoded';
	return { token, dash: 'solid', label, line: sinceText( label, begun, now ), since: begun };
}
function oeeFloorLineState( status:OeeLiveStatus ):OeeFloorState{
	const c = status.machines!, total = c.running+c.stopped+c.unknown, count = `${c.running} of ${total} running`;
	if( !status.planned )
		return { token: 'neutral', dash: 'dashed', label: 'Not planned', line: `Not planned, ${count}` };
	if( !total || c.running+c.stopped==0 )
		return { token: 'unknown', dash: 'dashed', label: 'No data', line: total ? `No data, ${count}` : 'No data' };
	if( !c.stopped )
		return { token: 'running', dash: 'dashed', label: 'All running', line: count };
	if( !c.running )
		return { token: 'Unplanned', dash: 'dashed', label: 'All stopped', line: count };
	return { token: 'uncoded', dash: 'dashed', label: 'Some stopped', line: count };
}
//the legend, machines' states and lines'
export type OeeFloorLegendItem = { token:OeeFloorToken; dash:OeeFloorDash; label:string };
export const oeeFloorLegend:OeeFloorLegendItem[] = [
	{ token: 'running', dash: 'solid', label: 'Running' }, { token: 'Unplanned', dash: 'solid', label: 'Unplanned stop' }, { token: 'Planned', dash: 'solid', label: 'Planned stop' },
	{ token: 'Excluded', dash: 'solid', label: 'Excluded' }, { token: 'uncoded', dash: 'solid', label: 'Uncoded stop' }, { token: 'small', dash: 'solid', label: 'Short stop' },
	{ token: 'unknown', dash: 'dashed', label: 'No data' }, { token: 'neutral', dash: 'solid', label: 'Not planned' }, { token: 'neutral', dash: 'dotted', label: 'No access' }
];
export const oeeFloorLineLegend:OeeFloorLegendItem[] = [
	{ token: 'running', dash: 'dashed', label: 'Line, all running' }, { token: 'uncoded', dash: 'dashed', label: 'Line, some stopped' }, { token: 'Unplanned', dash: 'dashed', label: 'Line, all stopped' }
];

//The text a shape keeps at a size on the screen:  its name, its OEE in large type, its availability, performance and quality in
//small type and its state line need `full`'s room;  a smaller shape keeps the name and the OEE.
export type FloorTextLevel = 'full'|'brief';
export const floorFullText = { width: 120, height: 76 };//px
export function floorTextLevel( box:OeeBox, pxPerMetre:number ):FloorTextLevel{
	return box.width*pxPerMetre>=floorFullText.width && box.height*pxPerMetre>=floorFullText.height ? 'full' : 'brief';
}
//a shape's accessible name:  what it is, its state and its OEE
export function floorShapeName( name:string, state:OeeFloorState, figures:OeeLiveFigures|undefined ):string{
	return `${name}, ${state.line}, OEE ${oeePercent( figures?.oee )}`;
}

//the side panel's loss split, the figures panel's over the live figures' times
export function oeeLiveSplit( f:OeeLiveFigures ):{planned:OeeSplitRow[]; beside:OeeSplitRow[]}|undefined{
	return f.planned==undefined ? undefined : oeeLossSplit( <OeeBucket><unknown>{ planned: f.planned, run: f.run ?? 0, unplanned: f.unplanned ?? 0, plannedStop: f.plannedStop ?? 0, small: f.small ?? 0, excluded: f.excluded ?? 0, unknown: f.unknown ?? 0 } );
}

//The plant's clock:  the wall time in `zone` now, and the zone as the header names it, with its offset from UTC.  A zone the
//browser doesn't know reads as UTC.
export function zoneClock( now:Date, zone:string ):{time:string; zone:string; offset:string}{
	let z = zone || 'UTC';
	try{ new Intl.DateTimeFormat( 'en-US', {timeZone: z} ); }
	catch{ z = 'UTC'; }
	const time = new Intl.DateTimeFormat( undefined, {timeZone: z, hourCycle: 'h23', hour: '2-digit', minute: '2-digit', second: '2-digit'} ).format( now );
	const offset = ( new Intl.DateTimeFormat( 'en-US', {timeZone: z, timeZoneName: 'longOffset'} ).formatToParts( now ).find( p=>p.type=='timeZoneName' )?.value ?? 'GMT' ).replace( /^GMT/, 'UTC' );
	return { time, zone: z, offset: offset=='UTC+00:00' ? 'UTC' : offset };
}
//the time left in a shift, to the minute
export function shiftLeft( end:Date, now:Date ):string{ return now>=end ? 'ending' : `${oeeDuration( Math.ceil( ( end.getTime()-now.getTime() )/60000 )*60000 )} left`; }