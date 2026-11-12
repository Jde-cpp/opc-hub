import { OeeSegment, toOeeBucket, toOeeLoss, toOeeTimeline, toOeeTimelines } from '../../../model/oee';
import { codable, oeeDayName, oeeKindColors, plannedColor, plannedPieces, segmentKind, toGantt, toGanttPairs, toGanttRows, toOeeTrend, toPareto, trendColor } from './oee-chart-data';

const t = ( h:number, m=0 )=>new Date( Date.UTC( 2026, 10, 11, h, m ) );
const iso = ( h:number, m=0 )=>t( h, m ).toISOString();
const segment = ( from:[number,number], to:[number,number], extra:object={} )=>({ start: iso( ...from ), end: iso( ...to ), state: 'stopped', small: false, uncoded: false, ...extra });
const Jam = {id: 3, name: 'Jam', category: 'Unplanned'}, Changeover = {id: 2, name: 'Changeover', category: 'Planned'}, NoOrders = {id: 5, name: 'No orders', category: 'Excluded'};
//the spec's shift (*The Gantt chart*):  every kind of loss
const shift = toOeeTimeline( [{
	planned: [{shift: 1, name: 'First', start: iso( 6 ), end: iso( 14 ), breaks: [{start: iso( 10 ), end: iso( 10, 20 )}]}],
	segments: [
		segment( [6, 0], [7, 0], {state: 'running'} ),
		segment( [7, 0], [7, 30], {stop: 1, reason: Changeover, coding: {id: 9, comment: 'size change', userName: 'op'}} ),
		segment( [7, 30], [8, 12], {state: 'running'} ),
		segment( [8, 12], [8, 40], {stop: 2, reason: Jam} ),
		segment( [8, 40], [9, 0], {state: 'running'} ),
		segment( [9, 0], [9, 2], {stop: 3, small: true} ),
		segment( [9, 2], [11, 0], {state: 'running'} ),
		segment( [11, 0], [11, 10], {state: 'unknown'} ),
		segment( [11, 10], [12, 30], {state: 'running'} ),
		segment( [12, 30], [12, 50], {stop: 4, uncoded: true} ),
		segment( [12, 50], [13, 0], {state: 'running'} ),
		segment( [13, 0], [14, 0], {stop: 5, reason: NoOrders} ),
		segment( [14, 0], [14, 0], {state: 'running'} )
	] }] );
const range = { start: t( 6 ), end: t( 14 ) };

describe( 'oee chart data', ()=>{
	it( "draws a segment by what it is:  a stop in its reason's category, uncoded and small in their own", ()=>{
		const kind = ( extra:object )=>segmentKind( <OeeSegment>{state: 'stopped', small: false, uncoded: false, ...extra} );
		expect( shift.segments.slice( 0, 12 ).map( segmentKind ) ).toEqual( ['running', 'Planned', 'running', 'Unplanned', 'running', 'small', 'running', 'unknown', 'running', 'uncoded', 'running', 'Excluded'] );
		expect( kind( {small: true, reason: Jam} ) ).toBe( 'Unplanned' );//a small stop with a reason wears its category
		expect( kind( {} ) ).toBe( 'uncoded' );
		expect( kind( {small: true, uncoded: true} ) ).toBe( 'small' );//as the gateway answers a small stop with no reason
		expect( codable( <OeeSegment>{state: 'unknown', stop: 1} ) ).toBe( false );//an unknown span's row is no stop
		expect( codable( <OeeSegment>{state: 'stopped', stop: 2} ) ).toBe( true );
		expect( kind( {reason: {id: 9, name: 'Odd', category: 'Other'}, uncoded: true} ) ).toBe( 'uncoded' );
	} );
	it( 'maps the timeline onto two rows, the state bands in order with their segments', ()=>{
		const g = toGantt( shift, range );
		expect( g.state ).toHaveLength( 12 );//the empty last segment draws nothing
		expect( g.state.every( p=>p.y==1 ) ).toBe( true );
		expect( g.state.map( p=>p.color ).slice( 0, 6 ) ).toEqual( [oeeKindColors.running, oeeKindColors.Planned, oeeKindColors.running, oeeKindColors.Unplanned, oeeKindColors.running, oeeKindColors.small] );
		const jam = g.state[3];
		expect( [jam.x, jam.x2, jam.name] ).toEqual( [t( 8, 12 ).getTime(), t( 8, 40 ).getTime(), 'Jam'] );
		expect( jam.custom.segment?.stop ).toBe( 2 );
		expect( jam.custom.text ).toContain( '28m' );
		expect( jam.custom.text ).toContain( 'Click to code the stop' );
		expect( g.state[0].custom.text ).not.toContain( 'Click' );//running time codes nothing
		expect( g.state[1].custom.text ).toContain( 'size change' );
		expect( g.state[1].custom.text ).toContain( 'Coded by op' );
		expect( g.state[5].name ).toBe( 'Small stop' );
	} );
	it( "cuts the breaks out of the planned row, within the range", ()=>{
		const g = toGantt( shift, range );
		expect( g.planned.map( p=>[p.x, p.x2, p.y, p.color, p.name] ) ).toEqual( [
			[t( 6 ).getTime(), t( 10 ).getTime(), 0, plannedColor, 'First'],
			[t( 10, 20 ).getTime(), t( 14 ).getTime(), 0, plannedColor, 'First']] );
		const o = shift.planned[0];
		expect( plannedPieces( o, {start: t( 9 ), end: t( 10, 10 )} ) ).toEqual( [{start: t( 9 ), end: t( 10 )}] );//a break running past the range's end
		expect( plannedPieces( o, {start: t( 10, 5 ), end: t( 12 )} ) ).toEqual( [{start: t( 10, 20 ), end: t( 12 )}] );
		expect( plannedPieces( {...o, breaks: [{start: t( 6 ), end: t( 14 )}]}, range ) ).toEqual( [] );
		expect( plannedPieces( {...o, breaks: []}, {start: t( 15 ), end: t( 16 )} ) ).toEqual( [] );
	} );
	it( 'names in the legend only what the chart shows, in a fixed order', ()=>{
		expect( toGantt( shift, range ).legend.map( l=>l.label ) ).toEqual( ['Planned time', 'Running', 'Unplanned stop', 'Planned stop', 'Excluded', 'Uncoded stop', 'Small stop', 'Unknown'] );
		const running = toOeeTimeline( [{planned: [], segments: [segment( [6, 0], [7, 0], {state: 'running'} )]}] );
		expect( toGantt( running, range ).legend ).toEqual( [{color: oeeKindColors.running, label: 'Running'}] );
	} );
	it( "gives the dashboard a row per machine, each band on its machine's row and named for it, with no planned row", ()=>{
		const rows = toGanttRows( [{name: 'Pump 1', segments: shift.segments.slice( 0, 2 )}, {name: 'Pump <2>', segments: shift.segments.slice( 3, 6 )}] );
		expect( rows.rows ).toEqual( ['Pump 1', 'Pump <2>'] );
		expect( rows.planned ).toEqual( [] );
		expect( rows.state.map( p=>[p.y, p.name] ) ).toEqual( [[0, 'Running'], [0, 'Changeover'], [1, 'Jam'], [1, 'Running'], [1, 'Small stop']] );
		expect( rows.state[1].custom.text ).toMatch( /^Pump 1:  Changeover, Planned, .*, size change, Coded by op, Click to open the machine's OEE page$/ );
		expect( rows.state[2].custom.tip ).toContain( '<b>Pump &lt;2&gt;:  Jam</b>' );
		expect( rows.legend.map( l=>l.label ) ).toEqual( ['Running', 'Unplanned stop', 'Planned stop', 'Small stop'] );
		expect( toGanttRows( [{name: 'Idle', segments: []}] ) ).toEqual( {rows: ['Idle'], groups: [], at: [0], planned: [], state: [], legend: []} );
	} );
	it( 'escapes a reason and a comment in the hover', ()=>{
		const g = toGantt( toOeeTimeline( [{planned: [], segments: [segment( [8, 0], [9, 0], {stop: 1, reason: {id: 1, name: '<Jam & co>', category: 'Unplanned'}, coding: {id: 1, comment: '<img src=x>'}} )]}] ), range );
		expect( g.state[0].custom.tip ).toContain( '<b>&lt;Jam &amp; co&gt;</b>' );
		expect( g.state[0].custom.tip ).toContain( '&lt;img src=x&gt;' );
		expect( g.state[0].custom.text ).toContain( '<Jam & co>' );
	} );

	it( "makes the Pareto's columns of minutes, longest first as answered, with the share down to each", ()=>{
		const p = toPareto( [{kind: 'reason', reason: NoOrders, ms: 3600000, stops: 1}, {kind: 'reason', reason: Jam, ms: 1680000, stops: 1}, {kind: 'uncoded', ms: 1200000, stops: 1}, {kind: 'small', ms: 240000, stops: 2}, {kind: 'reason', reason: Changeover, ms: 0, stops: 0}].map( toOeeLoss ) );
		expect( p.categories ).toEqual( ['No orders', 'Jam', 'Uncoded', 'Small stops'] );//a row with no time is no column
		expect( p.points.map( x=>x.y ) ).toEqual( [60, 28, 20, 4] );
		expect( p.points.map( x=>x.color ) ).toEqual( [oeeKindColors.Excluded, oeeKindColors.Unplanned, oeeKindColors.uncoded, oeeKindColors.small] );
		expect( p.points[1].custom.label ).toBe( '28m' );
		expect( p.points[1].custom.text ).toBe( 'Jam, 28m in 1 stop, 25.0% of the lost time, 78.6% down to here' );
		expect( p.points[3].custom.text ).toContain( '100.0% down to here' );
		expect( p.legend.map( l=>l.label ) ).toEqual( ['Unplanned stop', 'Excluded', 'Uncoded stop', 'Small stop'] );
		expect( toPareto( [] ) ).toEqual( {categories: [], points: [], legend: []} );
	} );
	it( "makes the trend's columns of OEE in percent, a bucket that measures nothing a gap", ()=>{
		const bucket = ( start:Date, extra:object )=>toOeeBucket( {start: start.toISOString(), end: start.toISOString(), measured: [], partial: false, ...extra} );
		const day = ( d:Date )=>d.toISOString().substring( 5, 10 );
		const buckets = [bucket( t( 6 ), {shift: 'First', oee: 0.6842, availability: 0.8, performance: 0.9, quality: 0.95, total: 16848, good: 16006}), bucket( t( 14 ), {shift: 'Second', oee: null, partial: true} )];
		const byShift = toOeeTrend( buckets, 'shift', day );
		expect( byShift.categories ).toEqual( ['First 11-11', 'Second 11-11'] );
		expect( byShift.points.map( p=>p.y ) ).toEqual( [68.4, null] );
		expect( byShift.points[0].color ).toBe( trendColor );
		expect( byShift.points[0].custom.label ).toBe( '68.4%' );
		expect( byShift.points[0].custom.text ).toContain( 'Availability 80.0%, performance 90.0%, quality 95.0%' );
		expect( byShift.points[1].custom.text ).toContain( 'Partial' );
		expect( toOeeTrend( buckets, 'day', day ).categories ).toEqual( ['11-11', '11-11'] );
		expect( byShift.legend ).toEqual( [] );//one series:  its title names it
	} );
	it( "names a machine's UTC day by UTC's date, which the browser's may not be", ()=>{
		const late = new Date( '2026-11-11T23:30:00Z' );
		expect( oeeDayName( true )( late ) ).toBe( late.toLocaleDateString( undefined, {month: 'short', day: 'numeric', timeZone: 'UTC'} ) );
		expect( oeeDayName()( late ) ).toBe( late.toLocaleDateString( undefined, {month: 'short', day: 'numeric'} ) );
	} );
} );
describe( 'the grouped rows', ()=>{
	const band = ( from:[number,number], to:[number,number], extra:object={} )=>toOeeTimeline( [{planned: [], segments: [segment( from, to, extra )]}] ).segments;
	it( "puts a line's machines under a row that names it, one for each run of its rows, with no band of its own", ()=>{
		const rows = toGanttRows( [
			{name: 'Pump 1', segments: band( [6, 0], [7, 0], {state: 'running'} ), group: 'Area Packaging'}, {name: 'Pump 2', segments: band( [6, 0], [7, 0], {stop: 1, reason: Jam} ), group: 'Area Packaging'},
			{name: 'Pump 1', segments: band( [6, 0], [7, 0], {state: 'running'} ), group: 'Line Every pump'},
			{name: 'Pump 4', segments: band( [6, 0], [6, 0], {state: 'running'} )}, {name: 'Pump 5', segments: band( [6, 0], [7, 0], {state: 'unknown'} )}
		] );
		expect( rows.rows ).toEqual( ['Area Packaging', 'Pump 1', 'Pump 2', 'Line Every pump', 'Pump 1', 'Pump 4', 'Pump 5'] );
		expect( rows.groups ).toEqual( [0, 3] );
		expect( rows.at ).toEqual( [undefined, 0, 1, undefined, 2, 3, 4] );
		expect( rows.state.map( b=>[b.y, b.custom.kind] ) ).toEqual( [[1, 'running'], [2, 'Unplanned'], [4, 'running'], [6, 'unknown']] );//a band of no length isn't drawn
		expect( rows.state[1].custom.text ).toContain( 'Pump 2:  Jam' );
		expect( rows.planned ).toEqual( [] );
	} );
	it( 'is a row per machine with no group', ()=>{
		expect( toGanttRows( [{name: 'Pump 1', segments: []}, {name: 'Pump 2', segments: []}] ) ).toMatchObject( {rows: ['Pump 1', 'Pump 2'], groups: [], at: [0, 1]} );
	} );
	it( "draws a line's page a pair of rows per machine, its planned time over its state", ()=>{
		const planned = [{shift: 1, name: 'First', start: iso( 6 ), end: iso( 14 ), breaks: [{start: iso( 10 ), end: iso( 10, 20 )}]}];
		const timelines = toOeeTimelines( [
			{machine: {id: 4, name: 'Pump 1'}, planned, segments: [segment( [6, 0], [8, 0], {state: 'running'} ), segment( [8, 0], [8, 30], {stop: 2, uncoded: true} )]},
			{machine: {id: 5, name: 'Pump 2'}, planned: [], segments: [segment( [6, 0], [9, 0], {state: 'unknown', stop: 3} )]}
		] );
		const gantt = toGanttPairs( timelines, {start: t( 6 ), end: t( 14 )} );
		expect( gantt.rows ).toEqual( ['Pump 1, planned', 'Pump 1', 'Pump 2, planned', 'Pump 2'] );
		expect( gantt.planned.map( b=>[b.y, b.x, b.x2] ) ).toEqual( [[0, t( 6 ).getTime(), t( 10 ).getTime()], [0, t( 10, 20 ).getTime(), t( 14 ).getTime()]] );
		expect( gantt.state.map( b=>[b.y, b.custom.kind] ) ).toEqual( [[1, 'running'], [1, 'uncoded'], [3, 'unknown']] );
		expect( gantt.state[1].custom.text ).toMatch( /^Pump 1:  Uncoded stop, .*Click to code the stop$/ );
		expect( gantt.state[2].custom.text ).not.toContain( 'Click' );//an unknown span is no stop
		expect( gantt.legend.map( l=>l.label ) ).toEqual( ['Planned time', 'Running', 'Uncoded stop', 'Unknown'] );
	} );
} );