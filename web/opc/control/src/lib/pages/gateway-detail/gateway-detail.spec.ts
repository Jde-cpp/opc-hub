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
import { ActivatedRoute } from '@angular/router';
import { BehaviorSubject } from 'rxjs';
import { ComponentPageTitle } from 'jde-spa';
import { AppService, LogDetail, LogSettingsPanel, QLList } from 'jde-framework';
import { GatewayService } from '../../services/gateway-service';
import { GatewayDetail } from './gateway-detail';

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

//reviews/m3-closing.md #31:  the router reuses this page from one gateway to the next, and the log tabs loaded once - after a
//switch they kept A's levels and entries under B's title, and a Save sent the edits, diffed against A's, to B's id.
describe( 'GatewayDetail switching gateway', ()=>{
	const gateways:Record<string, object> = { A: {slug: 'A'}, B: {slug: 'B'} };
	const ids:Record<string, number> = { A: 1, B: 2 };
	let data:BehaviorSubject<object>;
	const routeData = ( instance:string )=>({ data: {routing: {path: `gateways/${instance}`}} });
	const settle = async ( fixture:{ detectChanges():void, whenStable():Promise<unknown> } )=>{
		for( let i=0; i<3; ++i ){ fixture.detectChanges(); await fixture.whenStable(); await new Promise( r=>setTimeout(r, 0) ); }
	};
	const open = async ( tab:number )=>{
		SettingsStub.created = []; LogsStub.created = [];
		data = new BehaviorSubject<object>( routeData('A') );
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({ providers: [
			{ provide: ActivatedRoute, useValue: {data} },
			{ provide: ComponentPageTitle, useValue: {} },
			{ provide: AppService, useValue: {instancePK: async ( name:string, program:string )=>program=="OpcGateway" ? ids[name] : undefined} },
			{ provide: GatewayService, useValue: {gateway: async ( name:string )=>gateways[name]} }
		]});
		TestBed.overrideComponent( GatewayDetail, {remove: {imports: [LogDetail, LogSettingsPanel, QLList]}, add: {imports: [SettingsStub, LogsStub, ListStub]}} );
		const fixture = TestBed.createComponent( GatewayDetail );
		fixture.componentInstance.tabIndex = tab;
		await settle( fixture );
		return fixture;
	};
	const go = async ( fixture:Parameters<typeof settle>[0], instance:string )=>{ data.next( routeData(instance) ); await settle( fixture ); };

	it( 'builds Log Settings again for the gateway it switched to', async ()=>{
		const fixture = await open( 2 );
		await go( fixture, 'B' );
		expect( SettingsStub.created ).toEqual( [1, 2] );
	} );

	it( 'and Logs', async ()=>{
		const fixture = await open( 1 );
		await go( fixture, 'B' );
		expect( LogsStub.created ).toEqual( [gateways['A'], gateways['B']] );
	} );
} );
