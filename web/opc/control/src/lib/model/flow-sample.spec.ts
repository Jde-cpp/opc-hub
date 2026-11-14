import sample from '../../assets/samples/pump-rpm.json';
import { importFlow } from './flow';
import { flowGraphFault, flowNodeFaults, flowNodeType, normalizeFlowGraph } from './flow-nodes';

//The demo's sample (#457, docs/flows/spec.md *Emulator and the demo*), the file the help's walk-through imports:  what Import
//reads of it, and what the editor's rules, mirrored from the gateway's, say of its graph, so a node type or a prop that changes
//under it fails here.  Its connection, its calendar and its webhook's host are the site's, which the gateway alone can refuse:
//the file was deployed on a hub over the bundled server's pumps but for those (docs/flows/plan.md, Phase 11's Built note).
describe( 'the pump rpm sample', ()=>{
	const read = ()=>importFlow( JSON.stringify( sample ) );

	it( 'is an export Import reads', ()=>{
		const flow = read();
		expect( {name: flow.name, slug: flow.slug} ).toEqual( {name: 'Pump 1 rpm', slug: 'pump1'} );
		expect( flow.description ).toBeTruthy();
		expect( flow.graph.nodes.map( n=>n.id ) ).toEqual( sample.graph.nodes.map( n=>n.id ) );//each node came with its id
		expect( flow.graph.nodes.map( n=>n.type ).sort() ).toEqual( ['calendar', 'change', 'function', 'http request', 'log', 'log', 'opc in', 'switch', 'trigger', 'variable', 'window'] );
	} );
	it( 'has nothing the editor would mark', ()=>{
		const { graph } = read();
		expect( flowGraphFault( graph ) ).toBeUndefined();
		expect( [...flowNodeFaults( graph )] ).toEqual( [] );
	} );
	//so the canvas is the file, and its first deploy stores what was imported
	it( 'is as the canvas holds it, each node that takes a wire wired', ()=>{
		const { graph } = read();
		expect( normalizeFlowGraph( graph ) ).toEqual( graph );
		const fed = new Set( graph.nodes.flatMap( n=>n.wires.flat() ) );
		expect( graph.nodes.filter( n=>flowNodeType( n.type )!.input && !fed.has( n.id ) ).map( n=>n.id ) ).toEqual( [] );
	} );
	it( "takes pump1's motorRpm through a minute's mean into the variable", ()=>{
		const by = new Map( read().graph.nodes.map( n=>[n.id, n] ) );
		//the node by its namespace's URI, since an index is one server's
		expect( by.get( 'rpm' )!.props ).toEqual( {connection: 'OpcServer', node: {nsu: 'urn:jde:pumps', i: 6012}} );
		expect( by.get( 'rpm' )!.wires ).toEqual( [['minute', 'watchdog']] );
		expect( by.get( 'minute' )!.props ).toEqual( {duration: 'PT1M'} );
		expect( by.get( 'mean' )!.props['code'] ).toContain( 'msg.payload.mean' );
		expect( by.get( 'mean' )!.wires ).toEqual( [['rpmMean', 'limit']] );
		expect( by.get( 'rpmMean' )!.props ).toMatchObject( {name: 'rpmMean', dataType: 'Double'} );
		expect( by.get( 'reset' )!.wires ).toEqual( [['minute']] );
	} );
} );