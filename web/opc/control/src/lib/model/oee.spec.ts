import { isOeeRangeRefusal, narrowOeeRange, oeeCodingCalls, oeeCount, oeeDuration, oeeLossSplit, oeePercent, oeePeriodRange, oeeSplitProblem, toOeeBucket, toOeeLoss, toOeeStop, toOeeStopsPage, toOeeTimeline } from './oee';

const at = ( h:number, m=0, day=11 )=>new Date( 2026, 10, day, h, m );//local, as the picker's days are

describe( 'oee model', ()=>{
	//the spec's shift (*The Gantt chart*):  390 planned minutes, 48 unplanned, 30 planned stop, 4 small
	const shift = { start: '2026-11-11T06:00:00.0000000Z', end: '2026-11-11T14:00:00.0000000Z', shift: 'First', planned: 23400000, run: 18720000, unplanned: 2880000, plannedStop: 1800000, small: 240000, excluded: 3600000, unknown: 600000,
		total: 16848, good: 16006, reject: 842, outside: 12, ideal: 16848000, availability: 0.8, performance: 0.9, quality: 0.95, oee: 0.684, teep: 0.556, measured: ['availability', 'performance', 'quality'], partial: true };
	it( 'reads a bucket, a factor that is not measured as none', ()=>{
		const b = toOeeBucket( shift );
		expect( b.start ).toEqual( new Date( '2026-11-11T06:00:00Z' ) );
		expect( [b.shift, b.planned, b.total, b.oee, b.partial] ).toEqual( ['First', 23400000, 16848, 0.684, true] );
		const bare = toOeeBucket( {start: shift.start, end: shift.end, planned: 0, performance: null, quality: null, oee: null, measured: null} );
		expect( [bare.shift, bare.performance, bare.oee, bare.measured, bare.partial, bare.run] ).toEqual( [undefined, undefined, undefined, [], false, 0] );
	} );
	it( 'splits the planned time into run and the stops, each with its share, and lists what is beside it', ()=>{
		const split = oeeLossSplit( toOeeBucket( shift ) );
		expect( split.planned.map( r=>[r.id, r.ms, oeePercent( r.share )] ) ).toEqual( [['run', 18720000, '80.0%'], ['unplanned', 2880000, '12.3%'], ['plannedStop', 1800000, '7.7%']] );
		expect( split.planned.reduce( ( sum, r )=>sum+r.ms, 0 ) ).toBe( 23400000 );//the three are the planned time
		expect( split.beside.map( r=>[r.id, r.ms] ) ).toEqual( [['small', 240000], ['excluded', 3600000], ['unknown', 600000]] );
		expect( oeePercent( split.beside[0].share ) ).toBe( '1.3%' );//of run
		expect( oeeLossSplit( toOeeBucket( {...shift, planned: 0, run: 0} ) ).planned[0].share ).toBeUndefined();
	} );
	it( 'says a factor to a tenth of a percent, a length in two units and a count as it reads', ()=>{
		expect( [oeePercent( 0.684 ), oeePercent( 1.04 ), oeePercent( undefined ), oeePercent( 0 )] ).toEqual( ['68.4%', '104.0%', '–', '0.0%'] );
		expect( [0, 8000, 252000, 240000, 23400000, 183600000, 86400000].map( oeeDuration ) ).toEqual( ['0s', '8s', '4m 12s', '4m', '6h 30m', '2d 3h', '1d'] );
		expect( [oeeCount( 16848 ), oeeCount( 12.345 )] ).toEqual( [( 16848 ).toLocaleString(), '12.3'] );
	} );
	it( 'reads a stop with its bounds as text, and a page with its continuation', ()=>{
		const json = { id: 7, start: '2026-11-11T08:12:00.1234567Z', end: '2026-11-11T08:40:00.0000000Z', ms: 1679877, small: false, open: false, code: 7, uncoded: false, reason: {id: 3, name: 'Jam', category: 'Unplanned'},
			codings: [{id: 1, start: '2026-11-11T08:12:00.1234567Z', end: '2026-11-11T08:40:00.0000000Z', reason: {id: 3, name: 'Jam', category: 'Unplanned'}, comment: 'film', userName: 'op', time: '2026-11-11T09:00:00Z'}] };
		const s = toOeeStop( json );
		expect( [s.startText, s.endText] ).toEqual( [json.start, json.end] );//to the tick, for codeOee
		expect( s.start ).toEqual( new Date( '2026-11-11T08:12:00.123Z' ) );
		expect( s.reason ).toEqual( {id: 3, name: 'Jam', category: 'Unplanned'} );
		expect( s.codings[0] ).toMatchObject( {id: 1, comment: 'film', userName: 'op'} );
		const open = toOeeStop( {id: 8, start: json.start, end: null, ms: 5000, small: true, open: true, uncoded: false, reason: null, codings: null} );
		expect( [open.end, open.endText, open.open, open.reason, open.codings] ).toEqual( [undefined, undefined, true, undefined, []] );
		expect( toOeeStopsPage( {continuation: 'next', stops: [json]} ) ).toMatchObject( {continuation: 'next', stops: [{id: 7}]} );
		expect( toOeeStopsPage( undefined ) ).toEqual( {stops: [], continuation: undefined} );
	} );
	it( 'reads a loss row by its kind and a timeline, a list of one', ()=>{
		expect( toOeeLoss( {kind: 'small', reason: null, ms: 240000, stops: 2} ) ).toEqual( {kind: 'small', reason: undefined, ms: 240000, stops: 2} );
		expect( toOeeLoss( {kind: 'reason', reason: {id: 3, name: 'Jam', category: 'Unplanned'}, ms: 1, stops: 1} ).reason?.name ).toBe( 'Jam' );
		const t = toOeeTimeline( [{ planned: [{shift: 1, name: 'First', start: '2026-11-11T06:00:00Z', end: '2026-11-11T14:00:00Z', breaks: [{start: '2026-11-11T10:00:00Z', end: '2026-11-11T10:20:00Z'}]}],
			segments: [{start: '2026-11-11T06:00:00.0000000Z', end: '2026-11-11T07:00:00.0000000Z', state: 'running', stop: null, reason: null, small: false, uncoded: false, coding: null},
				{start: '2026-11-11T07:00:00.0000000Z', end: '2026-11-11T07:30:00.0000000Z', state: 'stopped', stop: 4, reason: {id: 2, name: 'Changeover', category: 'Planned'}, small: false, uncoded: false, coding: {id: 9, comment: 'size', userName: 'op'}}] }] );
		expect( t.planned[0] ).toMatchObject( {shift: 1, name: 'First'} );
		expect( t.planned[0].breaks[0].end ).toEqual( new Date( '2026-11-11T10:20:00Z' ) );
		expect( t.segments.map( s=>[s.state, s.stop, s.coding?.id] ) ).toEqual( [['running', undefined, undefined], ['stopped', 4, 9]] );
		expect( t.segments[1].startText ).toBe( '2026-11-11T07:00:00.0000000Z' );
		expect( toOeeTimeline( [] ) ).toEqual( {planned: [], segments: []} );
	} );

	//2026-11-11 is a Wednesday
	it( "gives the picker's ranges:  a day from midnight, a week from Monday, an open period to now", ()=>{
		const now = at( 9, 30 );
		expect( oeePeriodRange( 'today', now ) ).toEqual( {start: at( 0 ), end: now} );
		expect( oeePeriodRange( 'yesterday', now ) ).toEqual( {start: at( 0, 0, 10 ), end: at( 0 )} );
		expect( oeePeriodRange( 'week', now ) ).toEqual( {start: at( 0, 0, 9 ), end: now} );
		expect( oeePeriodRange( 'lastWeek', now ) ).toEqual( {start: at( 0, 0, 2 ), end: at( 0, 0, 9 )} );
		expect( oeePeriodRange( 'week', at( 9, 30, 15 ) )?.start ).toEqual( at( 0, 0, 9 ) );//a Sunday is its week's last day
		expect( oeePeriodRange( 'week', at( 0, 0, 9 ) )?.start ).toEqual( at( 0, 0, 9 ) );
	} );
	it( 'ends the running shift now and a finished one at its end, and has none without an occurrence', ()=>{
		const now = at( 9, 30 ), shift = {start: at( 6 ), end: at( 14 )};
		expect( oeePeriodRange( 'shift', now, {shift} ) ).toEqual( {start: at( 6 ), end: now} );
		expect( oeePeriodRange( 'shift', at( 15 ), {shift} ) ).toEqual( shift );
		expect( oeePeriodRange( 'shift', now ) ).toBeUndefined();
		expect( oeePeriodRange( 'shift', at( 5 ), {shift} ) ).toBeUndefined();//not begun
	} );
	it( 'takes a custom range with a blank To as now, and refuses one that is no range', ()=>{
		const now = at( 9, 30 );
		expect( oeePeriodRange( 'custom', now, {from: at( 6 )} ) ).toEqual( {start: at( 6 ), end: now} );
		expect( oeePeriodRange( 'custom', now, {from: at( 6 ), to: at( 8 )} ) ).toEqual( {start: at( 6 ), end: at( 8 )} );
		expect( oeePeriodRange( 'custom', now, {from: at( 6 ), to: at( 12 )} ) ).toEqual( {start: at( 6 ), end: now} );//nothing is read past now
		expect( oeePeriodRange( 'custom', now, {from: at( 8 ), to: at( 6 )} ) ).toBeUndefined();
		expect( oeePeriodRange( 'custom', now, {from: at( 10 )} ) ).toBeUndefined();
		expect( oeePeriodRange( 'custom', now ) ).toBeUndefined();
	} );
	it( 'narrows a refused range to its newer half, down to a minute', ()=>{
		expect( narrowOeeRange( {start: at( 0 ), end: at( 8 )} ) ).toEqual( {start: at( 4 ), end: at( 8 )} );
		expect( narrowOeeRange( {start: at( 8, 0 ), end: at( 8, 1 )} ) ).toBeUndefined();
		expect( isOeeRangeRefusal( '(400)2026-11-10 to 2026-11-11 is 12000 segments, more than readLimit, 10000:  narrow the range.' ) ).toBe( true );
		expect( isOeeRangeRefusal( '(403)Forbidden' ) ).toBe( false );
	} );

	const stop = { start: new Date( '2026-11-11T08:12:00.123Z' ), startText: '2026-11-11T08:12:00.1234567Z', end: new Date( '2026-11-11T08:40:00Z' ), endText: '2026-11-11T08:40:00.0000000Z' };
	it( "codes a stop once, over its bounds as the gateway gave them", ()=>{
		expect( oeeCodingCalls( stop, {reason: 3, comment: ' film '} ) ).toEqual( [{start: stop.startText, end: stop.endText, reason: 3, comment: 'film'}] );
		expect( oeeCodingCalls( stop, {reason: 3, comment: '  '} ) ).toEqual( [{start: stop.startText, end: stop.endText, reason: 3}] );
	} );
	it( 'codes a split stop twice, meeting at the split time', ()=>{
		const at = new Date( '2026-11-11T08:20:00Z' );
		expect( oeeCodingCalls( stop, {reason: 3}, {at, second: {reason: 4, comment: 'no film'}} ) ).toEqual( [
			{start: stop.startText, end: '2026-11-11T08:20:00.000Z', reason: 3},
			{start: '2026-11-11T08:20:00.000Z', end: stop.endText, reason: 4, comment: 'no film'}] );
	} );
	it( 'codes an open stop to now, and takes a split only inside the stop', ()=>{
		const now = new Date( '2026-11-11T09:00:00Z' ), open = {start: stop.start, startText: stop.startText};
		expect( oeeCodingCalls( open, {reason: 3}, undefined, now ) ).toEqual( [{start: stop.startText, end: '2026-11-11T09:00:00.000Z', reason: 3}] );
		expect( oeeSplitProblem( stop, undefined ) ).toBe( 'Enter the split time.' );
		expect( oeeSplitProblem( stop, stop.start ) ).toMatch( /inside the stop/ );
		expect( oeeSplitProblem( stop, stop.end ) ).toMatch( /inside the stop/ );
		expect( oeeSplitProblem( stop, new Date( '2026-11-11T08:20:00Z' ) ) ).toBeUndefined();
		expect( oeeSplitProblem( open, new Date( '2026-11-11T08:50:00Z' ), now ) ).toBeUndefined();
		expect( oeeSplitProblem( open, new Date( '2026-11-11T09:10:00Z' ), now ) ).toMatch( /inside the stop/ );
	} );
} );