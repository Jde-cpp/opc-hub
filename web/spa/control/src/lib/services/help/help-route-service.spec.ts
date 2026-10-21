import { TestBed } from '@angular/core/testing';
import { ActivatedRouteSnapshot, convertToParamMap } from '@angular/router';
import { RouteStore } from '../route-store';
import { HELP_TOPICS, HelpTopic } from './help-topic';
import { HelpRouteService, helpTopicResolver } from './help-route-service';
import { provideRoute } from 'jde-spa/testing';

const overview:HelpTopic = { id: 'overview', title: 'Overview', summary: 'What this is', icon: 'menu_book', url: 'assets/site/help/overview.md' };
const gateways:HelpTopic = { id: 'gateways', title: 'Gateways', icon: 'hub', url: 'assets/jde-opc/help/gateways.md', routes: ['gateways'] };

const configure = ()=>{
	TestBed.configureTestingModule({ providers: [
		HelpRouteService,
		provideRoute(),//RouteService injects it; nothing here reads it
		{ provide: HELP_TOPICS, useValue: [overview], multi: true },
		{ provide: HELP_TOPICS, useValue: [gateways], multi: true }
	]});
};

describe( 'HelpRouteService', ()=>{
	it( 'lists the registered topics as tiles, in registration order', async ()=>{
		configure();
		const items = await TestBed.inject( HelpRouteService ).docItems( [] );
		expect( items.map(i=>i.path) ).toEqual( ['overview', 'gateways'] );
		expect( items[0] ).toMatchObject( {title: 'Overview', summary: 'What this is', icon: 'menu_book', cardClass: 'card-help'} );
	});
});

describe( 'helpTopicResolver', ()=>{
	beforeEach( ()=>localStorage.clear() );

	const resolve = ( topic:string )=>{
		configure();
		return TestBed.runInInjectionContext( ()=>helpTopicResolver( <ActivatedRouteSnapshot>{paramMap: convertToParamMap({topic})}, <any>{} ) );
	};

	it( 'returns the topic for the segment and seeds the breadcrumb store', ()=>{
		expect( resolve('gateways') ).toBe( gateways );
		expect( TestBed.inject(RouteStore).getChildren('help').map(c=>c.title) ).toEqual( ['Overview', 'Gateways'] );
	});
	it( 'returns undefined for an unknown segment', ()=>expect( resolve('nope') ).toBeUndefined() );
});