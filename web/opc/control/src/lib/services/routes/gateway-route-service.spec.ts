import { TestBed } from '@angular/core/testing';
import { UrlSegment } from '@angular/router';
import { RouteStore } from 'jde-spa';
import { GATEWAY_SERVICE } from '../gateway-service';
import { GatewayCnnctnRouteService, GatewayRouteService } from './gateway-route-service';

//A18:  the two tile lists the cards pages read off a service, each kept in RouteStore under the page's url.
describe( 'Gateway route services', ()=>{
	const segments = ( ...paths:string[] )=>paths.map( p=>new UrlSegment(p, {}) );
	const configure = ()=>{
		const queryArray = vi.fn( async ()=>[{name: 'Line 1', slug: 'cn1'}] );
		const gateway = vi.fn( async ( slug:string )=>({slug, queryArray}) );
		const setChildren = vi.fn();
		TestBed.configureTestingModule({ providers: [
			{ provide: GATEWAY_SERVICE, useValue: {gateways: async ()=>[{slug: 'gw1', name: 'Plant'}], gateway} },
			{ provide: RouteStore, useValue: {setChildren} }
		]});
		return { gateway, setChildren };
	};

	it( 'lists the gateways', async ()=>{
		const { setChildren } = configure();
		const url = segments( 'gateways' );
		const items = await TestBed.inject( GatewayRouteService ).docItems( url );
		expect( items.map( i=>[i.path, i.title, i.icon] ) ).toEqual( [['gw1', 'Plant', 'router']] );
		expect( setChildren ).toHaveBeenCalledWith( url, items );
	} );

	it( 'lists the connections of the gateway the url names', async ()=>{
		const { gateway, setChildren } = configure();
		const url = segments( 'gateways', 'gw1' );
		const items = await TestBed.inject( GatewayCnnctnRouteService ).docItems( url );
		expect( gateway ).toHaveBeenCalledWith( 'gw1' );
		expect( items.map( i=>[i.path, i.title, i.icon] ) ).toEqual( [['cn1', 'Line 1', 'lan']] );
		expect( setChildren ).toHaveBeenCalledWith( url, items );
	} );
} );