import { OeeFloor, OeeFloorRights, toOeeFloorShape } from './oee-floor';
import { addShape, addVertex, EditShape, FloorBase, floorBatch, floorBatchQL, floorBatchRefusal, floorEdit, floorEditProblem, floorExport, floorImport, floorReach, floorRebase, floorShapeTitle, floorSides, floorStranded, floorUnplaced, isRectangle, movePoints, moveVertex, newShapePoints, removeVertex, resizePoints, rotatePoints, withoutShape, withShape } from './oee-floor-edit';
import { OeePlant } from './oee-lines';

//the demo's plant:  a site over two lines, three machines on the first and one on the second, and a fifth machine in no line
const plant:OeePlant = {
	machines: [{id: 1, name: 'Filler', slug: 'filler'}, {id: 2, name: 'Capper', slug: 'capper'}, {id: 3, name: 'Labeler', slug: 'labeler'}, {id: 4, name: 'Packer', slug: 'packer'}, {id: 5, name: 'Loose', slug: 'loose'}],
	lines: [{id: 10, name: 'Plant', slug: 'plant', level: 'Site'}, {id: 11, name: 'Bottling', slug: 'bottling', level: 'Line'}, {id: 12, name: 'Packing', slug: 'packing', level: 'Line'}],
	members: [
		{id: 1, lineId: 10, memberLineId: 11, sort: 0}, {id: 2, lineId: 10, memberLineId: 12, sort: 1},
		{id: 3, lineId: 11, machineId: 1, sort: 0}, {id: 4, lineId: 11, machineId: 2, sort: 1}, {id: 5, lineId: 11, machineId: 3, sort: 2},
		{id: 6, lineId: 12, machineId: 4, sort: 0}
	]
};
const reach = floorReach( plant, 10 );
const floor:OeeFloor = { id: 1, lineId: 10, width: 40, height: 20, grid: 0.5 };
const row = ( id:number, kind:string, points:number[][], extra:object={} )=>toOeeFloorShape( { id, oeeFloorId: 1, kind, points, sort: 0, ...extra } )!;
const rect = ( x:number, y:number, w:number, h:number ):[number, number][]=>[[x, y], [x+w, y], [x+w, y+h], [x, y+h]];
const base = ():FloorBase=>({ floor, shapes: [
	row( 1, 'Machine', rect( 3, 3, 6, 4 ), {oeeMachineId: 1} ), row( 2, 'Machine', rect( 13, 3, 4, 4 ), {oeeMachineId: 2, sort: 1} ),
	row( 6, 'Aisle', [[0, 10], [40, 10]] ), row( 7, 'Zone', rect( 9, 4.5, 4, 1 ), {label: 'Conveyor'} ), row( 8, 'Label', [[38, 15]], {label: 'Dock'} )
] });
const empty = { floor: {width: 40, height: 20, grid: 0.5}, shapes: [] };
//docs/demo/spec.md *The floor*, the start of the demo's export
const example = `{ "width": 40, "height": 20, "grid": 0.5, "shapes": [
	{ "kind": "Wall", "points": [[0,0],[40,0],[40,20],[0,20],[0,0]] },
	{ "kind": "Aisle", "points": [[0,10],[40,10]] },
	{ "kind": "Machine", "machine": "filler", "points": [[3,3],[9,3],[9,7],[3,7]] },
	{ "kind": "Zone", "label": "Conveyor", "points": [[9,4.5],[13,4.5],[13,5.5],[9,5.5]] },
	{ "kind": "Machine", "machine": "capper", "points": [[13,3],[17,3],[17,7],[13,7]] },
	{ "kind": "Label", "label": "Dock", "points": [[38,15]] }
]}`;
const imported = ( text:string )=>{
	const y = floorImport( text, empty, reach );
	if( 'problem' in y )
		throw new Error( y.problem );
	return y.edit;
};
const problem = ( text:string, current=empty )=>{ const y = floorImport( text, current, reach ); return 'problem' in y ? y.problem : undefined; };

describe( 'oee-floor-edit', ()=>{
	it( 'starts from the rows, and places a shape of a default size on the grid and on the floor', ()=>{
		const edit = floorEdit( base() );
		expect( edit.floor ).toEqual( {width: 40, height: 20, grid: 0.5} );
		expect( edit.shapes.map( s=>[s.key, s.id, s.kind] ) ).toEqual( [['s1', 1, 'Machine'], ['s2', 2, 'Machine'], ['s6', 6, 'Aisle'], ['s7', 7, 'Zone'], ['s8', 8, 'Label']] );
		const placed = addShape( edit, {kind: 'Machine', machineId: 3}, [20.3, 9.8] );
		expect( placed.key ).toBe( 'n1' );
		expect( placed.edit.shapes.at( -1 ) ).toEqual( {key: 'n1', kind: 'Machine', machineId: 3, points: rect( 18.5, 8.5, 4, 3 ), sort: 2} );//4 by 3 about the point, its corner on the half metre, after the two machines
		expect( addShape( placed.edit, {kind: 'Zone'}, [0, 0] ).key ).toBe( 'n2' );
		expect( edit.shapes.length ).toBe( 5 );//the edit given isn't changed
		expect( newShapePoints( 'Machine', [39.9, 19.9], floor ) ).toEqual( rect( 36, 17, 4, 3 ) );//held to the floor
		expect( newShapePoints( 'Line', [20, 10], floor ) ).toEqual( rect( 14, 7, 12, 6 ) );
		expect( newShapePoints( 'Wall', [20.2, 10.2], floor ) ).toEqual( [[17, 10], [23, 10]] );
		expect( newShapePoints( 'Label', [38.2, 15.3], floor ) ).toEqual( [[38, 15.5]] );
		expect( newShapePoints( 'Machine', [1, 1], {width: 3, height: 2, grid: 0.5} ) ).toEqual( rect( 0, 0, 3, 2 ) );//a floor smaller than the default
		expect( newShapePoints( 'Zone', [0.1, 0.2], {width: 40, height: 20, grid: 0.1} ) ).toEqual( rect( 0, 0, 4, 2 ) );
		expect( newShapePoints( 'Zone', [2.3, 1.2], {width: 40, height: 20, grid: 0.1} ) ).toEqual( rect( 0.3, 0.2, 4, 2 ) );//no 0.30000000000000004
	} );
	it( 'moves a shape by its box, on the grid and within the floor', ()=>{
		expect( movePoints( rect( 3, 3, 6, 4 ), 1.3, -0.2, floor ) ).toEqual( rect( 4.5, 3, 6, 4 ) );
		expect( movePoints( rect( 3, 3, 6, 4 ), 100, 100, floor ) ).toEqual( rect( 34, 16, 6, 4 ) );
		expect( movePoints( rect( 3, 3, 6, 4 ), -100, -100, floor ) ).toEqual( rect( 0, 0, 6, 4 ) );
		expect( movePoints( [[38, 15]], 1.2, 0.3, floor ) ).toEqual( [[39, 15.5]] );
		expect( movePoints( [[0, 10], [40, 10]], 3, 2, floor ) ).toEqual( [[0, 12], [40, 12]] );//as wide as the floor:  down alone
	} );
	it( 'resizes by a handle of its box, keeping the opposite corner', ()=>{
		expect( resizePoints( rect( 3, 3, 6, 4 ), 'se', [11.2, 8.1], floor ) ).toEqual( rect( 3, 3, 8, 5 ) );
		expect( resizePoints( rect( 3, 3, 6, 4 ), 'nw', [1.1, 0.9], floor ) ).toEqual( rect( 1, 1, 8, 6 ) );//its south-east corner stays at 9, 7
		expect( resizePoints( rect( 3, 3, 6, 4 ), 'e', [12.6, 100], floor ) ).toEqual( rect( 3, 3, 9.5, 4 ) );//a side's handle moves its side alone
		expect( resizePoints( rect( 3, 3, 6, 4 ), 'w', [20, 5], floor ) ).toEqual( rect( 8.5, 3, 0.5, 4 ) );//never past the side opposite:  a grid is the least
		expect( resizePoints( rect( 3, 3, 6, 4 ), 'se', [100, 100], floor ) ).toEqual( rect( 3, 3, 37, 17 ) );//to the floor's edge
		expect( resizePoints( [[0, 0], [4, 0], [2, 2]], 'se', [8, 4], floor ) ).toEqual( [[0, 0], [8, 0], [4, 4]] );//every point keeps its place in the box
	} );
	it( 'turns a rectangle a quarter about its centre, on the grid and on the floor', ()=>{
		const turned = rotatePoints( rect( 3, 3, 6, 4 ), floor );
		expect( turned ).toEqual( [[8, 2], [8, 8], [4, 8], [4, 2]] );//6 by 4 about 6, 5 is 4 by 6
		expect( isRectangle( turned ) ).toBe( true );
		expect( rotatePoints( turned, floor ) ).toEqual( [[9, 7], [3, 7], [3, 3], [9, 3]] );//and back in its box, half way round
		expect( rotatePoints( rect( 3, 3, 5, 4 ), floor ) ).toEqual( [[7.5, 2.5], [7.5, 7.5], [3.5, 7.5], [3.5, 2.5]] );//its centre is off the grid:  the turned corner is on it
		expect( rotatePoints( rect( 0, 0, 6, 2 ), floor ) ).toEqual( [[4, 0], [4, 6], [2, 6], [2, 0]] );//held to the floor
		expect( rotatePoints( rect( 0, 0, 6, 2 ), {width: 40, height: 3, grid: 0.5} ) ).toEqual( rect( 0, 0, 6, 2 ) );//no room for it turned
		expect( rotatePoints( [[38, 15]], floor ) ).toEqual( [[38, 15]] );
	} );
	it( "adds a vertex to a zone, moves it and takes it out, never below the kind's fewest", ()=>{
		const zone = rect( 9, 4.5, 4, 1 );
		expect( isRectangle( zone ) ).toBe( true );
		expect( floorSides( zone, 'Zone' ) ).toEqual( [{index: 0, at: [11, 4.5]}, {index: 1, at: [13, 5]}, {index: 2, at: [11, 5.5]}, {index: 3, at: [9, 5]}] );//the last closes it
		const added = addVertex( zone, 0 );
		expect( added ).toEqual( [[9, 4.5], [11, 4.5], [13, 4.5], [13, 5.5], [9, 5.5]] );
		const moved = moveVertex( added, 1, [11.2, 3.8], floor );
		expect( moved ).toEqual( [[9, 4.5], [11, 4], [13, 4.5], [13, 5.5], [9, 5.5]] );
		expect( isRectangle( moved ) ).toBe( false );
		expect( moveVertex( added, 1, [-3, 50], floor )[1] ).toEqual( [0, 20] );//held to the floor
		expect( addVertex( zone, 3 ) ).toEqual( [...zone, [9, 5]] );//on the closing side
		expect( removeVertex( moved, 1, 'Zone' ) ).toEqual( zone );
		expect( removeVertex( [[0, 0], [4, 0], [2, 2]], 0, 'Zone' ).length ).toBe( 3 );
		expect( removeVertex( [[0, 10], [40, 10]], 0, 'Aisle' ).length ).toBe( 2 );
		expect( floorSides( [[0, 10], [20, 10], [40, 10]], 'Aisle' ).map( s=>s.at ) ).toEqual( [[10, 10], [30, 10]] );//an open run
		expect( floorSides( [[38, 15]], 'Label' ) ).toEqual( [] );
		expect( isRectangle( [[0, 0], [40, 0], [40, 20], [0, 20], [0, 0]] ) ).toBe( false );//the wall, closed by hand
		expect( isRectangle( [[0, 0], [4, 4], [4, 0], [0, 4]] ) ).toBe( false );//its corners, crossed
	} );
	it( 'lists what the line reaches that has no shape, and marks a shape it no longer reaches', ()=>{
		const edit = floorEdit( base() );
		expect( floorUnplaced( reach, edit.shapes ) ).toEqual( { machines: [plant.machines[2], plant.machines[3]], lines: [plant.lines[1], plant.lines[2]] } );
		const placed = addShape( addShape( edit, {kind: 'Machine', machineId: 3}, [5, 5] ).edit, {kind: 'Line', lineId: 12}, [20, 10] ).edit;
		expect( floorUnplaced( reach, placed.shapes ) ).toEqual( { machines: [plant.machines[3]], lines: [plant.lines[1]] } );
		expect( floorUnplaced( reach, withoutShape( edit, 's1' ).shapes ).machines.map( m=>m.name ) ).toEqual( ['Filler', 'Labeler', 'Packer'] );//a shape deleted is to place again
		const shape = ( extra:Partial<EditShape> ):EditShape=>({ key: 'x', kind: 'Machine', points: rect( 0, 0, 1, 1 ), sort: 0, ...extra });
		expect( [shape( {machineId: 1} ), shape( {machineId: 5} ), shape( {machineId: 9} ), shape( {kind: 'Line', lineId: 11} ), shape( {kind: 'Line', lineId: 10} ), shape( {kind: 'Zone'} )].map( s=>floorStranded( s, reach ) ) ).toEqual( [false, true, true, false, true, false] );
		expect( [shape( {machineId: 1} ), shape( {machineId: 1, label: 'Filler 2'} ), shape( {machineId: 9} ), shape( {kind: 'Line', lineId: 11} ), shape( {kind: 'Zone', label: 'Conveyor'} ), shape( {kind: 'Wall'} )].map( s=>floorShapeTitle( s, plant ) ) )
			.toEqual( ['Filler', 'Filler 2', 'Machine 9', 'Bottling', 'Zone Conveyor', 'Wall'] );
	} );
	it( 'says what the gateway would refuse of the floor as it stands', ()=>{
		const edit = floorEdit( base() ), name = ( s:EditShape )=>floorShapeTitle( s, plant ), size = ( floor:object )=>floorEditProblem( {...edit, floor: {...edit.floor, ...floor}}, name );
		expect( floorEditProblem( edit, name ) ).toBeUndefined();
		expect( size( {width: 0} ) ).toBe( "The floor's width must be above zero." );
		expect( size( {grid: NaN} ) ).toBe( "The floor's grid must be above zero." );
		expect( size( {width: 8} ) ).toBe( 'Filler has a point at [9, 3], outside the floor, 8 by 20.' );
		expect( size( {width: 17} ) ).toBe( 'Aisle has a point at [40, 10], outside the floor, 17 by 20.' );//a point on the edge is inside:  the capper's 17 passes
		expect( floorEditProblem( withShape( edit, 's7', {points: [[1, 1], [2, 2], [3, 3]]} ), name ) ).toBe( 'Zone Conveyor must enclose an area.' );
		expect( floorEditProblem( withShape( edit, 's6', {points: [[5, 5], [5, 5]]} ), name ) ).toBe( 'Aisle must have a length.' );
		expect( floorEditProblem( withShape( edit, 's8', {points: [[1, 1], [2, 2]]} ), name ) ).toBe( 'Label Dock is one point.' );
		const tenths = withShape( {...edit, floor: {...edit.floor, grid: 0.1}}, 's7', {points: [[0, 0], [0.1, 0.7], [0.3, 2.1]]} );//on one line, a shoelace of 2.8e-17 in doubles
		expect( floorEditProblem( tenths, name ) ).toBe( 'Zone Conveyor must enclose an area.' );
		expect( floorEditProblem( withShape( edit, 's7', {label: 'x'.repeat( 256 )}), name ) ).toBeUndefined();
		expect( floorEditProblem( withShape( edit, 's7', {label: 'x'.repeat( 257 )}), name ) ).toBe( "A Zone's text is 257 characters, past the 256 a shape keeps." );
		expect( floorEditProblem( addShape( edit, {kind: 'Machine', machineId: 2}, [30, 12] ).edit, name ) ).toBe( 'Capper is on the floor twice:  a machine appears once on a floor.' );
		expect( floorEditProblem( addShape( addShape( edit, {kind: 'Line', lineId: 11}, [20, 12] ).edit, {kind: 'Line', lineId: 11}, [20, 12] ).edit, name ) ).toBe( 'Bottling is on the floor twice:  a line appears once on a floor.' );
	} );
	it( 'builds the batch from a changed, an added and a deleted shape, in the order the rules need', ()=>{
		const start = floorEdit( base() );
		expect( floorBatch( base(), start ) ).toEqual( [] );
		let edit = withShape( start, 's1', {points: rect( 42, 3, 6, 4 )} );//moved into the room the floor gains
		edit = withoutShape( edit, 's2' );
		edit = withShape( edit, 's7', {label: undefined} );
		edit = addShape( edit, {kind: 'Zone', label: 'Pallets'}, [30, 12] ).edit;
		edit = addShape( edit, {kind: 'Machine', machineId: 2}, [20, 5] ).edit;//the capper deleted and placed again
		edit = { ...edit, floor: {width: 50, height: 15, grid: 1} };
		const batch = floorBatch( base(), edit );
		expect( batch ).toEqual( [
			{ op: 'purgeShape', id: 2 },
			{ op: 'updateFloor', args: {width: 50} },
			{ op: 'updateShape', id: 1, args: {points: rect( 42, 3, 6, 4 )} },
			{ op: 'updateShape', id: 7, args: {label: null} },
			{ op: 'createShape', args: {kind: 'Zone', points: rect( 28, 11, 4, 2 ), label: 'Pallets', sort: 1} },
			{ op: 'createShape', args: {kind: 'Machine', points: rect( 18, 3.5, 4, 3 ), oeeMachineId: 2, sort: 1} },
			{ op: 'updateFloor', args: {height: 15, grid: 1} }
		] );
		expect( floorBatch( base(), {...start, floor: {width: 50, height: 20, grid: 1}} ) ).toEqual( [{ op: 'updateFloor', args: {width: 50, grid: 1} }] );//grown alone:  once, ahead
		expect( floorBatch( base(), {...start, floor: {width: 30, height: 20, grid: 0.5}} ) ).toEqual( [{ op: 'updateFloor', args: {width: 30} }] );
		expect( floorBatch( base(), withShape( start, 's8', {sort: 3, label: 'Dock'} ) ) ).toEqual( [{ op: 'updateShape', id: 8, args: {sort: 3} }] );//what differs alone
		expect( floorBatchQL( 1, batch.slice( 0, 5 ) ) ).toEqual( {
			ql: 'mutation purgeOeeFloorShape( id:$id0 ) updateOeeFloor( id:$id1, width:$width1 ) updateOeeFloorShape( id:$id2, points:$points2 ) updateOeeFloorShape( id:$id3, label:$label3 ) createOeeFloorShape( oeeFloorId:$oeeFloorId4, kind:$kind4, points:$points4, label:$label4, sort:$sort4 )',
			vars: { id0: 2, id1: 1, width1: 50, id2: 1, points2: rect( 42, 3, 6, 4 ), id3: 7, label3: null, oeeFloorId4: 1, kind4: 'Zone', points4: rect( 28, 11, 4, 2 ), label4: 'Pallets', sort4: 1 }
		} );
	} );
	it( 'says what Save needs that the caller lacks', ()=>{
		const none:OeeFloorRights = { createFloor: false, updateFloor: false, createShape: false, updateShape: false, purgeShape: false }, update = {...none, updateShape: true};
		const start = floorEdit( base() ), moved = withShape( start, 's1', {points: rect( 4, 3, 6, 4 )} ), added = addShape( moved, {kind: 'Zone'}, [30, 12] ).edit;
		expect( floorBatchRefusal( floorBatch( base(), moved ), update ) ).toBeUndefined();
		expect( floorBatchRefusal( floorBatch( base(), added ), update ) ).toBe( "Save needs Create on the floor's shapes (oee_floor_shapes), which your roles don't grant." );
		expect( floorBatchRefusal( floorBatch( base(), {...withoutShape( added, 's2' ), floor: {...start.floor, grid: 1}} ), update ) ).toBe( "Save needs Create and Purge on the floor's shapes (oee_floor_shapes), and Update on the floors (oee_floors), which your roles don't grant." );
		expect( floorBatchRefusal( floorBatch( base(), withoutShape( added, 's2' ) ), none ) ).toMatch( /^Save needs Create, Update and Purge on the floor's shapes/ );
		expect( floorBatchRefusal( floorBatch( base(), {...withoutShape( added, 's2' ), floor: {...start.floor, grid: 1}} ), {...none, createShape: true, updateShape: true, purgeShape: true, updateFloor: true} ) ).toBeUndefined();
		expect( floorBatchRefusal( [], none ) ).toBeUndefined();
	} );
	it( 'takes up the rows read again after a save refused part-way', ()=>{
		const was = base();
		let edit = withShape( floorEdit( was ), 's1', {points: rect( 4, 3, 6, 4 )} );
		edit = withoutShape( edit, 's2' );
		edit = addShape( edit, {kind: 'Zone', label: 'Pallets'}, [30, 12] ).edit;
		edit = { ...edit, floor: {...edit.floor, height: 15} };
		//the purge, the update and the create went in, the floor's did not, and another hand made a label meanwhile
		const now:FloorBase = { floor, shapes: [row( 1, 'Machine', rect( 4, 3, 6, 4 ), {oeeMachineId: 1} ), ...was.shapes.slice( 2 ), row( 9, 'Zone', rect( 28, 11, 4, 2 ), {label: 'Pallets', sort: 1} ), row( 10, 'Label', [[5, 18]], {label: 'Exit', sort: 1} )] };
		const rebased = floorRebase( edit, was, now );
		expect( rebased.shapes.map( s=>[s.key, s.id] ) ).toEqual( [['s1', 1], ['s6', 6], ['s7', 7], ['s8', 8], ['n1', 9], ['s10', 10]] );//the zone has its row, and the label joins
		expect( floorBatch( now, rebased ) ).toEqual( [{ op: 'updateFloor', args: {height: 15} }] );//what is still to write
		//nothing went in, and the filler's row was purged by another hand:  it is to make again
		const gone:FloorBase = { floor, shapes: was.shapes.slice( 1 ) };
		expect( floorBatch( gone, floorRebase( edit, was, gone ) ).map( m=>m.op=='createShape' ? [m.op, m.args.kind, m.args.oeeMachineId] : [m.op] ) ).toEqual( [['purgeShape'], ['createShape', 'Machine', 1], ['createShape', 'Zone', undefined], ['updateFloor']] );
	} );
	it( "keeps what another hand changed of what the edit left, and gives a machine placed twice the other's row", ()=>{
		const was = base();
		let edit = withShape( floorEdit( was ), 's1', {points: rect( 4, 3, 6, 4 )} );
		edit = addShape( edit, {kind: 'Machine', machineId: 3}, [30, 12] ).edit;//the labeler, placed here
		//nothing went in, and meanwhile another hand moved the dock, purged the aisle, took the conveyor's text, placed the labeler and changed the grid
		const now:FloorBase = { floor: {...floor, grid: 1}, shapes: [
			...was.shapes.slice( 0, 2 ), row( 7, 'Zone', rect( 9, 4.5, 4, 1 ) ), row( 8, 'Label', [[30, 15]], {label: 'Dock'} ), row( 11, 'Machine', rect( 20, 15, 4, 3 ), {oeeMachineId: 3, sort: 2} )
		] };
		const rebased = floorRebase( edit, was, now );
		expect( rebased.floor.grid ).toBe( 1 );
		expect( rebased.shapes.map( s=>[s.key, s.id, s.label] ) ).toEqual( [['s1', 1, undefined], ['s2', 2, undefined], ['s7', 7, undefined], ['s8', 8, 'Dock'], ['n1', 11, undefined]] );//no aisle, and the labeler the other's row
		expect( floorEditProblem( rebased, s=>floorShapeTitle( s, plant ) ) ).toBeUndefined();
		expect( floorBatch( now, rebased ) ).toEqual( [//the edit's own changes alone, the labeler where this one placed it
			{ op: 'updateShape', id: 1, args: {points: rect( 4, 3, 6, 4 )} }, { op: 'updateShape', id: 11, args: {points: rect( 28, 10.5, 4, 3 )} }
		] );
		//the edit changed what another hand purged:  it is made again, and what it took out stays out
		const moved = withoutShape( withShape( floorEdit( was ), 's6', {points: [[0, 11], [40, 11]]} ), 's8' );
		expect( floorBatch( now, floorRebase( moved, was, now ) ).map( m=>m.op ) ).toEqual( ['purgeShape', 'createShape'] );
	} );
	it( "exports the spec's example and reads it back, machines and lines by slug", ()=>{
		const edit = imported( example );
		expect( edit.floor ).toEqual( {width: 40, height: 20, grid: 0.5} );
		expect( edit.shapes.map( s=>[s.key, s.id, s.kind, s.machineId, s.label, s.sort] ) ).toEqual( [
			['n1', undefined, 'Wall', undefined, undefined, 0], ['n2', undefined, 'Aisle', undefined, undefined, 0], ['n3', undefined, 'Machine', 1, undefined, 0],
			['n4', undefined, 'Zone', undefined, 'Conveyor', 0], ['n5', undefined, 'Machine', 2, undefined, 1], ['n6', undefined, 'Label', undefined, 'Dock', 0]
		] );
		expect( edit.shapes[2].points ).toEqual( rect( 3, 3, 6, 4 ) );
		expect( floorExport( edit, reach ) ).toBe( example );
		expect( floorBatch( {floor, shapes: []}, edit ).map( m=>m.op ) ).toEqual( Array( 6 ).fill( 'createShape' ) );//the same size, so the shapes alone
		//a line by its slug, a kind's shapes by their order whatever their rows', and a shape the line no longer reaches left out
		let more = addShape( edit, {kind: 'Line', lineId: 11, label: 'North'}, [10, 5] ).edit;
		more = addShape( more, {kind: 'Machine', machineId: 5}, [30, 15] ).edit;
		more = withShape( withShape( more, 'n3', {sort: 1} ), 'n5', {sort: 0} );
		const lines = floorExport( more, reach ).split( '\n' );
		expect( lines.length ).toBe( 9 );
		expect( [lines[3], lines[5]] ).toEqual( ['\t{ "kind": "Machine", "machine": "capper", "points": [[13,3],[17,3],[17,7],[13,7]] },', '\t{ "kind": "Machine", "machine": "filler", "points": [[3,3],[9,3],[9,7],[3,7]] },'] );
		expect( lines[7] ).toBe( '\t{ "kind": "Line", "line": "bottling", "label": "North", "points": [[4,2],[16,2],[16,8],[4,8]] }' );
		expect( imported( floorExport( more, reach ) ).shapes.map( s=>s.lineId ?? s.machineId ?? s.kind ) ).toEqual( ['Wall', 'Aisle', 2, 'Zone', 1, 'Label', 11] );
		expect( imported( floorExport( empty, reach ) ) ).toEqual( empty );
		expect( imported( '{ "width": 10, "height": 5, "shapes": [] }' ).floor.grid ).toBe( 0.5 );//the default
	} );
	it( 'refuses an import onto a floor with shapes, and a file no floor takes', ()=>{
		expect( problem( example, floorEdit( base() ) ) ).toBe( 'Import reads onto an empty floor, and this one has 5 shapes:  delete them first.' );
		expect( problem( example, {...empty, shapes: [floorEdit( base() ).shapes[0]]} ) ).toBe( 'Import reads onto an empty floor, and this one has a shape:  delete it first.' );
		expect( problem( 'not json' ) ).toMatch( /^The file isn't json:  / );
		expect( problem( '{ "width": 40 }' ) ).toMatch( /^The file isn't a floor's/ );
		const one = ( shape:object, size:object={} )=>problem( JSON.stringify( {width: 40, height: 20, grid: 0.5, ...size, shapes: [shape]} ) );
		expect( one( {kind: 'Blob', points: [[1, 1]]} ) ).toBe( 'Shape 1:  kind is Machine, Line, Zone, Wall, Aisle or Label, not "Blob".' );
		expect( one( {kind: 'Label', points: [[1, 'x']]} ) ).toBe( 'Shape 1:  points is a list of [x, y] in metres.' );
		expect( one( {kind: 'Machine', machine: 'loose', points: rect( 1, 1, 2, 2 )} ) ).toBe( 'Shape 1:  machine "loose" isn\'t one the floor\'s line reaches.' );
		expect( one( {kind: 'Line', line: 'plant', points: rect( 1, 1, 2, 2 )} ) ).toBe( 'Shape 1:  line "plant" isn\'t one the floor\'s line reaches.' );//the floor's own
		expect( one( {kind: 'Machine', points: rect( 1, 1, 2, 2 )} ) ).toMatch( /^Shape 1:  machine undefined isn't one/ );
		expect( one( {kind: 'Zone', machine: 'filler', points: rect( 1, 1, 2, 2 )} ) ).toBe( 'Shape 1:  a Zone names no machine or line.' );
		expect( one( {kind: 'Zone', points: rect( 39, 1, 2, 2 )} ) ).toBe( 'Shape 1 has a point at [41, 1], outside the floor, 40 by 20.' );
		expect( one( {kind: 'Machine', machine: 'filler', points: [[1, 1], [2, 2]]} ) ).toBe( 'Shape 1 must enclose an area.' );
		expect( one( {kind: 'Label', points: [[1, 1]]}, {width: 0} ) ).toBe( "The floor's width must be above zero." );
		expect( problem( JSON.stringify( {width: 40, height: 20, shapes: [{kind: 'Machine', machine: 'filler', points: rect( 1, 1, 2, 2 )}, {kind: 'Machine', machine: 'filler', points: rect( 5, 1, 2, 2 )}]} ) ) ).toBe( "Shape 2:  machine 'filler' is on the floor already." );
	} );
} );