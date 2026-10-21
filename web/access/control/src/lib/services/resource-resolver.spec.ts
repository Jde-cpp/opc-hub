import { TestBed } from '@angular/core/testing';
import { ActivatedRouteSnapshot, convertToParamMap, Router, RouterStateSnapshot, UrlSegment } from '@angular/router';
import { RecentVisits, RouteItem, RouteStore } from 'jde-spa';
import { SnackbarService } from 'jde-framework';
import { Resource } from '../model/resource';
import { AccessService } from './access-service';
import { ResourceResolver } from './resource-resolver';
import { routerStub } from 'jde-spa/testing';
import { snackbarStub } from 'jde-framework/testing';

//#251:  `logs` is app's and every OpcServer's, so the page is keyed on schema and slug.
const resources = [
	new Resource( {id: 17, schemaName: "app", slug: "logs", name: "logs"} ),
	new Resource( {id: 1, schemaName: "access", slug: "acl", name: "acl"} ),
	new Resource( {id: 18, schemaName: "opc.debug", slug: "logs", name: "logs"} )
];
const snapshot = ( schema:string, slug:string )=>({
	paramMap: convertToParamMap( {schema, slug} ),
	parent: { url: [new UrlSegment("access", {})] }
}) as unknown as ActivatedRouteSnapshot;

describe( 'ResourceResolver', ()=>{
	const create = ()=>{
		const router = routerStub(), snackbar = snackbarStub();
		const forget = vi.fn(), setChildren = vi.fn(), loadResources = vi.fn().mockResolvedValue( resources );
		TestBed.configureTestingModule({ providers: [
			ResourceResolver,
			{ provide: AccessService, useValue: {loadResources} },
			{ provide: Router, useValue: router },
			{ provide: SnackbarService, useValue: snackbar },
			{ provide: RecentVisits, useValue: {forget} },
			{ provide: RouteStore, useValue: {setChildren} }
		]});
		return { resolver: TestBed.inject( ResourceResolver ), navigateByUrl: router.navigateByUrl, error: snackbar.error, forget, setChildren, loadResources };
	};

	it( 'resolves a slug two schemas share by its schema', async ()=>{
		const { resolver, setChildren } = create();
		const data = await resolver.resolve( snapshot("opc.debug", "logs"), {url: "/access/resources/opc.debug/logs"} as RouterStateSnapshot );
		expect( data.resource.id ).toBe( 18 );
		expect( data.accessUrl ).toBe( "/access" );
		expect( data.routing.parent!.path ).toBe( "/access/resources" );
		expect( data.routing.path ).toBe( "opc.debug/logs" );
		//reviews-issue/251-resource-page.md #13:  one entry under the list's url, not one per schema
		expect( setChildren ).toHaveBeenCalledTimes( 1 );
		expect( RouteStore.key(setChildren.mock.calls[0][0]) ).toBe( "access/resources" );
		expect( setChildren.mock.calls[0][1].map( (c:RouteItem)=>[c.path, c.title] ) ).toContainEqual( ["opc.debug/logs", "logs (opc.debug)"] );
	} );

	it( "lists the table resources in the list's order, a shared name with its schema", async ()=>{
		const { resolver } = create();
		const data = await resolver.resolve( snapshot("app", "logs"), {url: "/access/resources/app/logs"} as RouterStateSnapshot );
		expect( data.routing.siblings!.map( s=>[s.path, s.title] ) ).toEqual( [["access/acl", "acl"], ["app/logs", "logs (app)"], ["opc.debug/logs", "logs (opc.debug)"]] );
	} );

	it( 'sends an unknown resource back to the list, and forgets the visit', async ()=>{
		const { resolver, loadResources, error, navigateByUrl, forget } = create();
		const data = await resolver.resolve( snapshot("access", "logs"), {url: "/access/resources/access/logs"} as RouterStateSnapshot );
		expect( data ).toBeNull();
		expect( loadResources.mock.calls ).toEqual( [[], [true]] );//asked the server before saying so
		expect( error ).toHaveBeenCalledWith( "There is no resource 'access/logs'." );
		expect( navigateByUrl ).toHaveBeenCalledWith( "/access/resources" );
		expect( forget ).toHaveBeenCalled();
	} );

	//reviews-issue/251-resource-page.md #2:  the cached list predates a resource registered since - the Resources list, which
	//queries fresh, showed the row, and clicking it said there was no such resource.
	it( 'refreshes the cached list for a resource registered since', async ()=>{
		const { resolver, loadResources, error } = create();
		const added = new Resource( {id: 30, schemaName: "opc.new", slug: "nodeIds", name: "nodeIds"} );
		loadResources.mockImplementation( async ( refresh?:boolean )=>refresh ? [...resources, added] : resources );
		const data = await resolver.resolve( snapshot("opc.new", "nodeIds"), {url: "/access/resources/opc.new/nodeIds"} as RouterStateSnapshot );
		expect( data.resource.id ).toBe( 30 );
		expect( data.routing.siblings!.map( s=>s.path ) ).toContain( "opc.new/nodeIds" );
		expect( error ).not.toHaveBeenCalled();
	} );

	it( 'keeps the cached list for a resource it holds', async ()=>{
		const { resolver, loadResources } = create();
		await resolver.resolve( snapshot("access", "acl"), {url: "/access/resources/access/acl"} as RouterStateSnapshot );
		expect( loadResources.mock.calls ).toEqual( [[]] );
	} );
} );