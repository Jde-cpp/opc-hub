if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { LiveAnnouncer } from '@angular/cdk/a11y';
import { HttpErrorResponse } from '@angular/common/http';
import { TestBed } from '@angular/core/testing';
import { ActivatedRoute, Router } from '@angular/router';
import { BehaviorSubject, Observable, of } from 'rxjs';
import { ComponentPageTitle } from 'jde-spa';
import { SnackbarService } from 'jde-framework';
import { MatDialog } from '@angular/material/dialog';
import { OpcStore } from '../../../services/opc-store';
import { ServerCnnctn } from '../../../model/server-cnnctn';
import { namespaceColumns, serverDescColumns } from '../../../model/server';
import { Gateway } from '../../../services/gateway-service';
import { ClientDetail } from './client-detail';

//angular-review3 L2: only group-detail clamped the stored tab index.  The Connection tab used to be gated on the row being
//saved, so a stored index of 1 named a tab that did not exist for a new connection - mat-tab-group hard-loops on an index
//it cannot resolve.  The tab is there for every row since #250 (Test Connection answers on it), and a new row still opens on
//Properties, where the typing is.  An existing connection whose server is unreachable keeps the tab (in its not-connected state).
const create = ( row:any )=>{
	TestBed.configureTestingModule({ providers: [
		{ provide: ActivatedRoute, useValue: {data: of({pageData: {row, routing: {}, schema: {enums: new Map()}, gateway: {}}})} },
		{ provide: Router, useValue: {navigate: ()=>{}, url: "/gateways/g/clients/$new"} },
		{ provide: ComponentPageTitle, useValue: {} },
		{ provide: SnackbarService, useValue: {exception: ()=>{}} }
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
//the page had no Purge.  group-detail's button and DetailPage's confirmation, on the per-gateway ql the resolver passes with the row.
let confirmed = true;//what the stubbed confirmation dialog answers
const createWithQl = async ( row:any, mutations:string[] )=>{
	TestBed.configureTestingModule({ providers: [
		{ provide: ActivatedRoute, useValue: {data: of({pageData: {row, routing: {}, schema: {enums: new Map()}, gateway: {slug: "g", mutate: async ( ql:string )=>{ mutations.push( ql ); }}}})} },
		{ provide: Router, useValue: {navigate: ()=>{}, url: "/gateways/g/clients/eng-test"} },
		{ provide: ComponentPageTitle, useValue: {} },
		{ provide: SnackbarService, useValue: {exception: ()=>{}} },
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
	type Send = ( ql:string, vars:any )=>Promise<unknown>;
	const gateway = ( send:Send, slug="g" )=>({slug, postQL: async ( ql:string, vars:any )=>({ testConnection: await send(ql, vars) })});//the gateway keys a mutation's result by its command
	const pageData = ( row:any, ql:unknown )=>({pageData: {row, routing: {}, schema: {enums: new Map()}, gateway: ql}});
	let announced:string[] = [];
	beforeEach( ()=>{ announced = []; } );
	const create = async ( row:any, send:Send, data:Observable<any>=of(pageData(row, gateway(send))), snackbar:unknown={exception: ()=>{}} )=>{
		TestBed.configureTestingModule({ providers: [
			{ provide: LiveAnnouncer, useValue: {announce: async ( m:string )=>{ announced.push( m ); }} },
			{ provide: ActivatedRoute, useValue: {data} },
			{ provide: Router, useValue: {navigate: ()=>{}, url: "/gateways/g/clients/" + (row.slug ?? "$new")} },
			{ provide: ComponentPageTitle, useValue: {} },
			{ provide: SnackbarService, useValue: snackbar },
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
		expect( sent[0].ql ).toMatch( /^mutation testConnection\( opc: \$opc, url: \$url, certificateUri: \$certificateUri \)/ );//POSTed:  a GET must not connect the gateway anywhere
		expect( sent[0].vars ).toEqual( {opc: "plc1", url: "opc.tcp://plc:4840", certificateUri: "urn:plc"} );//trimmed, as the save trims them
		expect( page.tabIndex() ).toBe( 1 );
		expect( page.server()?.applicationName ).toBe( "PLC 1" );
		expect( page.server()?.policy ).toBe( described.securityPolicyUri );
		expect( page.server()?.mode ).toBe( "SignAndEncrypt" );
		expect( page.server()?.namespaces ).toEqual( described.namespaces );
		expect( page.server()?.connection.slug ).toBe( "plc1" );
		expect( page.serverError() ).toBeUndefined();
		expect( page.testing() ).toBeUndefined();
	} );

	it( 'shows the gateway\'s reason in the not-connected state when the test fails', async ()=>{
		const page = await create( {id: 7, slug: "plc1", name: "PLC 1", url: "opc.tcp://plc:4840", certificateUri: "", server: {applicationName: "old"}}, async ()=>{ throw new Error( "(502)'opc.tcp://plc:4840' refused the connection" ); } );
		expect( page.server() ).toBeDefined();//the resolver's describe, until the test answers
		await page.onTestClick();
		expect( page.server() ).toBeUndefined();
		expect( page.serverError() ).toContain( "refused the connection" );
		expect( page.tabIndex() ).toBe( 1 );
	} );

	it( 'retries a saved connection with the slug it has and the url the form holds', async ()=>{
		const sent:any[] = [];
		const page = await create( {id: 7, slug: "plc1", name: "PLC 1", url: "opc.tcp://old:4840", certificateUri: "urn:plc", serverError: "no route to host"}, async ( ql, vars )=>{ sent.push(vars); return described; } );
		expect( page.serverError() ).toBe( "no route to host" );
		edit( page, {url: "opc.tcp://plc:4840"} );//an unsaved edit is what gets tested
		await page.onTestClick();
		expect( sent ).toEqual( [{opc: "plc1", url: "opc.tcp://plc:4840", certificateUri: "urn:plc"}] );
		expect( page.serverError() ).toBeUndefined();
		expect( page.server()?.connection.url ).toBe( "opc.tcp://plc:4840" );
	} );

	//#250 review L1:  the hint sat in the not-connected state, under a red icon and Not connected - an untried connection read as a failure.
	it( 'marks a new connection untested until it is tried', async ()=>{
		const page = await create( {}, async ()=>described );
		expect( page.untested ).toBe( true );
		expect( page.serverError() ).toBeUndefined();
		edit( page, {slug: "plc1", url: "opc.tcp://plc:4840"} );
		await page.onTestClick();
		expect( page.untested ).toBe( false );
	} );

	it( 'never marks a saved connection untested', async ()=>{
		const page = await create( {id: 7, slug: "plc1", name: "PLC 1", url: "opc.tcp://plc:4840", certificateUri: "", serverError: "no route to host"}, async ()=>described );
		expect( page.untested ).toBe( false );
	} );

	it( 'is not untested once a test of a new connection failed', async ()=>{
		const page = await create( {}, async ()=>{ throw new Error( "no route to host" ); } );
		edit( page, {slug: "plc1", url: "opc.tcp://plc:4840"} );
		await page.onTestClick();
		expect( page.untested ).toBe( false );
		expect( page.serverError() ).toBe( "no route to host" );
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

	//#250 review #12:  a viewer's press answered 403, and the tab replaced a good describe with Not connected and the denial.
	it( "keeps the tab's result when the gateway refuses the user, and says so in a toast", async ()=>{
		const toasts:string[] = [];
		const forbidden = new HttpErrorResponse( {status: 403, error: "[user]User does not have 'Update' access to 'serverConnections'."} );
		const page = await create( {id: 7, slug: "plc1", name: "PLC 1", url: "opc.tcp://plc:4840", certificateUri: "", server: {applicationName: "PLC 1"}}, async ()=>{ throw forbidden; }, undefined, {exception: ( m:string )=>toasts.push( m )} );
		edit( page, {url: "opc.tcp://plc2:4840"} );
		await page.onTestClick();
		expect( page.server()?.applicationName ).toBe( "PLC 1" );
		expect( page.serverError() ).toBeUndefined();
		expect( page.staleFor() ).toBe( "URL opc.tcp://plc:4840" );//still the resolver's describe, marked as before
		expect( page.testing() ).toBeUndefined();
		expect( toasts ).toEqual( ["Test Connection was refused."] );
		expect( announced ).toEqual( ["Connecting to opc.tcp://plc2:4840."] );//the toast is announced, not the tab
	} );

	//#250 review #15:  the tab's states were role=status regions inserted with their text - a screen reader may have said neither
	//that the test started nor what it found.
	it( 'announces the test as it starts and as it ends', async ()=>{
		const page = await create( {}, async ()=>described );
		edit( page, {slug: "plc1", url: "opc.tcp://plc:4840"} );
		await page.onTestClick();
		expect( announced ).toEqual( ["Connecting to opc.tcp://plc:4840.", "Connected to PLC 1."] );
	} );

	it( 'announces a failed test with its reason', async ()=>{
		const page = await create( {}, async ()=>{ throw new Error( "(502)'opc.tcp://plc:4840' refused the connection" ); } );
		edit( page, {slug: "plc1", url: "opc.tcp://plc:4840"} );
		await page.onTestClick();
		expect( announced ).toEqual( ["Connecting to opc.tcp://plc:4840.", "Not connected:  (502)'opc.tcp://plc:4840' refused the connection"] );
	} );

	//#250 review L3:  the describe's columns were listed here and in OpcStore.getConnection, so a ServerDesc field added to one
	//would show blank on the other path - after a test, or on the page's load.
	it( "selects the describe the resolver's OpcStore query selects", async ()=>{
		TestBed.configureTestingModule({});
		let describe = "";
		const query = async ( ql:string )=>{ describe = ql; return {connection: {slug: "plc1"}, desc: {}, policy: "", mode: "", namespaces: []}; };
		await TestBed.inject( OpcStore ).getConnection( {slug: "g", query} as unknown as Gateway, "plc1" );
		expect( describe ).toContain( `serverDescription( opc: $opc ){ ${serverDescColumns} }` );
		expect( describe ).toContain( `namespaces( opc: $opc ){ ${namespaceColumns} }` );
		expect( ClientDetail.testMutation ).toContain( `{ ${serverDescColumns} securityPolicyUri securityMode namespaces{ ${namespaceColumns} } }` );
	} );

	//#250 review #6:  the button is disabledInteractive, so focus stays on it while a test runs - and a disabled press still clicks.
	it( 'ignores a press while it cannot test', async ()=>{
		const sent:any[] = [];
		let answer!:( r:unknown )=>void;
		const page = await create( {}, ( ql, vars )=>{ sent.push(vars); return new Promise( r=>{ answer = r; } ); } );
		await page.onTestClick();//no url or slug yet
		expect( sent ).toHaveLength( 0 );
		edit( page, {slug: "plc1", url: "opc.tcp://plc:4840"} );
		const click = page.onTestClick();
		await page.onTestClick();//pressed again while the first runs
		expect( sent ).toHaveLength( 1 );
		answer( described );
		await click;
		expect( page.server()?.applicationName ).toBe( "PLC 1" );
	} );

	//#250 review #9:  a passing test of one url stayed on the tab after the form was edited to another, and read as a check of it.
	//Marked rather than cleared:  a saved row's own describe is worth keeping, and editing the field back makes it current again.
	it( 'marks a result stale once the form no longer holds what it was for', async ()=>{
		const page = await create( {}, async ()=>described );
		edit( page, {slug: "plc1", url: "opc.tcp://plc:4840", certificateUri: "urn:plc"} );
		await page.onTestClick();
		expect( page.staleFor() ).toBeUndefined();
		edit( page, {url: "opc.tcp://other:4840", name: "PLC 1"} );
		expect( page.staleFor() ).toBe( "URL opc.tcp://plc:4840" );
		expect( page.server()?.applicationName ).toBe( "PLC 1" );//kept, not cleared
		edit( page, {url: " opc.tcp://plc:4840 ", certificateUri: "", slug: "plc2"} );//the slug names the certificate the gateway presents
		expect( page.staleFor() ).toBe( "Certificate URI urn:plc, slug plc1" );
		edit( page, {certificateUri: "urn:plc ", slug: "plc1"} );
		expect( page.staleFor() ).toBeUndefined();//whitespace is not a change - the test and the save both trim
		edit( page, {url: "opc.tcp://other:4840"} );
		await page.onTestClick();
		expect( page.staleFor() ).toBeUndefined();//tested again
	} );

	const saved = {id: 7, slug: "plc1", name: "PLC 1", url: "opc.tcp://plc:4840", certificateUri: ""};
	it( "marks the resolver's describe against the saved row", async ()=>{
		const page = await create( {...saved, server: {applicationName: "PLC 1"}}, async ()=>described );
		expect( page.staleFor() ).toBeUndefined();
		edit( page, {certificateUri: "urn:plc"} );
		expect( page.staleFor() ).toBe( "an empty Certificate URI" );
	} );

	it( "marks the resolver's failure against the saved row", async ()=>{
		const page = await create( {...saved, serverError: "no route to host"}, async ()=>described );
		edit( page, {url: "opc.tcp://plc2:4840"} );
		expect( page.staleFor() ).toBe( "URL opc.tcp://plc:4840" );
	} );

	it( 'marks nothing on a new connection before its first test', async ()=>{
		const page = await create( {}, async ()=>described );
		edit( page, {slug: "plc1", url: "opc.tcp://plc:4840"} );
		expect( page.staleFor() ).toBeUndefined();
	} );

	it( 'marks a result stale when the form was edited while the test ran', async ()=>{
		let answer!:( r:unknown )=>void;
		const page = await create( {}, ()=>new Promise( r=>{ answer = r; } ) );
		edit( page, {slug: "plc1", url: "opc.tcp://plc:4840"} );
		const click = page.onTestClick();
		edit( page, {url: "opc.tcp://plc:4841"} );
		answer( described );
		await click;
		expect( page.staleFor() ).toBe( "URL opc.tcp://plc:4840" );
	} );

	//#250 review #5:  the router reuses this page across connections, and a slow test of one answered on the next.
	it( 'drops a test that answers after the page moved to another connection', async ()=>{
		let answer!:( r:unknown )=>void;
		const send:Send = ()=>new Promise( r=>{ answer = r; } );
		const plc1 = {id: 7, slug: "plc1", name: "PLC 1", url: "opc.tcp://plc1:4840", certificateUri: ""};
		const route = new BehaviorSubject<any>( pageData(plc1, gateway(send)) );
		const page = await create( plc1, send, route );
		const click = page.onTestClick();
		expect( page.testing() ).toBe( "opc.tcp://plc1:4840" );
		route.next( pageData({id: 8, slug: "plc2", name: "PLC 2", url: "opc.tcp://plc2:4840", certificateUri: "", serverError: "plc2 is down"}, gateway(send)) );
		expect( page.testing() ).toBeUndefined();//plc2 is not being tested
		expect( page.canTest() ).toBe( true );
		answer( described );
		await click;
		expect( page.server() ).toBeUndefined();
		expect( page.serverError() ).toBe( "plc2 is down" );
	} );
	//#250 review #11:  the gateway was resolved once, in ngOnInit, and the router reuses this page - a connection opened on a second
	//gateway was tested, saved and deleted on the first.
	it( "tests on the gateway of the connection it shows", async ()=>{
		const sentTo:string[] = [];
		const on = ( slug:string )=>gateway( async ()=>{ sentTo.push( slug ); return described; }, slug );
		const plc = {id: 7, slug: "plc1", name: "PLC 1", url: "opc.tcp://plc1:4840", certificateUri: ""};
		const route = new BehaviorSubject<any>( pageData(plc, on("g1")) );
		const page = await create( plc, async ()=>described, route );
		await page.onTestClick();
		route.next( pageData({...plc, id: 3}, on("g2")) );
		await page.onTestClick();
		expect( sentTo ).toEqual( ["g1", "g2"] );
		expect( page.ql.slug ).toBe( "g2" );//Save and Delete go through it too
	} );
} );