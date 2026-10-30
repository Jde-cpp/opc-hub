import { LiveAnnouncer } from '@angular/cdk/a11y';
import { HttpErrorResponse } from '@angular/common/http';
import { TestBed } from '@angular/core/testing';
import { BehaviorSubject, Observable } from 'rxjs';
import { Properties } from 'jde-framework';
import { MatDialog } from '@angular/material/dialog';
import { OpcStore } from '../../../services/opc-store';
import { ServerCnnctn } from '../../../model/server-cnnctn';
import { namespaceColumns, serverDescColumns } from '../../../model/server';
import { ClientDetail } from './client-detail';
import { configureDetailPage, dialogStub } from 'jde-framework/testing';

//angular-review3 L2: only group-detail clamped the stored tab index.  The Connection tab used to be gated on the row being
//saved, so a stored index of 1 named a tab that did not exist for a new connection - mat-tab-group hard-loops on an index
//it cannot resolve.  The tab is there for every row since #250 (Test Connection answers on it), and a new row still opens on
//Properties, where the typing is.  An existing connection whose server is unreachable keeps the tab (in its not-connected state).
const create = ( row:any )=>{
	configureDetailPage( row, {gateway: {}} );
	const page = TestBed.createComponent( ClientDetail ).componentInstance;
	page.ngOnInit();//the route.data subscription lives in ngOnInit since C2 - createComponent alone does not call it
	return page;
};

describe( 'ClientDetail tab index', ()=>{
	beforeEach( ()=>localStorage.setItem('client-detail/tabIndex', '1') );//Connection
	afterEach( ()=>localStorage.removeItem('client-detail/tabIndex') );

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
//the page had no Purge.  group-detail's button and DetailPage's confirmation, on the per-gateway ql the row comes with.  `confirm`
//is what the confirmation answers.
const createWithQl = async ( row:any, confirm=true )=>{
	const mutate = vi.fn( async ( _ql:string )=>{} );
	configureDetailPage( row, {gateway: {slug: "g", mutate}, providers: [
		{ provide: OpcStore, useValue: {forget: ()=>{}} },
		{ provide: MatDialog, useValue: dialogStub( confirm ) }
	]} );
	const page = TestBed.createComponent( ClientDetail ).componentInstance;
	await page.ngOnInit();
	return { page, mutate };
};
describe( 'ClientDetail purge', ()=>{
	const deleted = {id: 2, name: "eng-test", slug: "eng-test", deleted: "2026-09-25T16:33:00Z"};

	it( 'purges by id once confirmed', async ()=>{
		const { page, mutate } = await createWithQl( deleted );
		expect( page.isDeleted ).toBe( true );
		await page.onPurgeClick();
		expect( mutate.mock.calls ).toEqual( [["purgeServerConnection(id:2)"]] );
	} );

	it( 'sends nothing when the confirmation is declined', async ()=>{
		const { page, mutate } = await createWithQl( deleted, false );
		await page.onPurgeClick();
		expect( mutate ).not.toHaveBeenCalled();
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
	//the page over a gateway whose test answers through `send`, and `announced`, what it told the LiveAnnouncer
	const create = async ( row:any, send:Send, data?:Observable<any> )=>{
		const announce = vi.fn( async ( _m:string )=>{} );
		const { snackbar } = configureDetailPage( row, {gateway: gateway(send), data, providers: [
			{ provide: LiveAnnouncer, useValue: {announce} },
			{ provide: OpcStore, useValue: {forget: ()=>{}} }
		]} );
		const page = TestBed.createComponent( ClientDetail ).componentInstance;
		await page.ngOnInit();
		return { page, snackbar, announced: ()=>announce.mock.calls.map( ([m])=>m ) };
	};
	const edit = ( page:ClientDetail, fields:Record<string,unknown> )=>page.properties.set( Properties.edited(ServerCnnctn, page.properties(), fields) );//the Properties form's keystrokes
	const unset = { securityMode: null, securityPolicy: null, sessionTimeout: null, requestTimeout: null, pingInterval: null, ttl: null, locale: null };//the spec's settings as a form that leaves them sends them

	it( 'needs a url and a valid slug', async ()=>{
		const { page } = await create( {}, async ()=>({}) );
		expect( page.canTest() ).toBe( false );//a blank new row
		edit( page, {url: "opc.tcp://plc:4840"} );
		expect( page.canTest() ).toBe( false );//no slug - nothing to name the certificate
		edit( page, {slug: "bad slug"} );
		expect( page.canTest() ).toBe( false );
		edit( page, {slug: "plc1"} );
		expect( page.canTest() ).toBe( true );
	} );

	it( "tests a new connection's form values and shows the server on the Connection tab", async ()=>{
		const send = vi.fn( async ( _ql:string, _vars:any )=>described );
		const { page } = await create( {}, send );
		edit( page, {slug: " plc1 ", url: "opc.tcp://plc:4840 ", certificateUri: "urn:plc"} );
		expect( page.tabIndex() ).toBe( 0 );
		await page.onTestClick();
		expect( send ).toHaveBeenCalledTimes( 1 );
		expect( send.mock.calls[0][0] ).toMatch( /^mutation testConnection\( opc: \$opc, url: \$url, certificateUri: \$certificateUri, securityMode: \$securityMode, securityPolicy: \$securityPolicy, sessionTimeout: \$sessionTimeout, requestTimeout: \$requestTimeout, pingInterval: \$pingInterval, ttl: \$ttl, locale: \$locale \)/ );//POSTed:  a GET must not connect the gateway anywhere
		expect( send.mock.calls[0][1] ).toEqual( {opc: "plc1", url: "opc.tcp://plc:4840", certificateUri: "urn:plc", ...unset} );//trimmed, as the save trims them;  the settings null, as the save leaves them
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
		const { page } = await create( {id: 7, slug: "plc1", name: "PLC 1", url: "opc.tcp://plc:4840", certificateUri: "", server: {applicationName: "old"}}, async ()=>{ throw new Error( "(502)'opc.tcp://plc:4840' refused the connection" ); } );
		expect( page.server() ).toBeDefined();//the resolver's describe, until the test answers
		await page.onTestClick();
		expect( page.server() ).toBeUndefined();
		expect( page.serverError() ).toContain( "refused the connection" );
		expect( page.tabIndex() ).toBe( 1 );
	} );

	it( 'retries a saved connection with the slug it has and the url the form holds', async ()=>{
		const send = vi.fn( async ( _ql:string, _vars:any )=>described );
		const { page } = await create( {id: 7, slug: "plc1", name: "PLC 1", url: "opc.tcp://old:4840", certificateUri: "urn:plc", serverError: "no route to host"}, send );
		expect( page.serverError() ).toBe( "no route to host" );
		edit( page, {url: "opc.tcp://plc:4840"} );//an unsaved edit is what gets tested
		await page.onTestClick();
		expect( send.mock.calls.map( ([, vars])=>vars ) ).toEqual( [{opc: "plc1", url: "opc.tcp://plc:4840", certificateUri: "urn:plc", ...unset}] );
		expect( page.serverError() ).toBeUndefined();
		expect( page.server()?.connection.url ).toBe( "opc.tcp://plc:4840" );
	} );

	//#250 review L1:  the hint sat in the not-connected state, under a red icon and Not connected - an untried connection read as a failure.
	it( 'marks a new connection untested until it is tried', async ()=>{
		const { page } = await create( {}, async ()=>described );
		expect( page.untested ).toBe( true );
		expect( page.serverError() ).toBeUndefined();
		edit( page, {slug: "plc1", url: "opc.tcp://plc:4840"} );
		await page.onTestClick();
		expect( page.untested ).toBe( false );
	} );

	it( 'never marks a saved connection untested', async ()=>{
		const { page } = await create( {id: 7, slug: "plc1", name: "PLC 1", url: "opc.tcp://plc:4840", certificateUri: "", serverError: "no route to host"}, async ()=>described );
		expect( page.untested ).toBe( false );
	} );

	it( 'is not untested once a test of a new connection failed', async ()=>{
		const { page } = await create( {}, async ()=>{ throw new Error( "no route to host" ); } );
		edit( page, {slug: "plc1", url: "opc.tcp://plc:4840"} );
		await page.onTestClick();
		expect( page.untested ).toBe( false );
		expect( page.serverError() ).toBe( "no route to host" );
	} );

	it( 'shows the url under test while the gateway connects', async ()=>{
		let answer!:( r:unknown )=>void;
		const { page } = await create( {}, ()=>new Promise( r=>{ answer = r; } ) );
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
		const forbidden = new HttpErrorResponse( {status: 403, error: "[user]User does not have 'Update' access to 'serverConnections'."} );
		const { page, snackbar, announced } = await create( {id: 7, slug: "plc1", name: "PLC 1", url: "opc.tcp://plc:4840", certificateUri: "", server: {applicationName: "PLC 1"}}, async ()=>{ throw forbidden; } );
		edit( page, {url: "opc.tcp://plc2:4840"} );
		await page.onTestClick();
		expect( page.server()?.applicationName ).toBe( "PLC 1" );
		expect( page.serverError() ).toBeUndefined();
		expect( page.staleFor() ).toBe( "URL opc.tcp://plc:4840" );//still the resolver's describe, marked as before
		expect( page.testing() ).toBeUndefined();
		expect( snackbar.exception.mock.calls.map( c=>c[0] ) ).toEqual( ["Test Connection was refused."] );
		expect( announced() ).toEqual( ["Connecting to opc.tcp://plc2:4840."] );//the toast is announced, not the tab
	} );

	//#250 review #15:  the tab's states were role=status regions inserted with their text - a screen reader may have said neither
	//that the test started nor what it found.
	it( 'announces the test as it starts and as it ends', async ()=>{
		const { page, announced } = await create( {}, async ()=>described );
		edit( page, {slug: "plc1", url: "opc.tcp://plc:4840"} );
		await page.onTestClick();
		expect( announced() ).toEqual( ["Connecting to opc.tcp://plc:4840.", "Connected to PLC 1."] );
	} );

	it( 'announces a failed test with its reason', async ()=>{
		const { page, announced } = await create( {}, async ()=>{ throw new Error( "(502)'opc.tcp://plc:4840' refused the connection" ); } );
		edit( page, {slug: "plc1", url: "opc.tcp://plc:4840"} );
		await page.onTestClick();
		expect( announced() ).toEqual( ["Connecting to opc.tcp://plc:4840.", "Not connected:  (502)'opc.tcp://plc:4840' refused the connection"] );
	} );

	//#250 review L3:  the describe's columns were listed here and in OpcStore.getConnection, so a ServerDesc field added to one
	//would show blank on the other path - after a test, or on the page's load.  opc-store.spec checks the store's half.
	it( "selects the describe the resolver's OpcStore query selects", ()=>{
		expect( ClientDetail.testMutation ).toContain( `{ ${serverDescColumns} securityPolicyUri securityMode namespaces{ ${namespaceColumns} } }` );
	} );

	//#250 review #6:  the button is disabledInteractive, so focus stays on it while a test runs - and a disabled press still clicks.
	it( 'ignores a press while it cannot test', async ()=>{
		let answer!:( r:unknown )=>void;
		const send = vi.fn( ( _ql:string, _vars:any )=>new Promise( r=>{ answer = r; } ) );
		const { page } = await create( {}, send );
		await page.onTestClick();//no url or slug yet
		expect( send ).not.toHaveBeenCalled();
		edit( page, {slug: "plc1", url: "opc.tcp://plc:4840"} );
		const click = page.onTestClick();
		await page.onTestClick();//pressed again while the first runs
		expect( send ).toHaveBeenCalledTimes( 1 );
		answer( described );
		await click;
		expect( page.server()?.applicationName ).toBe( "PLC 1" );
	} );

	//#250 review #9:  a passing test of one url stayed on the tab after the form was edited to another, and read as a check of it.
	//Marked rather than cleared:  a saved row's own describe is worth keeping, and editing the field back makes it current again.
	it( 'marks a result stale once the form no longer holds what it was for', async ()=>{
		const { page } = await create( {}, async ()=>described );
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
		const { page } = await create( {...saved, server: {applicationName: "PLC 1"}}, async ()=>described );
		expect( page.staleFor() ).toBeUndefined();
		edit( page, {certificateUri: "urn:plc"} );
		expect( page.staleFor() ).toBe( "an empty Certificate URI" );
	} );

	it( "marks the resolver's failure against the saved row", async ()=>{
		const { page } = await create( {...saved, serverError: "no route to host"}, async ()=>described );
		edit( page, {url: "opc.tcp://plc2:4840"} );
		expect( page.staleFor() ).toBe( "URL opc.tcp://plc:4840" );
	} );

	it( 'marks nothing on a new connection before its first test', async ()=>{
		const { page } = await create( {}, async ()=>described );
		edit( page, {slug: "plc1", url: "opc.tcp://plc:4840"} );
		expect( page.staleFor() ).toBeUndefined();
	} );

	it( 'marks a result stale when the form was edited while the test ran', async ()=>{
		let answer!:( r:unknown )=>void;
		const { page } = await create( {}, ()=>new Promise( r=>{ answer = r; } ) );
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
		const { page } = await create( plc1, send, route );
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
	//#413 (docs/clients/spec.md *Connection settings*):  the test sends the settings the form holds, as Save would save them, so it
	//tests the connection Save would save - a pinned mode as its wire name, a duration as a whole number of milliseconds.
	it( 'sends the connection settings the form holds, as the save would', async ()=>{
		const send = vi.fn( async ( _ql:string, _vars:any )=>described );
		const { page } = await create( {}, send );
		edit( page, {slug: "plc1", url: "opc.tcp://plc:4840", certificateUri: "urn:plc", securityMode: "Sign", securityPolicy: "http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256", sessionTimeout: "60000", locale: " en-US,de "} );
		await page.onTestClick();
		expect( send.mock.calls[0][1] ).toEqual( {opc: "plc1", url: "opc.tcp://plc:4840", certificateUri: "urn:plc", ...unset, securityMode: "Sign", securityPolicy: "http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256", sessionTimeout: 60000, locale: "en-US,de"} );
		expect( page.staleFor() ).toBeUndefined();
		edit( page, {securityMode: null, sessionTimeout: "90000"} );
		expect( page.staleFor() ).toBe( "Security Mode Sign, Session Timeout 60000" );
	} );
	it( 'cannot test settings the gateway would refuse at save', async ()=>{
		const send = vi.fn( async ( _ql:string, _vars:any )=>described );
		const { page } = await create( {}, send );
		edit( page, {slug: "plc1", url: "opc.tcp://plc:4840", securityMode: "Sign"} );//Sign without a certificate URI
		expect( page.canTest() ).toBe( false );
		await page.onTestClick();
		expect( send ).not.toHaveBeenCalled();
		edit( page, {certificateUri: "urn:plc"} );
		expect( page.canTest() ).toBe( true );
	} );
	it( "labels the unset mode with the floor the certificate URI sets", async ()=>{
		const { page } = await create( {}, async ()=>described );
		edit( page, {certificateUri: ""} );
		expect( page.picks().securityMode[0] ).toEqual( {value: null, label: "Default (None)"} );
		edit( page, {certificateUri: "urn:plc"} );
		expect( page.picks().securityMode[0] ).toEqual( {value: null, label: "Default (Sign & Encrypt)"} );
		expect( page.picks().securityMode.map( o=>o.value ) ).toEqual( [null, "None", "Sign", "SignAndEncrypt"] );
		expect( page.picks().securityPolicy.map( o=>o.label ) ).toEqual( ["Default (strongest shared)", "None", "Basic256Sha256", "Aes128_Sha256_RsaOaep", "Aes256_Sha256_RsaPss"] );
	} );
	//#250 review #11:  the gateway was resolved once, in ngOnInit, and the router reuses this page - a connection opened on a second
	//gateway was tested, saved and deleted on the first.
	it( "tests on the gateway of the connection it shows", async ()=>{
		const sentTo = vi.fn( async ( _slug:string )=>described );
		const on = ( slug:string )=>gateway( ()=>sentTo( slug ), slug );
		const plc = {id: 7, slug: "plc1", name: "PLC 1", url: "opc.tcp://plc1:4840", certificateUri: ""};
		const route = new BehaviorSubject<any>( pageData(plc, on("g1")) );
		const { page } = await create( plc, async ()=>described, route );
		await page.onTestClick();
		route.next( pageData({...plc, id: 3}, on("g2")) );
		await page.onTestClick();
		expect( sentTo.mock.calls ).toEqual( [["g1"], ["g2"]] );
		expect( page.ql.slug ).toBe( "g2" );//Save and Delete go through it too
	} );
} );