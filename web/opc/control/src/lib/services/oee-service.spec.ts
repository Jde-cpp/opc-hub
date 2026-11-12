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
	it( "reads the gateway's machines by name, with its live lines and every membership", async ()=>{
		const members = [{id: 1, oeeLineId: 1, oeeMachineId: 5, memberLineId: null, sort: 0}, {id: 2, oeeLineId: 3, oeeMachineId: null, memberLineId: 1, sort: 1}];
		const g = { query: vi.fn( async ( ql:string )=><any>( ql==OeeService.machinesQuery ? {oeeMachines: [{id: 5, name: 'Pump 2', slug: 'pump2'}, {id: 4, name: 'Pump 1', slug: 'pump1'}]}
			: ql==OeeService.linesQuery ? {oeeLines: [{id: 1, name: 'Filling', slug: 'filling', level: 'Line'}]} : {oeeLineMembers: members} ) ) };
		expect( await service.plant( g ) ).toEqual( {
			machines: [{id: 4, name: 'Pump 1', slug: 'pump1'}, {id: 5, name: 'Pump 2', slug: 'pump2'}], lines: [{id: 1, name: 'Filling', slug: 'filling', level: 'Line'}],
			members: [{id: 1, lineId: 1, machineId: 5, memberLineId: undefined, sort: 0}, {id: 2, lineId: 3, machineId: undefined, memberLineId: 1, sort: 1}]//line 3 deleted, its members not yet purged
		} );
		expect( g.query.mock.calls.map( ([ql])=>ql ).sort() ).toEqual( ['oeeLineMembers{ id oeeLineId oeeMachineId memberLineId sort }', 'oeeLines( deleted: null ){ id name slug level }', 'oeeMachines( deleted: null ){ id name slug }'] );
		const warn = vi.spyOn( console, 'warn' ).mockImplementation( ()=>{} );
		const refused = { query: vi.fn( async ( ql:string )=>{ if( ql==OeeService.lineMembersQuery ) throw new Error( '(403)No Read on oeeLineMembers.' ); return <any>{oeeMachines: [{id: 4, name: 'Pump 1', slug: 'pump1'}]}; } ) };
		expect( await service.plant( refused ) ).toEqual( {machines: [{id: 4, name: 'Pump 1', slug: 'pump1'}], lines: [], members: []} );//the members withheld:  every machine is in no line
		expect( warn ).toHaveBeenCalledTimes( 1 );
		warn.mockRestore();
		await expect( service.plant( refused, true ) ).rejects.toThrow( 'No Read on oeeLineMembers.' );//a page that edits them says so
	} );
	it( "reads a line in a machine's place:  its figures with its members', its stops naming their machines, a timeline per machine", async ()=>{
		const member = { start: span.start, end: span.end, planned: 100, oee: 0.5, measured: [], partial: false };
		const f = stub( {oee: [{...member, planned: 200, machines: 2, coverage: {performance: 1, quality: 0}, members: [{...member, machine: {id: 4, name: 'Pump 1', slug: 'pump1'}}, {...member, machines: 1, line: {id: 3, name: 'Cell A', slug: 'cell-a', level: 'Cell'}}]}]} );
		const [bucket] = await service.figures( f, {line: 2}, range, undefined, true );
		expect( [bucket.machines, bucket.coverage, bucket.members!.map( m=>[m.machine, m.line, m.planned] )] ).toEqual( [2, {performance: 1, quality: 0}, [[{id: 4, name: 'Pump 1', slug: 'pump1'}, undefined, 100], [undefined, {id: 3, name: 'Cell A', slug: 'cell-a', level: 'Cell'}, 100]]] );
		expect( f.query ).toHaveBeenLastCalledWith( OeeService.figuresQL( 'line', false, true ), {line: 2, ...span, members: true} );
		expect( OeeService.figuresQL( 'line', false, true ) ).toMatch( /^oee\( line: \$line, start: \$start, end: \$end, members: \$members \)\{ start .* partial machines coverage\{ performance quality \} members\{ start .* partial machines machine\{ id name slug \} line\{ id name slug level \} \} \}$/ );
		const s = stub( {oeeStops: {stops: [{id: 7, start: span.start, end: span.end, ms: 1, small: false, open: false, uncoded: true, codings: [], machine: {id: 5, name: 'Pump 2'}}]}} );
		expect( ( await service.stops( s, {line: 2}, range, {uncoded: false, limit: 50} ) ).stops[0].machine ).toEqual( {id: 5, name: 'Pump 2'} );
		expect( s.query.mock.calls[0] ).toEqual( [`oeeStops( line: $line, start: $start, end: $end, uncoded: $uncoded, limit: $limit ){ ${OeeService.lineStopsSelection} }`, {line: 2, ...span, uncoded: false, limit: 50}] );
		expect( OeeService.lineStopsSelection ).toMatch( / time \} machine\{ id name \} \}$/ );
		const l = stub( {oeeLosses: []} );
		await service.losses( l, {line: 2}, range );
		expect( l.query ).toHaveBeenCalledWith( 'oeeLosses( line: $line, start: $start, end: $end ){ kind reason{ id name category } ms stops }', {line: 2, ...span} );
		const t = stub( {oeeTimeline: [{machine: {id: 4, name: 'Pump 1'}, planned: [], segments: []}, {machine: {id: 5, name: 'Pump 2'}, planned: [], segments: [{start: span.start, end: span.end, state: 'running', small: false, uncoded: false}]}]} );
		expect( ( await service.timelines( t, 2, range ) ).map( x=>[x.machine.name, x.segments.length] ) ).toEqual( [['Pump 1', 0], ['Pump 2', 1]] );
		expect( t.query ).toHaveBeenCalledWith( OeeService.lineTimelineQuery, {line: 2, ...span} );
		expect( OeeService.lineTimelineQuery ).toContain( 'oeeTimeline( line: $line, start: $start, end: $end ){ machine{ id name } planned{' );
		const sh = stub( {oee: []} );
		await service.shift( sh, {line: 2}, new Date( '2026-11-11T09:30:00Z' ) );
		expect( sh.query.mock.calls[0][0] ).toBe( 'oee( line: $line, start: $start, end: $end, by: $by ){ start end shift }' );
	} );
	it( 'reads the losses and the timeline', async ()=>{
		const l = stub( {oeeLosses: [{kind: 'uncoded', reason: null, ms: 1200000, stops: 1}]} );
		expect( await service.losses( l, 4, range ) ).toEqual( [{kind: 'uncoded', reason: undefined, ms: 1200000, stops: 1}] );
		expect( l.query ).toHaveBeenCalledWith( 'oeeLosses( machine: $machine, start: $start, end: $end ){ kind reason{ id name category } ms stops }', {machine: 4, ...span} );
		const t = stub( {oeeTimeline: [{planned: [], segments: [{start: span.start, end: span.end, state: 'running', small: false, uncoded: false}]}]} );
		expect( ( await service.timeline( t, 4, range ) ).segments ).toHaveLength( 1 );
		expect( t.query ).toHaveBeenCalledWith( 'oeeTimeline( machine: $machine, start: $start, end: $end ){ planned{ shift name start end breaks } segments{ start end state stop reason{ id name category } small uncoded coding } }', {machine: 4, ...span} );
	} );
	it( 'finds the shift as the newest shift bucket begun in the day and a half before now, whole', async ()=>{
		const g = stub( {oee: [{start: '2026-11-10T22:00:00Z', end: '2026-11-11T06:00:00Z', shift: 'Night'}, {start: '2026-11-11T06:00:00Z', end: '2026-11-11T14:00:00Z', shift: 'First'}, {start: '2026-11-11T14:00:00Z', end: '2026-11-11T22:00:00Z', shift: 'Second'}]} );
		const now = new Date( '2026-11-11T09:30:00Z' );
		expect( await service.shift( g, 4, now ) ).toEqual( {start: new Date( '2026-11-11T06:00:00Z' ), end: new Date( '2026-11-11T14:00:00Z' ), name: 'First'} );//the read runs past now, so its end is the occurrence's
		expect( g.query ).toHaveBeenCalledWith( 'oee( machine: $machine, start: $start, end: $end, by: $by ){ start end shift }', {machine: 4, start: '2026-11-09T21:30:00.000Z', end: '2026-11-12T09:30:00.000Z', by: 'shift'} );
		expect( await service.shift( stub( {oee: []} ), 4, now ) ).toBeUndefined();
	} );
	it( 'reads a stop by its id at a time within it', async ()=>{
		const g = stub( {oeeStops: {stops: [{id: 6, start: span.start, end: span.start, ms: 0, small: true, open: false, uncoded: true, codings: []}, {id: 7, start: span.start, open: true, uncoded: true, codings: []}]}} );
		const at = new Date( '2026-11-11T06:00:00Z' );
		expect( ( await service.stop( g, 4, 7, at ) )?.open ).toBe( true );
		expect( g.query.mock.calls[0][1] ).toEqual( {machine: 4, start: '2026-11-11T06:00:00.000Z', end: '2026-11-11T06:00:00.001Z', uncoded: false, limit: 2} );
		expect( await service.stop( g, 4, 8, at ) ).toBeUndefined();//made again by the engine
	} );
	it( 'lists the live reasons by name', async ()=>{
		const g = stub( {oeeReasons: [{id: 3, name: 'Jam', category: 'Unplanned'}, {id: 2, name: 'Changeover', category: 'Planned'}]} );
		expect( ( await service.reasons( g ) ).map( r=>r.name ) ).toEqual( ['Changeover', 'Jam'] );
		expect( g.query ).toHaveBeenCalledWith( 'oeeReasons( deleted: null ){ id name category }' );
	} );
	it( 'codes a range by POST, a comment left out as null, answering the coding written, and uncodes by id', async ()=>{
		const g = stub( {codeOee: {id: 9, start: '2026-11-11T08:12:00.1234567Z', end: '2026-11-11T08:40:00.0000000Z', reason: {id: 3, name: 'Jam', category: 'Unplanned'}, comment: null, userName: 'op'}} );
		const written = await service.code( g, 4, {start: '2026-11-11T08:12:00.1234567Z', end: '2026-11-11T08:40:00.0000000Z', reason: 3} );
		expect( written ).toMatchObject( {id: 9, reason: {name: 'Jam'}, userName: 'op'} );
		expect( g.postQL ).toHaveBeenLastCalledWith( 'mutation codeOee( machine: $machine, start: $start, end: $end, reason: $reason, comment: $comment ){ id start end reason{ id name category } comment userName time }', {machine: 4, start: '2026-11-11T08:12:00.1234567Z', end: '2026-11-11T08:40:00.0000000Z', reason: 3, comment: null} );
		expect( await service.code( stub( {} ), 4, {start: 'a', end: 'b', reason: 3} ) ).toBeUndefined();
		await service.uncode( g, 9 );
		expect( g.postQL ).toHaveBeenLastCalledWith( 'mutation uncodeOee( id: $id ){ id }', {id: 9} );
	} );
} );