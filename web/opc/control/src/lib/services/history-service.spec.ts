if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { NodeId } from '../model/node-id';
import { HistoryService } from './history-service';

const A = new NodeId( {ns:2, i:1} ), B = new NodeId( {ns:2, i:2} );

describe( 'HistoryService.query', ()=>{
	it( 'reads a server\'s own history with opc, only the arguments given', ()=>{
		const {ql, vars} = HistoryService.query( {opc: 'local'}, {nodes: [A, B], end: new Date(1700000000500), limit: 1000} );
		expect( ql ).toBe( "hist( opc: $opc, nodes: $nodes, end: $end, limit: $limit ){ continuation values{ node source server status value bound heartbeat } nodes{ node status } }" );
		expect( vars ).toEqual( {opc: 'local', nodes: [A.toJson(), B.toJson()], end: {seconds: 1700000000, nanos: 500000000}, limit: 1000} );
	} );
	it( 'reads a group with group, and asks for the modifications in modified mode', ()=>{
		const {ql, vars} = HistoryService.query( {group: 3}, {nodes: [A], start: new Date(1000), end: new Date(2000), modified: true, bounds: false, continuation: 'c'} );
		expect( ql ).toBe( "hist( group: $group, nodes: $nodes, start: $start, end: $end, returnBounds: $returnBounds, modified: $modified, continuation: $continuation ){ continuation values{ node source server status value bound heartbeat modification{ time type user } } nodes{ node status } }" );
		expect( vars['group'] ).toBe( 3 );
		expect( vars['opc'] ).toBeUndefined();
		expect( vars['returnBounds'] ).toBe( false );//false is an argument, not an absence
		expect( vars['continuation'] ).toBe( 'c' );
	} );
} );

describe( 'HistoryService.read', ()=>{
	it( 'sends the query through the reader and parses the page', async ()=>{
		let sent:{ql:string, vars:any}|undefined;
		const reader = { query: async <T>( ql:string, vars?:any )=>{ sent = {ql, vars}; return <T><unknown>{hist: {continuation: null, values: [{node: {ns:2, i:1}, source: {seconds: 1, nanos: 0}, status: 0, value: 4}], nodes: [{node: {ns:2, i:1}, status: 0}]}}; } };
		const page = await new HistoryService().read( reader, {opc: 'local'}, {nodes: [A], end: new Date(5000)} );
		expect( sent?.ql ).toContain( 'hist( opc: $opc, nodes: $nodes, end: $end )' );
		expect( page.continuation ).toBeNull();
		expect( page.values ).toHaveLength( 1 );
		expect( page.values[0].value ).toBe( 4 );
		expect( page.values[0].source?.getTime() ).toBe( 1000 );
	} );
	it( 'is an empty page when the gateway answers no data', async ()=>{
		const page = await new HistoryService().read( {query: async <T>()=><T><unknown>null}, {opc: 'local'}, {nodes: [A]} );
		expect( page ).toEqual( {values: [], continuation: null, nodes: []} );
	} );
} );