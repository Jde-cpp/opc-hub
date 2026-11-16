import { floorDrawing, floorListOrder, floorShapeName, floorTextLevel, OeeFloor, oeeFloorState, oeeLiveSplit, oeeReachedLines, parseFloorPoints, shiftLeft, toOeeFloorShape, zoneClock } from './oee-floor';
import { OeeLiveStatus } from './oee-live';
import { OeePlant } from './oee-lines';

const t = ( h:number, m=0, s=0 )=>new Date( Date.UTC( 2026, 10, 11, h, m, s ) );
const Jam = { id: 3, name: 'Jam', category: 'Unplanned' }, Changeover = { id: 4, name: 'Changeover', category: 'Planned' };
const figures = { start: t( 6 ), end: t( 9 ), availability: 0.8, performance: 0.9, quality: 0.95, oee: 0.684, partial: false, planned: 10800000, run: 9000000, unplanned: 1200000, plannedStop: 600000, small: 120000, excluded: 0, unknown: 0 };
const status = ( extra:Partial<OeeLiveStatus> ):OeeLiveStatus=>({ state: 'running', planned: true, calendarMissing: false, figures, ...extra });
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
const floor:OeeFloor = { id: 1, lineId: 10, width: 40, height: 20, grid: 0.5 };
const shape = ( id:number, kind:string, points:number[][], extra:object={} )=>toOeeFloorShape( { id, oeeFloorId: 1, kind, points: JSON.stringify( points ), sort: 0, ...extra } )!;
const rect = ( x:number, y:number, w:number, h:number )=>[[x, y], [x+w, y], [x+w, y+h], [x, y+h]];
const shapes = [
	shape( 1, 'Machine', rect( 3, 3, 6, 4 ), {oeeMachineId: 1} ), shape( 2, 'Machine', rect( 13, 3, 4, 4 ), {oeeMachineId: 2} ), shape( 3, 'Machine', rect( 21, 3, 6, 4 ), {oeeMachineId: 3, label: 'Labeller'} ),
	shape( 4, 'Machine', rect( 3, 13, 6, 4 ), {oeeMachineId: 4} ),
	shape( 5, 'Wall', [[0, 0], [40, 0], [40, 20], [0, 20], [0, 0]] ), shape( 6, 'Aisle', [[0, 10], [40, 10]] ), shape( 7, 'Zone', rect( 9, 4.5, 4, 1 ), {label: 'Conveyor'} ), shape( 8, 'Label', [[38, 15]], {label: 'Dock'} )
];

describe( 'oee-floor', ()=>{
	describe( 'the rows', ()=>{
		it( 'reads points as text or a list, and passes over what is neither', ()=>{
			expect( parseFloorPoints( '[[3,3],[9,3.5]]' ) ).toEqual( [[3, 3], [9, 3.5]] );
			expect( parseFloorPoints( [[1, 2]] ) ).toEqual( [[1, 2]] );
			expect( [parseFloorPoints( '[[3,3],[9]]' ), parseFloorPoints( 'not json' ), parseFloorPoints( [[1, 'x']] ), parseFloorPoints( 7 )] ).toEqual( [undefined, undefined, undefined, undefined] );
			expect( toOeeFloorShape( {id: 1, oeeFloorId: 1, kind: 'Blob', points: '[[1,1]]'} ) ).toBeUndefined();
			expect( toOeeFloorShape( {id: 1, oeeFloorId: 1, kind: 'Label', points: '[[1,1]]', label: '', oeeMachineId: null, memberLineId: null} ) ).toEqual( {id: 1, floorId: 1, kind: 'Label', points: [[1, 1]], sort: 0, machineId: undefined, lineId: undefined, label: undefined} );
		} );
		it( 'reaches the lines under a line, itself left out, and none under a deleted one', ()=>{
			expect( oeeReachedLines( plant, 10 ).map( l=>l.name ) ).toEqual( ['Bottling', 'Packing'] );
			expect( oeeReachedLines( plant, 11 ) ).toEqual( [] );
			expect( oeeReachedLines( {...plant, lines: plant.lines.filter( l=>l.id!=10 )}, 10 ) ).toEqual( [] );
		} );
	} );
	describe( 'the drawing', ()=>{
		it( 'boxes a line with no shape around its three machines, half a grid out, ahead of the shaped lines', ()=>{
			const d = floorDrawing( floor, shapes, plant, 10 );
			expect( d.machines.map( m=>[m.key, m.name] ) ).toEqual( [['m1', 'Filler'], ['m2', 'Capper'], ['m3', 'Labeller'], ['m4', 'Packer']] );
			expect( d.lines.map( l=>[l.key, l.name, l.boxed, l.box] ) ).toEqual( [
				['l11', 'Bottling', true, {x: 2.75, y: 2.75, width: 24.5, height: 4.5}],
				['l12', 'Packing', true, {x: 2.75, y: 12.75, width: 6.5, height: 4.5}]
			] );
			expect( d.lines[0].points ).toEqual( [[2.75, 2.75], [27.25, 2.75], [27.25, 7.25], [2.75, 7.25]] );
			expect( [d.walls.length, d.aisles.length, d.zones[0].label, d.labels[0].points] ).toEqual( [1, 1, 'Conveyor', [[38, 15]]] );
			expect( floorListOrder( d ).map( x=>x.name ) ).toEqual( ['Bottling', 'Packing', 'Filler', 'Capper', 'Labeller', 'Packer'] );
		} );
		it( 'holds a box to the floor, draws a shaped line as drawn, and leaves out a machine the line no longer reaches or that is gone', ()=>{
			const edge = [shape( 1, 'Machine', rect( 0, 0, 6, 4 ), {oeeMachineId: 1} ), shape( 9, 'Line', rect( 1, 12, 10, 6 ), {memberLineId: 12, sort: 1} ), shape( 4, 'Machine', rect( 3, 13, 6, 4 ), {oeeMachineId: 4} ),
				shape( 10, 'Machine', rect( 30, 3, 4, 4 ), {oeeMachineId: 5} ), shape( 11, 'Machine', rect( 30, 13, 4, 4 ), {oeeMachineId: 99} ), shape( 12, 'Line', rect( 30, 13, 4, 4 ), {memberLineId: 10} )];
			const d = floorDrawing( floor, edge, plant, 10 );
			expect( d.machines.map( m=>m.key ) ).toEqual( ['m1', 'm4'] );//the loose machine and the one that isn't there are left out
			expect( d.lines.map( l=>[l.key, l.boxed, l.box] ) ).toEqual( [['l11', true, {x: 0, y: 0, width: 6.25, height: 4.25}], ['l12', false, {x: 1, y: 12, width: 10, height: 6}]] );//the floor's own line has no shape of its own
			expect( floorDrawing( floor, shapes, plant, 11 ).lines ).toEqual( [] );//Bottling reaches no line
			expect( floorDrawing( floor, shapes, plant, 11 ).machines.map( m=>m.key ) ).toEqual( ['m1', 'm2', 'm3'] );
		} );
		it( 'keeps the name and the OEE alone where the shape is small on the screen', ()=>{
			const box = { x: 3, y: 3, width: 6, height: 4 };
			expect( floorTextLevel( box, 20 ) ).toBe( 'full' );//120 by 80 px
			expect( floorTextLevel( box, 12 ) ).toBe( 'brief' );//72 by 48 px, zoomed out
			expect( floorTextLevel( {...box, height: 3.5}, 20 ) ).toBe( 'brief' );//70 px tall
		} );
	} );
	describe( 'the state', ()=>{
		const now = t( 9 );
		it( 'reads planned first, then the state, and says since when', ()=>{
			expect( oeeFloorState( status( {state: 'running', since: t( 7, 48 )} ), 'live', now ) ).toEqual( { token: 'running', dash: 'solid', label: 'Running', line: 'Running 1h 12m', since: t( 7, 48 ) } );
			expect( oeeFloorState( status( {state: 'running', since: t( 7, 48 ), planned: false} ), 'live', now ) ).toEqual( { token: 'neutral', dash: 'solid', label: 'Not planned', line: 'Not planned' } );//a break outranks running
			expect( oeeFloorState( status( {state: 'unknown', since: t( 8, 50 )} ), 'live', now ) ).toEqual( { token: 'unknown', dash: 'dashed', label: 'No data', line: 'No data 10m', since: t( 8, 50 ) } );
			expect( oeeFloorState( status( {state: 'unknown'} ), 'live', now ).line ).toBe( 'No data' );
		} );
		it( 'colours a stop by its reason once it is no longer small, and uncoded where it has none', ()=>{
			const stop = ( small:boolean, reason?:typeof Jam )=>status( {state: 'stopped', since: t( 8, 56 ), stop: {start: t( 8, 56 ), small, reason}} );
			expect( oeeFloorState( stop( true ), 'live', now ) ).toEqual( { token: 'small', dash: 'solid', label: 'Stopped', line: 'Stopped 4m', since: t( 8, 56 ) } );
			expect( oeeFloorState( stop( true, Jam ), 'live', now ).token ).toBe( 'small' );//still small, whatever its code says
			expect( oeeFloorState( stop( false, Jam ), 'live', now ) ).toEqual( { token: 'Unplanned', dash: 'solid', label: 'Jam', line: 'Jam 4m', since: t( 8, 56 ) } );
			expect( oeeFloorState( stop( false, Changeover ), 'live', now ).token ).toBe( 'Planned' );
			expect( oeeFloorState( stop( false ), 'live', now ) ).toEqual( { token: 'uncoded', dash: 'solid', label: 'Uncoded', line: 'Uncoded 4m', since: t( 8, 56 ) } );
			expect( oeeFloorState( stop( false, {id: 9, name: 'Odd', category: 'Other'} ), 'live', now ).token ).toBe( 'uncoded' );
		} );
		it( 'draws a refused subscription No access over everything, and says how one stands before its first status', ()=>{
			expect( oeeFloorState( status( {state: 'running', since: t( 8 )} ), 'refused', now ) ).toEqual( { token: 'neutral', dash: 'dotted', label: 'No access', line: 'No access' } );
			expect( oeeFloorState( undefined, 'ended', now ) ).toEqual( { token: 'neutral', dash: 'dotted', label: 'Ended', line: 'Ended' } );
			expect( oeeFloorState( undefined, 'connecting', now ).line ).toBe( 'Connecting' );
			expect( oeeFloorState( undefined, 'dropped', now ).line ).toBe( 'Disconnected' );
			expect( oeeFloorState( status( {state: 'running', since: t( 8 )} ), 'dropped', now ).line ).toBe( 'Running 1h' );//the last it said stands
		} );
		it( "colours a line by its machines, always dashed, and counts them", ()=>{
			const line = ( running:number, stopped:number, unknown=0, planned=true )=>oeeFloorState( status( {state: 'unknown', planned, machines: {running, stopped, unknown}} ), 'live', now );
			expect( line( 3, 0 ) ).toEqual( { token: 'running', dash: 'dashed', label: 'All running', line: '3 of 3 running' } );
			expect( line( 2, 1 ) ).toEqual( { token: 'uncoded', dash: 'dashed', label: 'Some stopped', line: '2 of 3 running' } );
			expect( line( 0, 2, 1 ) ).toEqual( { token: 'Unplanned', dash: 'dashed', label: 'All stopped', line: '0 of 3 running' } );
			expect( line( 0, 0, 2 ) ).toEqual( { token: 'unknown', dash: 'dashed', label: 'No data', line: 'No data, 0 of 2 running' } );
			expect( line( 0, 0, 0 ).line ).toBe( 'No data' );
			expect( line( 2, 1, 0, false ) ).toEqual( { token: 'neutral', dash: 'dashed', label: 'Not planned', line: 'Not planned, 2 of 3 running' } );
		} );
		it( 'names a shape by its machine, its state and its OEE, and splits the live figures as the figures panel does', ()=>{
			const s = status( {state: 'running', since: t( 7, 48 )} );
			expect( floorShapeName( 'Filler', oeeFloorState( s, 'live', now ), s.figures ) ).toBe( 'Filler, Running 1h 12m, OEE 68.4%' );
			expect( floorShapeName( 'Filler', oeeFloorState( undefined, 'refused', now ), undefined ) ).toBe( 'Filler, No access, OEE –' );
			const split = oeeLiveSplit( figures )!;
			expect( split.planned.map( r=>[r.id, r.ms, r.share] ) ).toEqual( [['run', 9000000, 9000000/10800000], ['unplanned', 1200000, 1200000/10800000], ['plannedStop', 600000, 600000/10800000]] );
			expect( split.beside.map( r=>[r.id, r.ms] ) ).toEqual( [['small', 120000], ['excluded', 0], ['unknown', 0]] );
			expect( oeeLiveSplit( {partial: false} ) ).toBeUndefined();
		} );
	} );
	describe( 'the clock', ()=>{
		it( 'reads the wall time in the zone, UTC for one the browser lacks, and the time left in a shift', ()=>{
			const now = new Date( Date.UTC( 2026, 10, 11, 12, 34, 56 ) );
			expect( zoneClock( now, 'Etc/GMT-1' ) ).toEqual( { time: '13:34:56', zone: 'Etc/GMT-1', offset: 'UTC+01:00' } );//an hour ahead of UTC
			expect( zoneClock( now, 'UTC' ) ).toEqual( { time: '12:34:56', zone: 'UTC', offset: 'UTC' } );
			expect( zoneClock( now, 'Mars/Olympus' ).zone ).toBe( 'UTC' );
			expect( zoneClock( now, '' ).time ).toBe( '12:34:56' );
			expect( shiftLeft( t( 14 ), t( 12, 34, 10 ) ) ).toBe( '1h 26m left' );
			expect( shiftLeft( t( 14 ), t( 14 ) ) ).toBe( 'ending' );
		} );
	} );
} );