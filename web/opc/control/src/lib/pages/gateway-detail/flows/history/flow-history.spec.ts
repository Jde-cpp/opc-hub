import { TestBed } from '@angular/core/testing';
import { By } from '@angular/platform-browser';
import { stable } from 'jde-spa/testing';
import { Flow, FlowGraph, FlowRun, FlowVersion } from '../../../../model/flow';
import { normalizeFlowGraph } from '../../../../model/flow-nodes';
import { FlowTraceRecord } from '../../../../model/flow-trace';
import { FlowService, FlowTraceArgs } from '../../../../services/flow-service';
import { FlowCanvas } from '../canvas/flow-canvas';//here ahead of the tab's own import of it, which then waits for nothing
import { localInput } from '../flow-replay-dialog';
import { FlowHistory, flowPlayTick, rangeProblem } from './flow-history';

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
		trace: vi.fn( async ( _g:unknown, args:FlowTraceArgs )=>({ records: args.node ? records.filter( r=>r.node==args.node ) : records, continuation: args.continuation ? undefined : args.limit==200 ? 'more' : undefined }) ),
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
	return { fixture, tab, element, flows, gateway, text };
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
			expect( [tab.at(), tab.fired().size] ).toEqual( [start.getTime()+60_000, 0] );
			await vi.advanceTimersByTimeAsync( flowPlayTick );//ten seconds more:  past the first message's three records and the second's birth
			fixture.detectChanges();//stable() waits on a timer of its own, which the fake clock holds
			expect( tab.at() ).toBe( start.getTime()+70_000 );
			expect( [...tab.fired().keys()].sort() ).toEqual( ['fn', 'fn:0>out', 'in', 'in:0>fn', 'out'] );
			expect( element.querySelectorAll( 'flow-node-view.fired' ).length ).toBe( 3 );
			expect( element.querySelectorAll( 'f-connection.fired' ).length ).toBe( 2 );
			await vi.advanceTimersByTimeAsync( 1000 );
			fixture.detectChanges();
			expect( tab.fired().size ).toBe( 0 );//dark again
			await vi.advanceTimersByTimeAsync( 36_000 );//an hour at 100x
			expect( [tab.playing(), tab.at()] ).toEqual( [false, end.getTime()] );
			tab.play();//from the start again
			expect( tab.at() ).toBe( start.getTime() );
			tab.pause();
			expect( tab.playing() ).toBe( false );
		}
		finally{
			vi.useRealTimers();
		}
	} );
	//a run opened from Versions:  its range, to the minute past its finish, and its trace
	it( "opens a run's trace over its range", async ()=>{
		const { tab, fixture, flows } = await create();
		fixture.componentRef.setInput( 'request', {run, n: 1} );
		await stable( fixture );
		expect( tab.run() ).toBe( run );
		expect( flows.counts.mock.lastCall![1] ).toMatchObject( {run: 5, start: run.start, end: new Date( run.finish!.getTime()+60_000 )} );
		expect( flows.trace.mock.lastCall![1] ).toMatchObject( {run: 5} );
		tab.pickRun( undefined );
		await stable( fixture );
		expect( [tab.run(), flows.counts.mock.lastCall![1]] ).toMatchObject( [undefined, {run: undefined}] );
	} );
} );