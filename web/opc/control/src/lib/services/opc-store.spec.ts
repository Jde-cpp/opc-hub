import { TestBed } from '@angular/core/testing';
import { ENodeClass, OpcObject, UaNode } from '../model/node';
import { NodeRoute } from '../model/node-route';
import { NodeView } from '../model/node-view';
import { namespaceColumns, serverDescColumns } from '../model/server';
import { Gateway } from './gateway-service';
import { OpcStore } from './opc-store';

const gateway = "gw", cnnctn = "local";
//browse carries the {ns,name} pair findNodeId matches on; a distinct numeric id keeps every node's store entry its own.
const node = ( id:number, name:string ):UaNode=>new OpcObject( {ns: 2, i: id, name, browse: {ns: 2, name}} );

describe( 'OpcStore.findNodeId', ()=>{
	//The partial cache insertNode manufactures with its `store.children = []` single-child reset:  root has 'a' and 'c',
	//and 'a' has a child 'c' of its own - but 'a' has no 'x'.  OPC DI trees repeat names like this at every device level.
	const a = node( 1, "a" ), rootC = node( 2, "c" ), aC = node( 3, "c" );
	const create = ()=>{
		const store = TestBed.inject( OpcStore );
		store.setNodes( gateway, cnnctn, OpcObject.rootNode, [a, rootC] );
		store.setNodes( gateway, cnnctn, a, [aC] );
		return store;
	};

	it( 'resolves a path that is fully cached', ()=>{
		const store = create();
		expect( store.findNodeId(gateway, cnnctn, "2~a") ).toBe( a );
		expect( store.findNodeId(gateway, cnnctn, "2~a/2~c") ).toBe( aC );
		expect( store.findNodeId(gateway, cnnctn, "2~c") ).toBe( rootC );
	} );

	//angular-review3 #8: the walk was a forEach, which cannot break.  'x' missed, `storeNode` stayed on 'a', and the next
	//segment matched a's OWN 'c' - so the url a/x/c resolved to a node that is not on that path at all.  NodeResolver takes
	//any non-null answer as authoritative and skips the server query.
	it( 'fails the whole walk when a MIDDLE segment misses', ()=>{
		const store = create();
		expect( store.findNodeId(gateway, cnnctn, "2~a/2~x/2~c") ).toBeUndefined();
	} );

	it( 'fails when the first segment misses', ()=>{
		const store = create();
		expect( store.findNodeId(gateway, cnnctn, "2~x/2~c") ).toBeUndefined();
	} );

	it( 'fails when the last segment misses', ()=>{
		const store = create();
		expect( store.findNodeId(gateway, cnnctn, "2~a/2~x") ).toBeUndefined();
	} );

	//Phase 4 (#416):  a bare segment is the default namespace's name, else namespace 0's - `Objects/2~a`, as the gateway spells a page path.
	it( 'takes a bare segment for a namespace-0 name, as the Objects folder is', ()=>{
		const store = TestBed.inject( OpcStore );
		const objects = OpcObject.objectsNode, b = node( 4, "b" );
		store.setNodes( gateway, cnnctn, OpcObject.rootNode, [objects] );
		store.setNodes( gateway, cnnctn, objects, [b] );
		expect( store.findNodeId(gateway, cnnctn, "Objects/2~b") ).toBe( b );
		expect( store.findNodeId(gateway, cnnctn, "0~Objects/2~b") ).toBe( b );
		expect( store.findNodeId(gateway, cnnctn, "2~Objects/2~b") ).toBeUndefined();
	} );

	//reviews-issue/416-address-space.md #7:  a bare segment both namespaces hold is the default's, as the gateway's childNamed takes it
	it( "takes a bare segment for the default namespace's twin, whatever the browse's order", ()=>{
		const store = TestBed.inject( OpcStore );
		store.getConnection( <any>{ slug: gateway, query: async ()=>({ connection: {id: 1, slug: cnnctn, name: 'n', url: 'u', certificateUri: '', defaultBrowseNs: 2}, desc: {}, policy: '', mode: 'None', namespaces: [] }) }, cnnctn );
		const objects = OpcObject.objectsNode;
		const ns0 = new OpcObject( {ns: 0, i: 2253, name: 'Server', browse: {ns: 0, name: 'Server'}} ), own = node( 9, 'Server' );
		store.setNodes( gateway, cnnctn, OpcObject.rootNode, [objects] );
		store.setNodes( gateway, cnnctn, objects, [ns0, own] );//node-id order:  namespace 0 first
		return Promise.resolve().then( ()=>{
			expect( store.findNodeId(gateway, cnnctn, 'Objects/Server') ).toBe( own );
			expect( store.findNodeId(gateway, cnnctn, 'Objects/0~Server') ).toBe( ns0 );
		} );
	} );

	it( 'answers undefined for a connection it has never seen', ()=>{
		const store = create();
		expect( store.findNodeId(gateway, "other", "2~a") ).toBeUndefined();
	} );
} );

//reviews-issue/416-address-space.md #1:  the hist node picker browses from the Objects folder, which the store then held under no
//parent.  A deep link below it found no way down from Root, and insertNode handed addChildren an undefined parent.
describe( 'OpcStore.insertNode', ()=>{
	const route = ( leaf:UaNode, path:string )=>(<unknown>{ gatewaySlug: gateway, cnnctnSlug: cnnctn, node: leaf, nodeId: leaf.nodeId, path }) as NodeRoute;
	const objectsParent = { ns: 0, i: 85, name: 'Objects', browse: {ns: 0, name: 'Objects'}, nodeClass: ENodeClass.Object, path: 'Objects' };
	const pumpParent = { ns: 2, i: 10, name: 'pump1', browse: {ns: 2, name: 'pump1'}, nodeClass: ENodeClass.Object, path: 'Objects/2~pump1' };

	it( 'hangs a browse start the store holds under no parent beneath Root', ()=>{
		const store = TestBed.inject( OpcStore );
		const pump1 = node( 10, "pump1" ), types = new OpcObject( {ns: 0, i: 86, name: 'Types', browse: {ns: 0, name: 'Types'}} );
		store.setNodes( gateway, cnnctn, OpcObject.rootNode, [types] );
		store.setNodes( gateway, cnnctn, OpcObject.objectsNode, [pump1] );//the picker's browse
		const motorRpm = node( 11, "motorRpm" );
		const r = route( motorRpm, 'Objects/2~pump1/2~motorRpm' );
		expect( ()=>store.insertNode( r, [pumpParent, objectsParent], 2 ) ).not.toThrow();
		expect( r.node.parent?.key ).toBe( pump1.key );
		expect( store.findNodeId(gateway, cnnctn, 'Objects/2~pump1/2~motorRpm') ).toBe( motorRpm );
		expect( store.findNodeId(gateway, cnnctn, 'Types') ).toBe( types );//Root's other children stay
	} );

	//the #416 browser walk:  the picker's start is a bare id with no browse name (HistNodePickerDialog.toUaNode) - linked as it was, no
	//path could walk through it, and setRoute could not spell it
	it( 'names a browse start the picker left as a bare id, so a path walks through it', ()=>{
		const store = TestBed.inject( OpcStore );
		const bare = new OpcObject( {ns: 2, i: 10, name: {locale: 'en-US', text: 'ns=2;i=10'}} );//as the picker builds a group's root
		const motorRpm = node( 11, 'motorRpm' );
		store.setNodes( gateway, cnnctn, bare, [motorRpm] );
		const leaf = node( 12, 'EURange' );
		const r = (<unknown>{ gatewaySlug: gateway, cnnctnSlug: cnnctn, node: leaf, nodeId: leaf.nodeId, path: 'Objects/2~pump1/2~motorRpm/2~EURange' }) as NodeRoute;
		expect( ()=>store.insertNode( r, [objectsParent, pumpParent, {ns: 2, i: 11, name: 'motorRpm', browse: {ns: 2, name: 'motorRpm'}, nodeClass: ENodeClass.Variable, path: 'Objects/2~pump1/2~motorRpm'}], 2 ) ).not.toThrow();
		expect( store.findNodeId(gateway, cnnctn, 'Objects/2~pump1/2~motorRpm/2~EURange') ).toBe( leaf );
		expect( ()=>store.setRoute( r, 2 ) ).not.toThrow();
		expect( r.parent.path ).toBe( '/Objects/pump1/motorRpm' );//namespace 2 is the default:  bare;  the fake route's cnnctnUrl joins as ''
	} );

	it( 'places a deep-linked node the store holds under no parent, as a picker started at it leaves it', ()=>{
		const store = TestBed.inject( OpcStore );
		const pump1 = node( 10, "pump1" );
		store.setNodes( gateway, cnnctn, pump1, [node( 11, "motorRpm" )] );//the picker started at a group's root
		const r = route( node( 10, "pump1" ), 'Objects/2~pump1' );
		store.insertNode( r, [objectsParent], 2 );
		expect( store.findNodeId(gateway, cnnctn, 'Objects/2~pump1') ).toBe( pump1 );
		expect( ()=>store.setRoute( r, 2 ) ).not.toThrow();
		expect( r.parent.title ).toBe( 'Objects' );
	} );
} );

//reviews-issue/416-address-space.md #5:  with the System nodes toggle off, the sidenav's siblings dropped the page's own node when it
//was a system one - a ns-0 page opened from a References target or a search hit had no entry for itself.
describe( 'OpcStore.setRoute', ()=>{
	it( "keeps the page's own node among the siblings when the toggle hides the others like it", ()=>{
		const store = TestBed.inject( OpcStore );
		const server = new OpcObject( {ns: 0, i: 2253, name: 'Server', browse: {ns: 0, name: 'Server'}} );
		const aliases = new OpcObject( {ns: 0, i: 23470, name: 'Aliases', browse: {ns: 0, name: 'Aliases'}} );
		const pump1 = node( 10, 'pump1' );
		store.setNodes( gateway, cnnctn, OpcObject.rootNode, [OpcObject.objectsNode] );
		store.setNodes( gateway, cnnctn, OpcObject.objectsNode, [server, aliases, pump1] );
		const r = (<unknown>{ gatewaySlug: gateway, cnnctnSlug: cnnctn, node: server, nodeId: server.nodeId, path: 'Objects/Server' }) as NodeRoute;
		const titles = ()=>r.siblings!.map( s=>s.title ).sort();
		store.setShowSystem( false );
		store.setRoute( r, 2 );
		expect( titles() ).toEqual( ['Server', 'pump1'].sort() );
		store.setShowSystem( true );//#5, #15:  a flip sets the sidenav again at once, not at the next navigation
		expect( titles() ).toEqual( ['Aliases', 'Server', 'pump1'].sort() );
	} );

	//reviews-issue/416-address-space.md #15:  the toggle's one copy, kept per browser
	it( 'keeps the System nodes toggle per browser', ()=>{
		const store = TestBed.inject( OpcStore );
		store.setShowSystem( true );
		expect( new OpcStore().showSystem() ).toBe( true );
		store.setShowSystem( false );
		expect( new OpcStore().showSystem() ).toBe( false );
		expect( localStorage.getItem(`${NodeView.collectionName}/systemNodes`) ).toBeNull();//off is the default, and removes the key.  Its key alone:  `ng test` does not isolate spec files, so another's may be there.
	} );
} );

//reviews/m3-closing.md #5:  a describe that succeeded was kept for the life of the page and nothing removed it - an edited
//Name or Default Namespace never reached the node pages, and the Connection tab never went back to "Not connected".
describe( 'OpcStore.getConnection', ()=>{
	const props = ( name:string )=>({ connection: {id: 1, slug: cnnctn, name, url: "opc.tcp://plc:4840", certificateUri: "", defaultBrowseNs: 1}, desc: {}, policy: "", mode: "None", namespaces: [] });
	const gatewayOf = ( query:any )=>(<unknown>{ slug: gateway, query }) as Gateway;
	const create = ()=>TestBed.inject( OpcStore );

	it( 'memoizes a describe - the node pages share it', async ()=>{
		const store = create();
		const query = vi.fn().mockResolvedValue( props("Line 1") );
		await store.getConnection( gatewayOf(query), cnnctn );
		await store.getConnection( gatewayOf(query), cnnctn );
		expect( query ).toHaveBeenCalledTimes( 1 );
	} );

	it( 'describes again once the connection is forgotten', async ()=>{
		const store = create();
		const query = vi.fn().mockResolvedValueOnce( props("Line 1") ).mockResolvedValueOnce( props("Line 2") );
		await store.getConnection( gatewayOf(query), cnnctn );
		store.forget( gateway, cnnctn );
		const server = await store.getConnection( gatewayOf(query), cnnctn );
		expect( query ).toHaveBeenCalledTimes( 2 );
		expect( server.connection.name ).toBe( "Line 2" );
	} );

	it( 'describes fresh when asked, and keeps nothing when that fails', async ()=>{
		const store = create();
		const query = vi.fn().mockResolvedValueOnce( props("Line 1") ).mockRejectedValueOnce( new Error("server down") ).mockResolvedValueOnce( props("Line 2") );
		await store.getConnection( gatewayOf(query), cnnctn );
		await expect( store.getConnection(gatewayOf(query), cnnctn, {fresh: true}) ).rejects.toThrow( "server down" );
		const server = await store.getConnection( gatewayOf(query), cnnctn );//the memoized path must not answer with the stale describe
		expect( query ).toHaveBeenCalledTimes( 3 );
		expect( server.connection.name ).toBe( "Line 2" );
	} );

	//#250 review L3:  ClientDetail's test mutation selects the same describe (client-detail.spec checks that half)
	it( 'selects the shared describe columns', async ()=>{
		const store = create();
		const query = vi.fn().mockResolvedValue( props("Line 1") );
		await store.getConnection( gatewayOf(query), cnnctn );
		expect( query.mock.calls[0][0] ).toContain( `serverDescription( opc: $opc ){ ${serverDescColumns} }` );
		expect( query.mock.calls[0][0] ).toContain( `namespaces( opc: $opc ){ ${namespaceColumns} }` );
	} );

	it( 'forgets the connection\'s nodes with it - a new url can be another server', ()=>{
		const store = create();
		const a = node( 1, "a" );
		store.setNodes( gateway, cnnctn, OpcObject.rootNode, [a] );
		expect( store.findNodeId(gateway, cnnctn, "2~a") ).toBe( a );
		store.forget( gateway, cnnctn );
		expect( store.findNodeId(gateway, cnnctn, "2~a") ).toBeUndefined();
	} );
} );