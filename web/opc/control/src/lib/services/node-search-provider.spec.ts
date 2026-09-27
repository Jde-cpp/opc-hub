import { TestBed } from '@angular/core/testing';
import { Router } from '@angular/router';
import { GATEWAY_SERVICE } from './gateway-service';
import { OPC_STORE } from './opc-store';
import { NodeSearchProvider } from './node-search-provider';

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
	let fail:boolean;
	let refuse:boolean;
	let described:boolean;
	const gateway = { slug: "gw", queryArray: async ()=>{ if( refuse ) throw Object.assign( new Error("(403)User does not have Read access to gateway/search."), {status: 403} ); if( fail ) throw new Error( "Query failed. (80340000)BadNodeIdUnknown" ); return [{ connection:{slug:"kep", name:"External 6"}, path:"Channel1/Device1/Tag1", name:"Tag1", nodeClass:2, depth:3 }]; } };
	let provider:NodeSearchProvider;
	beforeEach( ()=>{
		fail = refuse = false; described = true;
		vi.spyOn( console, 'warn' ).mockImplementation( ()=>{} );
		TestBed.configureTestingModule({ providers: [
			{ provide: Router, useValue: { url: "/gateways/gw/kep/Channel1" } },
			{ provide: GATEWAY_SERVICE, useValue: { gateway: async ()=>gateway } },
			{ provide: OPC_STORE, useValue: {
				cnnctnName: ( g:string, c:string )=>described ? "External 6" : c,
				getConnection: async ()=>{ throw new Error( "the notice must not describe the connection" ); }//opc-server-search.md #9
			} }
		]});
		provider = TestBed.inject( NodeSearchProvider );
	} );
	afterEach( ()=>vi.restoreAllMocks() );

	it( 'says the search is unavailable, with the error, as a notice that cannot be picked', async ()=>{
		fail = true;
		const results = await provider.search( "tag", undefined, 20 );
		expect( results ).toHaveLength( 1 );
		expect( results[0].title ).toBe( "Search is unavailable for External 6" );
		expect( results[0].summary ).toContain( "BadNodeIdUnknown" );
		expect( results[0].disabled ).toBe( true );
	} );

	it( 'falls back to the slug when no node page has described the connection', async ()=>{
		fail = true; described = false;
		expect( (await provider.search("tag", undefined, 20))[0].title ).toBe( "Search is unavailable for kep" );
	} );

	it( 'logs the raw error it turns into the notice', async ()=>{
		fail = true;
		await provider.search( "tag", undefined, 20 );
		expect( console.warn ).toHaveBeenCalledWith( expect.stringContaining("'kep'"), expect.objectContaining({message: expect.stringContaining("BadNodeIdUnknown")}) );
	} );

	it( 'says not permitted, not unavailable, for a refusal', async ()=>{
		refuse = true;
		const [notice] = await provider.search( "tag", undefined, 20 );
		expect( notice.title ).toBe( "Search is not permitted for External 6" );
		expect( notice.summary ).toContain( "Read access" );
		expect( notice.icon ).toBe( "block" );
		expect( notice.disabled ).toBe( true );
	} );

	it( 'answers hits, not a notice, when the index is fine', async ()=>{
		const results = await provider.search( "tag", undefined, 20 );
		expect( results.map(r=>[r.title, r.disabled]) ).toEqual( [["Tag1", undefined]] );
	} );
} );
