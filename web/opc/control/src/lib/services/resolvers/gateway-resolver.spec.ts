import { TestBed } from '@angular/core/testing';
import { ActivatedRouteSnapshot, Router, RouterStateSnapshot } from '@angular/router';
import { HttpErrorResponse } from '@angular/common/http';
import { LocalPrefs, ProfileStore, RouteStore } from 'jde-spa';
import { AppInstanceRoute, QLListResolver, SnackbarService, View } from 'jde-framework';
import { Gateway, GATEWAY_SERVICE } from '../gateway-service';
import { gatewayTableSettings } from '../../pages/gateway-detail/gateway-table-settings';
import { GatewayData, GatewayResolver } from './gateway-resolver';
import { fakeSessionStorage, provideRoute, routerStub, profileStoreStub } from 'jde-spa/testing';
import { qlAnswering, scalar, schemaOf, snackbarStub } from 'jde-framework/testing';

//reviews/m3-closing.md #6:  the resolver never adopted QLListResolver.loadOrFail.  A refused or failed serverConnections query
//rejected the resolve, the router turned it into a NavigationError nobody saw, and the Connections page - where the help says
//a new user adds a server - was a dead click:  the url never changed and nothing was said.
describe( 'GatewayResolver', ()=>{
	const schema = schemaOf( "ServerConnection", scalar("id", "ID"), scalar("name"), scalar("slug") );
	const data = ():GatewayData=>{
		const profile = QLListResolver.viewSet( "serverConnections", {} as ProfileStore, new LocalPrefs(), gatewayTableSettings );
		profile.reset( [View.fromTable({columns: ["name"], sort: "name"}, schema)] );
		return { schema, profile, routing: new AppInstanceRoute("gateways", "opchub", gatewayTableSettings), columns: {}, results: undefined };
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
		expect( y.childrenKey ).toBe( "apps/gateways/opchub" );//for the tab's Retry
	} );

	it( 'loads the rows and the sidenav children otherwise', async ()=>{
		const store = routeStore();
		const ql = <unknown>{ query: qlAnswering( {serverConnections: [{name: "Line 1", slug: "line1"}]} ) } as Gateway;
		const y:any = await GatewayResolver.load( ql, data(), store, "apps/gateways/opchub" );
		expect( y.error ).toBeUndefined();
		expect( y.results.serverConnections ).toHaveLength( 1 );
		expect( store.setChildren ).toHaveBeenCalledWith( "apps/gateways/opchub", [{title: "Line 1", path: "line1"}] );
		expect( y.childrenKey ).toBe( "apps/gateways/opchub" );//reviews-issue/231-paging.md #5:  the tab's page turns write there too, not over routing.path - the browse tree's gateways/opchub
	} );

	//#231:  the tab pages through QLListResolver.load, which asks for a row past the page;  the first page the resolver loads
	//has to be cut the same way, or it would show 26 rows and then page to the 26th again
	it( 'loads one page, and says whether there is another', async ()=>{
		const all = Array.from( {length: 30}, (_, i)=>({name: `Line ${i+1}`, slug: `line${i+1}`}) );
		const query = qlAnswering( {serverConnections: all} );
		const y:any = await GatewayResolver.load( <unknown>{query} as Gateway, data(), routeStore(), "apps/gateways/opchub" );
		expect( query.mock.calls[0][0] ).toContain( "limit:26" );
		expect( y.results.serverConnections ).toHaveLength( 25 );
		expect( y.more ).toBe( true );
		expect( y.skip ).toBe( 0 );
	} );

	//reviews-issue/231-paging.md #10:  the tab loads through QLListResolver.loadOrFail, so it opens on the page it was left on (#8)
	it( 'opens the tab on the page it was left on', async ()=>{
		fakeSessionStorage().set( "apps/gateways/opchub/skip", "25" );
		try{
			const all = Array.from( {length: 30}, (_, i)=>({name: `Line ${i+1}`, slug: `line${i+1}`}) );
			const query = qlAnswering( {serverConnections: all} );
			const y:any = await GatewayResolver.load( <unknown>{query} as Gateway, data(), routeStore(), "apps/gateways/opchub" );
			expect( query.mock.calls[0][0] ).toContain( "skip:25" );
			expect( y.skip ).toBe( 25 );
			expect( y.results.serverConnections.map( (r:any)=>r.slug ) ).toEqual( ["line26", "line27", "line28", "line29", "line30"] );
		}
		finally{
			vi.unstubAllGlobals();
		}
	} );

	//no gateway of that name - a stale bookmark or Recently-visited tile - leaves no page to say it on
	it( 'says so and lands on Applications when the instance cannot be opened', async ()=>{
		const router = routerStub(), snackbar = snackbarStub();
		TestBed.configureTestingModule({ providers: [
			GatewayResolver,
			provideRoute(),
			{ provide: Router, useValue: router },
			{ provide: SnackbarService, useValue: snackbar },
			{ provide: GATEWAY_SERVICE, useValue: {gateway: async ( name:string )=>{ throw new Error(`Gateway '${name}' not found.`); }} },
			{ provide: RouteStore, useValue: routeStore() },
			{ provide: ProfileStore, useValue: profileStoreStub() }
		]});
		const route = <unknown>{ params: {instance: "gone"}, data: {tableSettings: gatewayTableSettings}, parent: {url: [{path: "apps"}, {path: "gateways"}, {path: "gone"}]} } as ActivatedRouteSnapshot;
		await TestBed.inject( GatewayResolver ).resolve( route, {} as RouterStateSnapshot ).catch( ()=>{} );
		expect( snackbar.exception ).toHaveBeenCalledWith( "Could not open gone.", expect.any(Error) );
		expect( router.navigateByUrl ).toHaveBeenCalledWith( "/apps" );
	} );

	//the state's words came from the route title - "No access to gateways/opchub." - which is not something to ask for
	it( 'names server connections, not the instance path', ()=>{
		expect( (gatewayTableSettings as any).noun ).toBe( "server connections" );
	} );
} );