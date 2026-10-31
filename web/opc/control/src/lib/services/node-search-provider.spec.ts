import { TestBed } from '@angular/core/testing';
import { Router } from '@angular/router';
import { GATEWAY_SERVICE } from './gateway-service';
import { OPC_STORE } from './opc-store';
import { NodeSearchProvider, NodeSearchRow } from './node-search-provider';
import { routerStub, silence } from 'jde-spa/testing';

describe( 'NodeSearchProvider.displayPath', ()=>{
	it( 'drops the namespace from every segment, as the breadcrumbs do', ()=>{
		expect( NodeSearchProvider.displayPath('5~pumps/5~pump1') ).toBe( 'pumps/pump1' );
		expect( NodeSearchProvider.displayPath('5~pumpManual') ).toBe( 'pumpManual' );
	});

	it( 'leaves a default-namespace name, and a ~ that is not a namespace, as they are', ()=>{
		expect( NodeSearchProvider.displayPath('Objects/pump~1') ).toBe( 'Objects/pump~1' );
	});
});

//reviews/m3-closing.md #33:  a connection whose index failed answered the search with nothing, which read as "no such node".
describe( 'NodeSearchProvider inside a connection', ()=>{
	//the provider over a gateway whose index fails, or refuses the user;  `described` is whether a node page has named the connection
	const create = ( {fail=false, refuse=false, described=true}:{fail?:boolean, refuse?:boolean, described?:boolean} = {} )=>{
		const gateway = { slug: "gw", queryArray: async ()=>{ if( refuse ) throw Object.assign( new Error("(403)User does not have Read access to gateway/search."), {status: 403} ); if( fail ) throw new Error( "Query failed. (80340000)BadNodeIdUnknown" ); return [{ connection:{slug:"kep", name:"External 6"}, path:"Channel1/Device1/Tag1", name:"Tag1", nodeClass:2, depth:3 }]; } };
		TestBed.configureTestingModule({ providers: [
			{ provide: Router, useValue: routerStub( "/gateways/gw/kep/Channel1" ) },
			{ provide: GATEWAY_SERVICE, useValue: { gateway: async ()=>gateway } },
			{ provide: OPC_STORE, useValue: {
				cnnctnName: ( g:string, c:string )=>described ? "External 6" : c,
				getConnection: async ()=>{ throw new Error( "the notice must not describe the connection" ); }//opc-server-search.md #9
			} }
		]});
		return TestBed.inject( NodeSearchProvider );
	};
	beforeEach( ()=>silence() );
	afterEach( ()=>vi.restoreAllMocks() );

	it( 'says the search is unavailable, with the error, as a notice that cannot be picked', async ()=>{
		const results = await create( {fail: true} ).search( "tag", undefined, 20 );
		expect( results ).toHaveLength( 1 );
		expect( results[0].title ).toBe( "Search is unavailable for External 6" );
		expect( results[0].summary ).toContain( "BadNodeIdUnknown" );
		expect( results[0].disabled ).toBe( true );
	} );

	it( 'falls back to the slug when no node page has described the connection', async ()=>{
		expect( (await create( {fail: true, described: false} ).search("tag", undefined, 20))[0].title ).toBe( "Search is unavailable for kep" );
	} );

	it( 'logs the raw error it turns into the notice', async ()=>{
		await create( {fail: true} ).search( "tag", undefined, 20 );
		expect( console.warn ).toHaveBeenCalledWith( expect.stringContaining("'kep'"), expect.objectContaining({message: expect.stringContaining("BadNodeIdUnknown")}) );
	} );

	it( 'says not permitted, not unavailable, for a refusal', async ()=>{
		const [notice] = await create( {refuse: true} ).search( "tag", undefined, 20 );
		expect( notice.title ).toBe( "Search is not permitted for External 6" );
		expect( notice.summary ).toContain( "Read access" );
		expect( notice.icon ).toBe( "block" );
		expect( notice.disabled ).toBe( true );
	} );

	it( 'answers hits, not a notice, when the index is fine', async ()=>{
		const results = await create().search( "tag", undefined, 20 );
		expect( results.map(r=>[r.title, r.disabled]) ).toEqual( [["Tag1", undefined]] );
	} );
} );

function gateway( slug:string, rows:NodeSearchRow[]|Error ){
	return { slug, queryArray: vi.fn( async ( _ql:string, _vars:any )=>{ if( rows instanceof Error ) throw rows; return rows; } ) };
}
const lamp:NodeSearchRow = { connection: {slug: 'cn1', name: 'Line 1'}, path: '4~Examples/4~Lamp 1', name: 'Lamp 1', nodeClass: 1, depth: 2 };
const rpm:NodeSearchRow = { connection: {slug: 'cn2', name: 'Line 2'}, path: 'pump1/motorRpm', name: 'motorRpm', nodeClass: 2, depth: 2 };

describe( 'NodeSearchProvider', ()=>{
	function setup( url:string, gateways:ReturnType<typeof gateway>[] ){
		TestBed.configureTestingModule({ providers: [
			{ provide: Router, useValue: routerStub( url ) },
			{ provide: GATEWAY_SERVICE, useValue: { gateway: async (t:string)=>gateways.find(g=>g.slug==t), gateways: async ()=>gateways } },
			{ provide: OPC_STORE, useValue: { cnnctnName: ( g:string, c:string )=>c } },
		]});
		return TestBed.inject( NodeSearchProvider );
	}

	it( 'inside a connection searches only that connection, and builds an encodable commands array', async ()=>{
		const gw = gateway( 'gw', [lamp] ), other = gateway( 'gw2', [rpm] );
		const results = await setup( '/gateways/gw/cn1/4~Examples?x=1', [gw, other] ).search( 'lamp', undefined, 5 );
		expect( gw.queryArray ).toHaveBeenCalledTimes( 1 );
		expect( gw.queryArray.mock.calls[0][1] ).toEqual( {opc: 'cn1', text: 'lamp', limit: 5} );
		expect( gw.queryArray.mock.calls[0][0] ).toContain( 'opc:$opc' );
		expect( other.queryArray ).not.toHaveBeenCalled();
		expect( results ).toHaveLength( 1 );
		expect( results[0] ).toMatchObject( { title: 'Lamp 1', summary: 'Line 1/Examples/Lamp 1', icon: 'folder', rank: 0, route: ['/gateways', 'gw', 'cn1', '4~Examples', '4~Lamp 1'] } );//the summary names segments as the breadcrumbs do (3703ff0e)
	});

	it( 'elsewhere fans out over every gateway without an opc, ignoring one that fails', async ()=>{
		const warn = silence();
		const gw = gateway( 'gw', [rpm] ), dead = gateway( 'gw2', new Error('unreachable') );
		const results = await setup( '/access/users', [gw, dead] ).search( 'rpm', undefined, 5 );
		expect( gw.queryArray.mock.calls[0][1] ).toEqual( {text: 'rpm', limit: 5} );
		expect( gw.queryArray.mock.calls[0][0] ).not.toContain( 'opc:' );
		expect( results.map(r=>r.icon) ).toEqual( ['label'] );
		expect( results[0].rank ).toBe( 1 );//'motorRpm' contains but does not start with 'rpm'
		expect( warn ).toHaveBeenCalled();
		warn.mockRestore();
	});

	//reviews/m3-closing.md #9:  a Variable (or a Method) has no page - it is a row on its parent's - so its hit opens the parent;
	//the summary still names the node itself.  One directly under Objects opens the connection.
	it( 'opens a variable\'s parent, naming the variable in the summary', async ()=>{
		const top:NodeSearchRow = { connection: {slug: 'cn2', name: 'Line 2'}, path: 'status', name: 'status', nodeClass: 2, depth: 1 };
		const results = await setup( '/access/users', [gateway( 'gw', [rpm, top] )] ).search( 'r', undefined, 5 );
		expect( results[0] ).toMatchObject( { title: 'motorRpm', summary: 'Line 2/pump1/motorRpm', route: ['/gateways', 'gw', 'cn2', 'pump1'] } );
		expect( results[1].route ).toEqual( ['/gateways', 'gw', 'cn2'] );
	});

	it( 'returns nothing for blank text without asking any gateway', async ()=>{
		const gw = gateway( 'gw', [rpm] );
		expect( await setup( '/', [gw] ).search( '', 'node', 5 ) ).toEqual( [] );
		expect( gw.queryArray ).not.toHaveBeenCalled();
	});

	//the search service hands the text over as typed (a node id's string identifier keeps its case), so the rank is case-blind here
	it( 'ranks a starts-with hit first whatever the case typed', async ()=>{
		const results = await setup( '/access/users', [gateway( 'gw', [lamp] )] ).search( 'LAMP', undefined, 5 );
		expect( results[0].rank ).toBe( 0 );
	});
});

//clients spec, Gaps #6:  a node id typed into the search, `ns=2;i=1001`, opens its page by the path the gateway walks up
//from the node to the Objects folder (NodeQLAwait's id form, what the node-scoped resources link with).
describe( 'NodeSearchProvider by node id', ()=>{
	//the provider on a connection's page, over a gateway that answers the id form with `node` - or throws it
	const create = ( node:{name?:string, nodeClass?:number, path:string|null}|Error, url='/gateways/gw/cn1/4~Examples' )=>{
		const gateway = { slug: 'gw', queryArray: vi.fn( async ()=>[lamp] ), querySingle: vi.fn( async ( _ql:string, _vars:any )=>{ if( node instanceof Error ) throw node; return node; } ) };
		TestBed.configureTestingModule({ providers: [
			{ provide: Router, useValue: routerStub( url ) },
			{ provide: GATEWAY_SERVICE, useValue: { gateway: async ()=>gateway, gateways: async ()=>[gateway] } },
			{ provide: OPC_STORE, useValue: { cnnctnName: ()=>'Line 1' } },
		]});
		return { provider: TestBed.inject( NodeSearchProvider ), gateway };
	};

	it( 'opens the page the gateway walks up to, and never asks the name search', async ()=>{
		const { provider, gateway } = create( {name: 'Lamp 1', nodeClass: 1, path: '4~Examples/4~Lamp 1'} );
		const results = await provider.search( 'ns=4;i=6020', undefined, 5 );
		expect( gateway.querySingle.mock.calls[0][0] ).toContain( 'id:$id' );
		expect( gateway.querySingle.mock.calls[0][1] ).toEqual( {opc: 'cn1', id: {ns: 4, i: 6020}} );
		expect( gateway.queryArray ).not.toHaveBeenCalled();
		expect( results ).toHaveLength( 1 );
		expect( results[0] ).toMatchObject( { title: 'Lamp 1', route: ['/gateways', 'gw', 'cn1', '4~Examples', '4~Lamp 1'], summary: 'Line 1/Examples/Lamp 1', icon: 'folder', rank: 0 } );
		expect( results[0].disabled ).toBeUndefined();
	});

	it( 'opens a variable\'s parent', async ()=>{
		const [hit] = await create( {name: 'motorRpm', nodeClass: 2, path: 'pump1/motorRpm'} ).provider.search( 'ns=2;i=1001', undefined, 5 );
		expect( hit ).toMatchObject( { title: 'motorRpm', route: ['/gateways', 'gw', 'cn1', 'pump1'], summary: 'Line 1/pump1/motorRpm', icon: 'label' } );
	});

	it( 'opens the connection itself for the Objects folder, whose path is empty', async ()=>{
		const [objects] = await create( {name: 'Objects', nodeClass: 1, path: ''} ).provider.search( 'i=85', undefined, 5 );
		expect( objects ).toMatchObject( { title: 'Objects', route: ['/gateways', 'gw', 'cn1'], summary: 'Line 1' } );
	});

	it( 'keeps a string identifier\'s case, and the prefix form', async ()=>{
		const { provider, gateway } = create( {name: 'Tank1', nodeClass: 1, path: 'Tank1'} );
		await provider.search( 'ns=2;s=Tank1', 'node', 5 );
		expect( gateway.querySingle.mock.calls[0][1] ).toEqual( {opc: 'cn1', id: {ns: 2, s: 'Tank1'}} );
	});

	it( 'says so, as a notice, for a node the gateway cannot place', async ()=>{
		const [notice] = await create( {path: null} ).provider.search( 'i=58', undefined, 5 );
		expect( notice ).toMatchObject( { title: 'No page for i=58', disabled: true, route: [] } );
		expect( notice.summary ).toContain( 'Line 1' );
	});

	it( 'says the search is unavailable when the lookup fails', async ()=>{
		silence();
		const [notice] = await create( new Error('Query failed. (80340000)BadNodeIdUnknown') ).provider.search( 'ns=4;i=6020', undefined, 5 );
		expect( notice ).toMatchObject( { title: 'Search is unavailable for Line 1', disabled: true } );
		expect( notice.summary ).toContain( 'BadNodeIdUnknown' );
		vi.restoreAllMocks();
	});

	it( 'asks for a connection\'s page outside one, asking no gateway', async ()=>{
		const { provider, gateway } = create( {path: 'x'}, '/access/users' );
		const [notice] = await provider.search( 'ns=4;i=6020', undefined, 5 );
		expect( notice ).toMatchObject( { title: 'Open a connection to look up ns=4;i=6020', disabled: true } );
		expect( gateway.querySingle ).not.toHaveBeenCalled();
		expect( gateway.queryArray ).not.toHaveBeenCalled();
	});

	it( 'leaves a name that is not an id to the name search', async ()=>{
		const { provider, gateway } = create( {path: 'x'} );
		await provider.search( 'lamp', undefined, 5 );
		expect( gateway.querySingle ).not.toHaveBeenCalled();
		expect( gateway.queryArray ).toHaveBeenCalledTimes( 1 );
	});
});