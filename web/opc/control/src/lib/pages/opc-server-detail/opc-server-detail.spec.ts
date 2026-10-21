import { TestBed } from '@angular/core/testing';
import { HttpErrorResponse } from '@angular/common/http';
import { BehaviorSubject } from 'rxjs';
import { RouteStore } from 'jde-spa';
import { AppService, InstanceTabs } from 'jde-framework';
import { OpcServerService } from '../../services/opc-server-service';
import { OpcServerDetail } from './opc-server-detail';
import { flush, pageTitleStub, provideRoute, stable } from 'jde-spa/testing';
import { stubLogTabs } from 'jde-framework/testing';

//review3 L13: the banner was built with `${e}`, so the two shapes that actually reach production - an HttpErrorResponse
//and a {error:IError} ProtoService rejection - both rendered "[object Object]" where the failure should have been.
describe( 'OpcServerDetail error banner', ()=>{
	const create = ( thrown:any )=>{
		TestBed.configureTestingModule({ providers: [
			provideRoute( {params: {instance: 'opc1'}} ),
			pageTitleStub,
			{ provide: RouteStore, useValue: {getChildren: ()=>[]} },
			{ provide: AppService, useValue: {instancePK: async ()=>1} },
			{ provide: OpcServerService, useValue: {server: ()=>Promise.reject(thrown)} }
		]});
		const page = TestBed.createComponent( OpcServerDetail ).componentInstance;
		page.ngOnInit();
		return page;
	};

	it( 'quotes a ProtoService rejection', async ()=>{
		const page = create( {error: {requestId:1, message:"no such instance", httpStatus:404}} );
		await flush();
		expect( page.error() ).toBe( "Could not load the OPC server.  (404)no such instance" );
	} );

	it( 'quotes an HttpErrorResponse', async ()=>{
		const page = create( new HttpErrorResponse({status:500, error:{message:"server exploded"}}) );
		await flush();
		expect( page.error() ).toBe( "Could not load the OPC server.  server exploded" );
	} );

	it( 'never leaves [object Object] on screen', async ()=>{
		const page = create( {error: {message:"nope"}} );
		await flush();
		expect( page.error() ).not.toContain( "[object Object]" );
		expect( page.isLoading() ).toBe( false );//the banner exists so the page does not stay blank behind isLoading
	} );
} );

//reviews/m3-closing.md #31:  the router reuses this page from one ':instance' to the next (a sidenav sibling, a search hit), and
//both tabs loaded once, in ngOnInit - so after a switch the tab kept A's levels and entries under B's title, and a Save sent
//the edits, diffed against A's snapshot, to B's id.  Each tab is built for the instance it shows.
describe( 'OpcServerDetail switching instance', ()=>{
	const servers:Record<string, object> = { A: {name: 'A'}, B: {name: 'B'} };
	const ids:Record<string, number> = { A: 1, B: 2 };
	//the page on `tab`, with `go` to switch its instance and `fail` to make one instance's server lookup throw
	const open = async ( tab:number )=>{
		const params = new BehaviorSubject( {instance: 'A'} );
		let failing:string|undefined;
		TestBed.configureTestingModule({ providers: [
			provideRoute( {params} ),
			pageTitleStub,
			{ provide: RouteStore, useValue: {getChildren: ()=>[]} },
			{ provide: AppService, useValue: {instancePK: async ( name:string )=>ids[name]} },
			{ provide: OpcServerService, useValue: {server: async ( name:string )=>{ if( name==failing ) throw new Error( "down" ); return servers[name]; }} }
		]});
		const logTabs = stubLogTabs();
		TestBed.overrideComponent( OpcServerDetail, {set: {imports: [InstanceTabs]}} );//recompiled:  the banner tests above cached the page's template with the unstubbed InstanceTabs
		const fixture = TestBed.createComponent( OpcServerDetail );
		fixture.componentInstance.tabIndex.set( tab );
		await stable( fixture );
		const go = async ( instance:string )=>{ params.next( {instance} ); await stable( fixture ); };
		return { fixture, go, logTabs, fail: ( instance:string )=>{ failing = instance; } };
	};

	it( 'builds Log Settings again for the instance it switched to', async ()=>{
		const { go, logTabs } = await open( 1 );
		await go( 'B' );
		expect( logTabs.settings.mock.calls ).toEqual( [[1], [2]] );
	} );

	it( 'and Logs', async ()=>{
		const { go, logTabs } = await open( 0 );
		await go( 'B' );
		expect( logTabs.logs.mock.calls ).toEqual( [[servers['A']], [servers['B']]] );
	} );

	it( "drops a failed instance's error on the way back", async ()=>{
		const { fixture, go, fail } = await open( 1 );
		fail( 'B' );
		await go( 'B' );
		expect( fixture.componentInstance.error() ).toContain( "down" );
		await go( 'A' );
		expect( fixture.componentInstance.error() ).toBeUndefined();
	} );
} );