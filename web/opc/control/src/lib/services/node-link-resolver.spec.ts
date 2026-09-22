import { TestBed } from '@angular/core/testing';
import { NodeId } from '../model/node-id';
import { GATEWAY_SERVICE } from './gateway-service';
import { OPC_STORE } from './opc-store';
import { OpcNodeLinkResolver } from './node-link-resolver';

//Two gateways, three connections; only 'local' on 'gw2' is the server whose applicationName brackets "debug" - the
//accessResource node-access grants under, and the <x> of an "opc.<x>" resource schema.  The gateway's `node( id ){ path }`
//is stubbed:  the pump exists, node 9 answers null (outside the Objects tree), and 'down' cannot even be described.
describe( 'OpcNodeLinkResolver', ()=>{
	const queries:string[] = [];
	const gateway = ( slug:string, connections:string[] )=>({
		slug,
		queryArray: async ()=>connections.map( c=>({slug: c}) ),
		querySingle: async ( ql:string, vars:any )=>{ queries.push( `${slug}/${vars.opc}/${new NodeId(vars.id).uaString()}` ); return vars.id.i==5005 ? { name: "Pump 1", path: "pumps/pump1" } : { name: "Type", path: null }; }
	});
	const gw1 = gateway( "gw1", ["down", "other"] ), gw2 = gateway( "gw2", ["local"] );
	const described:string[] = [];
	const store = { getConnection: async ( g:any, cnnctn:string )=>{
		described.push( `${g.slug}/${cnnctn}` );
		if( cnnctn=="down" ) throw new Error( "unreachable" );
		return { accessResource: cnnctn=="local" ? "debug" : "other" };
	} };
	let resolver:OpcNodeLinkResolver;
	beforeEach( ()=>{
		queries.length = 0; described.length = 0;
		TestBed.configureTestingModule({ providers: [
			{ provide: GATEWAY_SERVICE, useValue: { gateways: async ()=>[gw1, gw2] } },
			{ provide: OPC_STORE, useValue: store }
		]});
		resolver = TestBed.inject( OpcNodeLinkResolver );
	} );

	it( 'places a node on the connection whose server carries the schema, and routes by the browse path', async ()=>{
		expect( await resolver.resolve("opc.debug", "ns=5;i=5005") ).toEqual( { route: ['/gateways', 'gw2', 'local', 'pumps', 'pump1'], name: "Pump 1", path: "pumps/pump1" } );
		expect( described ).toEqual( ["gw1/down", "gw1/other", "gw2/local"] );//an undescribable server is skipped, not fatal
		expect( queries ).toEqual( ["gw2/local/ns=5;i=5005"] );
	} );

	it( 'remembers the placement, not a miss', async ()=>{
		await resolver.resolve( "opc.debug", "ns=5;i=5005" );
		await resolver.resolve( "opc.debug", "ns=5;i=9" );
		expect( described ).toHaveLength( 3 );//the second call reused gw2/local
		expect( await resolver.resolve("opc.nowhere", "ns=5;i=5005") ).toBeUndefined();
		await resolver.resolve( "opc.nowhere", "ns=5;i=5005" );
		expect( described ).toHaveLength( 9 );//a miss is looked up again - the server may be up next time
	} );

	it( 'gives no link for a node the gateway cannot place, or a schema that is not a server', async ()=>{
		expect( await resolver.resolve("opc.debug", "ns=5;i=9") ).toBeUndefined();
		expect( await resolver.resolve("access", "x") ).toBeUndefined();
		expect( queries ).toEqual( ["gw2/local/ns=5;i=9"] );
	} );
} );

//reviews/m3-closing.md #29:  the cache dropped a miss but kept a REJECTED lookup, so one refused or failed request left every
//node of that server as plain text for the session, with no request ever sent again.  A gateway that refuses its connection
//list is now a miss for that gateway;  whatever else rejects is dropped from the cache.
describe( 'OpcNodeLinkResolver after a failed lookup', ()=>{
	let refuse:boolean;//gw1 answers its connection list with a 403
	let down:boolean;//the gateway list itself fails
	let connectionLists:number;
	const gateway = ( slug:string, connections:string[] )=>({
		slug,
		queryArray: async ()=>{
			++connectionLists;
			if( slug=="gw1" && refuse ) throw new Error( "(403)[bob]User does not have 'Read' access to 'serverConnections'." );
			return connections.map( c=>({slug: c}) );
		},
		querySingle: async ()=>({ name: "Pump 1", path: "pumps/pump1" })
	});
	const gateways = [gateway( "gw1", ["other"] ), gateway( "gw2", ["local"] )];
	let resolver:OpcNodeLinkResolver;
	beforeEach( ()=>{
		refuse = false; down = false; connectionLists = 0;
		TestBed.configureTestingModule({ providers: [
			{ provide: GATEWAY_SERVICE, useValue: { gateways: async ()=>{ if( down ) throw new Error( "Failed to fetch" ); return gateways; } } },
			{ provide: OPC_STORE, useValue: { getConnection: async ( _g:any, cnnctn:string )=>({ accessResource: cnnctn=="local" ? "debug" : "other" }) } }
		]});
		resolver = TestBed.inject( OpcNodeLinkResolver );
	} );
	const pump = { route: ['/gateways', 'gw2', 'local', 'pumps', 'pump1'], name: "Pump 1", path: "pumps/pump1" };

	it( 'a gateway that refuses its connections does not sink the next one', async ()=>{
		refuse = true;
		const warn = vi.spyOn( console, 'warn' ).mockImplementation( ()=>{} );
		expect( await resolver.resolve("opc.debug", "ns=5;i=5005") ).toEqual( pump );
		expect( warn ).toHaveBeenCalled();
		warn.mockRestore();
	} );

	it( 'looks again once the gateway list answers', async ()=>{
		down = true;
		await expect( resolver.resolve("opc.debug", "ns=5;i=5005") ).rejects.toThrow( "Failed to fetch" );
		down = false;
		expect( await resolver.resolve("opc.debug", "ns=5;i=5005") ).toEqual( pump );
		expect( connectionLists ).toBe( 2 );//a request went out this time
	} );
} );

describe( 'NodeId.fromUaString', ()=>{
	it( 'round-trips uaString for every identifier kind', ()=>{
		for( const s of ["ns=5;i=5005", "i=85", "ns=2;s=pump 1", "ns=3;g=12345678-1234-1234-1234-123456789abc"] )
			expect( NodeId.fromUaString(s).uaString() ).toBe( s );
	} );
	it( 'refuses anything else', ()=>{
		expect( ()=>NodeId.fromUaString("pump1") ).toThrow( /not a NodeId/ );
	} );
} );
