import { boxPoints, oeeReachedLines, pointsBox } from './oee-floor';
import { OeeFloor, oeeFloorDefault, OeeFloorKind, oeeFloorKinds, OeeFloorRights, OeeFloorShape, OeePoint, parseFloorPoints } from './oee-floor-rows';
import { OeeLineRef, OeePlant, oeeReaches } from './oee-lines';
import type { OeeMachineRef } from './oee-live-view';

//The floor's editor (#482, docs/demo/spec.md *Editing*), as pure functions over what it holds:  the floor's size and grid and
//its shapes, each a row of the gateway's or one not saved yet.  A shape is placed, moved, resized by its box's handles,
//turned a quarter and reshaped by its points, every point snapped to the grid and held to the floor, in metres;  what is on
//the floor is checked by the gateway's rules, so Save is refused here before it is there;  Save is the batch of mutations
//from the rows as read to the floor as edited, a purge, an update or a create per row that differs, and what the batch needs
//of the caller's rights;  and the floor goes to and from the spec's json, machines and lines by slug, the file the seed takes.
//In the editor's chunk alone, which the page imports when Edit is pressed.
export type FloorSize = { width:number; height:number; grid:number };
export type EditShape = { key:string; id?:number; kind:OeeFloorKind; machineId?:number; lineId?:number; points:OeePoint[]; label?:string; sort:number };
export type FloorEdit = { floor:FloorSize; shapes:EditShape[] };
//the floor and its shapes as the gateway holds them
export type FloorBase = { floor:OeeFloor; shapes:OeeFloorShape[] };
export function floorEdit( base:FloorBase ):FloorEdit{
	const { width, height, grid } = base.floor;
	return { floor: {width, height, grid}, shapes: base.shapes.map( toEditShape ) };
}
function toEditShape( s:OeeFloorShape ):EditShape{
	return { key: `s${s.id}`, id: s.id, kind: s.kind, machineId: s.machineId, lineId: s.lineId, points: s.points.map( ( [x, y] ):OeePoint=>[x, y] ), label: s.label, sort: s.sort };
}

//A kind's geometry (spec *The floor*):  a Machine, a Line and a Zone are closed polygons of three points or more, a Wall and an
//Aisle open polylines of two or more, a Label one point.
export type FloorGeometry = 'polygon'|'polyline'|'point';
export function floorGeometry( kind:OeeFloorKind ):FloorGeometry{ return kind=='Label' ? 'point' : kind=='Wall' || kind=='Aisle' ? 'polyline' : 'polygon'; }
const fewest = { polygon: 3, polyline: 2, point: 1 };
//a new shape's size in metres:  a machine's, a line's and a zone's rectangle, a wall's and an aisle's run
export const floorShapeDefaults = { Machine: {width: 4, height: 3}, Line: {width: 12, height: 6}, Zone: {width: 4, height: 2}, run: 6 };

const round = ( v:number )=>Math.round( v*1e6 )/1e6;//0.1+0.2 is not a grid's multiple
export const snapTo = ( v:number, grid:number )=>round( Math.round( v/grid )*grid );
const hold = ( v:number, min:number, max:number )=>Math.max( min, Math.min( v, max ) );
//a point on the grid and on the floor
const held = ( [x, y]:OeePoint, floor:FloorSize ):OeePoint=>[hold( snapTo( x, floor.grid ), 0, floor.width ), hold( snapTo( y, floor.grid ), 0, floor.height )];
//four points that are the corners of their own box, in either direction from any corner
export function isRectangle( points:OeePoint[] ):boolean{
	if( points.length!=4 )
		return false;
	const b = pointsBox( points );
	return b.width>0 && b.height>0 && points.every( ( [x, y], i )=>{
		const [nextX, nextY] = points[( i+1 )%4];
		return ( x==b.x || x==b.x+b.width ) && ( y==b.y || y==b.y+b.height ) && ( x==nextX )!=( y==nextY );
	} );
}

//A new shape's points about `at`:  a rectangle of its kind's default size, a run, or the point, its corner on the grid and
//the whole of it on the floor, which a floor smaller than the default cuts it to.
export function newShapePoints( kind:OeeFloorKind, at:OeePoint, floor:FloorSize ):OeePoint[]{
	const geometry = floorGeometry( kind );
	if( geometry=='point' )
		return [held( at, floor )];
	const size = geometry=='polygon' ? floorShapeDefaults[<'Machine'|'Line'|'Zone'>kind] : { width: floorShapeDefaults.run, height: 0 };
	const width = Math.min( size.width, floor.width ), height = Math.min( size.height, floor.height );
	const x = hold( snapTo( at[0]-width/2, floor.grid ), 0, floor.width-width ), y = hold( snapTo( at[1]-height/2, floor.grid ), 0, floor.height-height );
	return geometry=='polygon' ? boxPoints( {x, y, width, height} ) : [[x, y], [round( x+width ), y]];
}
//A shape placed about `at`, last of its kind in the drawing order, under a key of its own until it is saved.
export type NewShape = { kind:OeeFloorKind; machineId?:number; lineId?:number; label?:string };
export function addShape( edit:FloorEdit, what:NewShape, at:OeePoint ):{edit:FloorEdit; key:string}{
	const key = `n${1+Math.max( 0, ...edit.shapes.map( s=>s.key[0]=='n' ? Number( s.key.slice( 1 ) ) : 0 ) )}`;
	const sort = 1+Math.max( -1, ...edit.shapes.filter( s=>s.kind==what.kind ).map( s=>s.sort ) );
	return { edit: { ...edit, shapes: [...edit.shapes, { key, ...what, points: newShapePoints( what.kind, at, edit.floor ), sort }] }, key };
}
export function withShape( edit:FloorEdit, key:string, change:Partial<EditShape> ):FloorEdit{
	return { ...edit, shapes: edit.shapes.map( s=>s.key==key ? {...s, ...change} : s ) };
}
export function withoutShape( edit:FloorEdit, key:string ):FloorEdit{ return { ...edit, shapes: edit.shapes.filter( s=>s.key!=key ) }; }

//Moved by `dx`, `dy`:  its box's corner on the grid, the whole of it on the floor.
export function movePoints( points:OeePoint[], dx:number, dy:number, floor:FloorSize ):OeePoint[]{
	const b = pointsBox( points );
	const x = hold( snapTo( b.x+dx, floor.grid ), 0, floor.width-b.width ), y = hold( snapTo( b.y+dy, floor.grid ), 0, floor.height-b.height );
	return points.map( ( [px, py] )=>[round( px+x-b.x ), round( py+y-b.y )] );
}
//Resized by a handle of its box, a corner's or a side's, dragged to `to`:  the sides the handle holds go to the grid's point
//nearest it on the floor, never nearer than a grid to the sides opposite, which stay, and every point keeps its place in the box.
export const floorHandles = ['nw', 'n', 'ne', 'e', 'se', 's', 'sw', 'w'] as const;
export type FloorHandle = typeof floorHandles[number];
export function resizePoints( points:OeePoint[], handle:FloorHandle, to:OeePoint, floor:FloorSize ):OeePoint[]{
	const b = pointsBox( points ), [x, y] = held( to, floor ), grid = floor.grid;
	let left = b.x, right = b.x+b.width, top = b.y, bottom = b.y+b.height;
	if( handle.includes( 'w' ) )
		left = Math.max( 0, Math.min( x, right-grid ) );
	if( handle.includes( 'e' ) )
		right = Math.min( floor.width, Math.max( x, left+grid ) );
	if( handle.includes( 'n' ) )
		top = Math.max( 0, Math.min( y, bottom-grid ) );
	if( handle.includes( 's' ) )
		bottom = Math.min( floor.height, Math.max( y, top+grid ) );
	const sx = b.width ? ( right-left )/b.width : 1, sy = b.height ? ( bottom-top )/b.height : 1;
	return points.map( ( [px, py] )=>[round( left+( px-b.x )*sx ), round( top+( py-b.y )*sy )] );
}
//where a box's handle sits
export function handlePoint( points:OeePoint[], handle:FloorHandle ):OeePoint{
	const b = pointsBox( points );
	return [handle.includes( 'w' ) ? b.x : handle.includes( 'e' ) ? b.x+b.width : b.x+b.width/2, handle.includes( 'n' ) ? b.y : handle.includes( 's' ) ? b.y+b.height : b.y+b.height/2];
}
//A quarter turn clockwise about its box's centre, the turned box's corner on the grid and the whole of it on the floor;  as
//it was where the floor has no room for it turned.
export function rotatePoints( points:OeePoint[], floor:FloorSize ):OeePoint[]{
	const b = pointsBox( points );
	if( b.height>floor.width || b.width>floor.height )
		return points;
	const x = hold( snapTo( b.x+( b.width-b.height )/2, floor.grid ), 0, floor.width-b.height ), y = hold( snapTo( b.y+( b.height-b.width )/2, floor.grid ), 0, floor.height-b.width );
	return points.map( ( [px, py] )=>[round( x+b.height-( py-b.y ) ), round( y+px-b.x )] );
}
//a vertex moved to the grid's point nearest `to` on the floor
export function moveVertex( points:OeePoint[], index:number, to:OeePoint, floor:FloorSize ):OeePoint[]{ return points.map( ( p, i )=>i==index ? held( to, floor ) : p ); }
//The sides a vertex may be added to, each with its middle:  a polygon's last closes it, a polyline has one fewer than its points.
export function floorSides( points:OeePoint[], kind:OeeFloorKind ):{index:number; at:OeePoint}[]{
	const geometry = floorGeometry( kind ), count = geometry=='polygon' ? points.length : geometry=='polyline' ? points.length-1 : 0;
	return Array.from( {length: count}, ( _, index )=>{
		const a = points[index], b = points[( index+1 )%points.length];
		return { index, at: <OeePoint>[round( ( a[0]+b[0] )/2 ), round( ( a[1]+b[1] )/2 )] };
	} );
}
//a vertex added at the middle of the side after `index`, where a drag takes it from
export function addVertex( points:OeePoint[], index:number ):OeePoint[]{
	const a = points[index], b = points[( index+1 )%points.length];
	return [...points.slice( 0, index+1 ), [round( ( a[0]+b[0] )/2 ), round( ( a[1]+b[1] )/2 )], ...points.slice( index+1 )];
}
//a vertex taken out, but not below its kind's fewest
export function removeVertex( points:OeePoint[], index:number, kind:OeeFloorKind ):OeePoint[]{
	return points.length>fewest[floorGeometry( kind )] ? points.filter( ( _, i )=>i!=index ) : points;
}

//What the floor's line reaches, which a Machine or a Line shape may name (spec *The floor*):  the gateway's Reach and ReachLines.
export type FloorReach = { machines:Map<number,OeeMachineRef>; lines:Map<number,OeeLineRef> };
export function floorReach( plant:OeePlant, lineId:number ):FloorReach{
	return { machines: new Map( oeeReaches( plant )( lineId ).map( m=>[m.id, m] ) ), lines: new Map( oeeReachedLines( plant, lineId ).map( l=>[l.id, l] ) ) };
}
//the machines and lines reached that have no shape yet, in the line's order
export function floorUnplaced( reach:FloorReach, shapes:EditShape[] ):{machines:OeeMachineRef[]; lines:OeeLineRef[]}{
	const machines = new Set( shapes.map( s=>s.machineId ) ), lines = new Set( shapes.map( s=>s.lineId ) );
	return { machines: [...reach.machines.values()].filter( m=>!machines.has( m.id ) ), lines: [...reach.lines.values()].filter( l=>!lines.has( l.id ) ) };
}
//a shape whose machine or line the floor's line doesn't reach, or that is deleted:  kept, left out of the map, and still moved and purged (plan Phase 3)
export function floorStranded( s:EditShape, reach:FloorReach ):boolean{
	return s.kind=='Machine' ? !reach.machines.has( s.machineId! ) : s.kind=='Line' ? !reach.lines.has( s.lineId! ) : false;
}
//a shape as the editor names it:  its label, else its machine's or line's name;  a fixture's kind and its text
export function floorShapeTitle( s:Pick<EditShape,'kind'|'machineId'|'lineId'|'label'>, plant:OeePlant ):string{
	if( s.kind=='Machine' )
		return s.label ?? plant.machines.find( m=>m.id==s.machineId )?.name ?? `Machine ${s.machineId}`;
	if( s.kind=='Line' )
		return s.label ?? plant.lines.find( l=>l.id==s.lineId )?.name ?? `Line ${s.lineId}`;
	return s.label ? `${s.kind} ${s.label}` : s.kind;
}

//Why the gateway would refuse the floor as it stands (its Floors' rules, rules.h):  a size or a grid that isn't above zero, a
//point outside the floor, a polygon that encloses no area, a polyline with no length.  None for a floor it would take.
const area = ( p:OeePoint[] )=>Math.abs( p.reduce( ( sum, [x, y], i )=>{ const [nextX, nextY] = p[( i+1 )%p.length]; return sum+x*nextY-nextX*y; }, 0 ) )/2;
const run = ( p:OeePoint[] )=>p.slice( 1 ).reduce( ( sum, [x, y], i )=>sum+Math.hypot( x-p[i][0], y-p[i][1] ), 0 );
export function floorEditProblem( edit:FloorEdit, name:( s:EditShape )=>string ):string|undefined{
	const { width, height } = edit.floor;
	for( const field of <const>['width', 'height', 'grid'] ){
		if( !( edit.floor[field]>0 ) || !isFinite( edit.floor[field] ) )
			return `The floor's ${field} must be above zero.`;
	}
	for( const s of edit.shapes ){
		const geometry = floorGeometry( s.kind ), out = s.points.find( ( [x, y] )=>x<0 || y<0 || x>width || y>height );
		if( out )
			return `${name( s )} has a point at [${out[0]}, ${out[1]}], outside the floor, ${width} by ${height}.`;
		if( geometry=='polygon' && ( s.points.length<3 || !area( s.points ) ) )
			return `${name( s )} must enclose an area.`;
		if( geometry=='polyline' && ( s.points.length<2 || !run( s.points ) ) )
			return `${name( s )} must have a length.`;
		if( geometry=='point' && s.points.length!=1 )
			return `${name( s )} is one point.`;
	}
	return undefined;
}

//Save's batch (spec *Editing*), from the rows as read to the floor as edited:  a purge for each row no longer there, an update
//for each whose points, text or order differ, of those alone, a create for each shape with no row, and the floor's size and
//grid where they changed.  In the order the gateway's rules need:  the purges, which free a machine to be placed again;  the
//floor grown, since a point must be inside it;  the updates and the creates;  then the floor taken in, since it shrinks no
//further than its shapes.  A row another hand changed is written only where this one changed it too:  the last save wins.
export type FloorMutation =
	{ op:'purgeShape'; id:number } | { op:'updateFloor'; args:Partial<FloorSize> } |
	{ op:'updateShape'; id:number; args:{points?:OeePoint[]; label?:string|null; sort?:number} } |
	{ op:'createShape'; args:{kind:OeeFloorKind; points:OeePoint[]; oeeMachineId?:number; memberLineId?:number; label?:string; sort:number} };
const same = ( a:unknown, b:unknown )=>JSON.stringify( a )==JSON.stringify( b );
export function floorBatch( base:FloorBase, edit:FloorEdit ):FloorMutation[]{
	const rows = new Map( base.shapes.map( s=>[s.id, s] ) ), kept = new Set( edit.shapes.map( s=>s.id ) );
	const y:FloorMutation[] = base.shapes.filter( s=>!kept.has( s.id ) ).map( s=>({ op: 'purgeShape', id: s.id }) );
	const was = base.floor, now = edit.floor, room = { width: Math.max( was.width, now.width ), height: Math.max( was.height, now.height ) };
	const grown:Partial<FloorSize> = { ...( room.width!=was.width ? {width: room.width} : {} ), ...( room.height!=was.height ? {height: room.height} : {} ) };
	const taken:Partial<FloorSize> = { ...( now.width!=room.width ? {width: now.width} : {} ), ...( now.height!=room.height ? {height: now.height} : {} ) };
	const grid:Partial<FloorSize> = now.grid!=was.grid ? {grid: now.grid} : {}, shrinks = Object.keys( taken ).length>0;
	const first = shrinks ? grown : {...grown, ...grid};
	if( Object.keys( first ).length )
		y.push( { op: 'updateFloor', args: first } );
	for( const s of edit.shapes ){
		const row = s.id==undefined ? undefined : rows.get( s.id );
		if( !row )
			continue;
		const args = { ...( same( s.points, row.points ) ? {} : {points: s.points} ), ...( ( s.label || undefined )==row.label ? {} : {label: s.label || null} ), ...( s.sort==row.sort ? {} : {sort: s.sort} ) };
		if( Object.keys( args ).length )
			y.push( { op: 'updateShape', id: row.id, args } );
	}
	for( const s of edit.shapes ){
		if( s.id==undefined || !rows.has( s.id ) )
			y.push( { op: 'createShape', args: { kind: s.kind, points: s.points, ...( s.kind=='Machine' ? {oeeMachineId: s.machineId} : s.kind=='Line' ? {memberLineId: s.lineId} : {} ), ...( s.label ? {label: s.label} : {} ), sort: s.sort } } );
	}
	if( shrinks )
		y.push( { op: 'updateFloor', args: {...taken, ...grid} } );
	return y;
}
//The batch as one request of generic mutations, each with variables of its own, which the gateway runs in order and ends at
//the first it refuses, with what came before it written.
export function floorBatchQL( floorId:number, batch:FloorMutation[] ):{ql:string; vars:Record<string,unknown>}{
	const vars:Record<string,unknown> = {};
	const call = ( name:string, args:Record<string,unknown>, i:number )=>`${name}( ${Object.entries( args ).map( ( [key, value] )=>{ vars[`${key}${i}`] = value; return `${key}:$${key}${i}`; } ).join( ', ' )} )`;
	const calls = batch.map( ( m, i )=>
		m.op=='purgeShape' ? call( 'purgeOeeFloorShape', {id: m.id}, i ) : m.op=='updateFloor' ? call( 'updateOeeFloor', {id: floorId, ...m.args}, i )
		: m.op=='updateShape' ? call( 'updateOeeFloorShape', {id: m.id, ...m.args}, i ) : call( 'createOeeFloorShape', {oeeFloorId: floorId, ...m.args}, i ) );
	return { ql: `mutation ${calls.join( ' ' )}`, vars };
}
//What the batch needs that the caller lacks, as Save says it greyed;  none where the gateway's gates would pass it.
export function floorBatchRefusal( batch:FloorMutation[], rights:OeeFloorRights ):string|undefined{
	const has = ( op:FloorMutation['op'] )=>batch.some( m=>m.op==op );
	const shapes = [...( has( 'createShape' ) && !rights.createShape ? ['Create'] : [] ), ...( has( 'updateShape' ) && !rights.updateShape ? ['Update'] : [] ), ...( has( 'purgeShape' ) && !rights.purgeShape ? ['Purge'] : [] )];
	const lacks = [
		...( shapes.length ? [`${shapes.length>1 ? `${shapes.slice( 0, -1 ).join( ', ' )} and ${shapes.at( -1 )}` : shapes[0]} on the floor's shapes (oee_floor_shapes)`] : [] ),
		...( has( 'updateFloor' ) && !rights.updateFloor ? ['Update on the floors (oee_floors)'] : [] )
	];
	return lacks.length ? `Save needs ${lacks.join( ', and ' )}, which your roles don't grant.` : undefined;
}
//The floor as edited over the rows read again, after a save the gateway refused part-way:  a shape whose create went in
//takes its row, a row gone under an edit is a shape to make again, and a row another hand made meanwhile joins the floor, so
//the next batch is what is still to write.
export function floorRebase( edit:FloorEdit, was:FloorBase, now:FloorBase ):FloorEdit{
	const known = new Set( was.shapes.map( s=>s.id ) ), rows = new Set( now.shapes.map( s=>s.id ) ), made = now.shapes.filter( s=>!known.has( s.id ) );
	const shapes = edit.shapes.map( ( s ):EditShape=>{
		if( s.id!=undefined )
			return rows.has( s.id ) ? s : {...s, id: undefined};
		const i = made.findIndex( r=>r.kind==s.kind && r.machineId==s.machineId && r.lineId==s.lineId && r.label==( s.label || undefined ) && r.sort==s.sort && same( r.points, s.points ) );
		return i<0 ? s : {...s, id: made.splice( i, 1 )[0].id};
	} );
	return { ...edit, shapes: [...shapes, ...made.map( toEditShape )] };
}

//The floor as the spec's json (spec *The floor*), a shape a line:  its size and grid, and each shape with its kind, its
//machine or line by slug, its text and its points, a kind's shapes in their drawing order.  A shape the map leaves out, its
//machine or line not reached, is left out here too, since no floor takes it.
export function floorExport( edit:FloorEdit, reach:FloorReach ):string{
	const ordered = new Map( oeeFloorKinds.map( kind=>[kind, edit.shapes.filter( s=>s.kind==kind ).sort( ( a, b )=>a.sort-b.sort )] ) );
	const lines = edit.shapes.map( s=>ordered.get( s.kind )!.shift()! ).flatMap( ( s ):string[]=>{
		const named = s.kind=='Machine' ? ['machine', reach.machines.get( s.machineId! )?.slug] : s.kind=='Line' ? ['line', reach.lines.get( s.lineId! )?.slug] : undefined;
		if( named && !named[1] )
			return [];
		const fields = [['kind', s.kind], ...( named ? [named] : [] ), ...( s.label ? [['label', s.label]] : [] ), ['points', s.points]];
		return [`\t{ ${fields.map( ( [key, value] )=>`"${key}": ${JSON.stringify( value )}` ).join( ', ' )} }`];
	} );
	return `{ "width": ${edit.floor.width}, "height": ${edit.floor.height}, "grid": ${edit.floor.grid}, "shapes": [\n${lines.join( ',\n' )}\n]}`;
}
//The same json read onto an empty floor:  its size and grid, and its shapes as shapes to make, each kind's in the file's
//order.  A problem in the json's place:  a floor with shapes, a file that isn't a floor's, a machine or a line the floor's
//line doesn't reach or that is there twice, and whatever the gateway's rules would refuse.
export function floorImport( text:string, current:FloorEdit, reach:FloorReach ):{edit:FloorEdit}|{problem:string}{
	if( current.shapes.length )
		return { problem: `Import reads onto an empty floor, and this one has ${current.shapes.length==1 ? 'a shape' : `${current.shapes.length} shapes`}:  delete ${current.shapes.length==1 ? 'it' : 'them'} first.` };
	let json:any;
	try{
		json = JSON.parse( text );
	}
	catch( e ){
		return { problem: `The file isn't json:  ${( <Error>e ).message}` };
	}
	if( !json || typeof json!='object' || !Array.isArray( json.shapes ) )
		return { problem: 'The file isn\'t a floor\'s:  { "width", "height", "grid", "shapes": [ … ] }.' };
	const bySlug = <T extends {slug:string}>( of:Map<number,T> )=>new Map( [...of.values()].map( x=>[x.slug, x] ) );
	const machines = bySlug( reach.machines ), lines = bySlug( reach.lines ), next = new Map<OeeFloorKind,number>(), shapes:EditShape[] = [];
	for( const [i, s] of ( <any[]>json.shapes ).entries() ){
		const of = `Shape ${i+1}`, kind = <OeeFloorKind>s?.kind, points = parseFloorPoints( s?.points );
		if( !oeeFloorKinds.includes( kind ) )
			return { problem: `${of}:  kind is ${oeeFloorKinds.slice( 0, -1 ).join( ', ' )} or ${oeeFloorKinds.at( -1 )}, not ${JSON.stringify( s?.kind )}.` };
		if( !points )
			return { problem: `${of}:  points is a list of [x, y] in metres.` };
		const shape:EditShape = { key: `n${i+1}`, kind, points, sort: next.get( kind ) ?? 0 };
		next.set( kind, shape.sort+1 );
		if( kind=='Machine' || kind=='Line' ){
			const noun = kind=='Machine' ? 'machine' : 'line', slug = s[noun], found = typeof slug=='string' ? ( kind=='Machine' ? machines : lines ).get( slug ) : undefined;
			if( !found )
				return { problem: `${of}:  ${noun} ${JSON.stringify( slug )} isn't one the floor's line reaches.` };
			if( shapes.some( x=>x.kind==kind && ( kind=='Machine' ? x.machineId : x.lineId )==found.id ) )
				return { problem: `${of}:  ${noun} '${slug}' is on the floor already.` };
			if( kind=='Machine' )
				shape.machineId = found.id;
			else
				shape.lineId = found.id;
		}
		else if( s.machine!=undefined || s.line!=undefined )
			return { problem: `${of}:  a ${kind} names no machine or line.` };
		if( typeof s.label=='string' && s.label )
			shape.label = s.label;
		shapes.push( shape );
	}
	const edit:FloorEdit = { floor: { width: Number( json.width ), height: Number( json.height ), grid: json.grid==undefined ? oeeFloorDefault.grid : Number( json.grid ) }, shapes };
	const problem = floorEditProblem( edit, s=>`Shape ${shapes.indexOf( s )+1}` );
	return problem ? { problem } : { edit };
}