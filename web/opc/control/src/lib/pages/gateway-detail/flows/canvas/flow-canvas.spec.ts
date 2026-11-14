import { TestBed } from '@angular/core/testing';
import { FSelectionChangeEvent } from '@foblex/flow';
import { stable } from 'jde-spa/testing';
import { FlowGraph } from '../../../../model/flow';
import { normalizeFlowGraph } from '../../../../model/flow-nodes';
import { FlowCanvas } from './flow-canvas';
import { outPort, toCanvas, toGraph, wireKey, wireWidth } from './flow-canvas-state';

const node = ( id:string, type:string, more:object={} )=>({ id, type, name: '', x: 0, y: 0, props: {}, wires: <string[][]>[], ...more });
//an inject into a switch of three rules, each to a sink of its own
const graph = ():FlowGraph=>normalizeFlowGraph( { nodes: [
	node( 'in', 'inject', {wires: [['sw']]} ),
	node( 'sw', 'switch', {name: 'Route', x: 220, props: {rules: [{t: 'gt', v: 10}, {t: 'null'}, {t: 'else'}]}, wires: [['a'], ['b'], ['c', 'a']]} ),
	node( 'a', 'debug', {x: 440, y: -70} ), node( 'b', 'debug', {x: 440} ), node( 'c', 'log', {x: 440, y: 70} )
] } );

const create = async ( loaded:FlowGraph=graph(), readonly=false )=>{
	const fixture = TestBed.createComponent( FlowCanvas ), canvas = fixture.componentInstance;
	fixture.componentRef.setInput( 'readonly', readonly );
	const graphs:FlowGraph[] = [], selections:(string|undefined)[] = [], wires:(string|undefined)[] = [];
	canvas.graphChange.subscribe( g=>graphs.push( g ) );
	canvas.selectedChange.subscribe( id=>selections.push( id ) );
	canvas.wireSelected.subscribe( key=>wires.push( key ) );
	canvas.load( loaded );
	await stable( fixture );
	return { fixture, canvas, element: <HTMLElement>fixture.nativeElement, graphs, selections, wireKeys: wires };
};
const wires = ( canvas:FlowCanvas, id:string )=>canvas.graph().nodes.find( n=>n.id==id )!.wires;

//The canvas's state under jsdom (#455):  the graph in, the records the library holds, and the graph back out after each edit.
describe( 'FlowCanvas', ()=>{
	//jsdom has no ResizeObserver, which every fNode makes
	beforeEach( ()=>vi.stubGlobal( 'ResizeObserver', class{ observe(){} unobserve(){} disconnect(){} } ) );
	afterEach( ()=>vi.unstubAllGlobals() );

	it( 'maps a graph to the records and back', ()=>{
		const state = toCanvas( graph() );
		expect( state.nodes!.map( n=>[n.id, n.position.x] ) ).toEqual( [['in', 0], ['sw', 220], ['a', 440], ['b', 440], ['c', 440]] );
		expect( state.connections!.map( c=>c.id ) ).toEqual( ['out:0:in>in:sw', 'out:0:sw>in:a', 'out:1:sw>in:b', 'out:2:sw>in:c', 'out:2:sw>in:a'] );
		expect( toGraph( {nodes: state.nodes!, connections: state.connections!} ) ).toEqual( graph() );
		expect( outPort( 'out:12:a:b' ) ).toEqual( {port: 12, node: 'a:b'} );//a node's id may hold anything
		//a wire to no node of the graph has no connector to end at
		expect( toCanvas( {nodes: [node( 'x', 'function', {wires: [['gone']]} )]} ).connections ).toEqual( [] );
	} );
	it( 'draws the nodes with their ports, and says the graph it holds', async ()=>{
		const { element, canvas, graphs } = await create();
		expect( element.querySelectorAll( 'flow-node-view.f-node' ).length ).toBe( 5 );
		expect( element.querySelectorAll( 'f-connection' ).length ).toBe( 5 );
		expect( element.querySelectorAll( '[data-f-node-id="sw"] .port.out' ).length ).toBe( 3 );//a port a rule
		expect( element.querySelector( '[data-f-node-id="in"] .port.in' ) ).toBeNull();//a source takes no wire
		expect( element.querySelector( '[data-f-node-id="c"] .port.out' ) ).toBeNull();
		expect( element.querySelector( '[data-f-node-id="sw"]' )!.getAttribute( 'aria-label' ) ).toBe( 'Route, switch' );
		expect( element.querySelectorAll( '.palette .item' ).length ).toBe( 28 );
		expect( graphs.at( -1 ) ).toEqual( graph() );
		expect( canvas.state.canUndo() ).toBe( false );//a load begins the history again
	} );
	it( "changes a node's name and props as one step", async ()=>{
		const { canvas, fixture, element } = await create();
		canvas.patch( 'a', {name: 'High', props: {active: false}} );
		await stable( fixture );
		expect( canvas.graph().nodes.find( n=>n.id=='a' ) ).toMatchObject( {name: 'High', props: {active: false}} );
		expect( element.querySelector( '[data-f-node-id="a"] .name' )!.textContent ).toBe( 'High' );
		canvas.state.undo();
		expect( canvas.graph() ).toEqual( graph() );
		expect( canvas.state.canUndo() ).toBe( false );
	} );
	//a switch's ports are its rules:  a rule removed takes its wires, and the rules after it keep theirs
	it( "moves a switch's wires with its rules", async ()=>{
		const { canvas } = await create();
		const rules = <object[]>canvas.state.getNode( 'sw' )!.props['rules'];
		canvas.patch( 'sw', {props: {rules: [rules[0], rules[2]]}, ports: [0, 2]} );
		expect( wires( canvas, 'sw' ) ).toEqual( [['a'], ['c', 'a']] );
		canvas.state.undo();//the props and the wires together
		expect( wires( canvas, 'sw' ) ).toEqual( [['a'], ['b'], ['c', 'a']] );
		canvas.patch( 'sw', {props: {rules: [rules[2], rules[1], rules[0]]}, ports: [2, 1, 0]} );//the first and the last swapped
		expect( wires( canvas, 'sw' ).map( port=>[...port].sort() ) ).toEqual( [['a', 'c'], ['b'], ['a']] );
		canvas.patch( 'sw', {props: {rules: [rules[2], rules[1], rules[0], {t: 'true'}]}, ports: [0, 1, 2, -1]} );//a rule added:  a port with no wire
		expect( wires( canvas, 'sw' ).map( port=>port.length ) ).toEqual( [2, 1, 1, 0] );
	} );
	it( 'drops the wires of a port a Function node no longer has', async ()=>{
		const { canvas } = await create( normalizeFlowGraph( {nodes: [node( 'f', 'function', {props: {code: '', outputs: 2}, wires: [['d'], ['d']]} ), node( 'd', 'debug' )]} ) );
		canvas.patch( 'f', {props: {code: '', outputs: 1}} );
		expect( wires( canvas, 'f' ) ).toEqual( [['d']] );
	} );
	it( 'adds a palette node below the rest, selected, and removes the selection with its wires', async ()=>{
		const { canvas, fixture, selections } = await create();
		canvas.add( 'function' );
		await stable( fixture );
		const added = canvas.state.nodes().at( -1 )!;
		expect( added ).toMatchObject( {type: 'function', name: '', props: {code: 'return msg;'}, position: {x: 0, y: 140}} );
		expect( selections.at( -1 ) ).toBe( added.id );
		canvas.select( 'sw' );
		expect( canvas.state.selection().nodeIds ).toEqual( ['sw'] );
		expect( canvas.selection() ).toBe( 1 );
		canvas.remove();
		expect( canvas.graph().nodes.map( n=>n.id ) ).toEqual( ['in', 'a', 'b', 'c', added.id] );
		expect( wires( canvas, 'in' ) ).toEqual( [[]] );
		canvas.state.undo();//the node and its four wires back, one step
		expect( canvas.state.connections().length ).toBe( 5 );
		canvas.select( undefined );
		await stable( fixture );
		expect( selections.at( -1 ) ).toBeUndefined();
	} );
	it( 'marks the nodes at fault, the one refused, and the inject it may send', async ()=>{
		const { fixture, element } = await create();
		const injected:string[] = [];
		fixture.componentInstance.inject.subscribe( id=>injected.push( id ) );
		fixture.componentRef.setInput( 'faults', new Map( [['b', '`level` must be one of Trace, Debug, Information, Warning, Error.']] ) );
		fixture.componentRef.setInput( 'refused', 'c' );
		await stable( fixture );
		expect( [...element.querySelectorAll( 'flow-node-view.faulty' )].map( e=>e.getAttribute( 'data-f-node-id' ) ) ).toEqual( ['b'] );
		expect( element.querySelector( '[data-f-node-id="b"]' )!.getAttribute( 'aria-label' ) ).toContain( '`level` must be one of' );
		expect( [...element.querySelectorAll( 'flow-node-view.refused' )].map( e=>e.getAttribute( 'data-f-node-id' ) ) ).toEqual( ['c'] );
		const button = <HTMLButtonElement>element.querySelector( '[data-f-node-id="in"] .inject' );
		expect( button.disabled ).toBe( true );//not of the version running
		fixture.componentRef.setInput( 'injectable', new Set( ['in'] ) );
		await stable( fixture );
		button.click();
		expect( injected ).toEqual( ['in'] );
	} );
	//the view of a flow's activity or history (#456):  no palette, no undo, nothing dragged or wired, the link edges dashed, and
	//the decoration over the graph
	it( 'draws read-only, with the link edges and the decoration', async ()=>{
		const linked = normalizeFlowGraph( {nodes: [...graph().nodes, node( 'lo', 'link out', {y: 200, props: {name: 'k'}} ), node( 'li', 'link in', {x: 220, y: 200, props: {name: 'k'}} ), node( 'lx', 'link in', {x: 220, y: 260, props: {name: 'other'}} )]} );
		const { canvas, fixture, element, wireKeys } = await create( linked, true );
		expect( element.querySelector( '.palette' ) ).toBeNull();
		expect( element.querySelector( '.bar button[aria-label="Undo"]' ) ).toBeNull();
		expect( element.querySelector( 'f-connection-for-create' ) ).toBeNull();
		expect( element.querySelectorAll( 'f-connection' ).length ).toBe( 6 );//five wires and the link's edge, to the link in of its name alone
		expect( element.querySelectorAll( 'f-connection.link' ).length ).toBe( 1 );
		expect( element.querySelector( '[data-f-node-id="lo"] .port.out.link' ) ).not.toBeNull();
		expect( element.querySelector( '[data-f-node-id="li"] .port.in.link' ) ).not.toBeNull();
		expect( canvas.graph() ).toEqual( linked );//the edge is no wire of the graph's
		expect( toCanvas( linked ).connections!.length ).toBe( 5 );
		expect( wireKey( {sourceId: 'out:1:sw', targetId: 'in:b'} ) ).toBe( 'sw:1>b' );
		expect( [wireWidth( undefined ), wireWidth( 0 ), wireWidth( 9 ), wireWidth( 1e6 )] ).toEqual( [2, 2, 4, 8] );
		fixture.componentRef.setInput( 'decor', { nodes: new Map( [['sw', {status: {fill: 'red', shape: 'ring', text: 'down'}, counts: {received: 5, sent: [3, 1, 1], drops: 2, errors: 1}, error: 'boom', lit: 'own'}], ['a', {lit: 'kin'}]] ), wires: new Map( [['in:0>sw', {rate: 9, label: '9/s', message: '{ "payload": 1 }', lit: 'own'}], ['sw:0>a', {label: '3'}], ['sw:2>c', {lit: 'fired'}]] ) } );
		await stable( fixture );
		const sw = element.querySelector( '[data-f-node-id="sw"]' )!;
		expect( sw.className ).toMatch( /erred/ );
		expect( sw.className ).toMatch( /\bown\b/ );
		expect( [...sw.querySelector( '.badge .status' )!.classList].sort() ).toEqual( ['red', 'ring', 'status'] );
		expect( [...sw.querySelector( '.badge' )!.children].map( c=>c.textContent ) ).toEqual( ['', 'down', '2 dropped', '1 error'] );
		expect( sw.getAttribute( 'aria-label' ) ).toBe( 'Route, switch.  down.  boom' );
		expect( element.querySelector( '[data-f-node-id="a"]' )!.className ).toMatch( /\bkin\b/ );
		const busy = element.querySelector( 'f-connection#out\\:0\\:in\\>in\\:sw' )!;
		expect( busy.className ).toMatch( /\bbusy\b.*\bown\b|\bown\b.*\bbusy\b/ );
		expect( (<HTMLElement>busy).style.getPropertyValue( '--flow-width' ) ).toBe( '4px' );
		expect( busy.querySelector( '.wire-label' )!.textContent ).toBe( '9/s' );
		expect( element.querySelector( 'f-connection#out\\:0\\:sw\\>in\\:a .wire-label' )!.textContent ).toBe( '3' );
		expect( element.querySelector( 'f-connection#out\\:2\\:sw\\>in\\:c' )!.className ).toMatch( /\bfired\b/ );
		//a wire selected is said by its key
		canvas.flow().select( [], ['out:0:in>in:sw'], false );
		canvas.state.applySelectionChange( new FSelectionChangeEvent( [], [], ['out:0:in>in:sw'] ) );
		await stable( fixture );
		expect( wireKeys.at( -1 ) ).toBe( 'in:0>sw' );
		canvas.select( undefined );
		await stable( fixture );
		expect( wireKeys.at( -1 ) ).toBeUndefined();
	} );
	//the library's keyboard layer deletes the selection on Delete:  read-only, the key goes no further
	it( 'keeps a node selected read-only from the Delete key', async ()=>{
		const press = ( element:HTMLElement, key:string )=>element.querySelector( 'f-flow' )!.dispatchEvent( new KeyboardEvent( 'keydown', {key, bubbles: true} ) );
		const editable = await create();
		editable.canvas.select( 'b' );
		await stable( editable.fixture );//the library takes a selection once its wires have settled
		press( editable.element, 'Delete' );
		await stable( editable.fixture );
		expect( editable.canvas.state.nodes().length ).toBe( 4 );//editing, the layer deletes it
		TestBed.resetTestingModule();//a read-only canvas in the same test
		const { canvas, fixture, element } = await create( graph(), true );
		canvas.select( 'b' );
		await stable( fixture );
		press( element, 'Delete' );
		press( element, 'Backspace' );
		await stable( fixture );
		expect( [canvas.state.nodes().length, canvas.state.connections().length] ).toEqual( [5, 5] );
	} );
} );