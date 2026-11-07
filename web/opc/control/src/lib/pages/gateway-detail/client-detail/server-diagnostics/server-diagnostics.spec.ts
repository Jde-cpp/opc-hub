import { TestBed } from '@angular/core/testing';
import { SnackbarService } from 'jde-framework';
import { snackbarStub } from 'jde-framework/testing';
import { flush, stable } from 'jde-spa/testing';
import { ClientDiagnostics, clientDiagnosticsQuery, serverDiagnosticsIds, serverDiagnosticsQuery, serverDiagnosticsSummaryId, serverStatusId, sessionDiagnosticsArrayId, subscriptionDiagnosticsArrayId } from '../../../../model/server';
import { ageText, enumName, memberLabel, nodeIdText, refusedClientText, ServerDiagnostics } from './server-diagnostics';

//the embedded OpcServer's answers, as the gateway decodes them (open62541's reversible json:  the enum a number, a NodeId a string)
const status = { StartTime: "2026-11-04T01:33:11.617Z", CurrentTime: "2026-11-04T01:33:14.229Z", State: 0, BuildInfo: { ProductUri: "http://open62541.org", ManufacturerName: "open62541", ProductName: "open62541 OPC UA Server", SoftwareVersion: "1.5.10", BuildNumber: "Oct 31 2026", BuildDate: "2026-11-04T01:33:09.725Z" }, SecondsTillShutdown: 0, ShutdownReason: { Locale: null, Text: null } };
const summary = { ServerViewCount: 0, CurrentSessionCount: 1, CumulatedSessionCount: 3, SecurityRejectedSessionCount: 0, RejectedSessionCount: 0, SessionTimeoutCount: 0, SessionAbortCount: 0, CurrentSubscriptionCount: 1, CumulatedSubscriptionCount: 2, PublishingIntervalCount: 1, SecurityRejectedRequestsCount: 0, RejectedRequestsCount: 0 };
const session = { SessionId: "ns=1;g=883e3c6c", SessionName: "ns=1;g=883e3c6c", ClientDescription: { ApplicationUri: "urn:workstation:Jde-Cpp:OpcHub", ApplicationName: { Locale: null, Text: null }, ApplicationType: 1 }, EndpointUrl: "opc.tcp://127.0.0.1:4840", ActualSessionTimeout: 1.2E6, ClientConnectionTime: "2026-11-04T01:33:14.107Z", ClientLastContactTime: "2026-11-04T01:33:14.229Z", CurrentSubscriptionsCount: 1, CurrentMonitoredItemsCount: 4 };
const subscription = { SessionId: "ns=1;g=883e3c6c", SubscriptionId: 1, Priority: 0, PublishingInterval: 100, MaxKeepAliveCount: 10, MaxLifetimeCount: 10000, PublishingEnabled: true, MonitoredItemCount: 4 };
const client:ClientDiagnostics = { connectedSince: "2026-11-04T01:34:23.717Z", reconnects: 1, lastBreak: "2026-11-04T01:34:24.942Z", sessionAge: 3725, monitoredItems: 4, parkedItems: 0, reconnectWaiting: false, clients: 1 };
type Reads = { status?:unknown; summary?:unknown; sessions?:unknown; subscriptions?:unknown };
const rows = ( r:Reads )=>[ [subscriptionDiagnosticsArrayId, r.subscriptions ?? []], [serverStatusId, r.status ?? status], [serverDiagnosticsSummaryId, r.summary ?? summary], [sessionDiagnosticsArrayId, r.sessions ?? []] ].map( ([i, value])=>({ id: {ns: 0, i}, value }) );//in the gateway's id order, not the asked one

//the tab over a gateway whose query answers `answer( ql, vars )`:  the four reads, or the client, by the query sent
const createWith = ( answer:( ql:string, vars:any )=>Promise<unknown>, slug:string|null="plc" )=>{//null:  not saved - a default parameter would replace undefined
	const snackbar = snackbarStub();
	TestBed.configureTestingModule({ providers: [{ provide: SnackbarService, useValue: snackbar }] });
	const fixture = TestBed.createComponent( ServerDiagnostics );
	const query = vi.fn( answer );
	fixture.componentRef.setInput( "gateway", { query } );
	fixture.componentRef.setInput( "slug", slug ?? undefined );
	return { fixture, query, snackbar };
};
const create = ( r:Reads={}, c:ClientDiagnostics|null=client, {refuseClient=false, serverError}:{refuseClient?:boolean; serverError?:string}={} )=>createWith( async ( ql, _vars )=>{
	if( ql==clientDiagnosticsQuery ){
		if( refuseClient )
			throw { status: 403, error: {detail: "Forbidden"} };
		return { serverConnection: {clientDiagnostics: c} };
	}
	if( serverError )
		throw new Error( serverError );
	return { nodes: rows( r ) };
} );
const render = async ( fixture:ReturnType<typeof create>["fixture"] )=>{
	await stable( fixture, 2 );
	return fixture.nativeElement as HTMLElement;
};
const texts = ( root:HTMLElement, selector:string )=>Array.from( root.querySelectorAll(selector) ).map( e=>e.textContent!.trim() );
const cells = ( root:HTMLElement, table:string )=>[...root.querySelectorAll<HTMLElement>(`table.${table} tbody tr`)].map( r=>[...r.querySelectorAll("td")].map( c=>c.textContent!.trim() ) );
const field = ( root:HTMLElement, section:string, label:string )=>{
	const dts = [...root.querySelectorAll<HTMLElement>(`${section} dl.fields dt`)];
	const dt = dts.find( d=>d.textContent!.trim()==label );
	return dt?.nextElementSibling?.firstElementChild?.textContent!.trim();//the value, not the hint under it
};

describe( 'ServerDiagnostics', ()=>{
	it( 'sends the four reads by id with the client query, and matches the rows by id', async ()=>{
		const { fixture, query } = create( {sessions: [session], subscriptions: [subscription]} );
		const page = await render( fixture );
		expect( query.mock.calls ).toEqual( [[serverDiagnosticsQuery, {opc: "plc", id: serverDiagnosticsIds}], [clientDiagnosticsQuery, {opc: "plc"}]] );
		expect( serverDiagnosticsIds.map( id=>id.i ) ).toEqual( [2256, 2275, 3707, 2290] );//ServerStatus, ServerDiagnosticsSummary, SessionDiagnosticsArray, SubscriptionDiagnosticsArray
		expect( field(page, "section.status", "State") ).toBe( "Running" );
		expect( field(page, "section.status", "Product name") ).toBe( "open62541 OPC UA Server" );
		expect( field(page, "section.status", "Software version") ).toBe( "1.5.10" );
		expect( field(page, "section.status", "Start time") ).not.toBe( "" );//the date renders, in the viewer's locale
		expect( field(page, "section.status", "Shutdown reason") ).toBe( "none" );
		expect( field(page, "section.summary", "Current session count") ).toBe( "1" );
		expect( field(page, "section.summary", "Cumulated subscription count") ).toBe( "2" );
		expect( texts(page, "section.summary dt").length ).toBe( 12 );//every member the server sent, labelled from its name
		expect( cells(page, "sessions") ).toEqual( [["ns=1;g=883e3c6c", "urn:workstation:Jde-Cpp:OpcHub", "opc.tcp://127.0.0.1:4840", cells(page, "sessions")[0][3], cells(page, "sessions")[0][4], "1", "4"]] );
		expect( cells(page, "sessions")[0][3] ).not.toBe( "" );
		expect( cells(page, "subscriptions") ).toEqual( [["1", "ns=1;g=883e3c6c", "100 ms", "0", "yes", "4", "10", "10000"]] );
		expect( page.querySelector("table.sessions caption .row-count")?.textContent ).toBe( "(1)" );
	} );

	//the gateway's own shapes since its member walk (docs/clients/spec.md *Structures*, #421):  a DateTime {seconds, nanos}, a NodeId {ns, g}
	it( 'renders the gateway\'s own DateTime and NodeId shapes', async ()=>{
		const started = { seconds: 1762219991, nanos: 617000000 };
		const own = { ...status, StartTime: started, CurrentTime: { seconds: 1762219994, nanos: 0 } };
		const ownSession = { ...session, SessionId: { ns: 1, g: "883e3c6c" }, ClientConnectionTime: started };
		const ownSubscription = { ...subscription, SessionId: { ns: 1, g: "883e3c6c" } };
		const { fixture } = create( {status: own, sessions: [ownSession], subscriptions: [ownSubscription]} );
		const page = await render( fixture );
		const date = new Date( started.seconds*1000+617 );
		expect( field(page, "section.status", "Start time") ).toBe( fixture.componentInstance["statusFields"]().find( f=>f.label=="Start time" )!.value );
		expect( field(page, "section.status", "Start time") ).toContain( String(date.getFullYear()) );
		expect( cells(page, "sessions")[0][0] ).toBe( "ns=1;g=883e3c6c" );
		expect( cells(page, "sessions")[0][3] ).toContain( String(date.getFullYear()) );
		expect( cells(page, "subscriptions")[0][1] ).toBe( "ns=1;g=883e3c6c" );
	} );

	//a server may leave the arrays empty, and the tab says so - without naming the EnabledFlag, which does not decide it
	it( 'explains an empty session or subscription array as the server\'s', async ()=>{
		const page = await render( create().fixture );
		expect( cells(page, "sessions") ).toEqual( [["None listed. A server may leave its session diagnostics empty."]] );//the template's double space collapses
		expect( cells(page, "subscriptions") ).toEqual( [["None listed. A server may leave its subscription diagnostics empty."]] );
		expect( page.textContent ).not.toContain( "EnabledFlag" );
	} );

	it( 'shows the gateway\'s own client for the connection', async ()=>{
		const page = await render( create().fixture );
		expect( field(page, "section.client", "Reconnects") ).toBe( "1" );
		expect( field(page, "section.client", "Last break") ).not.toBe( "never" );
		expect( field(page, "section.client", "Session age") ).toBe( "1 h 2 m 5 s" );
		expect( field(page, "section.client", "Monitored items") ).toBe( "4" );
		expect( field(page, "section.client", "Reconnect waiting") ).toBe( "no" );
		expect( field(page, "section.client", "Clients") ).toBe( "1" );
	} );

	it( 'says a client that never broke has no last break', async ()=>{
		const page = await render( create({}, {...client, reconnects: 0, lastBreak: null}).fixture );
		expect( field(page, "section.client", "Last break") ).toBe( "never" );
		expect( field(page, "section.client", "Reconnects") ).toBe( "0" );
	} );

	//a reconnect between attempts:  no client is registered, and the gateway answers with the lost one its chain holds, sessionAge null
	it( 'says a lost client is reconnecting, with the items it parked', async ()=>{
		const page = await render( create({}, {...client, sessionAge: null, monitoredItems: 0, parkedItems: 4, reconnectWaiting: true}, {serverError: "BadConnectionClosed"}).fixture );
		expect( page.querySelector("section.client .reconnecting")?.textContent ).toContain( "Reconnecting" );
		expect( page.querySelector("section.client .no-client") ).toBeNull();
		expect( field(page, "section.client", "Session age") ).toBe( "none - reconnecting" );
		expect( field(page, "section.client", "Parked items") ).toBe( "4" );
		expect( field(page, "section.client", "Reconnect waiting") ).toBe( "yes" );
		expect( field(page, "section.client", "Reconnects") ).toBe( "1" );
	} );
	it( 'says nothing of a reconnect for a live client', async ()=>{
		const page = await render( create().fixture );
		expect( page.querySelector("section.client .reconnecting") ).toBeNull();
	} );

	//a server that does not answer yet - a connect waiting out its timeout - holds back its own sections alone
	it( 'shows the client while the server read is still connecting', async ()=>{
		const { fixture } = createWith( async ql=>ql==clientDiagnosticsQuery ? { serverConnection: {clientDiagnostics: client} } : new Promise( ()=>{} ) );
		const page = await render( fixture );
		expect( field(page, "section.client", "Reconnects") ).toBe( "1" );
		expect( page.querySelector("section.status") ).toBeNull();
		expect( fixture.componentInstance.isLoading() ).toBe( true );
	} );
	it( 'holds a null client until the server read answers', async ()=>{
		const { fixture } = createWith( async ql=>ql==clientDiagnosticsQuery ? { serverConnection: {clientDiagnostics: null} } : new Promise( ()=>{} ) );
		const page = await render( fixture );
		expect( page.querySelector("section.client .no-client") ).toBeNull();
		expect( fixture.componentInstance.clientLoading() ).toBe( true );
	} );

	//the first view of a connection with no client:  the server's reads build it, after the client query beside them answered null
	it( 'asks for the client again once the server read has connected it', async ()=>{
		let asked = 0;
		const { fixture, query } = createWith( async ( ql, _vars )=>ql==clientDiagnosticsQuery ? { serverConnection: {clientDiagnostics: asked++ ? client : null} } : { nodes: rows({}) } );
		const page = await render( fixture );
		expect( query.mock.calls.filter( ([ql])=>ql==clientDiagnosticsQuery ) ).toHaveLength( 2 );
		expect( page.querySelector("section.client .no-client") ).toBeNull();
		expect( field(page, "section.client", "Reconnects") ).toBe( "1" );
	} );
	it( 'does not ask again when the server read failed', async ()=>{
		const { fixture, query } = create( {}, null, {serverError: "BadConnectionClosed"} );
		const page = await render( fixture );
		expect( query.mock.calls.filter( ([ql])=>ql==clientDiagnosticsQuery ) ).toHaveLength( 1 );
		expect( page.querySelector("section.client .no-client")?.textContent ).toContain( "No session" );
	} );

	//null:  the gateway has no client for this session - the query never connects - and the server's reads still show
	it( 'says the gateway holds no client for this session when the client is null', async ()=>{
		const page = await render( create({}, null).fixture );
		expect( page.querySelector("section.client .no-client")?.textContent ).toContain( "No session" );
		expect( page.querySelector("section.client .no-client")?.textContent ).not.toContain( "Test Connection" );//its client is registered nowhere, so it never gives the gateway one
		expect( page.querySelector("section.client dl.fields") ).toBeNull();
		expect( field(page, "section.status", "State") ).toBe( "Running" );
	} );

	//the four reads need a session with the server:  they fail as one where the connection does, and the client's telemetry still answers
	it( 'shows why the server was not read, and the client beside it', async ()=>{
		const { fixture, snackbar } = create( {}, client, {serverError: "(502)'opc.tcp://plc:4840' refused the connection"} );
		const page = await render( fixture );
		expect( texts(page, ".not-read .title") ).toEqual( ["Server not read"] );
		expect( texts(page, ".not-read .detail") ).toEqual( ["(502)'opc.tcp://plc:4840' refused the connection"] );
		expect( page.querySelector("section.status") ).toBeNull();
		expect( field(page, "section.client", "Reconnects") ).toBe( "1" );
		expect( snackbar.exception ).toHaveBeenCalledTimes( 1 );
	} );

	it( 'says so in place when the client telemetry is refused, with no toast', async ()=>{
		const { fixture, snackbar } = create( {}, client, {refuseClient: true} );
		const page = await render( fixture );
		expect( texts(page, "section.client .empty") ).toEqual( [refusedClientText] );
		expect( snackbar.exception ).not.toHaveBeenCalled();
		expect( field(page, "section.status", "State") ).toBe( "Running" );
	} );

	it( 'reads nothing for a connection not saved yet, and says to save it', async ()=>{
		const { fixture, query } = createWith( async ()=>({}), null );
		const page = await render( fixture );
		expect( query ).not.toHaveBeenCalled();
		expect( texts(page, ".unsaved") ).toEqual( ["Save the connection to read the server's diagnostics."] );
		expect( (page.querySelector("button[aria-label=Refresh]") as HTMLButtonElement).disabled ).toBe( true );
	} );

	//a read the server does not serve answers a status alone ({sc}) where the others answered:  that section says so, the rest shows
	//a server restricting its diagnostics arrays answers a status, not an empty list
	//keyboard and screen-reader users reach neither through a hover
	it( 'shows a session\'s id under its name, where the Subscriptions table names it', async ()=>{
		const named = { ...session, SessionName: "Jde OpcHub" };
		const page = await render( create({sessions: [named], subscriptions: [subscription]}).fixture );
		expect( texts(page, "table.sessions td .node-id") ).toEqual( ["ns=1;g=883e3c6c"] );
		expect( texts(page, "table.sessions td:first-child > span:first-child") ).toEqual( ["Jde OpcHub"] );
		expect( cells(page, "subscriptions")[0][1] ).toBe( "ns=1;g=883e3c6c" );
	} );
	it( 'writes the client fields\' explanations as text', async ()=>{
		const page = await render( create().fixture );
		expect( texts(page, "section.client dd .hint") ).toEqual( [
			"Items a lost connection is holding for its reconnect - on no client until it comes back.",
			"A reconnect of this connection is in its backoff wait.",
			"The gateway's open clients for this connection, one per credential."
		] );
		expect( page.querySelector("section.client [title]") ).toBeNull();
		expect( field(page, "section.client", "Parked items") ).toBe( "0" );
	} );
	//the lists take the tab's width, not the form's, and scroll sideways where even that is too narrow - never a value broken mid-word
	it( 'gives the session list the tab\'s width, its values whole', async ()=>{
		const page = await render( create({sessions: [session], subscriptions: [subscription]}).fixture );
		const sessions = page.querySelector<HTMLElement>( "table.sessions" )!;
		const scroll = sessions.parentElement!;
		expect( scroll.classList ).toContain( "table-scroll" );
		expect( getComputedStyle(scroll).overflowX ).toBe( "auto" );
		expect( getComputedStyle(scroll).maxWidth ).toBe( "none" );
		expect( getComputedStyle(page.querySelector("section.client")!).maxWidth ).toBe( "var(--jde-form-max-width)" );
		expect( getComputedStyle(sessions.querySelector("td")!).overflowWrap ).toBe( "normal" );
		expect( [...sessions.querySelectorAll("td.time")].map( td=>getComputedStyle(td).whiteSpace ) ).toEqual( ["nowrap", "nowrap"] );
		expect( page.querySelector("table.subscriptions")!.parentElement!.classList ).toContain( "table-scroll" );
	} );
	it( 'says an array read was refused, not left empty', async ()=>{
		const page = await render( create({sessions: {sc: 0x801F0000}}).fixture );
		expect( cells(page, "sessions") ).toEqual( [["The server did not answer its session diagnostics (Bad 0x801F0000)."]] );
		expect( cells(page, "subscriptions") ).toEqual( [["None listed. A server may leave its subscription diagnostics empty."]] );
	} );
	it( 'marks one read the server did not answer, keeping the others', async ()=>{
		const page = await render( create({summary: {sc: 0x80340000}, sessions: [session]}).fixture );
		expect( texts(page, "section.summary .empty") ).toEqual( ["The server did not answer the diagnostics summary (Bad 0x80340000)."] );
		expect( field(page, "section.status", "State") ).toBe( "Running" );
		expect( cells(page, "sessions").length ).toBe( 1 );
	} );
	//the code as the rest of the app writes it, and a Good one is no failure
	it( 'says a Good read with no value came back empty', async ()=>{
		const page = await render( create({status: {sc: 0}, sessions: {sc: 0}}).fixture );
		expect( texts(page, "section.status .empty") ).toEqual( ["The server answered ServerStatus with no value."] );
		expect( cells(page, "sessions") ).toEqual( [["The server answered its session diagnostics with no value."]] );
		expect( page.textContent ).not.toContain( "0x00000000" );
	} );
	it( 'writes an Uncertain code by its severity', async ()=>{
		const page = await render( create({summary: {sc: 0x40920000}}).fixture );
		expect( texts(page, "section.summary .empty") ).toEqual( ["The server did not answer the diagnostics summary (Uncertain 0x40920000)."] );
	} );

	it( 'keeps the server sections up while a refresh is out', async ()=>{
		let hold = false;
		const { fixture } = createWith( async ql=>ql==clientDiagnosticsQuery ? { serverConnection: {clientDiagnostics: client} } : hold ? new Promise( ()=>{} ) : { nodes: rows({}) } );
		const page = await render( fixture );
		hold = true;
		( page.querySelector("button[aria-label=Refresh]") as HTMLButtonElement ).click();
		await stable( fixture, 2 );
		expect( fixture.componentInstance.serverLoading() ).toBe( true );
		expect( field(page, "section.status", "State") ).toBe( "Running" );
	} );
	it( 'clears another row\'s figures while the new row reads', async ()=>{
		let hold = false;
		const { fixture } = createWith( async ql=>hold ? new Promise( ()=>{} ) : ql==clientDiagnosticsQuery ? { serverConnection: {clientDiagnostics: client} } : { nodes: rows({}) } );
		const page = await render( fixture );
		expect( field(page, "section.status", "State") ).toBe( "Running" );
		hold = true;
		fixture.componentRef.setInput( "slug", "other" );
		await stable( fixture, 2 );
		expect( page.querySelector("section.status") ).toBeNull();
		expect( page.querySelector("section.client dl.fields") ).toBeNull();
	} );
	//the router reuses the page for another gateway's connection of the same slug
	it( 'reads again from another gateway with the same slug', async ()=>{
		const { fixture, query } = create();
		const page = await render( fixture );
		const other = vi.fn( async ( ql:string )=>ql==clientDiagnosticsQuery ? { serverConnection: {clientDiagnostics: {...client, reconnects: 7}} } : { nodes: rows({}) } );
		fixture.componentRef.setInput( "gateway", { query: other } );
		await stable( fixture, 2 );
		expect( query ).toHaveBeenCalledTimes( 2 );
		expect( other ).toHaveBeenCalledTimes( 2 );
		expect( field(page, "section.client", "Reconnects") ).toBe( "7" );
	} );
	//matTabContent destroys the tab when it is left:  a read still out neither lands nor toasts
	it( 'drops a read still out when the tab is destroyed', async ()=>{
		let fail!:( e:unknown )=>void;
		const { fixture, snackbar } = createWith( ql=>ql==clientDiagnosticsQuery ? Promise.resolve( {serverConnection: {clientDiagnostics: client}} ) : new Promise( (_, reject)=>fail = reject ) );
		await render( fixture );
		const component = fixture.componentInstance;
		fixture.destroy();
		fail( new Error("BadTimeout") );
		await flush( 2 );
		expect( snackbar.exception ).not.toHaveBeenCalled();
		expect( component.serverError() ).toBeNull();
	} );
	it( 'drops a read still out when the row becomes unsaved', async ()=>{
		let answer!:( v:unknown )=>void;
		const { fixture } = createWith( ql=>ql==clientDiagnosticsQuery ? Promise.resolve( {serverConnection: {clientDiagnostics: client}} ) : new Promise( resolve=>answer = resolve ) );
		await render( fixture );
		fixture.componentRef.setInput( "slug", undefined );
		await stable( fixture, 2 );
		answer( {nodes: rows({})} );
		await stable( fixture, 2 );
		expect( fixture.componentInstance.status() ).toBeUndefined();
	} );
	it( 'refreshes on the button, keeping what it replaces until the answer', async ()=>{
		const { fixture, query } = create();
		const page = await render( fixture );
		expect( query ).toHaveBeenCalledTimes( 2 );
		( page.querySelector("button[aria-label=Refresh]") as HTMLButtonElement ).click();
		await stable( fixture, 2 );
		expect( query ).toHaveBeenCalledTimes( 4 );
		expect( field(page, "section.status", "State") ).toBe( "Running" );
	} );
} );

//the members read loosely:  open62541's reversible json is what the gateway decodes, and the non-reversible form is what a server's
//own json, or another encoder, would give
describe( 'ServerDiagnostics decoding', ()=>{
	it( 'names an enum from a number or its Name_Value form', ()=>{
		expect( enumName( 0, ["Running", "Failed"] ) ).toBe( "Running" );
		expect( enumName( "Running_0", ["Running", "Failed"] ) ).toBe( "Running" );
		expect( enumName( "Custom_9", ["Running", "Failed"] ) ).toBe( "Custom" );
		expect( enumName( 9, ["Running", "Failed"] ) ).toBe( "9" );
		expect( enumName( undefined, ["Running"] ) ).toBe( "" );
	} );
	it( 'renders a NodeId from its string or object form', ()=>{
		expect( nodeIdText( "ns=1;g=abc" ) ).toBe( "ns=1;g=abc" );
		expect( nodeIdText( {ns: 1, g: "abc"} ) ).toBe( "ns=1;g=abc" );//the gateway's own shape (docs/clients/spec.md *Structures*)
		expect( nodeIdText( {i: 7} ) ).toBe( "i=7" );
		expect( nodeIdText( {ns: 3, s: "tag"} ) ).toBe( "ns=3;s=tag" );
		expect( nodeIdText( {Id: 5, Namespace: 2} ) ).toBe( "ns=2;i=5" );
		expect( nodeIdText( {Id: "abc", IdType: 1} ) ).toBe( "s=abc" );
		expect( nodeIdText( {Id: 7} ) ).toBe( "i=7" );
		expect( nodeIdText( undefined ) ).toBe( "" );
	} );
	it( 'labels a member from its PascalCase name', ()=>{
		expect( memberLabel( "CumulatedSessionCount" ) ).toBe( "Cumulated session count" );
		expect( memberLabel( "ServerViewCount" ) ).toBe( "Server view count" );
	} );
	it( 'writes an age in hours, minutes and seconds', ()=>{
		expect( ageText( 5 ) ).toBe( "5 s" );
		expect( ageText( 65 ) ).toBe( "1 m 5 s" );
		expect( ageText( 3600 ) ).toBe( "1 h 0 m 0 s" );
	} );
} );