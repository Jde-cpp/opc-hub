import { TestBed } from '@angular/core/testing';
import { SnackbarService } from 'jde-framework';
import { snackbarStub } from 'jde-framework/testing';
import { stable } from 'jde-spa/testing';
import { EndpointDescription, endpointQuery, endpointSessionsQuery, EndpointSession } from '../../../model/gateway-endpoint';
import { GatewayEndpoint, refusedEndpointText, refusedText } from './gateway-endpoint';

const policy = ( name:string )=>`http://opcfoundation.org/UA/SecurityPolicy#${name}`;
const endpoint:EndpointDescription = { url: 'opc.tcp://hub:4841', policies: [{uri: policy('None'), mode: 'None'}, {uri: policy('Basic256Sha256'), mode: 'Sign'}, {uri: policy('Basic256Sha256'), mode: 'SignAndEncrypt'}], trustedCertDirs: ['/var/lib/jde/ssl/clients'], rejectedCertDir: '/var/lib/jde/ssl/rejected' };
const session = ( since:string, user:EndpointSession['user'], type='IssuedToken' ):EndpointSession=>({ id: `ns=1;g=${since}`, type, policy: policy('Basic256Sha256'), mode: 'SignAndEncrypt', since, user });

//the tab over a gateway whose query answers `answer( ql )`:  the endpoint's description, or its sessions, by the query sent
const createWith = ( answer:( ql:string )=>Promise<unknown> )=>{
	const snackbar = snackbarStub();
	TestBed.configureTestingModule({ providers: [{ provide: SnackbarService, useValue: snackbar }] });
	const fixture = TestBed.createComponent( GatewayEndpoint );
	const query = vi.fn( answer );
	fixture.componentRef.setInput( "gateway", { query } );
	return { fixture, query, snackbar };
};
const create = ( e:EndpointDescription|null, sessions:EndpointSession[]=[], refuseSessions=false )=>createWith( async ql=>{
	if( ql!=endpointSessionsQuery )
		return { endpoint: e };
	if( refuseSessions )
		throw { status: 403, error: {detail: "Forbidden"} };
	return { endpoint: e ? {sessions} : null };
} );
const render = async ( fixture:ReturnType<typeof create>["fixture"] )=>{
	await stable( fixture, 2 );
	return fixture.nativeElement as HTMLElement;
};
const cells = ( page:HTMLElement )=>[...page.querySelectorAll<HTMLElement>("table.sessions tbody tr")].map( r=>[...r.querySelectorAll("td")].map(c=>c.textContent!.trim()) );

describe( 'GatewayEndpoint', ()=>{
	it( 'describes the endpoint and lists its sessions, oldest first', async ()=>{
		const { fixture, query } = create( endpoint, [session("2026-10-25T12:30:00Z", {id: 1, name: "duffyj"}), session("2026-10-25T12:00:00Z", null, "Anonymous"), session("2026-10-25T12:10:00Z", {id: 7}, "Username")] );
		const page = await render( fixture );
		expect( query.mock.calls ).toEqual( [[endpointQuery], [endpointSessionsQuery]] );
		expect( page.querySelector(".fields")?.textContent ).toContain( "opc.tcp://hub:4841" );
		expect( [...page.querySelectorAll(".policy")].map( p=>[...p.children].map( c=>c.textContent!.trim() ) ) ).toEqual( [["None", "no security"], ["Basic256Sha256", "Sign · Sign & Encrypt"]] );//name and modes, the grid's two columns
		expect( [...page.querySelectorAll(".dir")].map( p=>p.textContent ) ).toEqual( ["/var/lib/jde/ssl/clients", "/var/lib/jde/ssl/rejected"] );
		const rows = cells( page );
		expect( rows.map( r=>r.slice(0, 3) ) ).toEqual( [["anonymous", "Anonymous", "Basic256Sha256 / Sign & Encrypt"], ["user 7", "Username", "Basic256Sha256 / Sign & Encrypt"], ["duffyj", "IssuedToken", "Basic256Sha256 / Sign & Encrypt"]] );
		expect( rows[0][3] ).not.toBe( "" );//the date renders, in the viewer's locale
		expect( page.querySelector("table.sessions caption .row-count")?.textContent ).toBe( "(3)" );//the count beside the list it counts, not in the page's toolbar
	} );

	it( 'says so when the gateway has no endpoint, and lists no sessions', async ()=>{
		const { fixture } = create( null );
		const page = await render( fixture );
		expect( page.querySelector(".empty")?.textContent ).toContain( "no OPC UA endpoint" );
		expect( page.querySelector("table.sessions") ).toBeNull();
	} );

	it( 'says so when it holds no sessions', async ()=>{
		const page = await render( create( endpoint ).fixture );
		expect( page.querySelector(".empty")?.textContent ).toContain( "No sessions" );
		expect( page.querySelector(".fields") ).not.toBeNull();
	} );

	//the sessions' gate is gateway/sessions, which the rest of the description is not behind:  refused, the url and policies still show
	it( 'keeps the description when the sessions are refused', async ()=>{
		const { fixture, snackbar } = create( endpoint, [], true );
		const page = await render( fixture );
		expect( page.querySelector(".fields")?.textContent ).toContain( "opc.tcp://hub:4841" );
		expect( page.querySelector(".load-error")?.textContent ).toBe( refusedText );
		expect( snackbar.exception ).not.toHaveBeenCalled();
	} );

	it( 'reloads on refresh', async ()=>{
		const { fixture, query } = create( endpoint, [session("2026-10-25T12:00:00Z", {id: 1, name: "duffyj"})] );
		const page = await render( fixture );
		page.querySelector<HTMLButtonElement>( "button[aria-label=Refresh]" )!.click();
		await stable( fixture, 2 );
		expect( query ).toHaveBeenCalledTimes( 4 );
	} );

	//gateway/endpoint (servers-7-web review #2):  a refusal is said in place, as the sessions' is, with no toast
	it( 'says so when the endpoint is refused', async ()=>{
		const { fixture, snackbar } = createWith( async ()=>{ throw { status: 403, error: {detail: "Forbidden"} }; } );
		const page = await render( fixture );
		expect( page.querySelector(".load-error")?.textContent ).toBe( refusedEndpointText );
		expect( page.querySelector(".fields") ).toBeNull();
		expect( snackbar.exception ).not.toHaveBeenCalled();
	} );

	it( 'says why the endpoint could not be read, and keeps Refresh as the retry', async ()=>{
		const { fixture, snackbar } = createWith( async ()=>{ throw new Error( "The gateway is not connected" ); } );
		const page = await render( fixture );
		expect( page.querySelector(".load-error")?.textContent ).toContain( "The gateway is not connected" );
		expect( snackbar.exception ).toHaveBeenCalledTimes( 1 );
		expect( page.querySelector<HTMLButtonElement>( "button[aria-label=Refresh]" )!.disabled ).toBe( false );
	} );
} );