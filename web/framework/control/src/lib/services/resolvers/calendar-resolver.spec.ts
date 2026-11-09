import { TestBed } from '@angular/core/testing';
import { RouteItem, RouteStore } from 'jde-spa';
import { AppService } from '../app/app-service';
import { CalendarResolver } from './calendar-resolver';
import { Blank } from 'jde-spa/testing';
import { openRoute, slugQl } from 'jde-framework/testing';

//#396:  a calendar's page is under an instance's page, the application server's or a hub's, whose Calendars tab keeps its rows
//under `<page url>/calendars`.
describe( 'CalendarResolver', ()=>{
	afterEach( ()=>localStorage.clear() );
	const open = ( url:string, querySingle:()=>Promise<unknown>, before?:()=>void )=>{
		const app = slugQl( vi.fn( querySingle ), {collection: 'calendars', type: 'Calendar'} );
		const page = ( path:string )=>({ path, children: [
			{ path: '', component: Blank },
			{ path: 'calendars', pathMatch: <const>'full', redirectTo: '' },
			{ path: 'calendars/:slug', component: Blank, providers: [CalendarResolver], resolve: {pageData: CalendarResolver} }
		]});
		return openRoute( {routes: [page( 'apps/appServers/:instance' ), page( 'apps/gateways/:instance' )], providers: [{ provide: AppService, useValue: app }], url, before} ).then( opened=>({...opened, app}) );
	};

	it( "reads the row from the application server, with the tab's rows as its siblings under the page", async ()=>{
		const { leaf, app } = await open( '/apps/appServers/main/calendars/plant', async ()=>({id: 3, name: 'Plant', slug: 'plant', timeZone: 'America/New_York', dayStart: 360}),
			()=>TestBed.inject( RouteStore ).setChildren( 'apps/appServers/main/calendars', [{path: 'plant', title: 'Plant'}, {path: 'line2', title: 'Line 2'}] ) );
		const { row, routing } = leaf().data['pageData'];
		expect( row ).toMatchObject( {id: 3, timeZone: 'America/New_York', dayStart: 360} );
		expect( app.querySingle ).toHaveBeenCalledWith( 'calendar(slug:"temp")', {} );//the slug query of the Calendar type
		expect( routing.path ).toBe( 'calendars/plant' );
		expect( routing.title ).toBe( 'Plant' );
		expect( routing.siblings.map( ( s:RouteItem )=>s.path ) ).toEqual( ['calendars/plant', 'calendars/line2'] );
		expect( routing.parent ).toMatchObject( {path: '.', title: 'main'} );//the page itself:  a sibling link is ./calendars/<slug> from it
		expect( routing.tableSettings.noun ).toBe( 'calendars' );
	} );
	//a hub's page is its gateway's, and its calendars are under it
	it( "resolves under a hub's page too, from that page's rows", async ()=>{
		const { leaf } = await open( '/apps/gateways/hub/calendars/plant', async ()=>({id: 3, name: 'Plant', slug: 'plant'}),
			()=>TestBed.inject( RouteStore ).setChildren( 'apps/gateways/hub/calendars', [{path: 'plant', title: 'Plant'}] ) );
		expect( leaf().data['pageData'].routing.siblings.map( ( s:RouteItem )=>s.path ) ).toEqual( ['calendars/plant'] );
		expect( leaf().data['pageData'].routing.parent.title ).toBe( 'hub' );
	} );
	it( 'opens a new calendar without a query', async ()=>{
		const { leaf, app } = await open( '/apps/appServers/main/calendars/$new', async ()=>null );
		expect( leaf().data['pageData'].row ).toEqual( {} );
		expect( app.querySingle ).not.toHaveBeenCalled();
	} );
	it( "forgets a calendar that is gone, and goes back to the page", async ()=>{
		const { router, forget, error } = await open( '/apps/appServers/main/calendars/gone', async ()=>null );
		expect( error ).toHaveBeenCalledWith( "Slug not found:  'gone'" );
		expect( forget ).toHaveBeenCalledWith( '/apps/appServers/main/calendars/gone' );
		expect( router.url ).toBe( '/apps/appServers/main' );//the bare list url redirects to its tab's page
	} );
	it( 'keeps the page in Recently visited when the query merely failed', async ()=>{
		const { router, forget, snackbar } = await open( '/apps/appServers/main/calendars/plant', async ()=>{ throw new Error( '(500)server error' ); } );
		expect( snackbar.exception ).toHaveBeenCalled();
		expect( forget ).not.toHaveBeenCalled();
		expect( router.url ).toBe( '/apps/appServers/main' );
	} );
} );