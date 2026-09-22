import { signal } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { provideRouter, UrlSegment } from '@angular/router';
import { HttpErrorResponse } from '@angular/common/http';
import { RouterTestingHarness } from '@angular/router/testing';
import { HELP_TOPICS, IROUTE_SERVICE, RecentVisit, RecentVisits, RouteItem } from 'jde-spa';
import { Cards } from './cards';
import { CARD_STATUS, CardStatusValue, counted, countRows, HelpCardStatus, ICardStatus, statusFor } from './card-status';

const tiles = ()=>[
	new RouteItem( {path: 'gateways', title: 'Gateways', summary: 'Available Gateways'} ),
	new RouteItem( {path: 'access', title: 'Access', summary: 'Configure User Access'} ),
	new RouteItem( {path: 'help', title: 'Help', summary: 'Documentation'} )
];
const provider = ( url:string, status:()=>Promise<CardStatusValue> ):ICardStatus=>({ url, status });
const hero = { lead: 'Welcome to', name: 'OPC Hub', tilesHeading: 'Where do you want to begin?' };

//the root page's tiles with the given CARD_STATUS providers - a landing page when `landing`, plain cards otherwise
async function render( providers:ICardStatus[], landing = true, recent:RecentVisit[] = [], items:()=>RouteItem[] = tiles ){
	TestBed.configureTestingModule( {providers: [
		provideRouter( [{ path: '', component: Cards, data: landing ? {hero} : {summary: 'Welcome'},
			providers: [{provide: IROUTE_SERVICE, useValue: {docItems: async ()=>items(), children: async ()=>[]}}] }] ),
		...providers.map( p=>({provide: CARD_STATUS, useValue: p, multi: true}) ),
		{provide: RecentVisits, useValue: {visits: signal(recent), load: async ()=>{}, clear: ()=>{}}}
	]} );
	const harness = await RouterTestingHarness.create();
	await harness.navigateByUrl( '/' );
	await new Promise( r=>setTimeout(r) );//docItems and the statuses settle after the navigation
	harness.detectChanges();
	return harness.routeNativeElement!;
}
const byTitle = ( page:HTMLElement, selector:string, title:string )=>
	[...page.querySelectorAll<HTMLElement>(selector)].find( t=>t.querySelector('.tile-title, .section-card-title')!.textContent!.trim()==title )!;
const text = ( tile:HTMLElement, part:string )=>tile.querySelector( part )?.textContent!.trim();

describe( 'Cards landing tiles', ()=>{
	it( 'draws the status registered for the url a tile opens:  label, figure and detail', async ()=>{
		const page = await render( [
			provider( '/gateways', async ()=>({label: 'OPC connections', figure: 5, detail: 'on 2 gateways'}) ),
			provider( '/elsewhere', async ()=>({label: 'Nothing', figure: 9}) )
		] );
		const gateways = byTitle( page, '.tile', 'Gateways' );
		expect( [text(gateways, '.tile-label'), text(gateways, '.tile-figure'), text(gateways, '.tile-detail')] ).toEqual( ['OPC connections', '5', 'on 2 gateways'] );
		expect( gateways.getAttribute('aria-label') ).toBe( 'Gateways: 5 OPC connections, on 2 gateways' );//no summary:  the tile's own parts
		expect( text(byTitle(page, '.tile', 'Access'), '.tile-detail') ).toBe( 'Configure User Access' );
		expect( text(page, '.tiles-heading') ).toBe( 'Where do you want to begin?' );
	});

	it( 'reads the summary, when there is one, as the tile\'s accessible name - "1 OPC connection", not "1 OPC connections"', async ()=>{
		const page = await render( [ provider('/gateways', async ()=>({label: 'OPC connections', figure: 1, detail: 'on 1 gateway', summary: '1 OPC connection on 1 gateway'})) ] );
		expect( byTitle(page, '.tile', 'Gateways').getAttribute('aria-label') ).toBe( 'Gateways: 1 OPC connection on 1 gateway' );
	});

	it( 'keeps the static summary when the status rejects', async ()=>{
		const page = await render( [ provider('/access', ()=>Promise.reject(new Error('no rights'))) ] );
		const access = byTitle( page, '.tile', 'Access' );
		expect( access.querySelector('.tile-figure') ).toBeNull();
		expect( text(access, '.tile-detail') ).toBe( 'Configure User Access' );
	});

	it( 'flags a warn status', async ()=>{
		const page = await render( [ provider('/gateways', async ()=>({label: 'OPC connections', figure: 0, detail: '1 gateway not answering', warn: true})) ] );
		expect( byTitle(page, '.tile', 'Gateways').classList ).toContain( 'warn' );
		expect( byTitle(page, '.tile', 'Access').classList ).not.toContain( 'warn' );
	});

	it( 'lists the recently visited pages, each linked and tinted by its section', async ()=>{
		const page = await render( [], true, [
			{ url: '/access/roles/engineer', title: 'Engineer', path: 'Access › Roles', section: 'access', icon: 'badge', at: Date.now() },
			{ url: '/help/gateways', title: 'Gateways', path: 'Help', section: 'help', at: Date.now()-3*86_400_000 }
		] );
		const cards = [...page.querySelectorAll<HTMLAnchorElement>('.recent-card')];
		expect( cards.map(c=>[text(c, '.recent-title'), text(c, '.recent-path'), text(c, '.recent-time'), c.getAttribute('href')]) ).toEqual( [
			['Engineer', 'Access › Roles', 'just now', '/access/roles/engineer'],
			['Gateways', 'Help', '3 days ago', '/help/gateways']
		] );
		expect( cards[0].classList ).toContain( 'card-access' );
		expect( page.querySelector('.recent-empty') ).toBeNull();
	});

	it( 'says what the row is for before anything has been visited', async ()=>{
		const page = await render( [] );
		expect( page.querySelector('.recent-card') ).toBeNull();
		expect( text(page, '.recent-empty') ).toContain( 'will show up here' );
	});

	it( 'draws section cards on a page that is not a landing page, each with the figure its status gives', async ()=>{
		const page = await render( [
			provider( '/', async ()=>({label: 'x', figure: 0, summary: '2 gateways · 5 connections'}) ),
			provider( '/gateways', async ()=>({label: 'connections', figure: 5, detail: 'Local, Line 2'}) )
		], false );
		expect( page.querySelector('.tile') ).toBeNull();
		expect( page.querySelector('.recent-section') ).toBeNull();
		const gateways = byTitle( page, '.section-card', 'Gateways' );
		expect( text(gateways, '.section-card-sub') ).toBe( 'Available Gateways' );
		expect( text(gateways, '.section-card-figure b') ).toBe( '5' );
		expect( text(gateways, '.section-card-figure') ).toContain( 'connections · Local, Line 2' );
		expect( byTitle(page, '.section-card', 'Access').querySelector('.section-card-figure') ).toBeNull();
		expect( text(page, '.section-title') ).toBe( 'Welcome' );
		expect( text(page, '.section-status') ).toBe( '2 gateways · 5 connections' );//the page's own status, in its header
	});
});

describe( 'Cards section links', ()=>{
	it( 'opens a card that leaves the site in a new tab, and says so', async ()=>{
		const issues = 'https://github.com/Jde-cpp/opc-hub/issues';
		const page = await render( [], false, [], ()=>[ ...tiles(), new RouteItem({path: issues, title: 'Report an issue', icon: 'bug_report', externalRedirect: issues}) ] );
		const card = byTitle( page, '.section-card', 'Report an issue' ) as HTMLAnchorElement;
		expect( [card.getAttribute('href'), card.target, card.rel] ).toEqual( [issues, '_blank', 'noopener'] );
		expect( text(card, '.cdk-visually-hidden') ).toBe( '(opens in a new tab)' );
		expect( (byTitle(page, '.section-card', 'Gateways') as HTMLAnchorElement).target ).toBe( '' );
	});
});

//reviews/m3-closing.md #7:  a rejected docItems() - a refused serverConnections query, a gateway no longer registered, a
//gateway down - was an unhandled rejection, and the page said "Nothing to show here.", the same as a gateway with no
//connections.  It said so while loading too, and a failed move to another gateway left the previous one's cards up.
describe( 'Cards when the items cannot be loaded', ()=>{
	const cards = ( ...titles:string[] )=>titles.map( t=>new RouteItem({path: t.toLowerCase().replace(/ /g, ''), title: t}) );
	async function section( docItems:( segments:UrlSegment[] )=>Promise<RouteItem[]> ){
		TestBed.configureTestingModule( {providers: [
			provideRouter( [{ path: 'gateways/:gateway', component: Cards, data: {summary: 'Connections'},
				providers: [{provide: IROUTE_SERVICE, useValue: {docItems, children: async ()=>[]}}] }] ),
			{provide: RecentVisits, useValue: {visits: signal([]), load: async ()=>{}, clear: ()=>{}}}
		]} );
		return RouterTestingHarness.create();
	}
	const settle = async ( harness:RouterTestingHarness )=>{ await new Promise( r=>setTimeout(r) ); harness.detectChanges(); };
	const alert = ( harness:RouterTestingHarness )=>harness.routeNativeElement!.querySelector<HTMLElement>( '[role=alert]' );
	const saysNothing = ( harness:RouterTestingHarness )=>harness.routeNativeElement!.textContent!.includes( "Nothing to show here." );
	const titles = ( harness:RouterTestingHarness )=>[...harness.routeNativeElement!.querySelectorAll('.section-card-title')].map( t=>t.textContent!.trim() );

	it( 'says a refused load is refused, quoting the server - not that there is nothing', async ()=>{
		const harness = await section( async ()=>{ throw new HttpErrorResponse({status: 403, error: "User does not have 'Read' access to 'serverConnections'."}); } );
		await harness.navigateByUrl( '/gateways/gw1' );
		await settle( harness );
		expect( alert(harness)?.textContent ).toContain( "No access." );
		expect( alert(harness)?.textContent ).toContain( "User does not have 'Read' access to 'serverConnections'." );
		expect( alert(harness)?.querySelector('button') ).toBeNull();//asking again will not change a refusal
		expect( saysNothing(harness) ).toBe( false );
	} );

	it( 'says a failed load failed, in its own words, with a Retry that clears it', async ()=>{
		let fail = true;
		const harness = await section( async ()=>{ if( fail ) throw new Error( "No gateway 'gw1' is registered.  Registered: 'OpcHub'." ); return cards( "Line 1" ); } );
		await harness.navigateByUrl( '/gateways/gw1' );
		await settle( harness );
		expect( alert(harness)?.textContent ).toContain( "Could not load." );
		expect( alert(harness)?.textContent ).toContain( "No gateway 'gw1' is registered." );
		fail = false;
		alert( harness )!.querySelector( 'button' )!.click();
		await settle( harness );
		expect( alert(harness) ).toBeNull();
		expect( titles(harness) ).toEqual( ["Line 1"] );
	} );

	it( 'says there is nothing only once the load has answered', async ()=>{
		let answer!:( items:RouteItem[] )=>void;
		const harness = await section( ()=>new Promise<RouteItem[]>( resolve=>{ answer = resolve; } ) );
		await harness.navigateByUrl( '/gateways/gw1' );
		harness.detectChanges();
		expect( saysNothing(harness) ).toBe( false );
		answer( [] );
		await settle( harness );
		expect( saysNothing(harness) ).toBe( true );
	} );

	it( "drops the previous gateway's cards when the next one fails", async ()=>{
		const harness = await section( async ( segments )=>{ if( segments[1].path=="gw2" ) throw new Error( "gw2 is down" ); return cards( "Line 1" ); } );
		await harness.navigateByUrl( '/gateways/gw1' );
		await settle( harness );
		expect( titles(harness) ).toEqual( ["Line 1"] );
		await harness.navigateByUrl( '/gateways/gw2' );//the same route config, so the component is reused
		await settle( harness );
		expect( titles(harness) ).toEqual( [] );
		expect( alert(harness)?.textContent ).toContain( "gw2 is down" );
	} );
} );

describe( 'HelpCardStatus', ()=>{
	it( 'counts every registered topic and names the first', async ()=>{
		TestBed.configureTestingModule( {providers: [
			{provide: HELP_TOPICS, useValue: [{id: 'overview', title: 'Overview', summary: '', icon: '', url: ''}], multi: true},
			{provide: HELP_TOPICS, useValue: [{id: 'access', title: 'Access', summary: '', icon: '', url: ''}, {id: 'about', title: 'About', summary: '', icon: '', url: ''}], multi: true}
		]} );
		expect( await TestBed.inject(HelpCardStatus).status() ).toEqual( {label: 'Topics', figure: 3, detail: 'start with Overview', summary: '3 topics'} );
	});
});

describe( 'statusFor', ()=>{
	const providers = ['/gateways', '/gateways/:gateway', '/access/:collection'].map( url=>provider(url, async ()=>({label: '', figure: 0})) );
	it( 'matches a literal url, or a pattern segment for segment', ()=>{
		expect( ['/gateways', '/gateways/gw1', '/access/users', '/access', '/gateways/gw1/local'].map(u=>statusFor(providers, u)?.url) )
			.toEqual( ['/gateways', '/gateways/:gateway', '/access/:collection', undefined, undefined] );
	});
});

describe( 'counted', ()=>{
	it( 'pluralizes all but one', ()=>{
		expect( [0, 1, 2].map(n=>counted(n, 'role')) ).toEqual( ['0 roles', '1 role', '2 roles'] );
	});
});

describe( 'countRows', ()=>{
	const answering = ( value:unknown )=>({ query: async <Y>( ql:string )=>({ groups: value }) as Y });
	it( 'counts the rows', async ()=>expect( await countRows(answering([{id:1}, {id:2}]), 'groups') ).toBe( 2 ) );
	it( 'reads a {} answer as none', async ()=>expect( await countRows(answering({}), 'groups') ).toBe( 0 ) );
	it( 'rejects anything else', async ()=>{
		await expect( countRows(answering({id: 1}), 'groups') ).rejects.toThrow( /not a list of rows/ );
		await expect( countRows(answering(undefined), 'groups') ).rejects.toThrow( /not a list of rows/ );
	});
});
