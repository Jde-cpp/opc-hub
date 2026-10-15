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
import { ServerCnnctn } from '../../../model/server-cnnctn';
import { ClientDetail } from './client-detail';

//angular-review3 L2: only group-detail clamped the stored tab index.  The Connection tab used to be gated on the row being
//saved, so a stored index of 1 named a tab that did not exist for a new connection - mat-tab-group hard-loops on an index
//it cannot resolve.  The tab is there for every row since #250 (Test Connection answers on it), and a new row still opens on
//Properties, where the typing is.  An existing connection whose server is unreachable keeps the tab (in its not-connected state).
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


//#250:  Test Connection sends the form's values - saved or not - to the gateway's testConnection, and the Connection tab shows the
//answer in its existing states:  the server's description on success, Not connected and the gateway's reason on failure.  The
//button needs a url and a valid slug, since the gateway keys the certificate and the credential on the slug.
describe( 'ClientDetail test connection', ()=>{
	const described = { applicationUri: "urn:plc", productUri: "", applicationName: "PLC 1", applicationType: "Server", gatewayServerUri: "", discoveryProfileUri: "", discoveryUrls: ["opc.tcp://plc:4840"], securityPolicyUri: "http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256", securityMode: "SignAndEncrypt", namespaces: [{index: 0, uri: "http://opcfoundation.org/UA/"}] };
	const create = async ( row:any, querySingle:( ql:string, vars:any )=>Promise<unknown> )=>{
		TestBed.configureTestingModule({ providers: [
			{ provide: ActivatedRoute, useValue: {data: of({pageData: {row, routing: {}, schema: {enums: new Map()}}})} },
			{ provide: Router, useValue: {navigate: ()=>{}, url: "/gateways/g/clients/" + (row.slug ?? "$new")} },
			{ provide: ComponentPageTitle, useValue: {} },
			{ provide: SnackbarService, useValue: {exception: ()=>{}} },
			{ provide: GatewayService, useValue: {gateway: async ()=>({slug: "g", querySingle})} },
			{ provide: OpcStore, useValue: {forget: ()=>{}} }
		]});
		const page = TestBed.createComponent( ClientDetail ).componentInstance;
		await page.ngOnInit();
		return page;
	};
	const edit = ( page:ClientDetail, fields:Record<string,unknown> )=>page.properties.set( new ServerCnnctn({...page.properties(), ...fields} as any) );//what the Properties form does on each keystroke

	it( 'needs a url and a valid slug', async ()=>{
		const page = await create( {}, async ()=>({}) );
		expect( page.canTest() ).toBe( false );//a blank new row
		edit( page, {url: "opc.tcp://plc:4840"} );
		expect( page.canTest() ).toBe( false );//no slug - nothing to name the certificate
		edit( page, {slug: "bad slug"} );
		expect( page.canTest() ).toBe( false );
		edit( page, {slug: "plc1"} );
		expect( page.canTest() ).toBe( true );
	} );

	it( "tests a new connection's form values and shows the server on the Connection tab", async ()=>{
		const sent:any[] = [];
		const page = await create( {}, async ( ql, vars )=>{ sent.push({ql, vars}); return described; } );
		edit( page, {slug: " plc1 ", url: "opc.tcp://plc:4840 ", certificateUri: "urn:plc"} );
		expect( page.tabIndex() ).toBe( 0 );
		await page.onTestClick();
		expect( sent ).toHaveLength( 1 );
		expect( sent[0].ql ).toContain( "testConnection( opc: $opc, url: $url, certificateUri: $certificateUri )" );
		expect( sent[0].vars ).toEqual( {opc: "plc1", url: "opc.tcp://plc:4840", certificateUri: "urn:plc"} );//trimmed, as the save trims the slug
		expect( page.tabIndex() ).toBe( 1 );
		expect( page.server()?.applicationName ).toBe( "PLC 1" );
		expect( page.server()?.policy ).toBe( described.securityPolicyUri );
		expect( page.server()?.mode ).toBe( "SignAndEncrypt" );
		expect( page.server()?.namespaces ).toEqual( described.namespaces );
		expect( page.server()?.connection.slug ).toBe( "plc1" );
		expect( page.connectionDetail ).toBeUndefined();
		expect( page.testing() ).toBeUndefined();
	} );

	it( 'shows the gateway\'s reason in the not-connected state when the test fails', async ()=>{
		const page = await create( {id: 7, slug: "plc1", name: "PLC 1", url: "opc.tcp://plc:4840", certificateUri: "", server: {applicationName: "old"}}, async ()=>{ throw new Error( "(502)'opc.tcp://plc:4840' refused the connection" ); } );
		expect( page.server() ).toBeDefined();//the resolver's describe, until the test answers
		await page.onTestClick();
		expect( page.server() ).toBeUndefined();
		expect( page.connectionDetail ).toContain( "refused the connection" );
		expect( page.tabIndex() ).toBe( 1 );
	} );

	it( 'retries a saved connection with the slug it has and the url the form holds', async ()=>{
		const sent:any[] = [];
		const page = await create( {id: 7, slug: "plc1", name: "PLC 1", url: "opc.tcp://old:4840", certificateUri: "urn:plc", serverError: "no route to host"}, async ( ql, vars )=>{ sent.push(vars); return described; } );
		expect( page.connectionDetail ).toBe( "no route to host" );
		edit( page, {url: "opc.tcp://plc:4840"} );//an unsaved edit is what gets tested
		await page.onTestClick();
		expect( sent ).toEqual( [{opc: "plc1", url: "opc.tcp://plc:4840", certificateUri: "urn:plc"}] );
		expect( page.connectionDetail ).toBeUndefined();
		expect( page.server()?.connection.url ).toBe( "opc.tcp://plc:4840" );
	} );

	it( 'tells a new, untested connection what the button does', async ()=>{
		const page = await create( {}, async ()=>({}) );
		expect( page.connectionDetail ).toContain( "Test Connection" );
	} );

	it( 'shows the url under test while the gateway connects', async ()=>{
		let answer!:( r:unknown )=>void;
		const page = await create( {}, ()=>new Promise( r=>{ answer = r; } ) );
		edit( page, {slug: "plc1", url: "opc.tcp://plc:4840"} );
		const click = page.onTestClick();
		expect( page.testing() ).toBe( "opc.tcp://plc:4840" );
		expect( page.canTest() ).toBe( false );//not twice at once
		answer( described );
		await click;
		expect( page.testing() ).toBeUndefined();
		expect( page.canTest() ).toBe( true );
	} );
} );