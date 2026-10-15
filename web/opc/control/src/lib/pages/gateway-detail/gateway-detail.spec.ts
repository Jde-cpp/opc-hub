if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { Component, input, model, OnInit } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { ActivatedRoute, DefaultUrlSerializer, PRIMARY_OUTLET } from '@angular/router';
import { BehaviorSubject } from 'rxjs';
import { ComponentPageTitle } from 'jde-spa';
import { AppService, LogDetail, LogSettingsPanel, QLList } from 'jde-framework';
import { GatewayService } from '../../services/gateway-service';
import { GatewayDetail } from './gateway-detail';
import { GatewayHistList } from './hist-admin/gateway-hist-list';

//the three tabs' components, standing in for the real ones:  the log tabs record what they were created for.
@Component( {selector: 'log-settings', template: ''} )
class SettingsStub implements OnInit{
	service = input<unknown>(); instanceId = input<number>();
	static created:(number|undefined)[] = [];
	ngOnInit(){ SettingsStub.created.push( this.instanceId() ); }
}
@Component( {selector: 'log-detail', template: ''} )
class LogsStub implements OnInit{
	service = input<unknown>();
	static created:unknown[] = [];
	ngOnInit(){ LogsStub.created.push( this.service() ); }
}
@Component( {selector: 'ql-list', template: ''} )
class ListStub{ sideNav = model<unknown>(); }
@Component( {selector: 'gateway-hist-list', template: ''} )
class HistListStub implements OnInit{
	gateway = input<unknown>(); kind = input<string>(); url = input<string>();
	static created:{gateway:unknown; kind:string|undefined; url:string|undefined}[] = [];
	ngOnInit(){ HistListStub.created.push( {gateway: this.gateway(), kind: this.kind(), url: this.url()} ); }
}

//reviews/m3-closing.md #31:  the router reuses this page from one gateway to the next, and the log tabs loaded once - after a
//switch they kept A's levels and entries under B's title, and a Save sent the edits, diffed against A's, to B's id.
describe( 'GatewayDetail switching gateway', ()=>{
	const gateways:Record<string, object> = { A: {slug: 'A'}, B: {slug: 'B'}, 'My Hub': {slug: 'My Hub'} };
	const ids:Record<string, number> = { A: 1, B: 2, 'My Hub': 3 };
	let data:BehaviorSubject<object>;
	let instance:string;
	//the sidenav route's segments, as the router parses them from the url it shows, which percent-encodes the instance
	const parent = { get snapshot(){ return {url: new DefaultUrlSerializer().parse( `/apps/gateways/${encodeURIComponent( instance )}` ).root.children[PRIMARY_OUTLET].segments}; } };
	const routeData = ( instance:string )=>({ data: {routing: {path: `gateways/${instance}`}} });
	const settle = async ( fixture:{ detectChanges():void, whenStable():Promise<unknown> } )=>{
		for( let i=0; i<3; ++i ){ fixture.detectChanges(); await fixture.whenStable(); await new Promise( r=>setTimeout(r, 0) ); }
	};
	const open = async ( tab:number )=>{
		SettingsStub.created = []; LogsStub.created = []; HistListStub.created = [];
		instance = 'A';
		data = new BehaviorSubject<object>( routeData('A') );
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({ providers: [
			{ provide: ActivatedRoute, useValue: {data, parent} },
			{ provide: ComponentPageTitle, useValue: {} },
			{ provide: AppService, useValue: {instancePK: async ( name:string, program:string )=>program=="OpcGateway" ? ids[name] : undefined} },
			{ provide: GatewayService, useValue: {gateway: async ( name:string )=>gateways[name]} }
		]});
		TestBed.overrideComponent( GatewayDetail, {remove: {imports: [LogDetail, LogSettingsPanel, QLList, GatewayHistList]}, add: {imports: [SettingsStub, LogsStub, ListStub, HistListStub]}} );
		const fixture = TestBed.createComponent( GatewayDetail );
		fixture.componentInstance.tabIndex = tab;
		await settle( fixture );
		return fixture;
	};
	const go = async ( fixture:Parameters<typeof settle>[0], to:string )=>{ instance = to; data.next( routeData(to) ); await settle( fixture ); };

	//the tabs:  Connections, Templates, Groups, Logs, Log Settings (#337 put the two historian tabs beside Connections)
	it( 'builds Log Settings again for the gateway it switched to', async ()=>{
		const fixture = await open( 4 );
		await go( fixture, 'B' );
		expect( SettingsStub.created ).toEqual( [1, 2] );
	} );

	it( 'and Logs', async ()=>{
		const fixture = await open( 3 );
		await go( fixture, 'B' );
		expect( LogsStub.created ).toEqual( [gateways['A'], gateways['B']] );
	} );

	//#337:  the Templates and Groups lists are the gateway's own tables, so a switch builds them again too, under the page's url
	it( 'and the Templates list, keyed under the page url', async ()=>{
		const fixture = await open( 1 );
		await go( fixture, 'B' );
		expect( HistListStub.created.map( c=>c.gateway ) ).toEqual( [gateways['A'], gateways['B']] );
		expect( HistListStub.created[0] ).toEqual( {gateway: gateways['A'], kind: 'templates', url: 'apps/gateways/A'} );
	} );

	it( 'and the Groups list', async ()=>{
		const fixture = await open( 2 );
		await go( fixture, 'B' );
		expect( HistListStub.created.map( c=>c.kind ) ).toEqual( ['groups', 'groups'] );
	} );
	//the key is the one the resolvers read the rows back under:  the route's segments, decoded, not router.url's percent-encoding
	it( 'keys the lists under the instance decoded', async ()=>{
		const fixture = await open( 1 );
		await go( fixture, 'My Hub' );
		expect( HistListStub.created[1].url ).toBe( 'apps/gateways/My Hub' );
	} );
} );