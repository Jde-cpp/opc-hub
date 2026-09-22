if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { TestBed } from '@angular/core/testing';
import { ActivatedRoute, ActivatedRouteSnapshot, Router, RouterStateSnapshot } from '@angular/router';
import { HttpErrorResponse } from '@angular/common/http';
import { vi } from 'vitest';
import { ProfileStore, RouteStore } from 'jde-spa';
import { AppInstanceRoute, PageProfile, SnackbarService, TableSchema, View } from 'jde-framework';
import { Gateway, GATEWAY_SERVICE } from '../gateway-service';
import { gatewayTableSettings } from '../../pages/gateway-detail/gateway-detail';
import { GatewayData, GatewayResolver } from './gateway-resolver';

//reviews/m3-closing.md #6:  the resolver never adopted QLListResolver.loadOrFail.  A refused or failed serverConnections query
//rejected the resolve, the router turned it into a NavigationError nobody saw, and the Connections page - where the help says
//a new user adds a server - was a dead click:  the url never changed and nothing was said.
describe( 'GatewayResolver', ()=>{
	const schema = new TableSchema( {name: "ServerConnection", fields: [
		{name: "id", type: {name: "ID", kind: "SCALAR"}},
		{name: "name", type: {name: "String", kind: "SCALAR"}},
		{name: "slug", type: {name: "String", kind: "SCALAR"}}
	]} as any );
	const data = ():GatewayData=>{
		const profile = new PageProfile();
		profile.views = [ new View({columns: ["name"], sort: "name"}, schema) ];
		profile.showDeleted = false;
		return { schema, profile, routing: new AppInstanceRoute("gateways", "opchub", gatewayTableSettings), columns: {}, pageSettings: {} as any, results: undefined };
	};
	const routeStore = ()=>({ setChildren: vi.fn(), getChildren: ()=>[] }) as unknown as RouteStore;

	it( "turns a refused rows query into the Connections tab's no-access state", async ()=>{
		const refused = new HttpErrorResponse( {status: 403, error: "User does not have 'Read' access to 'serverConnections'."} );
		const store = routeStore();
		const ql = <unknown>{ query: async ()=>{ throw refused; } } as Gateway;
		const y:any = await GatewayResolver.load( ql, data(), store, "apps/gateways/opchub" );
		expect( y.error ).toBe( refused );
		expect( y.results.serverConnections ).toEqual( [] );
		expect( store.setChildren ).not.toHaveBeenCalled();//no rows is not an empty sidenav to remember
	} );

	it( 'loads the rows and the sidenav children otherwise', async ()=>{
		const store = routeStore();
		const ql = <unknown>{ query: async ()=>({serverConnections: [{name: "Line 1", slug: "line1"}]}) } as Gateway;
		const y:any = await GatewayResolver.load( ql, data(), store, "apps/gateways/opchub" );
		expect( y.error ).toBeUndefined();
		expect( y.results.serverConnections ).toHaveLength( 1 );
		expect( store.setChildren ).toHaveBeenCalledWith( "apps/gateways/opchub", [{title: "Line 1", path: "line1"}] );
	} );

	//no gateway of that name - a stale bookmark or Recently-visited tile - leaves no page to say it on
	it( 'says so and lands on Applications when the instance cannot be opened', async ()=>{
		const exception = vi.fn(), navigateByUrl = vi.fn();
		TestBed.configureTestingModule({ providers: [
			GatewayResolver,
			{ provide: ActivatedRoute, useValue: {} },
			{ provide: Router, useValue: {navigateByUrl} },
			{ provide: SnackbarService, useValue: {exception, error: vi.fn()} },
			{ provide: GATEWAY_SERVICE, useValue: {gateway: async ( name:string )=>{ throw new Error(`Gateway '${name}' not found.`); }} },
			{ provide: RouteStore, useValue: routeStore() },
			{ provide: ProfileStore, useValue: {} }
		]});
		const route = <unknown>{ params: {instance: "gone"}, data: {tableSettings: gatewayTableSettings}, parent: {url: [{path: "apps"}, {path: "gateways"}, {path: "gone"}]} } as ActivatedRouteSnapshot;
		await TestBed.inject( GatewayResolver ).resolve( route, {} as RouterStateSnapshot ).catch( ()=>{} );
		expect( exception ).toHaveBeenCalledWith( "Could not open gone.", expect.any(Error) );
		expect( navigateByUrl ).toHaveBeenCalledWith( "/apps" );
	} );

	//the state's words came from the route title - "No access to gateways/opchub." - which is not something to ask for
	it( 'names server connections, not the instance path', ()=>{
		expect( (gatewayTableSettings as any).noun ).toBe( "server connections" );
	} );
} );
