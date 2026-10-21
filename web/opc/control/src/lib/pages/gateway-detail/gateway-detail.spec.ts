import { Component, inject, InjectionToken, input, model, OnInit } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { DefaultUrlSerializer, PRIMARY_OUTLET } from '@angular/router';
import { BehaviorSubject } from 'rxjs';
import { AppService, QLList } from 'jde-framework';
import { GatewayService } from '../../services/gateway-service';
import { GatewayDetail } from './gateway-detail';
import { GatewayHistList } from './hist-admin/gateway-hist-list';
import { pageTitleStub, provideRoute, stable } from 'jde-spa/testing';
import { stubLogTabs } from 'jde-framework/testing';

//the list tabs' components, standing in for the real ones (the log tabs' are stubLogTabs').  A blanked QLList
//would still subscribe to the route's data in ngOnInit and read a resolver's results off it.
@Component( {selector: 'ql-list', template: ''} )
class ListStub{ sideNav = model<unknown>(); ql = input<unknown>(); }
type HistListInputs = { gateway:unknown; kind:string|undefined; url:string|undefined };
const HIST_LIST_CREATED = new InjectionToken<( inputs:HistListInputs )=>void>( 'HistListCreated' );//told each list built, with its inputs
@Component( {selector: 'gateway-hist-list', template: ''} )
class HistListStub implements OnInit{
	gateway = input<unknown>(); kind = input<string>(); url = input<string>();
	#created = inject( HIST_LIST_CREATED );
	ngOnInit(){ this.#created( {gateway: this.gateway(), kind: this.kind(), url: this.url()} ); }
}

//reviews/m3-closing.md #31:  the router reuses this page from one gateway to the next, and the log tabs loaded once - after a
//switch they kept A's levels and entries under B's title, and a Save sent the edits, diffed against A's, to B's id.
describe( 'GatewayDetail switching gateway', ()=>{
	const gateways:Record<string, object> = { A: {slug: 'A'}, B: {slug: 'B'}, 'My Hub': {slug: 'My Hub'} };
	const ids:Record<string, number> = { A: 1, B: 2, 'My Hub': 3 };
	const routeData = ( instance:string )=>({ data: {routing: {path: `gateways/${instance}`}} });
	//the page on `tab`, with `go` to switch its gateway and `created`, told each historian list built
	const open = async ( tab:number )=>{
		let instance = 'A';
		const down = new Set<string>();//a lookup that rejects:  the gateway's by its name, its instance id's by `<name>#pk`
		const data = new BehaviorSubject<object>( routeData('A') );
		//the sidenav route's segments, as the router parses them from the url it shows, which percent-encodes the instance
		const parent = { get snapshot(){ return {url: new DefaultUrlSerializer().parse( `/apps/gateways/${encodeURIComponent( instance )}` ).root.children[PRIMARY_OUTLET].segments}; } };
		const created = vi.fn<( inputs:HistListInputs )=>void>();
		TestBed.configureTestingModule({ providers: [
			provideRoute( {data, parent} ),
			pageTitleStub,
			{ provide: AppService, useValue: {instancePK: async ( name:string, program:string )=>{
				if( down.has(`${name}#pk`) )
					throw new Error( `${name} is not registered` );
				return program=="OpcGateway" ? ids[name] : undefined;
			}} },
			{ provide: GatewayService, useValue: {gateway: async ( name:string )=>{
				if( down.has(name) )
					throw new Error( `${name} is down` );
				return gateways[name];
			}} },
			{ provide: HIST_LIST_CREATED, useValue: created }
		]});
		TestBed.overrideComponent( GatewayDetail, {remove: {imports: [QLList, GatewayHistList]}, add: {imports: [ListStub, HistListStub]}} );
		const logTabs = stubLogTabs();
		const fixture = TestBed.createComponent( GatewayDetail );
		fixture.componentInstance.tabIndex.set( tab );
		await stable( fixture );
		const go = async ( to:string )=>{ instance = to; data.next( routeData(to) ); await stable( fixture ); };
		return { fixture, go, logTabs, created: ()=>created.mock.calls.map( ([c])=>c ), fail: ( lookup:string )=>down.add( lookup ) };
	};

	//the tabs:  Connections, Templates, Groups, Logs, Log Settings (#337 put the two historian tabs beside Connections)
	it( 'builds Log Settings again for the gateway it switched to', async ()=>{
		const { go, logTabs } = await open( 4 );
		await go( 'B' );
		expect( logTabs.settings.mock.calls ).toEqual( [[1], [2]] );
	} );

	it( 'and Logs', async ()=>{
		const { go, logTabs } = await open( 3 );
		await go( 'B' );
		expect( logTabs.logs.mock.calls ).toEqual( [[gateways['A']], [gateways['B']]] );
	} );

	//#337:  the Templates and Groups lists are the gateway's own tables, so a switch builds them again too, under the page's url
	it( 'and the Templates list, keyed under the page url', async ()=>{
		const { go, created } = await open( 1 );
		await go( 'B' );
		expect( created().map( c=>c.gateway ) ).toEqual( [gateways['A'], gateways['B']] );
		expect( created()[0] ).toEqual( {gateway: gateways['A'], kind: 'templates', url: 'apps/gateways/A'} );
	} );

	it( 'and the Groups list', async ()=>{
		const { go, created } = await open( 2 );
		await go( 'B' );
		expect( created().map( c=>c.kind ) ).toEqual( ['groups', 'groups'] );
	} );
	//the key is the one the resolvers read the rows back under:  the route's segments, decoded, not router.url's percent-encoding
	it( 'keys the lists under the instance decoded', async ()=>{
		const { go, created } = await open( 1 );
		await go( 'My Hub' );
		expect( created()[1].url ).toBe( 'apps/gateways/My Hub' );
	} );

	//reviews-issue/web-refactor-review.md #4:  a rejected lookup went unhandled inside the subscription, and the page stayed blank
	const shown = ( fixture:{ nativeElement:HTMLElement } )=>({ tabs: !!fixture.nativeElement.querySelector( 'instance-tabs' ), text: fixture.nativeElement.textContent!.trim() });
	it( 'says why a gateway cannot be loaded, and drops it on the way back', async ()=>{
		const { fixture, go, fail } = await open( 0 );
		fail( 'B' );
		await go( 'B' );
		expect( shown(fixture) ).toEqual( {tabs: false, text: 'Could not load the gateway.  B is down'} );
		await go( 'A' );
		expect( fixture.componentInstance.error() ).toBeUndefined();
		expect( shown(fixture).tabs ).toBe( true );
	} );
	it( 'and why its instance cannot', async ()=>{
		const { fixture, go, fail } = await open( 0 );
		fail( 'B#pk' );
		await go( 'B' );
		expect( shown(fixture) ).toEqual( {tabs: false, text: 'Could not load the gateway.  B is not registered'} );
	} );
} );