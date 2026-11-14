import { diffFlowGraphs, exportFlow, Flow, flowContextScope, flowFaultText, FlowGraph, importFlow, readFlowGraph, sameFlowGraph, toFlowDeployed, toFlowFault, toFlowRun, toFlowVersion } from './flow';

const node = ( id:string, type:string, more:object={} )=>({ id, type, name: '', x: 0, y: 0, props: {}, wires: [], ...more });
const graph:FlowGraph = { nodes: [node( 'in', 'inject', {wires: [['fn']]} ), node( 'fn', 'function', {name: 'Scale', props: {code: 'return msg;'}, wires: [['out']]} ), node( 'out', 'debug' )] };

describe( 'Flow', ()=>{
	//the form saves the name, the description and the trace level;  enabled and version are never its to send
	it( 'saves what its form holds', ()=>{
		const row = new Flow( {id: 3, name: 'Pump', slug: 'pump', enabled: false, version: 2, trace: 'counts', guid: 'g'} );
		expect( row.enabled ).toBe( false );
		expect( row.version ).toBe( 2 );
		const edited = new Flow( {...row.properties, name: 'Pump 1', trace: null} );
		expect( edited.equals( row ) ).toBe( false );
		expect( edited.mutation( row ).map( m=>m.toString() ) ).toEqual( ['updateFlow( id:3, name:"Pump 1", trace:null )'] );
		expect( Object.assign( new Flow( row ), {enabled: true, version: 3} ).equals( row ) ).toBe( true );
		expect( new Flow( row ).mutation( row ) ).toEqual( [] );
		expect( new Flow( {name: 'New', slug: 'new'} ).enabled ).toBe( true );
	} );
	it( "reads a refusal's node", ()=>{
		expect( toFlowFault( "Node 'fn':  `code` must be text, the body." ) ).toEqual( {node: 'fn', message: '`code` must be text, the body.'} );
		expect( toFlowFault( "(400)Node 'fn':  `code` must be text, the body." ) ).toEqual( {node: 'fn', message: '`code` must be text, the body.'} );//as a thrown answer's text leads
		expect( toFlowFault( '(403)A graph is an object with a `nodes` list.' ) ).toEqual( {message: 'A graph is an object with a `nodes` list.'} );
		expect( flowFaultText( {node: 'fn', message: 'x'}, graph ) ).toBe( "Node 'Scale':  x" );
		expect( flowFaultText( {node: 'in', message: 'x'}, graph ) ).toBe( 'The inject node:  x' );
		expect( flowFaultText( {node: 'gone', message: 'x'}, graph ) ).toBe( "Node 'gone':  x" );
		expect( flowFaultText( {message: 'x'} ) ).toBe( 'x' );
	} );
	it( "reads a deploy's answer, a version, a run and a context value's scope", ()=>{
		expect( toFlowDeployed( {flow: 3, version: 4, deployed: '2026-11-14T10:00:00Z', userName: 'ann', running: false, dropped: 2, fault: {node: null, message: 'no group'}} ) )
			.toEqual( {version: 4, deployed: new Date( '2026-11-14T10:00:00Z' ), userName: 'ann', running: false, dropped: 2, fault: {node: undefined, message: 'no group'}} );
		const version = toFlowVersion( {id: 9, version: 4, graph: JSON.stringify( graph ), deployed: '2026-11-14T10:00:00Z', identityId: 1, userName: 'ann', comment: null} );
		expect( version.graph ).toEqual( graph );
		expect( version.comment ).toBeUndefined();
		expect( toFlowVersion( {id: 9, version: 1, graph: '{"nodes":1}'} ).unread ).toContain( '`nodes` list' );
		expect( toFlowVersion( {id: 9, version: 1, graph: 'x'} ).unread ).toBeTruthy();
		expect( toFlowRun( {id: 5, version: 4, start: '2026-11-14T08:00:00Z', finish: '2026-11-14T09:00:00Z', mode: 'commit', started: '2026-11-14T10:00:00Z', userName: 'ann', status: null} ).status ).toBeUndefined();
		expect( [{flowId: undefined, nodeId: ''}, {flowId: 3, nodeId: ''}, {flowId: 3, nodeId: 'fn'}].map( flowContextScope ) ).toEqual( ['global', 'flow', 'node'] );
	} );
} );

describe( 'flow graphs', ()=>{
	//a node as the gateway reads one:  no name, place, props or wires is none of each
	it( 'reads a graph whole', ()=>{
		expect( readFlowGraph( {nodes: [{id: 'a', type: 'inject'}, {id: 'b', type: 'debug', name: 'Out', x: 10, y: 20, props: {active: false}, wires: [['a']]}]} ) ).toEqual( { nodes: [
			{ id: 'a', type: 'inject', name: '', x: 0, y: 0, props: {}, wires: [] },
			{ id: 'b', type: 'debug', name: 'Out', x: 10, y: 20, props: {active: false}, wires: [['a']] }
		] } );
		expect( ()=>readFlowGraph( {} ) ).toThrow( 'A graph is an object with a `nodes` list.' );
		expect( ()=>readFlowGraph( {nodes: [1]} ) ).toThrow( 'Node 1 of the graph is not an object.' );
	} );
	//an export of the editor's, a bare graph, and what is neither
	it( 'imports an export and a bare graph', ()=>{
		const text = exportFlow( {name: 'Pump', slug: 'pump', description: 'Mean rpm'}, graph );
		expect( importFlow( text ) ).toEqual( {name: 'Pump', slug: 'pump', description: 'Mean rpm', graph} );
		expect( importFlow( JSON.stringify( graph ) ) ).toEqual( {graph} );
		expect( importFlow( '{"graph":{"nodes":[]},"name":""}' ) ).toEqual( {name: undefined, slug: undefined, description: undefined, graph: {nodes: []}} );
		expect( ()=>importFlow( '{' ) ).toThrow( 'It is not json' );
		expect( ()=>importFlow( '{"name":"x"}' ) ).toThrow( '`nodes` list' );
		expect( ()=>importFlow( '[{"id":"n1","type":"inject","z":"f1","wires":[[]]}]' ) ).toThrow( 'Node-RED export' );//the importer's seam
	} );
	//Versions' diff, by node:  the later one's added, the earlier one's removed, and in both what changed
	it( 'diffs two graphs by node', ()=>{
		const later:FlowGraph = { nodes: [
			node( 'in', 'inject', {x: 40, wires: [['fn']]} ),
			node( 'fn', 'function', {name: 'Scale up', props: {code: 'return null;', outputs: 2}, wires: [[], ['log']]} ),
			node( 'log', 'log', {name: 'Line'} )
		] };
		expect( diffFlowGraphs( graph, later ) ).toEqual( [
			{ id: 'in', name: '', type: 'inject', kind: 'moved', what: [] },
			{ id: 'fn', name: 'Scale up', type: 'function', kind: 'changed', what: ["name, from 'Scale'", 'code', 'outputs, added', 'output 1 unwired from debug', 'output 2 wired to Line'] },
			{ id: 'log', name: 'Line', type: 'log', kind: 'added', what: [] },
			{ id: 'out', name: '', type: 'debug', kind: 'removed', what: [] }
		] );
		expect( diffFlowGraphs( graph, graph ) ).toEqual( [] );
		expect( diffFlowGraphs( graph, {nodes: [...graph.nodes.slice( 0, 2 ), node( 'out', 'log' )]} )[0] ).toEqual( {id: 'out', name: '', type: 'log', kind: 'changed', what: ['type, from debug']} );
	} );
	it( 'compares graphs whatever order their keys are written in', ()=>{
		expect( sameFlowGraph( graph, JSON.parse( JSON.stringify( graph ) ) ) ).toBe( true );
		expect( sameFlowGraph( {nodes: [node( 'a', 'filter', {props: {band: 1, percent: true}} )]}, {nodes: [node( 'a', 'filter', {props: {percent: true, band: 1}} )]} ) ).toBe( true );
		expect( sameFlowGraph( graph, {nodes: [...graph.nodes].reverse()} ) ).toBe( false );
		expect( sameFlowGraph( {nodes: [node( 'a', 'inject', {x: 1} )]}, {nodes: [node( 'a', 'inject' )]} ) ).toBe( false );
	} );
} );