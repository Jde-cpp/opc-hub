import { FlowGraph } from '../model/flow';
import { FlowService } from './flow-service';

//the service over a stub gateway:  what each call sends, and what it makes of the answer
const stub = ( answer:unknown )=>({ query: vi.fn( async ( ..._args:unknown[] )=><any>answer ), postQL: vi.fn( async ( ..._args:unknown[] )=><any>answer ) });
const graph:FlowGraph = { nodes: [{id: 'in', type: 'inject', name: '', x: 0, y: 0, props: {}, wires: [[]]}] };

describe( 'FlowService', ()=>{
	const service = new FlowService();
	it( "reads a flow's versions, newest first, with their graphs", async ()=>{
		const g = stub( {flowVersions: [{id: 1, version: 1, graph: JSON.stringify( graph ), userName: 'ann'}, {id: 2, version: 2, graph: '{"nodes":[]}', userName: 'bo', comment: 'emptied'}]} );
		const versions = await service.versions( g, 3 );
		expect( versions.map( v=>[v.version, v.graph.nodes.length, v.comment] ) ).toEqual( [[2, 0, 'emptied'], [1, 1, undefined]] );
		expect( g.query ).toHaveBeenCalledWith( 'flowVersions( flowId: $flow ){ id version graph deployed identityId userName comment }', {flow: 3} );
	} );
	it( "reads the flow's context values and the gateway's", async ()=>{
		const g = { query: vi.fn( async ( ql:string, _vars?:unknown ):Promise<any>=>ql.includes( 'null' ) ? {flowContexts: [{id: 9, flowId: null, nodeId: '', name: 'site', value: '"A"'}]} : {flowContexts: [{id: 5, flowId: 3, nodeId: 'fn', name: 'count', value: '2'}]} ) };
		expect( await service.contexts( g, 3 ) ).toEqual( [{id: 5, flowId: 3, nodeId: 'fn', name: 'count', value: '2'}, {id: 9, flowId: undefined, nodeId: '', name: 'site', value: '"A"'}] );
		expect( g.query.mock.calls ).toEqual( [['flowContexts( flowId: $flow ){ id flowId nodeId name value }', {flow: 3}], ['flowContexts( flowId: null ){ id flowId nodeId name value }']] );
	} );
	//the row made, with what the gateway gave it:  the stock op's list, or the object the hub answers
	it( 'creates a flow', async ()=>{
		const g = stub( {createFlow: [{id: 7, guid: 'abc', enabled: true, version: null}]} );
		const flow = await service.create( g, {name: 'Pump', slug: 'pump', description: 'Mean rpm'} );
		expect( flow ).toMatchObject( {id: 7, guid: 'abc', enabled: true, version: undefined, name: 'Pump', slug: 'pump'} );
		expect( g.postQL ).toHaveBeenCalledWith( 'mutation createFlow( name: $name, slug: $slug, description: $description ){ id guid enabled version }', {name: 'Pump', slug: 'pump', description: 'Mean rpm'} );
		const hub = stub( {createFlow: {id: 8, guid: 'def'}} );
		expect( (await service.create( hub, {name: 'B', slug: 'b'} )).id ).toBe( 8 );
		expect( hub.postQL ).toHaveBeenCalledWith( 'mutation createFlow( name: $name, slug: $slug ){ id guid enabled version }', {name: 'B', slug: 'b'} );
		await expect( service.create( stub( {} ), {name: 'B', slug: 'b'} ) ).rejects.toThrow( 'answered no id' );
	} );
	it( 'updates, enables and purges by id', async ()=>{
		const g = stub( {} );
		await service.update( g, 7, {trace: null, name: 'Pump 1'} );
		expect( g.postQL ).toHaveBeenLastCalledWith( 'mutation updateFlow( id: $id, trace: $trace, name: $name )', {id: 7, trace: null, name: 'Pump 1'} );
		await service.setEnabled( g, 7, false );
		expect( g.postQL ).toHaveBeenLastCalledWith( 'mutation updateFlow( id: $id, enabled: $enabled )', {id: 7, enabled: false} );
		await service.purge( g, 7 );
		expect( g.postQL ).toHaveBeenLastCalledWith( 'mutation purgeFlow( id: $id )', {id: 7} );
	} );
	//the graph goes as json;  the answer says whether the version runs, and why not
	it( 'deploys a graph with its comment', async ()=>{
		const g = stub( {deployFlow: {flow: 7, version: 3, deployed: '2026-11-14T10:00:00Z', userName: 'ann', running: false, dropped: 1, fault: {node: 'in', message: 'no group'}}} );
		expect( await service.deploy( g, 7, graph, ' the first ' ) ).toEqual( {version: 3, deployed: new Date( '2026-11-14T10:00:00Z' ), userName: 'ann', running: false, dropped: 1, fault: {node: 'in', message: 'no group'}} );
		expect( g.postQL ).toHaveBeenCalledWith( 'mutation deployFlow( flow: $flow, graph: $graph, comment: $comment ){ flow version deployed userName running dropped fault{ node message } }', {flow: 7, graph, comment: 'the first'} );
		await service.deploy( g, 7, graph, '  ' );
		expect( g.postQL.mock.lastCall![1] ).toMatchObject( {comment: null} );
		const refused = { postQL: vi.fn( async ():Promise<any>=>{ throw new Error( "Node 'in':  `message` must be an object, the message the node sends." ); } ) };
		await expect( service.deploy( refused, 7, graph ) ).rejects.toThrow( "Node 'in'" );
	} );
	it( "injects a node's message, or one in its place", async ()=>{
		const g = stub( {injectFlow: {node: 'in'}} );
		await service.inject( g, 7, 'in' );
		expect( g.postQL ).toHaveBeenLastCalledWith( 'mutation injectFlow( flow: $flow, node: $node, message: $message ){ node }', {flow: 7, node: 'in', message: null} );
		await service.inject( g, 7, 'in', {payload: 5} );
		expect( g.postQL.mock.lastCall![1] ).toEqual( {flow: 7, node: 'in', message: {payload: 5}} );
	} );
	//no flow is the gateway's scope, no node the flow's;  no value takes the key away
	it( 'sets a context value at each scope, and takes one away', async ()=>{
		const g = stub( {} );
		await service.setContext( g, {flowId: 7, nodeId: 'fn', name: 'count'}, 3 );
		expect( g.postQL ).toHaveBeenLastCalledWith( 'mutation setFlowContext( flow: $flow, node: $node, name: $name, value: $value ){ name value }', {flow: 7, node: 'fn', name: 'count', value: 3} );
		await service.setContext( g, {flowId: 7, name: 'limits'}, {high: 9} );
		expect( g.postQL.mock.lastCall![1] ).toEqual( {flow: 7, node: null, name: 'limits', value: {high: 9}} );
		await service.setContext( g, {name: 'site'}, undefined );
		expect( g.postQL.mock.lastCall![1] ).toEqual( {flow: null, node: null, name: 'site', value: null} );
	} );
	//the three reads, each given the arguments it has, a time as ISO text (#456)
	it( 'reads the trace, a message and the counts', async ()=>{
		const g = stub( {
			flowTrace: {continuation: 'abc', records: [{time: '2026-11-14T08:00:00Z', msgId: {low: 5, high: 0, unsigned: true}, parentId: null, runId: 0, node: 'in', kind: 'birth', ports: [0], json: {payload: 1}, cut: false, error: null, status: null, blocks: []}]},
			flowMessage: {more: true, records: [{time: '2026-11-14T08:00:00Z', msgId: 5, parentId: null, runId: 0, node: 'in', kind: 'birth', ports: [], json: null, cut: false, error: null, status: null, blocks: []}]},
			flowCounts: {continuation: null, counts: [{time: '2026-11-14T08:00:00Z', node: 'in', received: 0, sent: [3], drops: 0, errors: 0}]}
		} );
		const start = new Date( '2026-11-14T08:00:00Z' ), end = new Date( '2026-11-14T09:00:00Z' );
		const trace = await service.trace( g, {flow: 7, start, end, node: 'in', kind: 'birth', limit: 200} );
		expect( trace.continuation ).toBe( 'abc' );
		expect( trace.records[0] ).toMatchObject( {msgId: '5', node: 'in', kind: 'birth', ports: [0], json: {payload: 1}} );
		expect( g.query ).toHaveBeenLastCalledWith( 'flowTrace( flow: $flow, start: $start, end: $end, node: $node, kind: $kind, limit: $limit ){ continuation records{ time msgId parentId runId node kind ports json cut error status{ fill shape text } blocks } }', {flow: 7, start: '2026-11-14T08:00:00.000Z', end: '2026-11-14T09:00:00.000Z', node: 'in', kind: 'birth', limit: 200} );
		await service.trace( g, {flow: 7, end, msg: '5', run: 3, continuation: 'abc'} );
		expect( g.query.mock.lastCall![1] ).toEqual( {flow: 7, end: '2026-11-14T09:00:00.000Z', msg: '5', run: 3, continuation: 'abc'} );
		const lineage = await service.message( g, {flow: 7, id: '5', start} );
		expect( [lineage.more, lineage.records[0].msgId] ).toEqual( [true, '5'] );
		expect( g.query ).toHaveBeenLastCalledWith( 'flowMessage( flow: $flow, id: $id, start: $start ){ more records{ time msgId parentId runId node kind ports json cut error status{ fill shape text } blocks } }', {flow: 7, id: '5', start: '2026-11-14T08:00:00.000Z'} );
		const counts = await service.counts( g, {flow: 7, start, end, by: 'PT1M'} );
		expect( [counts.continuation, counts.counts[0].counts.sent] ).toEqual( [undefined, [3]] );
		expect( g.query ).toHaveBeenLastCalledWith( 'flowCounts( flow: $flow, start: $start, end: $end, by: $by ){ continuation counts{ time node received sent drops errors } }', {flow: 7, start: '2026-11-14T08:00:00.000Z', end: '2026-11-14T09:00:00.000Z', by: 'PT1M'} );
	} );
	it( 'replays the current version, lists the runs and cancels one', async ()=>{
		const g = stub( {replayFlow: {id: 5, version: 3, start: '2026-11-14T08:00:00Z', finish: '2026-11-14T09:00:00Z', mode: 'dry', started: '2026-11-14T10:00:00Z', userName: 'ann', status: null}, flowRuns: [{id: 4, version: 2, mode: 'commit', status: 'completed'}, {id: 5, version: 3, mode: 'dry', status: null}]} );
		const run = await service.replay( g, 7, new Date( '2026-11-14T08:00:00Z' ), new Date( '2026-11-14T09:00:00Z' ), 'dry' );
		expect( run ).toMatchObject( {id: 5, version: 3, mode: 'dry', status: undefined} );
		expect( g.postQL ).toHaveBeenLastCalledWith( 'mutation replayFlow( flow: $flow, from: $from, to: $to, mode: $mode ){ id version start finish mode started userName status }', {flow: 7, from: '2026-11-14T08:00:00.000Z', to: '2026-11-14T09:00:00.000Z', mode: 'dry'} );
		expect( (await service.runs( g, 7 )).map( r=>[r.id, r.status] ) ).toEqual( [[5, undefined], [4, 'completed']] );
		expect( g.query ).toHaveBeenLastCalledWith( 'flowRuns( flowId: $flow ){ id version start finish mode started finished userName status }', {flow: 7} );
		await service.cancelRun( g, 5 );
		expect( g.postQL ).toHaveBeenLastCalledWith( 'mutation cancelFlowRun( id: $id ){ id status finished }', {id: 5} );
	} );
} );