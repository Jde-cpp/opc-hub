import { TestBed } from '@angular/core/testing';
import { By } from '@angular/platform-browser';
import { stable } from 'jde-spa/testing';
import { Flow, FlowGraph, FlowRun, FlowVersion } from '../../../../model/flow';
import { normalizeFlowGraph } from '../../../../model/flow-nodes';
import { FlowTraceRecord } from '../../../../model/flow-trace';
import { FlowService, FlowTraceArgs } from '../../../../services/flow-service';
import { FlowCanvas } from '../canvas/flow-canvas';//here ahead of the tab's own import of it, which then waits for nothing
import { localInput } from '../flow-replay-dialog';
import { flowFiredFor, FlowHistory, historyInput, flowPlayPage, flowPlayTick, rangeProblem } from './flow-history';

const t = ( s:string )=>new Date( `2026-11-14T${s}Z` );
const node = ( id:string, type:string, more:object={} )=>({ id, type, name: '', x: 0, y: 0, props: {}, wires: <string[][]>[], ...more });
const graph = ():FlowGraph=>normalizeFlowGraph( {nodes: [node( 'in', 'inject', {wires: [['fn']]} ), node( 'fn', 'function', {name: 'Scale', x: 220, props: {code: 'return msg;'}, wires: [['out']]} ), node( 'out', 'debug', {x: 440} )]} );
const version = ( n:number, deployed:string, g:FlowGraph=graph() ):FlowVersion=>({ id: n, version: n, graph: g, identityId: 1, userName: 'ann', deployed: t( deployed ) });
const start = t( '08:00:00' ), end = t( '09:00:00' );//the range read
const row = new Flow( {id: 7, name: 'Pump', slug: 'pump', guid: 'g', enabled: true, version: 2} );
const record = ( time:string, node:string, msgId:string|undefined, more:Partial<FlowTraceRecord>={} ):FlowTraceRecord=>({ time: t( time ), msgId, runId: 0, node, kind: 'hop', ports: [0], cut: false, blocks: [], ...more });
const counts = ( time:string, node:string, received:number, sent:number[] )=>({ time: t( time ), node, counts: {received, sent, drops: 0, errors: 0} });
const run:FlowRun = { id: 5, version: 2, mode: 'dry', userName: 'ann', status: 'completed', start: t( '06:00:00' ), finish: t( '06:30:00' ) };

const create = async ()=>{
	const records = [record( '08:01:01', 'in', '1', {kind: 'birth', json: {payload: 1}} ), record( '08:01:01.010', 'fn', '1', {json: {payload: 2}} ), record( '08:01:01.020', 'out', '1', {ports: []} ), record( '08:01:05', 'in', '2', {kind: 'birth'} )];
	const flows = {
		runs: vi.fn( async ( ..._args:unknown[] )=>[run] ),
		counts: vi.fn( async ( ..._args:unknown[] )=>({ counts: [counts( '08:00:00', 'in', 0, [2] ), counts( '08:00:00', 'fn', 2, [2] ), counts( '08:01:00', 'fn', 1, [1] )] }) ),
		trace: vi.fn( async ( _g:unknown, args:FlowTraceArgs )=>({ records: records.filter( r=>(!args.node || r.node==args.node) && r.time>=args.start && r.time<=args.end ), continuation: args.continuation ? undefined : args.limit==200 ? 'more' : undefined }) ),
		message: vi.fn( async ( ..._args:unknown[] )=>({ records: records.slice( 0, 3 ), more: false }) )
	};
	TestBed.configureTestingModule( {providers: [{ provide: FlowService, useValue: flows }]} );
	const gateway = { query: vi.fn() };
	const fixture = TestBed.createComponent( FlowHistory ), tab = fixture.componentInstance;
	fixture.componentRef.setInput( 'gateway', gateway );
	fixture.componentRef.setInput( 'flow', row );
	fixture.componentRef.setInput( 'versions', [version( 2, '08:00:30' ), version( 1, '07:00:00', {nodes: [node( 'in', 'inject', {wires: [[]]} )]} )] );
	tab.from.set( localInput( start ) );//typed as the input holds it, local:  what the tab's first read, on its flow, takes
	tab.to.set( localInput( end ) );
	for( let i=0; i<100 && !tab.ready(); ++i )
		await stable( fixture, 1 );
	expect( fixture.debugElement.query( By.directive( FlowCanvas ) ) ).not.toBeNull();
	await stable( fixture );
	const element = <HTMLElement>fixture.nativeElement;
	const text = ( selector:string )=>element.querySelector( selector )?.textContent?.replace( /\s+/g, ' ' ).trim();
	return { fixture, tab, element, flows, gateway, text, records };
};

describe( 'FlowHistory', ()=>{
	beforeEach( ()=>vi.stubGlobal( 'ResizeObserver', class{ observe(){} unobserve(){} disconnect(){} } ) );
	afterEach( ()=>vi.unstubAllGlobals() );

	it( 'refuses a range that is not one', ()=>{
		expect( rangeProblem( '', '2026-11-14T09:00' ) ).toBe( 'From is a time.' );
		expect( rangeProblem( '2026-11-14T09:00', 'x' ) ).toBe( 'To is a time.' );
		expect( rangeProblem( '2026-11-14T09:00', '2026-11-14T08:00' ) ).toBe( 'From is before To.' );
		expect( rangeProblem( '2026-11-14T08:00', '2026-11-14T09:00' ) ).toBeUndefined();
	} );
	//the range read:  the counts over it by the minute onto the wires, the list's first page, and the scrubber at the end
	it( 'reads the counts onto the wires and lists the records', async ()=>{
		const { tab, fixture, element, flows, gateway, text } = await create();
		expect( flows.counts ).toHaveBeenCalledWith( gateway, {flow: 7, run: undefined, start, end, by: 'PT1M', continuation: undefined} );
		expect( flows.trace ).toHaveBeenCalledWith( gateway, {flow: 7, run: undefined, start, end, node: undefined, kind: undefined, msg: undefined, limit: 200, continuation: undefined} );
		expect( tab.at() ).toBe( end.getTime() );
		expect( tab.version()?.version ).toBe( 2 );
		expect( tab.graphCounts()?.wires.get( 'fn:0>out' ) ).toBe( 3 );
		expect( text( 'f-connection#out\\:0\\:fn\\>in\\:out .wire-label' ) ).toBe( '3' );
		expect( text( '[data-f-node-id="fn"] .badge' ) ).toBeUndefined();//no status, no drops, no errors:  no badge
		expect( text( '.figures' ) ).toBe( '3 received · 0 dropped · 0 errors' );
		expect( [...element.querySelectorAll( '.list .row .node' )].map( e=>e.textContent ) ).toEqual( ['inject', 'Scale', 'debug', 'inject'] );
		expect( element.querySelectorAll( '.scrubber .mark' ).length ).toBe( 1 );//version 2's deploy within the range
		//Load more:  the next page by the continuation, appended
		(<HTMLButtonElement>element.querySelector( '.more button' )).click();
		await stable( fixture );
		expect( flows.trace.mock.lastCall![1] ).toMatchObject( {continuation: 'more'} );
		expect( tab.records().length ).toBe( 8 );
		expect( tab.continuation() ).toBeUndefined();
		//the list narrowed by node, the counts kept
		tab.filterNode.set( 'fn' );
		await tab.filter();
		expect( tab.records().map( r=>r.node ) ).toEqual( ['fn'] );
		expect( flows.counts ).toHaveBeenCalledTimes( 1 );
	} );
	//the scrubber's time picks the version drawn:  before version 2's deploy, version 1's one node
	it( 'draws the version in force at the scrubber', async ()=>{
		const { tab, fixture, element } = await create();
		expect( element.querySelectorAll( 'flow-node-view' ).length ).toBe( 3 );
		tab.seek( t( '08:00:10' ).getTime() );
		await stable( fixture );
		expect( tab.version()?.version ).toBe( 1 );
		expect( element.querySelectorAll( 'flow-node-view' ).length ).toBe( 1 );
	} );
	//a record selected:  its lineage read, its path lit on the canvas and its own records listed with their json
	it( "lights a selected message's path", async ()=>{
		const { tab, fixture, element, flows, gateway, text } = await create();
		(<HTMLButtonElement>element.querySelectorAll( '.list .row' )[1]).click();
		await stable( fixture );
		expect( flows.message ).toHaveBeenCalledWith( gateway, {flow: 7, id: '1', run: undefined, start, end} );
		expect( tab.at() ).toBe( t( '08:01:01.010' ).getTime() );
		expect( tab.path()?.own ).toEqual( {nodes: ['in', 'fn', 'out'], wires: ['in:0>fn', 'fn:0>out']} );
		expect( [...element.querySelectorAll( 'flow-node-view.own' )].map( e=>e.getAttribute( 'data-f-node-id' ) ) ).toEqual( ['in', 'fn', 'out'] );
		expect( element.querySelectorAll( 'f-connection.own' ).length ).toBe( 2 );
		expect( text( '.lineage h3' ) ).toBe( 'Message 1' );
		expect( [...element.querySelectorAll( '.lineage .steps li' )].map( li=>li.querySelector( '.node' )!.textContent ) ).toEqual( ['inject', 'Scale', 'debug'] );
		expect( element.querySelectorAll( '.lineage .steps .json' ).length ).toBe( 2 );//where a node changed it
		expect( element.querySelector( '.list li.selected .node' )!.textContent ).toBe( 'Scale' );
		(<HTMLButtonElement>element.querySelectorAll( '.list .row' )[1]).click();//again:  unselected
		await stable( fixture );
		expect( [tab.selected(), element.querySelectorAll( 'flow-node-view.own' ).length] ).toEqual( [undefined, 0] );
	} );
	//Play on fake timers:  the records read ahead, stepped at the speed, each lighting its node and wire for a moment, the
	//scrubber following, and the player stopping at the range's end
	it( 'plays the range through the canvas at speed', async ()=>{
		const { tab, fixture, element, flows } = await create();
		vi.useFakeTimers();
		try{
			vi.setSystemTime( 10_000 );
			tab.setSpeed( 100 );
			tab.play();
			expect( [tab.playing(), tab.at()] ).toEqual( [true, start.getTime()] );
			await vi.advanceTimersByTimeAsync( 0 );//the first page lands
			expect( flows.trace.mock.lastCall![1] ).toMatchObject( {limit: 1000, continuation: undefined} );
			await vi.advanceTimersByTimeAsync( 6*flowPlayTick );//600 ms at 100x:  a minute in, nothing yet
			expect( [tab.at(), tab.fired().nodes.size, tab.fired().wires.size] ).toEqual( [start.getTime()+60_000, 0, 0] );
			await vi.advanceTimersByTimeAsync( flowPlayTick );//ten seconds more:  past the first message's three records and the second's birth
			fixture.detectChanges();//stable() waits on a timer of its own, which the fake clock holds
			expect( tab.at() ).toBe( start.getTime()+70_000 );
			expect( [...tab.fired().nodes.keys(), ...tab.fired().wires.keys()].sort() ).toEqual( ['fn', 'fn:0>out', 'in', 'in:0>fn', 'out'] );
			expect( element.querySelectorAll( 'flow-node-view.fired' ).length ).toBe( 3 );
			expect( element.querySelectorAll( 'f-connection.fired' ).length ).toBe( 2 );
			await vi.advanceTimersByTimeAsync( 1000 );
			fixture.detectChanges();
			expect( [tab.fired().nodes.size, tab.fired().wires.size] ).toEqual( [0, 0] );//dark again
			await vi.advanceTimersByTimeAsync( 36_000 );//an hour at 100x
			expect( [tab.playing(), tab.at()] ).toEqual( [false, end.getTime()] );
			tab.play();//from the start again
			expect( tab.at() ).toBe( start.getTime() );
			tab.pause();
			expect( tab.playing() ).toBe( false );
			//a node's id may hold a wire key's `>`:  its mark is a node's
			tab.fired.set( {nodes: new Map( [['a>b', 1]] ), wires: new Map( [['fn:0>out', 1]] )} );
			expect( [tab.decor().nodes.get( 'a>b' )?.lit, tab.decor().wires.get( 'fn:0>out' )?.lit, tab.decor().wires.has( 'a>b' )] ).toEqual( ['fired', 'fired', false] );
		}
		finally{
			vi.useRealTimers();
		}
	} );
	//Play from a time picked:  the player reads from there, so no record before it fires
	it( 'plays from the scrubber', async ()=>{
		const { tab, flows } = await create();
		vi.useFakeTimers();
		try{
			vi.setSystemTime( 10_000 );
			tab.seek( t( '08:01:03' ).getTime() );//between the first message's records and the second's birth
			tab.setSpeed( 10 );
			tab.play();
			await vi.advanceTimersByTimeAsync( 0 );
			expect( flows.trace.mock.lastCall![1] ).toMatchObject( {limit: flowPlayPage, start: t( '08:01:03' ), continuation: undefined} );
			await vi.advanceTimersByTimeAsync( 3*flowPlayTick );//three seconds at 10x:  past the birth at 08:01:05
			expect( [...tab.fired().nodes.keys()] ).toEqual( ['in'] );
		}
		finally{
			vi.useRealTimers();
		}
	} );
	//a play let go while its page is out:  what it reads lands on it alone, so the new play reads its page once and plays on
	it( "keeps a superseded play's read off the new one", async ()=>{
		const { tab, flows } = await create();
		vi.useFakeTimers();
		try{
			const pages:{ resolve:( v:unknown )=>void; reject:( e:unknown )=>void }[] = [];
			const list = flows.trace.getMockImplementation()!;
			flows.trace.mockImplementation( ( g, args )=>args.limit==flowPlayPage ? new Promise<any>( ( resolve, reject )=>pages.push( {resolve, reject} ) ) : list( g, args ) );
			tab.play();
			await tab.read();
			tab.play();
			expect( pages.length ).toBe( 2 );
			pages[0].resolve( {records: [], continuation: 'next'} );
			await vi.advanceTimersByTimeAsync( 2*flowPlayTick );
			expect( pages.length ).toBe( 2 );//the second play's page still out:  none asked over it
			await tab.read();
			tab.play();
			pages[1].reject( new Error( 'The gateway is gone.' ) );
			await vi.advanceTimersByTimeAsync( flowPlayTick );
			expect( [tab.playing(), tab.playError()] ).toEqual( [true, undefined] );
			tab.pause();
		}
		finally{
			vi.useRealTimers();
		}
	} );
	//paused where the player waits for its next page, at the record it last fired:  Play goes on from there, firing it once
	it( 'fires a record once over a pause at its time', async ()=>{
		const { tab, flows, records } = await create();
		vi.useFakeTimers();
		try{
			vi.setSystemTime( 10_000 );
			const list = flows.trace.getMockImplementation()!;
			flows.trace.mockImplementation( ( g, args )=>args.limit!=flowPlayPage ? list( g, args ) : args.continuation ? new Promise<any>( ()=>{} ) : Promise.resolve( {records: records.slice( 0, 3 ), continuation: 'next'} ) );
			tab.setSpeed( 100 );
			tab.play();
			await vi.advanceTimersByTimeAsync( 10*flowPlayTick );
			expect( [tab.at(), [...tab.fired().nodes.keys()]] ).toEqual( [t( '08:01:01.020' ).getTime(), ['in', 'fn', 'out']] );
			tab.pause();
			await vi.advanceTimersByTimeAsync( 1000 );
			tab.play();
			await vi.advanceTimersByTimeAsync( flowPlayTick );
			expect( tab.fired().nodes.size ).toBe( 0 );//the marks dark, and none lit again
			tab.pause();
		}
		finally{
			vi.useRealTimers();
		}
	} );
	//Play to the range's end:  the last records' marks fade as the others do
	it( 'lets the last marks go dark once Play has ended', async ()=>{
		const { tab } = await create();
		tab.to.set( historyInput( t( '08:01:06' ) ) );
		await tab.read();
		vi.useFakeTimers();
		try{
			vi.setSystemTime( 10_000 );
			tab.setSpeed( 100 );
			tab.play();
			await vi.advanceTimersByTimeAsync( 7*flowPlayTick );//the range's 66 seconds at 100x, its records fired in the last step
			expect( [tab.playing(), tab.fired().nodes.size] ).toEqual( [false, 3] );
			await vi.advanceTimersByTimeAsync( flowFiredFor+flowPlayTick );
			expect( [tab.fired().nodes.size, tab.fired().wires.size] ).toEqual( [0, 0] );
		}
		finally{
			vi.useRealTimers();
		}
	} );
	//Hour, Day and Week to now, to the second
	it( 'reads to the second', async ()=>{
		const { tab, flows } = await create();
		vi.useFakeTimers();
		try{
			vi.setSystemTime( t( '10:00:50' ) );
			tab.preset( 3_600_000 );
			expect( [tab.from(), tab.to()].map( s=>new Date( s ).getTime() ) ).toEqual( [t( '09:00:50' ).getTime(), t( '10:00:50' ).getTime()] );
			expect( flows.counts.mock.lastCall![1] ).toMatchObject( {start: t( '09:00:50' ), end: t( '10:00:50' )} );
		}
		finally{
			vi.useRealTimers();
		}
	} );
	//a filter changed while the counts are out:  the list read anew, and the counts kept once they land
	it( 'keeps the counts over a filter changed while they are out', async ()=>{
		const { tab, flows } = await create();
		let answer!:( v:unknown )=>void;
		flows.counts.mockImplementationOnce( ()=>new Promise<any>( r=>answer = r ) );
		const reading = tab.read();
		tab.filterNode.set( 'fn' );
		await tab.filter();
		answer( {counts: [counts( '08:00:00', 'fn', 2, [2] )]} );
		await reading;
		expect( [tab.counts().length, tab.graphCounts()?.wires.get( 'fn:0>out' ), tab.records().map( r=>r.node )] ).toEqual( [1, 2, ['fn']] );
	} );
	//a run opened from Versions:  its range, to the second past its finish, its trace, and the version it ran, whatever was
	//deployed over its range
	it( "opens a run's trace over its range", async ()=>{
		const { tab, fixture, flows, element, text } = await create();
		fixture.componentRef.setInput( 'request', {run, n: 1} );
		await stable( fixture );
		expect( tab.run() ).toBe( run );
		expect( flows.counts.mock.lastCall![1] ).toMatchObject( {run: 5, start: run.start, end: new Date( run.finish!.getTime()+1000 )} );
		expect( flows.trace.mock.lastCall![1] ).toMatchObject( {run: 5} );
		expect( [tab.version()?.version, element.querySelectorAll( 'flow-node-view' ).length, element.querySelectorAll( '.scrubber .mark' ).length] ).toEqual( [2, 3, 0] );
		//a run started since the tab read the runs:  among the select's, and the one it shows
		const later:FlowRun = { ...run, id: 6, status: undefined };
		fixture.componentRef.setInput( 'request', {run: later, n: 2} );
		await stable( fixture );
		expect( tab.runs().map( r=>r.id ) ).toEqual( [6, 5] );
		expect( text( '.run .mat-mdc-select-value' ) ).toBe( 'Replay 6, version 2, under way' );
		tab.pickRun( undefined );
		await stable( fixture );
		expect( [tab.run(), flows.counts.mock.lastCall![1]] ).toMatchObject( [undefined, {run: undefined}] );
	} );
	//a range that is no range:  nothing of the last trace's kept under another's arguments, and nothing read
	it( 'keeps nothing of the last trace over a range that is no range', async ()=>{
		const { tab, fixture, flows, element } = await create();
		expect( [tab.records().length, tab.continuation()] ).toEqual( [4, 'more'] );
		const reads = flows.trace.mock.calls.length;
		tab.from.set( '' );
		tab.openRun( {...run, start: undefined, finish: undefined, status: undefined} );//a run under way, with no range of its own
		await stable( fixture );
		expect( [tab.range(), tab.records(), tab.continuation(), tab.counts(), flows.trace.mock.calls.length] ).toEqual( [undefined, [], undefined, [], reads] );
		expect( element.querySelector( '.more' ) ).toBeNull();
		expect( element.querySelector( '.problem' )!.textContent ).toBe( 'From is a time.' );
	} );
} );