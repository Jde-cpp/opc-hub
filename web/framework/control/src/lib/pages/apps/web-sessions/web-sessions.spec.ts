import { TestBed } from '@angular/core/testing';
import { SnackbarService } from '../../../shared/snackbar/snackbar-service';
import { byLastUpdate, refusedText, sessionUser, WebSession, WebSessions, webSessionsQuery } from './web-sessions';

const session = ( endpoint:string, lastUpdate:string, user?:{loginName:string, domain?:string} ):WebSession=>
	({ endpoint, lastUpdate, expiration: "2026-10-17T13:00:00Z", ...user });

let asked:string[];//the queries the component sent
let toasted:number;
const create = ( rows:WebSession[], failWith?:unknown )=>{
	asked = [];
	toasted = 0;
	TestBed.configureTestingModule({ providers: [{ provide: SnackbarService, useValue: { exception: ()=>{ ++toasted; } } }] });
	const fixture = TestBed.createComponent( WebSessions );
	fixture.componentRef.setInput( "service", { query: async ( ql:string )=>{ asked.push( ql ); if( failWith ) throw failWith; return { sessions: rows }; } } );
	fixture.componentRef.setInput( "instanceId", 42 );
	return fixture;
};
const render = async ( fixture:ReturnType<typeof create> )=>{
	fixture.detectChanges();
	await fixture.whenStable();
	fixture.detectChanges();
	return fixture.nativeElement as HTMLElement;
};
const cells = ( page:HTMLElement )=>[...page.querySelectorAll<HTMLElement>("mat-row")].map( r=>[...r.querySelectorAll("mat-cell")].map(c=>c.textContent!.trim()) );

describe( 'WebSessions', ()=>{
	it( 'asks the application server for the instance\'s sessions and lists them, most recent first', async ()=>{
		const page = await render( create([ session("10.0.0.2", "2026-10-17T12:00:00Z"), session("10.0.0.1", "2026-10-17T12:30:00Z", {loginName: "duffyj", domain: "google"}) ]) );
		expect( asked ).toEqual( [webSessionsQuery(42)] );
		expect( asked[0] ).toContain( "instanceId:42" );
		const rows = cells( page );
		expect( rows.map(r=>r.slice(0, 2)) ).toEqual( [["google\\duffyj", "10.0.0.1"], ["anonymous", "10.0.0.2"]] );
		expect( rows[0][2] ).not.toBe( "" );//the dates render, in the viewer's locale
		expect( rows[0][3] ).not.toBe( "" );
		expect( page.querySelector(".count")?.textContent ).toContain( "2 sessions" );
	} );

	it( 'says so when the instance holds none', async ()=>{
		const page = await render( create([]) );
		expect( page.querySelector(".empty")?.textContent ).toContain( "No web sessions" );
		expect( page.querySelector("mat-table") ).toBeNull();
	} );

	it( 'reloads on refresh', async ()=>{
		const fixture = create( [session("10.0.0.1", "2026-10-17T12:00:00Z")] );
		const page = await render( fixture );
		page.querySelector<HTMLButtonElement>( "button[aria-label=Refresh]" )!.click();
		await fixture.whenStable();
		expect( asked ).toHaveLength( 2 );
	} );

	//listing is gated as the purge is (admin over `sessions`):  a refusal is a state the tab reports in place, not an error to toast.
	it( 'reports a refusal as a permission, without a toast', async ()=>{
		const page = await render( create([], {status: 403}) );
		expect( page.querySelector(".load-error")?.textContent ).toContain( refusedText );
		expect( toasted ).toBe( 0 );
	} );

	it( 'shows any other failure, with its reason', async ()=>{
		const page = await render( create([], new Error("The instance is not connected")) );
		expect( page.querySelector(".load-error")?.textContent ).toContain( "The instance is not connected" );
		expect( toasted ).toBe( 1 );
	} );
} );

describe( 'sessionUser', ()=>{
	it( 'is domain\\login for a signed-in session, the login alone without a domain, and anonymous otherwise', ()=>{
		expect( sessionUser(session("h", "t", {loginName: "duffyj", domain: "google"})) ).toBe( "google\\duffyj" );
		expect( sessionUser(session("h", "t", {loginName: "duffyj"})) ).toBe( "duffyj" );
		expect( sessionUser(session("h", "t")) ).toBe( "anonymous" );
	} );
} );

describe( 'byLastUpdate', ()=>{
	it( 'puts the most recently active first', ()=>{
		const rows = [session("a", "2026-10-17T12:00:00Z"), session("b", "2026-10-17T12:30:00Z"), session("c", "2026-10-16T23:59:59Z")].sort( byLastUpdate );
		expect( rows.map(r=>r.endpoint) ).toEqual( ["b", "a", "c"] );
	} );
} );