import { TestBed } from '@angular/core/testing';
import { ActivatedRouteSnapshot, convertToParamMap, Router, RouterStateSnapshot, UrlSegment } from '@angular/router';
import { vi } from 'vitest';
import { RecentVisits, RouteStore } from 'jde-spa';
import { SnackbarService } from 'jde-framework';
import { Resource } from '../model/resource';
import { AccessService } from './access-service';
import { ResourceResolver } from './resource-resolver';

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
	let navigateByUrl:any, error:any, forget:any, setChildren:any;
	let resolver:ResourceResolver;
	beforeEach( ()=>{
		navigateByUrl = vi.fn().mockResolvedValue( true );
		error = vi.fn();
		forget = vi.fn();
		setChildren = vi.fn();
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({ providers: [
			ResourceResolver,
			{ provide: AccessService, useValue: {loadResources: async ()=>resources} },
			{ provide: Router, useValue: {navigateByUrl} },
			{ provide: SnackbarService, useValue: {error, exception: vi.fn()} },
			{ provide: RecentVisits, useValue: {forget} },
			{ provide: RouteStore, useValue: {setChildren} }
		]});
		resolver = TestBed.inject( ResourceResolver );
	} );

	it( 'resolves a slug two schemas share by its schema', async ()=>{
		const data = await resolver.resolve( snapshot("opc.debug", "logs"), {url: "/access/resources/opc.debug/logs"} as RouterStateSnapshot );
		expect( data.resource.id ).toBe( 18 );
		expect( data.accessUrl ).toBe( "/access" );
		expect( data.routing.parent!.path ).toBe( "/access/resources" );
		expect( data.routing.path ).toBe( "opc.debug/logs" );
		expect( setChildren ).toHaveBeenCalledWith( "access/resources/opc.debug", [{title: "logs", path: "logs"}] );//the last crumb's name
	} );

	it( "lists the table resources in the list's order, a shared name with its schema", async ()=>{
		const data = await resolver.resolve( snapshot("app", "logs"), {url: "/access/resources/app/logs"} as RouterStateSnapshot );
		expect( data.routing.siblings!.map( s=>[s.path, s.title] ) ).toEqual( [["access/acl", "acl"], ["app/logs", "logs (app)"], ["opc.debug/logs", "logs (opc.debug)"]] );
	} );

	it( 'sends an unknown resource back to the list, and forgets the visit', async ()=>{
		const data = await resolver.resolve( snapshot("access", "logs"), {url: "/access/resources/access/logs"} as RouterStateSnapshot );
		expect( data ).toBeNull();
		expect( error ).toHaveBeenCalledWith( "There is no resource 'access/logs'." );
		expect( navigateByUrl ).toHaveBeenCalledWith( "/access/resources" );
		expect( forget ).toHaveBeenCalled();
	} );
} );