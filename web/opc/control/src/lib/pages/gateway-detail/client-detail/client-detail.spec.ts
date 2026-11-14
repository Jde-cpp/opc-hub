import { LiveAnnouncer } from '@angular/cdk/a11y';
import { HttpErrorResponse } from '@angular/common/http';
import { TestBed } from '@angular/core/testing';
import { BehaviorSubject, Observable } from 'rxjs';
import { Properties } from 'jde-framework';
import { MatDialog } from '@angular/material/dialog';
import { OpcStore } from '../../../services/opc-store';
import { ServerCnnctn } from '../../../model/server-cnnctn';
import { namespaceColumns, serverDescColumns } from '../../../model/server';
import { identityColumns, opIds, opQuery } from '../../../model/open-protocol';
import { flush } from 'jde-spa/testing';
import { ClientDetail } from './client-detail';
import { DiscoverDialog } from './discover-dialog';
import { configureDetailPage, dialogStub, scalar, slugSchema } from 'jde-framework/testing';

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
	const unset = { kind: "ua", driver: null, securityMode: null, securityPolicy: null, sessionTimeout: null, requestTimeout: null, pingInterval: null, ttl: null, locale: null };//the spec's settings as a form that leaves them sends them, and a UA row's kind

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
		expect( send.mock.calls[0][0] ).toMatch( /^mutation testConnection\( opc: \$opc, url: \$url, kind: \$kind, driver: \$driver, certificateUri: \$certificateUri, securityMode: \$securityMode, securityPolicy: \$securityPolicy, sessionTimeout: \$sessionTimeout, requestTimeout: \$requestTimeout, pingInterval: \$pingInterval, ttl: \$ttl, locale: \$locale \)/ );//POSTed:  a GET must not connect the gateway anywhere
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
	it( "selects the describe the resolver's OpcStore query selects, and an Open Protocol row's identity beside it", ()=>{
		expect( ClientDetail.testMutation ).toContain( `{ ${serverDescColumns} securityPolicyUri securityMode namespaces{ ${namespaceColumns} } ${identityColumns} }` );
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

//#414 (docs/clients/spec.md *Discovery*):  Discover opens the dialog on the form's url, and the endpoint picked fills the url, the
//Certificate URI with the server's applicationUri, and the mode and policy - on Properties, unsaved, for Test Connection or Save.
describe( 'ClientDetail discover', ()=>{
	const pick = { url: 'opc.tcp://plc:4840', certificateUri: 'urn:plc', securityMode: 'SignAndEncrypt', securityPolicy: 'http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256' };
	const create = async ( row:any, answer:unknown )=>{
		const dialog = dialogStub( answer );
		const announce = vi.fn( async ( _m:string )=>{} );
		configureDetailPage( row, {gateway: {slug: "g"}, providers: [
			{ provide: MatDialog, useValue: dialog },
			{ provide: LiveAnnouncer, useValue: {announce} },
			{ provide: OpcStore, useValue: {forget: ()=>{}} }
		]} );
		const page = TestBed.createComponent( ClientDetail ).componentInstance;
		await page.ngOnInit();
		return { page, dialog, announced: ()=>announce.mock.calls.map( ([m])=>m ) };
	};
	const edit = ( page:ClientDetail, fields:Record<string,unknown> )=>page.properties.set( Properties.edited(ServerCnnctn, page.properties(), fields) );

	it( 'needs a url, and nothing else', async ()=>{
		const { page, dialog } = await create( {}, pick );
		expect( page.canDiscover() ).toBe( false );
		await page.onDiscoverClick();
		expect( dialog.open ).not.toHaveBeenCalled();
		edit( page, {url: " opc.tcp://host:4840 "} );
		expect( page.canDiscover() ).toBe( true );//no slug yet:  discovery presents no certificate
	} );

	it( 'opens the dialog on the form\'s url and fills the form from the pick', async ()=>{
		const { page, dialog, announced } = await create( {}, pick );
		edit( page, {url: " opc.tcp://host:4840 ", securityMode: "None"} );
		page.tabIndex.set( 1 );
		await page.onDiscoverClick();
		expect( dialog.open.mock.calls.map( ([component, config])=>[component, (config as any).data] ) ).toEqual( [[DiscoverDialog, {gateway: page.gateway, url: "opc.tcp://host:4840"}]] );
		const p = page.properties() as ServerCnnctn;
		expect( [p.url, p.certificateUri, p.securityMode, p.securityPolicy] ).toEqual( [pick.url, pick.certificateUri, pick.securityMode, pick.securityPolicy] );
		expect( page.tabIndex() ).toBe( 0 );
		expect( announced() ).toEqual( ["URL, Certificate URI, Security Mode and Security Policy set from opc.tcp://plc:4840."] );
		expect( page.canTest() ).toBe( false );//still no slug
		edit( page, {slug: "plc1"} );
		expect( page.canTest() ).toBe( true );
	} );

	it( 'leaves the form alone on a cancel', async ()=>{
		const { page, announced } = await create( {id: 7, slug: "plc1", name: "PLC 1", url: "opc.tcp://old:4840", certificateUri: "urn:old"}, undefined );
		await page.onDiscoverClick();
		const p = page.properties() as ServerCnnctn;
		expect( [p.url, p.certificateUri] ).toEqual( ["opc.tcp://old:4840", "urn:old"] );
		expect( announced() ).toEqual( [] );
	} );

	it( 'marks a saved row\'s result stale once a pick changed the form', async ()=>{
		const { page } = await create( {id: 7, slug: "plc1", name: "PLC 1", url: "opc.tcp://old:4840", certificateUri: "", server: {applicationName: "PLC 1"}}, pick );
		await page.onDiscoverClick();
		expect( page.staleFor() ).toBe( "URL opc.tcp://old:4840, an empty Certificate URI, an unset Security Mode, an unset Security Policy" );
	} );
} );
//#415:  the certificate panel's data and Trust.  onRow loads the rejected server certificate and the gateway's own from the row
//(no connect);  Trust POSTs trustServer by thumbprint, and a refusal is a toast, as Test Connection's is.
describe( 'ClientDetail trust', ()=>{
	//open on the Connection tab (server-properties), where the panel lives:  a cert load resolving triggers a tick, and the
	//Properties tab's form would render against the test's stub schema.  As the tab-index describe does.
	beforeEach( ()=>localStorage.setItem('client-detail/tabIndex', '1') );
	afterEach( ()=>localStorage.removeItem('client-detail/tabIndex') );
	const rejected = { subject: "CN=plc", issuer: "CN=plc", notBefore: "2026-01-01T00:00:00Z", notAfter: "2027-01-01T00:00:00Z", uri: "urn:plc", thumbprint: "AB12CD34" };
	//the fixture is destroyed at the end:  this gateway's query resolves, so onRow's certificate load is real async, and change
	//detection would otherwise run against the torn-down row after the test.
	//`certificates` is what the gateway's row answers at each load, `test` its testConnection, `connect` the re-describe after a trust
	const make = ( row:any, opts:{ trust?:( vars:any )=>Promise<void>; certificates?:()=>unknown; test?:()=>Promise<unknown>; connect?:()=>Promise<unknown> }={} )=>{
		const posts:{ ql:string; vars:any }[] = [];
		const gateway = {
			slug: "g",
			query: async ( _ql:string, _vars:any )=>({ serverConnection: opts.certificates ? opts.certificates() : { rejectedCertificate: rejected, clientCertificate: null } }),
			postQL: async ( ql:string, vars:any )=>{
				posts.push( {ql, vars} );
				if( ql==ClientDetail.testMutation )
					return { testConnection: await opts.test!() };
				if( opts.trust ) await opts.trust( vars );
				return { trustServer: true };
			}
		};
		const { snackbar } = configureDetailPage( row, {gateway, providers: [
			{ provide: OpcStore, useValue: {forget: ()=>{}, getConnection: opts.connect ?? (async ()=>undefined)} },
			{ provide: LiveAnnouncer, useValue: {announce: async ()=>{}} }
		]} );
		const fixture = TestBed.createComponent( ClientDetail );
		return { fixture, page: fixture.componentInstance, posts, snackbar };
	};
	//detectChanges runs ngOnInit once (the route subscription) and renders the Connection tab;  destroy unsubscribes it.
	const open = async ( fixture:any )=>{ fixture.detectChanges(); await flush( 2 ); };

	it( 'loads the row\'s certificates on open', async ()=>{
		const { fixture, page } = make( {id: 7, slug: "plc", name: "plc"} );
		await open( fixture );
		expect( page.certificates()?.rejectedCertificate?.thumbprint ).toBe( "AB12CD34" );
		fixture.destroy();
	} );

	it( 'posts trustServer by thumbprint and reloads', async ()=>{
		const { fixture, page, posts } = make( {id: 7, slug: "plc", name: "plc"} );
		await open( fixture );
		await page.onTrust( "AB12CD34" );
		await flush( 2 );
		expect( posts[0] ).toEqual( {ql: ClientDetail.trustMutation, vars: {opc: "plc", thumbprint: "AB12CD34"}} );
		expect( page.trusting() ).toBe( false );
		fixture.destroy();
	} );

	//reviews-issue/415-clients-trust.md #2:  a test is a connect under the slug, and the gateway records the certificate it rejects
	//for the slug as it does a scheduled connect's - the panel had stayed on the row load's answer until a page reload.
	it( 'reloads the certificates after a test the gateway rejected the server on', async ()=>{
		let answer:unknown = { rejectedCertificate: null, clientCertificate: null };
		const { fixture, page } = make( {id: 7, slug: "plc", name: "plc", url: "opc.tcp://plc:4840"}, {certificates: ()=>answer, test: async ()=>{
			answer = { rejectedCertificate: rejected, clientCertificate: null };//the gateway wrote the .der and recorded its thumbprint
			throw new Error( "server certificate for 'opc.tcp://plc:4840' rejected" );
		}} );
		await open( fixture );
		expect( page.certificates()?.rejectedCertificate ).toBeNull();
		await page.onTestClick();
		expect( page.serverError() ).toContain( "rejected" );
		expect( page.certificates()?.rejectedCertificate?.thumbprint ).toBe( "AB12CD34" );
		fixture.destroy();
	} );

	it( 'clears the rejected certificate once a test connects', async ()=>{
		let answer:unknown = { rejectedCertificate: rejected, clientCertificate: null };
		const described = { applicationUri: "urn:plc", productUri: "", applicationName: "PLC 1", applicationType: "Server", gatewayServerUri: "", discoveryProfileUri: "", discoveryUrls: ["opc.tcp://plc:4840"], securityPolicyUri: "http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256", securityMode: "SignAndEncrypt", namespaces: [] };
		const { fixture, page } = make( {id: 7, slug: "plc", name: "plc", url: "opc.tcp://plc:4840"}, {certificates: ()=>answer, test: async ()=>{
			answer = { rejectedCertificate: null, clientCertificate: null };//trusted by hand:  its file moved out of the rejected directory
			return described;
		}} );
		await open( fixture );
		expect( page.certificates()?.rejectedCertificate?.thumbprint ).toBe( "AB12CD34" );
		await page.onTestClick();
		expect( page.server()?.applicationName ).toBe( "PLC 1" );
		expect( page.certificates()?.rejectedCertificate ).toBeNull();
		fixture.destroy();
	} );

	//reviews-issue/415-clients-trust.md #5:  the button came back over the certificate just trusted while the gateway reconnected,
	//and a second press posted trustServer again, for a 404 toast.
	it( 'keeps Trust disabled until the reconnect has answered', async ()=>{
		let connected!:()=>void;
		const { fixture, page, posts } = make( {id: 7, slug: "plc", name: "plc"}, {connect: ()=>new Promise<undefined>( resolve=>connected = ()=>resolve(undefined) )} );
		await open( fixture );
		const trust = page.onTrust( "AB12CD34" );
		await flush( 2 );
		expect( posts.length ).toBe( 1 );
		expect( page.trusting() ).toBe( true );//the mutation answered, the reconnect has not
		await page.onTrust( "AB12CD34" );//pressed again
		expect( posts.length ).toBe( 1 );
		connected();
		await trust;
		expect( page.trusting() ).toBe( false );
		fixture.destroy();
	} );

	it( 'toasts a refused trust and keeps the panel', async ()=>{
		const refusal = new HttpErrorResponse( {status: 403, error: {message: "Administer required"}} );
		const { fixture, page, snackbar } = make( {id: 7, slug: "plc", name: "plc"}, {trust: async ()=>{ throw refusal; }} );
		await open( fixture );
		await page.onTrust( "AB12CD34" );
		await flush( 2 );
		expect( snackbar.exception ).toHaveBeenCalled();
		expect( page.trusting() ).toBe( false );
		fixture.destroy();
	} );
} );

//#427 (docs/clients/spec.md *Diagnostics*):  the Server tab, built when shown - matTabContent - over the row's gateway and slug, so a
//page opened on Properties or Connection sends the server nothing, and a new connection's tab has no slug to read under.
describe( 'ClientDetail server tab', ()=>{
	beforeEach( ()=>localStorage.setItem('client-detail/tabIndex', '2') );//Server
	afterEach( ()=>localStorage.removeItem('client-detail/tabIndex') );
	const reads = { nodes: [{id: {ns: 0, i: 2256}, value: {State: 0, BuildInfo: {}}}, {id: {ns: 0, i: 2275}, value: {CurrentSessionCount: 1}}, {id: {ns: 0, i: 2290}, value: []}, {id: {ns: 0, i: 3707}, value: []}] };
	const make = ( row:any )=>{
		const query = vi.fn( async ( ql:string, _vars:any )=>ql.startsWith( "nodes(" ) ? reads : { serverConnection: {clientDiagnostics: null, rejectedCertificate: null, clientCertificate: null} } );
		configureDetailPage( row, {gateway: {slug: "g", query}, providers: [
			{ provide: OpcStore, useValue: {forget: ()=>{}} },
			{ provide: LiveAnnouncer, useValue: {announce: async ()=>{}} }
		]} );
		const fixture = TestBed.createComponent( ClientDetail );
		return { fixture, page: fixture.componentInstance, query };
	};
	//detectChanges runs ngOnInit once (the route subscription) and renders the Server tab;  destroy unsubscribes it.
	const open = async ( fixture:any )=>{ fixture.detectChanges(); await flush( 3 ); };

	it( 'reads the server and the client for the row once the tab is shown', async ()=>{
		const { fixture, page, query } = make( {id: 7, slug: "plc", name: "plc"} );
		await open( fixture );
		expect( page.tabIndex() ).toBe( 2 );
		const sent = query.mock.calls.map( ([ql, vars])=>[ql.split( "(" )[0], vars?.opc] );
		expect( sent ).toContainEqual( ["nodes", "plc"] );
		expect( sent ).toContainEqual( ["serverConnection", "plc"] );
		expect( (fixture.nativeElement as HTMLElement).querySelector("server-diagnostics section.status") ).not.toBeNull();
		fixture.destroy();
	} );
	//a new connection lands on Properties (the tab-index describe), whose form cannot render against the stub schema here;  its Server
	//tab, shown later, has no slug and reads nothing - server-diagnostics.spec's 'not saved yet'
} );
//docs/open-protocol/spec.md *Web* (#475):  the Kind pick swaps the form - a UA row's url, certificate URI, namespace, mode and policy for
//an Open Protocol row's own fields, rendered after the pick - Test Connection sends the kind and the driver settings and shows the
//controller's identity, and a saved Open Protocol row's `_Op` is read from its driver server for the Connection tab.
describe( 'ClientDetail Open Protocol', ()=>{
	const identity = { controllerName: "PF6000-1", supplierCode: "ACT", openProtocolVersion: "2.20.1", controllerSoftwareVersion: "3.8.0", toolSoftwareVersion: null, controllerSerialNumber: "SN1", systemType: 3, systemSubtype: 1, stationId: 2, stationName: "Station A" };
	const opRows = [ {id: {ns: 2, s: "_Op.Connected"}, value: true}, {id: {ns: 2, s: "_Op.Url"}, value: "open-protocol://plc:4545"}, {id: {ns: 2, s: "_Op.Skew"}, value: {sc: 0x80320000}} ];
	//a row's describe as the resolver stores it, whole:  the Connection tab renders every field of it
	const described = ( applicationName:string )=>({ applicationName, applicationUri: "urn:x", applicationType: "Server", productUri: "", gatewayServerUri: "", discoveryProfileUri: "", discoveryUrls: [], policy: "http://opcfoundation.org/UA/SecurityPolicy#None", mode: "None", namespaces: [] });
	type Send = ( ql:string, vars:any )=>Promise<unknown>;
	const unsetSettings = { securityMode: null, securityPolicy: null, sessionTimeout: null, requestTimeout: null, pingInterval: null, ttl: null, locale: null };
	//the connection's schema with the columns the form shows, so the Properties tab renders where a test lands on it - a new row's does -
	//and the Open Protocol fields with it
	const schema = ()=>slugSchema( "ServerConnection", scalar("url"), scalar("description"), scalar("certificateUri"), scalar("defaultBrowseNs", "Int"), scalar("kind"), scalar("driver"), scalar("securityMode"), scalar("securityPolicy"), scalar("sessionTimeout", "Int"), scalar("requestTimeout", "Int"), scalar("pingInterval", "Int"), scalar("ttl", "Int"), scalar("locale") );
	const pageData = ( row:any, gateway:unknown )=>({pageData: {row, routing: {}, schema: schema(), gateway}});
	//the page over a gateway whose test answers through `send` and whose row reads answer the certificates and `_Op`, rendered on the
	//stored tab:  this gateway's queries resolve, so the row's loads are real async and change detection runs after them - through
	//the fixture, never a bare ngOnInit, and destroyed at the end of each test, as the trust describe's are
	const create = async ( row:any, send:Send=async ()=>identity, nodes:unknown[]=opRows, data?:Observable<any> )=>{
		const query = vi.fn( async ( ql:string, _vars:any )=>ql.startsWith( "nodes(" ) ? { nodes } : { serverConnection: {rejectedCertificate: null, clientCertificate: null} } );
		const announce = vi.fn( async ( _m:string )=>{} );
		const gateway = { slug: "g", query, postQL: async ( ql:string, vars:any )=>({ testConnection: await send(ql, vars) }) };
		configureDetailPage( row, {gateway, data: data ?? new BehaviorSubject<any>( pageData(row, gateway) ), providers: [
			{ provide: LiveAnnouncer, useValue: {announce} },
			{ provide: OpcStore, useValue: {forget: ()=>{}} }
		]} );
		const fixture = TestBed.createComponent( ClientDetail );
		const page = fixture.componentInstance;
		fixture.detectChanges();//runs ngOnInit once (the route subscription) and renders the tab the stored index names
		await flush( 2 );
		return { fixture, page, query, gateway, announced: ()=>announce.mock.calls.map( ([m])=>m ) };
	};
	const edit = ( page:ClientDetail, fields:Record<string,unknown> )=>page.properties.set( Properties.edited(ServerCnnctn, page.properties(), fields) );
	beforeEach( ()=>localStorage.setItem('client-detail/tabIndex', '1') );//Connection, where a saved row's `_Op` shows;  a new row still lands on Properties
	afterEach( ()=>localStorage.removeItem('client-detail/tabIndex') );

	it( 'offers the two kinds, and the flow server\'s row its own, read-only', async ()=>{
		const { fixture, page } = await create( {} );
		expect( page.picks().kind ).toEqual( [{value: "ua", label: "OPC UA"}, {value: "open-protocol", label: "Open Protocol"}] );
		expect( page.readonlyFields() ).toEqual( [] );
		fixture.destroy();
		TestBed.resetTestingModule();//this test builds twice
		const flow = await create( {id: 3, slug: "flow-server", name: "Flows", kind: "flow", url: "opc.tcp://127.0.0.1:4860"} );
		expect( flow.page.picks().kind.map( o=>o.value ) ).toEqual( ["ua", "open-protocol", "flow"] );
		expect( flow.page.readonlyFields() ).toEqual( ["slug", "kind"] );
		flow.fixture.destroy();
	} );
	it( 'swaps the form with the kind:  the url and the four the gateway fixes go, the driver column never shows', async ()=>{
		const { fixture, page } = await create( {} );
		expect( page.formKind() ).toBe( "ua" );
		expect( page.excludedColumns() ).toEqual( ["driver"] );
		expect( page.fieldOrder.indexOf("kind") ).toBe( page.fieldOrder.indexOf("description")+1 );
		edit( page, {kind: "open-protocol"} );
		expect( page.formKind() ).toBe( "open-protocol" );
		expect( page.excludedColumns() ).toEqual( ["driver", "url", "certificateUri", "defaultBrowseNs", "securityMode", "securityPolicy"] );
		expect( page.canDiscover() ).toBe( false );//a controller answers no discovery
		fixture.destroy();
	} );
	it( 'renders its own fields after the Kind pick, and only for an Open Protocol form', async ()=>{
		localStorage.setItem( 'client-detail/tabIndex', '0' );//Properties, where the form is
		const { fixture, page } = await create( {id: 7, slug: "pf", name: "PF", kind: "open-protocol", url: "open-protocol://plc:4545", driver: '{"timeZone":"UTC"}', server: described("Jde-Cpp open-protocol pf")} );
		expect( Object.keys(page.slots()) ).toEqual( ["kind"] );
		const root = fixture.nativeElement as HTMLElement;
		const labels = ()=>Array.from( root.querySelectorAll('properties mat-label') ).map( l=>l.textContent!.trim() );
		expect( labels() ).toEqual( ["Slug", "Name", "Description", "Kind", "Host", "Port", "Time Zone", "Revision Ceilings", "Session Timeout", "Request Timeout", "Ping Interval", "Idle TTL", "Locales"] );
		expect( (root.querySelector('open-protocol-fields input[type=text]') as HTMLInputElement).value ).toBe( "plc" );
		edit( page, {kind: "ua"} );
		await flush( 2 );
		expect( page.slots() ).toEqual( {} );
		expect( labels() ).toEqual( ["Slug", "Name", "Description", "Kind", "URL", "Certificate URI", "Default Namespace", "Security Mode", "Security Policy", "Session Timeout", "Request Timeout", "Ping Interval", "Idle TTL", "Locales"] );
		fixture.destroy();
	} );
	it( 'tests an Open Protocol form with its kind and driver settings and shows the controller it reached', async ()=>{
		const send = vi.fn( async ( _ql:string, _vars:any )=>identity );
		const { fixture, page, announced } = await create( {}, send );
		edit( page, {kind: "open-protocol", slug: "pf", certificateUri: "urn:x", securityMode: "Sign"} );//the two left from a UA form say nothing
		expect( page.canTest() ).toBe( false );//no host yet
		edit( page, {host: "plc", port: "", timeZone: "UTC", revisions: "0061:3"} );
		expect( page.canTest() ).toBe( true );
		await page.onTestClick();
		expect( send.mock.calls[0][1] ).toEqual( {opc: "pf", url: "open-protocol://plc:4545", kind: "open-protocol", driver: '{"timeZone":"UTC","revisions":{"0061":3}}', certificateUri: "", ...unsetSettings} );
		expect( page.identity()?.controllerName ).toBe( "PF6000-1" );
		expect( page.server() ).toBeUndefined();
		expect( page.serverError() ).toBeUndefined();
		expect( page.untested ).toBe( false );
		expect( page.tabIndex() ).toBe( 1 );
		expect( announced() ).toEqual( ["Connecting to open-protocol://plc:4545.", "Connected to PF6000-1."] );
		expect( page.staleFor() ).toBeUndefined();
		edit( page, {timeZone: "Europe/Stockholm", revisions: ""} );
		expect( page.staleFor() ).toBe( "Time Zone UTC, Revision Ceilings 0061:3" );
		edit( page, {kind: "ua", url: "opc.tcp://plc:4840"} );
		expect( page.staleFor() ).toContain( "Kind Open Protocol" );
		fixture.destroy();
	} );
	it( 'cannot test a field the gateway would refuse', async ()=>{
		const send = vi.fn( async ( _ql:string, _vars:any )=>identity );
		const { fixture, page } = await create( {}, send );
		edit( page, {kind: "open-protocol", slug: "pf", host: "plc", port: "70000"} );
		expect( page.canTest() ).toBe( false );
		await page.onTestClick();
		expect( send ).not.toHaveBeenCalled();
		edit( page, {port: "4545"} );
		expect( page.canTest() ).toBe( true );
		fixture.destroy();
	} );
	it( 'shows a failed test\'s reason in the not-connected state and clears the identity', async ()=>{
		const { fixture, page } = await create( {}, async ()=>{ throw new Error( "(504)'plc:4545' did not answer MID 0001 within 10 s" ); } );
		edit( page, {kind: "open-protocol", slug: "pf", host: "plc"} );
		await page.onTestClick();
		expect( page.identity() ).toBeUndefined();
		expect( page.serverError() ).toContain( "MID 0001" );
		expect( page.untested ).toBe( false );
		fixture.destroy();
	} );
	it( 'reads a saved row\'s _Op from its driver server, by the eleven ids, and keeps its describe under a test\'s identity', async ()=>{
		const { fixture, page, query } = await create( {id: 7, slug: "pf", name: "PF", kind: "open-protocol", url: "open-protocol://plc:4545", server: described("Jde-Cpp open-protocol pf")} );
		expect( query.mock.calls.filter( ([ql])=>ql==opQuery ).map( ([, vars])=>vars ) ).toEqual( [{opc: "pf", id: opIds}] );
		expect( page.op()?.Connected ).toEqual( {value: true} );
		expect( page.op()?.Skew ).toEqual( {sc: 0x80320000} );
		expect( page.opReading() ).toBe( false );
		await flush( 2 );
		const root = fixture.nativeElement as HTMLElement;
		expect( root.querySelector('server-properties section.op h3')!.textContent ).toBe( "Driver" );
		expect( Array.from(root.querySelectorAll('server-properties section.op dt')).map( d=>d.textContent ) ).toContain( "Clock Skew" );
		await page.onTestClick();
		expect( page.identity()?.controllerName ).toBe( "PF6000-1" );
		expect( page.server()?.applicationName ).toBe( "Jde-Cpp open-protocol pf" );//the driver server's session is not the test's to drop
		expect( query.mock.calls.filter( ([ql])=>ql==opQuery ).length ).toBe( 2 );//read again after the test
		await flush( 2 );
		expect( Array.from(root.querySelectorAll('server-properties section')).map( s=>s.className ) ).toEqual( ["identity", "op"] );
		fixture.destroy();
	} );
	it( 'reads no _Op for a UA row or a new one, and says why one could not be read', async ()=>{
		const ua = await create( {id: 7, slug: "plc", name: "PLC", url: "opc.tcp://plc:4840", server: described("PLC")} );
		expect( ua.query.mock.calls.filter( ([ql])=>ql==opQuery ) ).toEqual( [] );
		expect( ua.page.op() ).toBeUndefined();
		expect( (ua.fixture.nativeElement as HTMLElement).querySelector('server-properties dl.fields') ).not.toBeNull();//the description, as before
		ua.fixture.destroy();
		TestBed.resetTestingModule();//this test builds three times
		const fresh = await create( {} );
		expect( fresh.query.mock.calls.filter( ([ql])=>ql==opQuery ) ).toEqual( [] );
		fresh.fixture.destroy();
		TestBed.resetTestingModule();
		const gateway = { slug: "g", query: vi.fn( async ( ql:string, _vars:any )=>{ if( ql.startsWith("nodes(") ) throw new Error( "(502) no session with the driver server" ); return { serverConnection: {rejectedCertificate: null, clientCertificate: null} }; } ) };
		const pf = {id: 7, slug: "pf", name: "PF", kind: "open-protocol", url: "open-protocol://plc:4545", server: described("Jde-Cpp open-protocol pf")};
		configureDetailPage( pf, {gateway, data: new BehaviorSubject<any>( pageData(pf, gateway) ), providers: [
			{ provide: LiveAnnouncer, useValue: {announce: async ()=>{}} },
			{ provide: OpcStore, useValue: {forget: ()=>{}} }
		]} );
		const fixture = TestBed.createComponent( ClientDetail );
		const page = fixture.componentInstance;
		fixture.detectChanges();
		await flush( 2 );
		expect( page.op() ).toBeUndefined();
		expect( page.opError() ).toContain( "no session" );
		expect( page.opReading() ).toBe( false );
		expect( (fixture.nativeElement as HTMLElement).querySelector('server-properties section.op p.op-error')!.textContent ).toContain( "no session" );
		fixture.destroy();
	} );
	it( 'drops an _Op read that answers after the page moved to another connection', async ()=>{
		let answer!:( r:unknown )=>void;
		const query = vi.fn( ( ql:string, _vars:any )=>ql.startsWith( "nodes(" ) ? new Promise( r=>{ answer = r; } ) : Promise.resolve( {serverConnection: {rejectedCertificate: null, clientCertificate: null}} ) );
		const gateway = { slug: "g", query };
		const pf = {id: 7, slug: "pf", name: "PF", kind: "open-protocol", url: "open-protocol://plc:4545", server: described("Jde-Cpp open-protocol pf")};
		const route = new BehaviorSubject<any>( pageData(pf, gateway) );
		configureDetailPage( pf, {gateway, data: route, providers: [
			{ provide: LiveAnnouncer, useValue: {announce: async ()=>{}} },
			{ provide: OpcStore, useValue: {forget: ()=>{}} }
		]} );
		const fixture = TestBed.createComponent( ClientDetail );
		const page = fixture.componentInstance;
		fixture.detectChanges();
		expect( page.opReading() ).toBe( true );
		route.next( pageData({id: 8, slug: "plc", name: "PLC", url: "opc.tcp://plc:4840", server: described("PLC")}, gateway) );
		expect( page.opReading() ).toBe( false );
		answer( {nodes: opRows} );
		await flush( 2 );
		expect( page.op() ).toBeUndefined();//the first row's, for a page that shows the second
		fixture.destroy();
	} );
} );