import { TestBed } from '@angular/core/testing';
import { By } from '@angular/platform-browser';
import { stable } from 'jde-spa/testing';
import { Feed } from 'jde-opc/testing';
import { Flow, FlowGraph, FlowVersion } from '../../../../model/flow';
import { FlowLiveEvent } from '../../../../model/flow-live';
import { normalizeFlowGraph } from '../../../../model/flow-nodes';
import { FlowCanvas } from '../canvas/flow-canvas';//here ahead of the tab's own import of it, which then waits for nothing
import { FlowActivity } from './flow-activity';

const node = ( id:string, type:string, more:object={} )=>({ id, type, name: '', x: 0, y: 0, props: {}, wires: <string[][]>[], ...more });
const graph = ():FlowGraph=>normalizeFlowGraph( {nodes: [node( 'in', 'inject', {wires: [['fn']]} ), node( 'fn', 'function', {name: 'Scale', x: 220, props: {code: 'return msg;'}, wires: [['out']]} ), node( 'out', 'debug', {x: 440} ), node( 'lo', 'link out', {y: 100, props: {name: 'k'}} ), node( 'li', 'link in', {x: 220, y: 100, props: {name: 'k'}} )]} );
const version = ( n:number, g:FlowGraph=graph() ):FlowVersion=>({ id: n, version: n, graph: g, identityId: 1, userName: 'ann' });
const row = new Flow( {id: 7, name: 'Pump', slug: 'pump', guid: 'g', enabled: true, version: 2} );
const counts = ( received:number, sent:number[], drops=0, errors=0 )=>({ received, sent, drops, errors });
const ack = ( v=2 ):FlowLiveEvent=>({ kind: 'ack', version: v, nodes: [{node: 'in', counts: counts( 0, [3] )}, {node: 'fn', status: {fill: 'green', shape: 'dot', text: 'ok'}, counts: counts( 3, [3] )}, {node: 'out', counts: counts( 3, [] )}] });
const push = ( more:object={} ):FlowLiveEvent=>({ kind: 'activity', activity: { nodes: [], wires: [], hops: [], hopsDropped: 0, ...more } });
const hop = ( msgId:string, node:string, kind:'hop'|'birth'|'error'='hop', more:object={} )=>({ time: new Date( 1000 ), msgId, runId: 0, node, kind, ports: [0], cut: false, blocks: [], ...more });

const create = async ( {versions=[version( 2 ), version( 1 )], active=true}:{versions?:FlowVersion[]; active?:boolean}={} )=>{
	const feed = new Feed<FlowLiveEvent, {flow:number; hops:boolean}>();
	const gateway = { flowSubscribe: vi.fn( ( flow:number, hops=false )=>feed.open( {flow, hops} ) ) };
	const fixture = TestBed.createComponent( FlowActivity ), tab = fixture.componentInstance;
	fixture.componentRef.setInput( 'gateway', gateway );
	fixture.componentRef.setInput( 'flow', row );
	fixture.componentRef.setInput( 'versions', versions );
	fixture.componentRef.setInput( 'active', active );
	const refreshes:number[] = [];
	tab.refresh.subscribe( ()=>refreshes.push( 1 ) );
	for( let i=0; i<100 && !tab.ready(); ++i )
		await stable( fixture, 1 );
	expect( fixture.debugElement.query( By.directive( FlowCanvas ) ) ).not.toBeNull();
	await stable( fixture );
	const element = <HTMLElement>fixture.nativeElement;
	const text = ( selector:string )=>element.querySelector( selector )?.textContent?.replace( /\s+/g, ' ' ).trim();
	return { fixture, tab, element, gateway, feed, refreshes, text };
};

describe( 'FlowActivity', ()=>{
	beforeEach( ()=>vi.stubGlobal( 'ResizeObserver', class{ observe(){} unobserve(){} disconnect(){} } ) );
	afterEach( ()=>vi.unstubAllGlobals() );

	//the subscription asks for the hops, draws the ack's version read-only with its link edge, and ends when the tab is left
	it( 'subscribes while shown, and draws the running version with its figures', async ()=>{
		const { fixture, tab, element, gateway, feed, text } = await create();
		expect( gateway.flowSubscribe ).toHaveBeenCalledWith( 7, true );
		expect( [tab.status().kind, text( '.state' )] ).toEqual( ['connecting', 'sensors_offSubscribing…'] );
		feed.next( ack() );
		await stable( fixture );
		expect( [tab.status().kind, tab.state().version] ).toEqual( ['live', 2] );
		expect( element.querySelectorAll( 'flow-node-view' ).length ).toBe( 5 );
		expect( element.querySelector( '.palette' ) ).toBeNull();
		expect( element.querySelectorAll( 'f-connection' ).length ).toBe( 3 );//two wires and the link's edge
		expect( element.querySelector( 'f-connection.link' ) ).not.toBeNull();
		expect( element.querySelector( '[data-f-node-id="fn"] .badge .status' )!.className ).toContain( 'green' );
		expect( text( '[data-f-node-id="fn"] .badge' ) ).toBe( 'ok' );
		expect( text( '.figures' ) ).toBe( '6 received · 6 sent · 0 dropped · 0 errors' );
		fixture.componentRef.setInput( 'active', false );
		await stable( fixture );
		expect( [tab.status().kind, feed.subscribers.length] ).toEqual( ['off', 0] );
		fixture.componentRef.setInput( 'active', true );
		await stable( fixture );
		expect( [tab.status().kind, feed.subscribers.length] ).toEqual( ['connecting', 1] );
	} );
	it( 'draws a push:  the wire busy with its label and message, a node red on an error, its drops counted', async ()=>{
		const { fixture, tab, element, feed, text } = await create();
		feed.next( ack() );
		feed.next( push( {nodes: [{node: 'fn', counts: counts( 2, [1], 1, 1 ), error: 'boom'}], wires: [{node: 'in', port: 0, to: 'fn', count: 2, msgId: '9', time: new Date( 1000 ), json: {payload: 4}, cut: false}]} ) );
		await stable( fixture );
		const wire = element.querySelector( 'f-connection#out\\:0\\:in\\>in\\:fn' )!;
		expect( wire.className ).toContain( 'busy' );
		expect( wire.querySelector( '.wire-label' )!.textContent ).toMatch( /\/s$/ );
		expect( tab.state().wires.get( 'in:0>fn' ) ).toMatchObject( {count: 2, msgId: '9', json: {payload: 4}} );
		const fn = element.querySelector( '[data-f-node-id="fn"]' )!;
		expect( fn.className ).toContain( 'erred' );
		expect( [...element.querySelector( '[data-f-node-id="fn"] .badge' )!.children].map( c=>c.textContent ) ).toEqual( ['', 'ok', '1 dropped', '1 error'] );
		expect( fn.getAttribute( 'aria-label' ) ).toBe( 'Scale, function.  ok.  boom' );
		//a wire selected shows its last message;  a node selected its figures
		tab.selectedWire.set( 'in:0>fn' );
		await stable( fixture );
		expect( text( '.selection h3' ) ).toBe( 'inject, output 1, to Scale' );
		expect( tab.wireText( {node: 'a:1>b', port: 2, to: 'c'} ) ).toBe( 'a:1>b, output 3, to c' );//by its ends:  an id holding the key's marks
		expect( text( '.selection .json' ) ).toBe( '{ "payload": 4 }' );
		tab.selectedWire.set( undefined );
		tab.selectedNode.set( 'fn' );
		await stable( fixture );
		expect( text( '.selection .hint' ) ).toBe( '5 received, 4 sent, 1 dropped, 1 error; status ok.' );
		expect( text( '.selection .error' ) ).toContain( 'boom' );
	} );
	//the debug panel:  the hops newest first, filtered, paused and cleared, one expanded to its json
	it( 'lists the hops in the debug panel', async ()=>{
		const { fixture, tab, element, feed, text } = await create();
		feed.next( ack() );
		feed.next( push( {hops: [hop( '1', 'in', 'birth', {json: {payload: 1}} ), hop( '1', 'fn' ), hop( '1', 'out', 'error', {error: 'boom', ports: []} )]} ) );
		await stable( fixture );
		const rows = ()=>[...element.querySelectorAll( '.hops li:not(.empty)' )].map( li=>li.querySelector( '.node' )!.textContent );
		expect( rows() ).toEqual( ['debug', 'Scale', 'inject'] );
		tab.filterNode.set( 'fn' );
		await stable( fixture );
		expect( rows() ).toEqual( ['Scale'] );
		tab.filterNode.set( undefined );
		tab.filterKind.set( 'birth' );
		await stable( fixture );
		expect( rows() ).toEqual( ['inject'] );
		(<HTMLButtonElement>element.querySelector( '.hops .row' )).click();
		await stable( fixture );
		expect( text( '.hops .detail .hint' ) ).toBe( 'Message 1, sent on output 0.' );
		expect( text( '.hops .detail .json' ) ).toBe( '{ "payload": 1 }' );
		tab.filterKind.set( undefined );
		tab.pause( true );
		feed.next( push( {hops: [hop( '2', 'in', 'birth' )], hopsDropped: 3} ) );
		await stable( fixture );
		expect( rows() ).toEqual( ['debug', 'Scale', 'inject'] );//the list stands
		expect( text( '.dropped' ) ).toBe( '3 older records left out.' );
		tab.pause( false );
		await stable( fixture );
		expect( rows() ).toEqual( ['inject', 'debug', 'Scale', 'inject'] );
		tab.clear();
		await stable( fixture );
		expect( rows() ).toEqual( [] );
		expect( element.querySelector( '.hops .empty' ) ).not.toBeNull();
		//hops off:  the subscription again without them
		tab.setHops( false );
		await stable( fixture );
		expect( feed.latest!.args ).toEqual( {flow: 7, hops: false} );
		expect( text( '.hops .empty' ) ).toContain( 'Hops are off' );
	} );
	//a deploy is a new ack:  the version the page lacks is asked for once, and drawn once it comes
	it( 'asks the page for a version it lacks, and draws it when it comes', async ()=>{
		const { fixture, tab, element, feed, refreshes, text } = await create();
		feed.next( ack( 3 ) );
		await stable( fixture );
		expect( [refreshes.length, tab.graph()] ).toEqual( [1, undefined] );
		expect( text( '.overlay' ) ).toBe( "Reading version 3's graph…" );
		feed.next( ack( 3 ) );
		await stable( fixture );
		expect( refreshes.length ).toBe( 1 );
		fixture.componentRef.setInput( 'versions', [version( 3, {nodes: [node( 'in', 'inject', {wires: [[]]} )]} ), version( 2 )] );
		await stable( fixture );
		expect( element.querySelectorAll( 'flow-node-view' ).length ).toBe( 1 );
		expect( element.querySelector( '.overlay' ) ).toBeNull();
	} );
	it( 'says a drop, and an end with its reason, with the subscription again on request', async ()=>{
		const { fixture, tab, element, feed, text } = await create();
		feed.next( ack() );
		feed.next( {kind: 'dropped'} );
		await stable( fixture );
		expect( tab.status().kind ).toBe( 'dropped' );
		expect( text( '.state' ) ).toContain( 'Connection to the gateway lost' );
		feed.error( {error: {requestId: 1, message: "Flow 'pump' is not running:  disabled.", sc: 0, httpStatus: 409}} );
		await stable( fixture );
		expect( tab.status() ).toEqual( {kind: 'ended', message: "Flow 'pump' is not running:  disabled.", httpStatus: 409} );
		expect( text( '.state' ) ).toContain( "Ended: Flow 'pump' is not running: disabled." );
		(<HTMLButtonElement>element.querySelector( '.actions button' )).click();
		await stable( fixture );
		expect( [tab.status().kind, feed.subscribers.length] ).toEqual( ['connecting', 1] );
	} );
} );