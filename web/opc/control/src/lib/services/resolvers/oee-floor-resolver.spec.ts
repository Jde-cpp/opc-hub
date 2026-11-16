import { TestBed } from '@angular/core/testing';
import { RouteStore } from 'jde-spa';
import { AppService, CalendarService } from 'jde-framework';
import { GATEWAY_SERVICE } from '../gateway-service';
import { OeeFloorService } from '../oee-floor-service';
import { OeeFloorPageData, OeeFloorResolver } from './oee-floor-resolver';
import { Blank } from 'jde-spa/testing';
import { openRoute } from 'jde-framework/testing';

const Plant = { id: 10, name: 'Plant', slug: 'plant', level: 'Site', calendarId: 2 }, Loose = { id: 11, name: 'Loose line', slug: 'loose', level: 'Line' };
const floor = { id: 1, lineId: 10, width: 40, height: 20, grid: 0.5 };
//The route behind a harness:  `service` answers the line by its slug, the floors, the floor's shapes and the plant;  the
//calendars are the application server's, or a refusal of them.
async function open( url:string, {calendars=<unknown>[{id: 2, name: 'plant', slug: 'plant', timeZone: 'America/New_York', dayStart: 360}], gatewayDown=false}:{calendars?:unknown; gatewayDown?:boolean}={} ){
	const gateway = { slug: 'gw', name: 'gw' };
	const service = {
		line: vi.fn( async ( _g:unknown, slug:string )=>slug=='plant' ? Plant : slug=='loose' ? Loose : undefined ),
		floors: vi.fn( async ()=>[floor] ),
		floorShapes: vi.fn( async ( _g:unknown, id:number )=>id==1 ? [{id: 5, floorId: 1, kind: <const>'Machine', machineId: 1, points: <[number, number][]>[[3, 3], [9, 3], [9, 7], [3, 7]], sort: 0}] : [] ),
		plant: vi.fn( async ()=>({ machines: [{id: 1, name: 'Filler', slug: 'filler'}], lines: [Plant, Loose], members: [{id: 1, lineId: 10, machineId: 1, sort: 0}] }) )
	};
	const calendarService = { calendars: vi.fn( async ()=>{ if( calendars instanceof Error ) throw calendars; return calendars; } ) };
	const routeStore = { getChildren: vi.fn( ( url:string )=>url=='apps/gateways/gw/lines' ? [{path: 'plant', title: 'Plant'}, {path: 'loose', title: 'Loose line'}] : [] ) };
	const opened = await openRoute( {routes: [{ path: 'apps/gateways/:instance', children: [
		{ path: '', component: Blank },
		{ path: 'lines', pathMatch: 'full', redirectTo: '' },//as the app's:  the list is a tab of the gateway's page
		{ path: 'lines/:slug/oee', component: Blank },
		{ path: 'lines/:slug/floor', component: Blank, providers: [OeeFloorResolver], resolve: {pageData: OeeFloorResolver}, runGuardsAndResolvers: 'paramsChange' }
	]}], providers: [
		{ provide: GATEWAY_SERVICE, useValue: {gateway: async ()=>{ if( gatewayDown ) throw new Error( 'Failed to fetch' ); return gateway; }} },
		{ provide: OeeFloorService, useValue: service }, { provide: AppService, useValue: {} }, { provide: CalendarService, useValue: calendarService }, { provide: RouteStore, useValue: routeStore }
	], url} );
	return { ...opened, service, calendarService, data: ()=><OeeFloorPageData>opened.leaf().data['pageData'] };
}

describe( 'OeeFloorResolver', ()=>{
	it( "resolves the line, its floor, the shapes it can draw, the plant and the calendar's zone, with the floors' pages as siblings", async ()=>{
		const p = await open( '/apps/gateways/gw/lines/plant/floor' );
		expect( p.router.url ).toBe( '/apps/gateways/gw/lines/plant/floor' );
		const d = p.data();
		expect( [d.line.name, d.floor, d.zone, d.calendar?.slug] ).toEqual( ['Plant', floor, 'America/New_York', 'plant'] );
		expect( d.shapes.map( s=>[s.kind, s.machineId, s.points] ) ).toEqual( [['Machine', 1, [[3, 3], [9, 3], [9, 7], [3, 7]]]] );//the floor's, as the service reads them
		expect( d.plant.machines.map( m=>m.slug ) ).toEqual( ['filler'] );
		expect( [d.routing.path, d.routing.title, d.routing.siblings?.map( s=>s.path ), d.routing.parent?.title] ).toEqual( ['lines/plant/floor', 'Plant', ['lines/plant/floor'], 'gw'] );//the loose line has no floor, so no sibling
		expect( p.service.floorShapes.mock.calls[0][1] ).toBe( 1 );
	} );
	it( 'reads UTC for a line with no calendar, and for one whose calendars the application server refuses', async ()=>{
		const p = await open( '/apps/gateways/gw/lines/plant/floor', {calendars: new Error( '(403)refused' )} );
		expect( [p.data().zone, p.data().calendar] ).toEqual( ['UTC', undefined] );
		expect( p.calendarService.calendars ).toHaveBeenCalledTimes( 1 );
	} );
	it( "lands a line with no floor on its OEE page, saying so", async ()=>{
		const p = await open( '/apps/gateways/gw/lines/loose/floor' );
		expect( p.router.url ).toBe( '/apps/gateways/gw/lines/loose/oee' );
		expect( p.snackbar.info ).toHaveBeenCalledWith( "'Loose line' has no floor." );
		expect( p.service.floorShapes ).not.toHaveBeenCalled();
	} );
	it( "lands a slug that is no line's on the gateway's page, forgetting the visit, and a gateway that doesn't answer too", async ()=>{
		const p = await open( '/apps/gateways/gw/lines/nothing/floor' );
		expect( p.router.url ).toBe( '/apps/gateways/gw' );
		expect( p.error ).toHaveBeenCalledWith( expect.stringContaining( 'nothing' ) );
		expect( p.forget ).toHaveBeenCalledWith( '/apps/gateways/gw/lines/nothing/floor' );
		TestBed.resetTestingModule();//a second route, in the same test
		const down = await open( '/apps/gateways/gw/lines/plant/floor', {gatewayDown: true} );
		expect( down.router.url ).toBe( '/apps/gateways/gw' );
		expect( down.snackbar.exception ).toHaveBeenCalledWith( "Could not load the floor of 'plant'", expect.any( Error ) );
	} );
} );