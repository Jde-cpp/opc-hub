import { OeeService } from './oee-service';

//the service over a stub gateway:  what each call sends, and what it makes of the answer
const stub = ( answer:unknown )=>({ query: vi.fn( async ( ..._args:unknown[] )=><any>answer ), postQL: vi.fn( async ( ..._args:unknown[] )=><any>answer ) });
const range = { start: new Date( '2026-11-11T06:00:00Z' ), end: new Date( '2026-11-11T14:00:00Z' ) };
const span = { start: '2026-11-11T06:00:00.000Z', end: '2026-11-11T14:00:00.000Z' };

describe( 'OeeService', ()=>{
	const service = new OeeService();
	it( 'reads the figures as one bucket, or one per by', async ()=>{
		const g = stub( {oee: [{start: span.start, end: span.end, planned: 23400000, oee: 0.684, measured: ['availability'], partial: false}]} );
		const buckets = await service.figures( g, 4, range );
		expect( buckets ).toHaveLength( 1 );
		expect( [buckets[0].planned, buckets[0].oee] ).toEqual( [23400000, 0.684] );
		expect( g.query ).toHaveBeenLastCalledWith( OeeService.figuresQuery, {machine: 4, ...span} );
		expect( OeeService.figuresQuery ).toMatch( /^oee\( machine: \$machine, start: \$start, end: \$end \)\{ start end shift planned run .* outside ideal availability performance quality oee teep measured partial \}$/ );
		await service.figures( g, 4, range, 'day' );
		expect( g.query ).toHaveBeenLastCalledWith( OeeService.figuresByQuery, {machine: 4, ...span, by: 'day'} );
		expect( OeeService.figuresByQuery ).toContain( 'end: $end, by: $by )' );
		expect( await service.figures( stub( {} ), 4, range ) ).toEqual( [] );
	} );
	it( 'pages the stops, naming a continuation only where it has one', async ()=>{
		const g = stub( {oeeStops: {continuation: 'next', stops: [{id: 7, start: span.start, end: span.end, ms: 1, small: false, open: false, uncoded: true, codings: []}]}} );
		const page = await service.stops( g, 4, range, {uncoded: true, limit: 50} );
		expect( [page.continuation, page.stops[0].id] ).toEqual( ['next', 7] );
		expect( g.query ).toHaveBeenLastCalledWith( `oeeStops( machine: $machine, start: $start, end: $end, uncoded: $uncoded, limit: $limit ){ ${OeeService.stopsSelection} }`, {machine: 4, ...span, uncoded: true, limit: 50} );
		await service.stops( g, 4, range, {uncoded: false, limit: 50, continuation: 'next'} );
		expect( g.query ).toHaveBeenLastCalledWith( `oeeStops( machine: $machine, start: $start, end: $end, uncoded: $uncoded, limit: $limit, continuation: $continuation ){ ${OeeService.stopsSelection} }`, {machine: 4, ...span, uncoded: false, limit: 50, continuation: 'next'} );
	} );
	it( 'reads the losses and the timeline', async ()=>{
		const l = stub( {oeeLosses: [{kind: 'uncoded', reason: null, ms: 1200000, stops: 1}]} );
		expect( await service.losses( l, 4, range ) ).toEqual( [{kind: 'uncoded', reason: undefined, ms: 1200000, stops: 1}] );
		expect( l.query ).toHaveBeenCalledWith( 'oeeLosses( machine: $machine, start: $start, end: $end ){ kind reason{ id name category } ms stops }', {machine: 4, ...span} );
		const t = stub( {oeeTimeline: [{planned: [], segments: [{start: span.start, end: span.end, state: 'running', small: false, uncoded: false}]}]} );
		expect( ( await service.timeline( t, 4, range ) ).segments ).toHaveLength( 1 );
		expect( t.query ).toHaveBeenCalledWith( 'oeeTimeline( machine: $machine, start: $start, end: $end ){ planned{ shift name start end breaks } segments{ start end state stop reason{ id name category } small uncoded coding } }', {machine: 4, ...span} );
	} );
	it( 'finds the shift as the newest shift bucket of the day and a half before now', async ()=>{
		const g = stub( {oee: [{start: '2026-11-10T22:00:00Z', end: '2026-11-11T06:00:00Z', shift: 'Night'}, {start: '2026-11-11T06:00:00Z', end: '2026-11-11T09:30:00Z', shift: 'First'}]} );
		const now = new Date( '2026-11-11T09:30:00Z' );
		expect( await service.shift( g, 4, now ) ).toEqual( {start: new Date( '2026-11-11T06:00:00Z' ), end: now} );
		expect( g.query ).toHaveBeenCalledWith( 'oee( machine: $machine, start: $start, end: $end, by: $by ){ start end shift }', {machine: 4, start: '2026-11-09T21:30:00.000Z', end: '2026-11-11T09:30:00.000Z', by: 'shift'} );
		expect( await service.shift( stub( {oee: []} ), 4, now ) ).toBeUndefined();
	} );
	it( 'lists the live reasons by name', async ()=>{
		const g = stub( {oeeReasons: [{id: 3, name: 'Jam', category: 'Unplanned'}, {id: 2, name: 'Changeover', category: 'Planned'}]} );
		expect( ( await service.reasons( g ) ).map( r=>r.name ) ).toEqual( ['Changeover', 'Jam'] );
		expect( g.query ).toHaveBeenCalledWith( 'oeeReasons( deleted: null ){ id name category }' );
	} );
	it( 'codes a range by POST, a comment left out as null, and uncodes by id', async ()=>{
		const g = stub( {codeOee: {id: 9}} );
		await service.code( g, 4, {start: '2026-11-11T08:12:00.1234567Z', end: '2026-11-11T08:40:00.0000000Z', reason: 3} );
		expect( g.postQL ).toHaveBeenLastCalledWith( 'mutation codeOee( machine: $machine, start: $start, end: $end, reason: $reason, comment: $comment ){ id }', {machine: 4, start: '2026-11-11T08:12:00.1234567Z', end: '2026-11-11T08:40:00.0000000Z', reason: 3, comment: null} );
		await service.uncode( g, 9 );
		expect( g.postQL ).toHaveBeenLastCalledWith( 'mutation uncodeOee( id: $id ){ id }', {id: 9} );
	} );
} );