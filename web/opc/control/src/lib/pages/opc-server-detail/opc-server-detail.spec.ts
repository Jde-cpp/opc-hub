if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { Component, input, OnInit } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { ActivatedRoute } from '@angular/router';
import { HttpErrorResponse } from '@angular/common/http';
import { BehaviorSubject, of } from 'rxjs';
import { ComponentPageTitle, RouteStore } from 'jde-spa';
import { AppService, LogDetail, LogSettingsPanel } from 'jde-framework';
import { OpcServerService } from '../../services/opc-server-service';
import { OpcServerDetail } from './opc-server-detail';

//review3 L13: the banner was built with `${e}`, so the two shapes that actually reach production - an HttpErrorResponse
//and a {error:IError} ProtoService rejection - both rendered "[object Object]" where the failure should have been.
describe( 'OpcServerDetail error banner', ()=>{
	const create = ( thrown:any )=>{
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({ providers: [
			{ provide: ActivatedRoute, useValue: {params: of({instance: 'opc1'})} },
			{ provide: ComponentPageTitle, useValue: {} },
			{ provide: RouteStore, useValue: {getChildren: ()=>[]} },
			{ provide: AppService, useValue: {instancePK: async ()=>1} },
			{ provide: OpcServerService, useValue: {server: ()=>Promise.reject(thrown)} }
		]});
		const page = TestBed.createComponent( OpcServerDetail ).componentInstance;
		page.ngOnInit();
		return page;
	};
	const settle = ()=>new Promise( r=>setTimeout(r, 0) );

	it( 'quotes a ProtoService rejection', async ()=>{
		const page = create( {error: {requestId:1, message:"no such instance", httpStatus:404}} );
		await settle();
		expect( page.error() ).toBe( "Could not load the OPC server.  (404)no such instance" );
	} );

	it( 'quotes an HttpErrorResponse', async ()=>{
		const page = create( new HttpErrorResponse({status:500, error:{message:"server exploded"}}) );
		await settle();
		expect( page.error() ).toBe( "Could not load the OPC server.  server exploded" );
	} );

	it( 'never leaves [object Object] on screen', async ()=>{
		const page = create( {error: {message:"nope"}} );
		await settle();
		expect( page.error() ).not.toContain( "[object Object]" );
		expect( page.isLoading() ).toBe( false );//the banner exists so the page does not stay blank behind isLoading
	} );
} );

//the two tabs' components, standing in for the real ones:  each records what it was created for.
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

//reviews/m3-closing.md #31:  the router reuses this page from one ':instance' to the next (a sidenav sibling, a search hit), and
//both tabs loaded once, in ngOnInit - so after a switch the tab kept A's levels and entries under B's title, and a Save sent
//the edits, diffed against A's snapshot, to B's id.  Each tab is built for the instance it shows.
describe( 'OpcServerDetail switching instance', ()=>{
	const servers:Record<string, object> = { A: {name: 'A'}, B: {name: 'B'} };
	const ids:Record<string, number> = { A: 1, B: 2 };
	let params:BehaviorSubject<{instance:string}>;
	let failing:string|undefined;
	const open = async ( tab:number )=>{
		SettingsStub.created = []; LogsStub.created = [];
		params = new BehaviorSubject( {instance: 'A'} );
		failing = undefined;
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({ providers: [
			{ provide: ActivatedRoute, useValue: {params} },
			{ provide: ComponentPageTitle, useValue: {} },
			{ provide: RouteStore, useValue: {getChildren: ()=>[]} },
			{ provide: AppService, useValue: {instancePK: async ( name:string )=>ids[name]} },
			{ provide: OpcServerService, useValue: {server: async ( name:string )=>{ if( name==failing ) throw new Error( "down" ); return servers[name]; }} }
		]});
		TestBed.overrideComponent( OpcServerDetail, {remove: {imports: [LogDetail, LogSettingsPanel]}, add: {imports: [SettingsStub, LogsStub]}} );
		const fixture = TestBed.createComponent( OpcServerDetail );
		fixture.componentInstance.tabIndex = tab;
		await settle( fixture );
		return fixture;
	};
	const settle = async ( fixture:{ detectChanges():void, whenStable():Promise<unknown> } )=>{
		for( let i=0; i<3; ++i ){ fixture.detectChanges(); await fixture.whenStable(); await new Promise( r=>setTimeout(r, 0) ); }
	};
	const go = async ( fixture:Parameters<typeof settle>[0], instance:string )=>{ params.next( {instance} ); await settle( fixture ); };

	it( 'builds Log Settings again for the instance it switched to', async ()=>{
		const fixture = await open( 1 );
		await go( fixture, 'B' );
		expect( SettingsStub.created ).toEqual( [1, 2] );
	} );

	it( 'and Logs', async ()=>{
		const fixture = await open( 0 );
		await go( fixture, 'B' );
		expect( LogsStub.created ).toEqual( [servers['A'], servers['B']] );
	} );

	it( "drops a failed instance's error on the way back", async ()=>{
		const fixture = await open( 1 );
		failing = 'B';
		await go( fixture, 'B' );
		expect( fixture.componentInstance.error() ).toContain( "down" );
		await go( fixture, 'A' );
		expect( fixture.componentInstance.error() ).toBeUndefined();
	} );
} );
