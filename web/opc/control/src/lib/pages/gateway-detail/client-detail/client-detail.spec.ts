if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { TestBed } from '@angular/core/testing';
import { ActivatedRoute, Router } from '@angular/router';
import { of } from 'rxjs';
import { ComponentPageTitle } from 'jde-spa';
import { SnackbarService } from 'jde-framework';
import { GatewayService } from '../../../services/gateway-service';
import { MatDialog } from '@angular/material/dialog';
import { OpcStore } from '../../../services/opc-store';
import { ClientDetail } from './client-detail';

//angular-review3 L2: only group-detail clamped the stored tab index.  Here the Connection tab is gated on the row being
//saved, so a stored index of 1 named a tab that does not exist for a new connection - mat-tab-group hard-loops on an index
//it cannot resolve.  An existing connection whose server is unreachable keeps the tab (in its not-connected state).
const create = ( row:any )=>{
	TestBed.configureTestingModule({ providers: [
		{ provide: ActivatedRoute, useValue: {data: of({pageData: {row, routing: {}, schema: {enums: new Map()}}})} },
		{ provide: Router, useValue: {navigate: ()=>{}, url: "/gateways/g/clients/$new"} },
		{ provide: ComponentPageTitle, useValue: {} },
		{ provide: SnackbarService, useValue: {exception: ()=>{}} },
		{ provide: GatewayService, useValue: {gateway: async ()=>({})} }
	]});
	const page = TestBed.createComponent( ClientDetail ).componentInstance;
	page.ngOnInit();//the route.data subscription lives in ngOnInit since C2 - createComponent alone does not call it
	return page;
};

describe( 'ClientDetail tab index', ()=>{
	beforeEach( ()=>localStorage.setItem('client-detail', '1') );//Connection
	afterEach( ()=>localStorage.removeItem('client-detail') );

	it( 'clamps to Properties for a new connection', ()=>{
		expect( create({}).tabIndex() ).toBe( 0 );
	} );

	it( 'keeps the stored index when the Connection tab exists', ()=>{
		expect( create({id: 7, name: "plc", server: {id: 2}}).tabIndex() ).toBe( 1 );
	} );

	it( 'keeps the stored index for a saved connection whose server is unreachable', ()=>{
		expect( create({id: 7, name: "plc", serverError: "no route to host"}).tabIndex() ).toBe( 1 );
	} );
} );
//reviews/install-issues.md #53:  a deleted connection keeps its slug, and #51's refusal says to restore or purge it - but
//the page had no Purge.  group-detail's button and DetailPage's confirmation, on the per-gateway ql ngOnInit resolves.
let confirmed = true;//what the stubbed confirmation dialog answers
const createWithQl = async ( row:any, mutations:string[] )=>{
	TestBed.configureTestingModule({ providers: [
		{ provide: ActivatedRoute, useValue: {data: of({pageData: {row, routing: {}, schema: {enums: new Map()}}})} },
		{ provide: Router, useValue: {navigate: ()=>{}, url: "/gateways/g/clients/eng-test"} },
		{ provide: ComponentPageTitle, useValue: {} },
		{ provide: SnackbarService, useValue: {exception: ()=>{}} },
		{ provide: GatewayService, useValue: {gateway: async ()=>({slug: "g", mutate: async ( ql:string )=>{ mutations.push( ql ); }})} },
		{ provide: OpcStore, useValue: {forget: ()=>{}} },
		{ provide: MatDialog, useValue: {open: ()=>({afterClosed: ()=>({subscribe: ( f:( y:boolean )=>void )=>f( confirmed )})})} }
	]});
	const page = TestBed.createComponent( ClientDetail ).componentInstance;
	await page.ngOnInit();
	return page;
};
describe( 'ClientDetail purge', ()=>{
	afterEach( ()=>{ confirmed = true; } );
	const deleted = {id: 2, name: "eng-test", slug: "eng-test", deleted: "2026-09-25T16:33:00Z"};

	it( 'purges by id once confirmed', async ()=>{
		const sent:string[] = [];
		const page = await createWithQl( deleted, sent );
		expect( page.isDeleted ).toBe( true );
		await page.onPurgeClick();
		expect( sent ).toEqual( ["purgeServerConnection(id:2)"] );
	} );

	it( 'sends nothing when the confirmation is declined', async ()=>{
		confirmed = false;
		const sent:string[] = [];
		const page = await createWithQl( deleted, sent );
		await page.onPurgeClick();
		expect( sent ).toEqual( [] );
	} );
} );
