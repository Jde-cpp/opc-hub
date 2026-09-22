import { TestBed } from '@angular/core/testing';
import { vi } from 'vitest';
import { OpcObject, UaNode } from '../model/node';
import { Gateway } from './gateway-service';
import { OpcStore } from './opc-store';

const gateway = "gw", cnnctn = "local";
//browse carries the {ns,name} pair findNodeId matches on; a distinct numeric id keeps every node's store entry its own.
const node = ( id:number, name:string ):UaNode => new OpcObject( {ns: 2, i: id, name, browse: {ns: 2, name}} );

describe( 'OpcStore.findNodeId', ()=>{
	let store:OpcStore;
	//The partial cache insertNode manufactures with its `store.children = []` single-child reset:  root has 'a' and 'c',
	//and 'a' has a child 'c' of its own - but 'a' has no 'x'.  OPC DI trees repeat names like this at every device level.
	const a = node( 1, "a" ), rootC = node( 2, "c" ), aC = node( 3, "c" );
	beforeEach( ()=>{
		TestBed.configureTestingModule({});
		store = TestBed.inject( OpcStore );
		store.setNodes( gateway, cnnctn, OpcObject.rootNode, [a, rootC] );
		store.setNodes( gateway, cnnctn, a, [aC] );
	} );

	it( 'resolves a path that is fully cached', ()=>{
		expect( store.findNodeId(gateway, cnnctn, "2~a") ).toBe( a );
		expect( store.findNodeId(gateway, cnnctn, "2~a/2~c") ).toBe( aC );
		expect( store.findNodeId(gateway, cnnctn, "2~c") ).toBe( rootC );
	} );

	//angular-review3 #8: the walk was a forEach, which cannot break.  'x' missed, `storeNode` stayed on 'a', and the next
	//segment matched a's OWN 'c' - so the url a/x/c resolved to a node that is not on that path at all.  NodeResolver takes
	//any non-null answer as authoritative and skips the server query.
	it( 'fails the whole walk when a MIDDLE segment misses', ()=>{
		expect( store.findNodeId(gateway, cnnctn, "2~a/2~x/2~c") ).toBeUndefined();
	} );

	it( 'fails when the first segment misses', ()=>{
		expect( store.findNodeId(gateway, cnnctn, "2~x/2~c") ).toBeUndefined();
	} );

	it( 'fails when the last segment misses', ()=>{
		expect( store.findNodeId(gateway, cnnctn, "2~a/2~x") ).toBeUndefined();
	} );

	it( 'answers undefined for a connection it has never seen', ()=>{
		expect( store.findNodeId(gateway, "other", "2~a") ).toBeUndefined();
	} );
} );

//reviews/m3-closing.md #5:  a describe that succeeded was kept for the life of the page and nothing removed it - an edited
//Name or Default Namespace never reached the node pages, and the Connection tab never went back to "Not connected".
describe( 'OpcStore.getConnection', ()=>{
	let store:OpcStore;
	const props = ( name:string )=>({ connection: {id: 1, slug: cnnctn, name, url: "opc.tcp://plc:4840", certificateUri: "", defaultBrowseNs: 1}, desc: {}, policy: "", mode: "None", namespaces: [] });
	const gatewayOf = ( query:any )=>(<unknown>{ slug: gateway, query }) as Gateway;
	beforeEach( ()=>{
		TestBed.configureTestingModule({});
		store = TestBed.inject( OpcStore );
	} );

	it( 'memoizes a describe - the node pages share it', async ()=>{
		const query = vi.fn().mockResolvedValue( props("Line 1") );
		await store.getConnection( gatewayOf(query), cnnctn );
		await store.getConnection( gatewayOf(query), cnnctn );
		expect( query ).toHaveBeenCalledTimes( 1 );
	} );

	it( 'describes again once the connection is forgotten', async ()=>{
		const query = vi.fn().mockResolvedValueOnce( props("Line 1") ).mockResolvedValueOnce( props("Line 2") );
		await store.getConnection( gatewayOf(query), cnnctn );
		store.forget( gateway, cnnctn );
		const server = await store.getConnection( gatewayOf(query), cnnctn );
		expect( query ).toHaveBeenCalledTimes( 2 );
		expect( server.connection.name ).toBe( "Line 2" );
	} );

	it( 'describes fresh when asked, and keeps nothing when that fails', async ()=>{
		const query = vi.fn().mockResolvedValueOnce( props("Line 1") ).mockRejectedValueOnce( new Error("server down") ).mockResolvedValueOnce( props("Line 2") );
		await store.getConnection( gatewayOf(query), cnnctn );
		await expect( store.getConnection(gatewayOf(query), cnnctn, {fresh: true}) ).rejects.toThrow( "server down" );
		const server = await store.getConnection( gatewayOf(query), cnnctn );//the memoized path must not answer with the stale describe
		expect( query ).toHaveBeenCalledTimes( 3 );
		expect( server.connection.name ).toBe( "Line 2" );
	} );

	it( 'forgets the connection\'s nodes with it - a new url can be another server', ()=>{
		const a = node( 1, "a" );
		store.setNodes( gateway, cnnctn, OpcObject.rootNode, [a] );
		expect( store.findNodeId(gateway, cnnctn, "2~a") ).toBe( a );
		store.forget( gateway, cnnctn );
		expect( store.findNodeId(gateway, cnnctn, "2~a") ).toBeUndefined();
	} );
} );
