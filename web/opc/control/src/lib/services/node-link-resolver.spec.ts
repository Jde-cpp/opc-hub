import { TestBed } from '@angular/core/testing';
import { NodeId } from '../model/node-id';
import { GATEWAY_SERVICE } from './gateway-service';
import { OPC_STORE } from './opc-store';
import { OpcNodeLinkResolver } from './node-link-resolver';
import { silence } from 'jde-spa/testing';
import { fakeGateway } from 'jde-opc/testing';

//Two gateways, three connections; only 'local' on 'gw2' is the server whose applicationName brackets "debug" - the
//accessResource node-access grants under, and the <x> of an "opc.<x>" resource schema.  The gateway's `node( id ){ path }`
//is stubbed:  the pump exists, node 9 answers null (outside the Objects tree), and 'down' cannot even be described.
describe( 'OpcNodeLinkResolver', ()=>{
	//the resolver, with `queried` - each node asked of a gateway, as gateway/connection/node - and `described` - each server it read
	const create = ()=>{
		const querySingle = vi.fn( async ( _slug:string, vars:any )=>vars.id.i==5005 ? { name: "Pump 1", path: "pumps/pump1" } : { name: "Type", path: null } );
		const gateway = ( slug:string, connections:string[] )=>Object.assign( fakeGateway( slug, {serverConnections: connections.map( c=>({slug: c}) )} ), {
			querySingle: ( _ql:string, vars:any )=>querySingle( slug, vars )
		} );
		const getConnection = vi.fn( async ( _g:any, cnnctn:string )=>{
			if( cnnctn=="down" ) throw new Error( "unreachable" );
			return { accessResource: cnnctn=="local" ? "debug" : "other" };
		} );
		TestBed.configureTestingModule({ providers: [
			{ provide: GATEWAY_SERVICE, useValue: { gateways: async ()=>[gateway( "gw1", ["down", "other"] ), gateway( "gw2", ["local"] )] } },
			{ provide: OPC_STORE, useValue: { getConnection } }
		]});
		return {
			resolver: TestBed.inject( OpcNodeLinkResolver ),
			getConnection,
			queried: ()=>querySingle.mock.calls.map( ([slug, vars])=>`${slug}/${vars.opc}/${new NodeId(vars.id).uaString()}` ),
			described: ()=>getConnection.mock.calls.map( ([g, cnnctn])=>`${g.slug}/${cnnctn}` )
		};
	};

	it( 'places a node on the connection whose server carries the schema, and routes by the browse path', async ()=>{
		const { resolver, queried, described } = create();
		expect( await resolver.resolve("opc.debug", "ns=5;i=5005") ).toEqual( { route: ['/gateways', 'gw2', 'local', 'pumps', 'pump1'], name: "Pump 1", path: "pumps/pump1" } );
		expect( described() ).toEqual( ["gw1/down", "gw1/other", "gw2/local"] );//an undescribable server is skipped, not fatal
		expect( queried() ).toEqual( ["gw2/local/ns=5;i=5005"] );
	} );

	it( 'remembers the placement, not a miss', async ()=>{
		const { resolver, getConnection } = create();
		await resolver.resolve( "opc.debug", "ns=5;i=5005" );
		await resolver.resolve( "opc.debug", "ns=5;i=9" );
		expect( getConnection ).toHaveBeenCalledTimes( 3 );//the second call reused gw2/local
		expect( await resolver.resolve("opc.nowhere", "ns=5;i=5005") ).toBeUndefined();
		await resolver.resolve( "opc.nowhere", "ns=5;i=5005" );
		expect( getConnection ).toHaveBeenCalledTimes( 9 );//a miss is looked up again - the server may be up next time
	} );

	it( 'gives no link for a node the gateway cannot place, or a schema that is not a server', async ()=>{
		const { resolver, queried } = create();
		expect( await resolver.resolve("opc.debug", "ns=5;i=9") ).toBeUndefined();
		expect( await resolver.resolve("access", "x") ).toBeUndefined();
		expect( queried() ).toEqual( ["gw2/local/ns=5;i=9"] );
	} );
} );

//reviews/m3-closing.md #29:  the cache dropped a miss but kept a REJECTED lookup, so one refused or failed request left every
//node of that server as plain text for the session, with no request ever sent again.  A gateway that refuses its connection
//list is now a miss for that gateway;  whatever else rejects is dropped from the cache.
describe( 'OpcNodeLinkResolver after a failed lookup', ()=>{
	//the resolver over two gateways;  `state.refuse` has gw1 answer its connection list with a 403, `state.down` fails the gateway
	//list itself, and `listConnections` is told of each connection list asked for
	const create = ()=>{
		const state = { refuse: false, down: false };
		const listConnections = vi.fn( async ( slug:string, connections:string[] )=>{
			if( slug=="gw1" && state.refuse ) throw new Error( "(403)[bob]User does not have 'Read' access to 'serverConnections'." );
			return connections.map( c=>({slug: c}) );
		} );
		const gateway = ( slug:string, connections:string[] )=>Object.assign( fakeGateway( slug ), {
			queryArray: ()=>listConnections( slug, connections ),
			querySingle: async ()=>({ name: "Pump 1", path: "pumps/pump1" })
		} );
		const gateways = [gateway( "gw1", ["other"] ), gateway( "gw2", ["local"] )];
		TestBed.configureTestingModule({ providers: [
			{ provide: GATEWAY_SERVICE, useValue: { gateways: async ()=>{ if( state.down ) throw new Error( "Failed to fetch" ); return gateways; } } },
			{ provide: OPC_STORE, useValue: { getConnection: async ( _g:any, cnnctn:string )=>({ accessResource: cnnctn=="local" ? "debug" : "other" }) } }
		]});
		return { resolver: TestBed.inject( OpcNodeLinkResolver ), state, listConnections };
	};
	const pump = { route: ['/gateways', 'gw2', 'local', 'pumps', 'pump1'], name: "Pump 1", path: "pumps/pump1" };

	it( 'a gateway that refuses its connections does not sink the next one', async ()=>{
		const { resolver, state } = create();
		state.refuse = true;
		const warn = silence();
		expect( await resolver.resolve("opc.debug", "ns=5;i=5005") ).toEqual( pump );
		expect( warn ).toHaveBeenCalled();
		warn.mockRestore();
	} );

	it( 'looks again once the gateway list answers', async ()=>{
		const { resolver, state, listConnections } = create();
		state.down = true;
		await expect( resolver.resolve("opc.debug", "ns=5;i=5005") ).rejects.toThrow( "Failed to fetch" );
		state.down = false;
		expect( await resolver.resolve("opc.debug", "ns=5;i=5005") ).toEqual( pump );
		expect( listConnections ).toHaveBeenCalledTimes( 2 );//a request went out this time
	} );
} );