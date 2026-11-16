import { TestBed } from '@angular/core/testing';
import { By } from '@angular/platform-browser';
import { provideRouter } from '@angular/router';
import { BehaviorSubject } from 'rxjs';
import { OeeLiveEvent, OeeLiveStatus } from '../../../model/oee-live';
import { OeeFloorService } from '../../../services/oee-floor-service';
import { OeeFloorPageData } from '../../../services/resolvers/oee-floor-resolver';
import { OeeFloorMap } from './oee-floor-map';
import { OeeFloorPage } from './oee-floor-page';
import { flush, pageTitleStub, provideRoute, stable } from 'jde-spa/testing';
import { Feed } from 'jde-opc/testing';

const t = ( h:number, m=0, s=0 )=>new Date( Date.UTC( 2026, 10, 11, h, m, s ) );
const Jam = { id: 3, name: 'Jam', category: 'Unplanned' };
const shift = { name: 'First', start: t( 6 ), end: t( 14 ) };
const figures = { start: t( 6 ), end: t( 9 ), availability: 0.8, performance: 0.9, quality: 0.95, oee: 0.684, partial: false, planned: 10800000, run: 9000000, unplanned: 1200000, plannedStop: 600000, small: 120000, excluded: 0, unknown: 0 };
const running = ( since:Date, extra:Partial<OeeLiveStatus>={} ):OeeLiveStatus=>({ state: 'running', since, planned: true, shift, figures, calendarMissing: false, lastCount: t( 8, 59, 30 ), ...extra });
const stopped = ( since:Date, stop:object={} ):OeeLiveStatus=>({ state: 'stopped', since, planned: true, shift, figures, calendarMissing: false, stop: {start: since, small: false, ...stop} });
const lineStatus = ( machines:{running:number; stopped:number; unknown:number}, extra:Partial<OeeLiveStatus>={} ):OeeLiveStatus=>({ state: 'unknown', planned: true, shift, figures, calendarMissing: false, machines, ...extra });
const Plant = { id: 10, name: 'Plant', slug: 'plant', level: 'Site', calendarId: 2 }, Bottling = { id: 11, name: 'Bottling', slug: 'bottling', level: 'Line' }, Packing = { id: 12, name: 'Packing', slug: 'packing', level: 'Line' };
const rect = ( x:number, y:number, w:number, h:number ):[number, number][]=>[[x, y], [x+w, y], [x+w, y+h], [x, y+h]];
const pageData = ( gateway:unknown, {calendar=true, zone='America/New_York'}={} ):OeeFloorPageData=>(<OeeFloorPageData><unknown>{//`calendar` false:  a line with none
	gateway, routing: {}, line: {...Plant, calendarId: calendar ? 2 : undefined}, zone, calendar: calendar ? {id: 2, name: 'plant', slug: 'plant', timeZone: zone, dayStart: 360} : undefined,
	floor: { id: 1, lineId: 10, width: 40, height: 20, grid: 0.5 },
	shapes: [
		{ id: 1, floorId: 1, kind: 'Machine', machineId: 1, points: rect( 3, 3, 6, 4 ), sort: 0 }, { id: 2, floorId: 1, kind: 'Machine', machineId: 2, points: rect( 13, 3, 4, 4 ), sort: 1 },
		{ id: 4, floorId: 1, kind: 'Machine', machineId: 4, points: rect( 3, 13, 6, 4 ), sort: 2 }, { id: 5, floorId: 1, kind: 'Label', points: [[38, 15]], label: 'Dock', sort: 0 }
	],
	plant: {
		machines: [{id: 1, name: 'Filler', slug: 'filler', groupId: 21}, {id: 2, name: 'Capper', slug: 'capper'}, {id: 4, name: 'Packer', slug: 'packer', groupId: 24}],
		lines: [Plant, Bottling, Packing],
		members: [{id: 1, lineId: 10, memberLineId: 11, sort: 0}, {id: 2, lineId: 10, memberLineId: 12, sort: 1}, {id: 3, lineId: 11, machineId: 1, sort: 0}, {id: 4, lineId: 11, machineId: 2, sort: 1}, {id: 6, lineId: 12, machineId: 4, sort: 0}]
	}
});
//The page over stubs:  a gateway whose oeeSubscribe opens a feed per machine and oeeSubscribeLine one per line, under its id
//negated, `push` a status on one;  `service` answers a machine's group.  `open` moves the page to another floor, as the router does.
const create = async ( {zone='America/New_York'}={} )=>{
	const feeds = new Map<number,Feed<OeeLiveEvent,number>>();
	const feed = ( id:number )=>feeds.get( id ) ?? feeds.set( id, new Feed<OeeLiveEvent,number>() ).get( id )!;
	const gateway = { name: 'gw', slug: 'gw', feed, oeeSubscribe: vi.fn( ( machine:number )=>feed( machine ).open( machine ) ), oeeSubscribeLine: vi.fn( ( line:number )=>feed( -line ).open( -line ) ), querySingle: vi.fn( async ()=>({ name: 'Machine', nodeClass: 1, path: 'Objects/2~Machine' }) ) };
	const service = { group: vi.fn( async ( _g:unknown, id:number )=>id==21 ? { slug: 'filler', connection: 'plc1', root: 'ns=2;i=5001' } : id==24 ? { slug: 'packer', connection: 'plc4' } : undefined ) };
	const data = new BehaviorSubject<object>( {pageData: pageData( gateway, {zone} )} );
	const title = { title: '' };
	TestBed.configureTestingModule({ providers: [provideRouter( [] ), provideRoute( {data} ), { ...pageTitleStub, useValue: title }, { provide: OeeFloorService, useValue: service }] });
	const fixture = TestBed.createComponent( OeeFloorPage );
	fixture.componentRef.setInput( 'sideNav', undefined );
	await stable( fixture );
	const page = fixture.componentInstance;
	const push = async ( id:number, status:OeeLiveStatus )=>{
		feed( id ).next( {kind: 'status', status} );
		await flush();
		fixture.detectChanges();
	};
	const el = ()=><HTMLElement>fixture.nativeElement;
	const shapes = ()=>[...el().querySelectorAll( 'g.shape' )].map( g=>({ name: g.getAttribute( 'aria-label' )!, token: g.getAttribute( 'data-token' ), dash: g.getAttribute( 'data-dash' ), texts: [...g.querySelectorAll( 'text' )].map( x=>x.textContent!.trim() ) }) );
	const words = ( n:Node ):string[]=>n.nodeType==Node.TEXT_NODE ? [n.textContent!.trim().replace( /\s+/g, ' ' )] : [...n.childNodes].flatMap( words );
	const text = ( selector:string )=>[...el().querySelectorAll( selector )].map( e=>words( e ).filter( x=>!!x ).join( ' ' ) );//an element's texts, text node by text node
	const tiles = ()=>[...el().querySelectorAll( '.list .tile' )].map( a=>[...a.querySelectorAll( '*' )].filter( e=>!e.children.length ).map( e=>e.textContent!.trim().replace( /\s+/g, ' ' ) ).filter( x=>!!x ).join( ' ' ) );
	const shape = ( key:string )=>el().querySelector( `g.shape[aria-label^="${key}"]` )!;
	const map = ()=><OeeFloorMap>fixture.debugElement.query( By.directive( OeeFloorMap ) ).componentInstance;
	const open = async ( next:OeeFloorPageData )=>{
		data.next( {pageData: next} );
		await stable( fixture );
	};
	return { page, fixture, gateway, service, title, push, shapes, text, tiles, shape, map, open, el };
};

describe( 'OeeFloorPage', ()=>{
	afterEach( ()=>{ vi.useRealTimers(); vi.restoreAllMocks(); } );
	it( 'subscribes once per machine shape, per boxed line and for the floor\'s own line, and draws each by its state', async ()=>{
		const p = await create();
		expect( p.title.title ).toBe( 'Plant - Floor' );
		expect( p.gateway.oeeSubscribe.mock.calls.map( ( [id] )=>id ) ).toEqual( [1, 2, 4] );
		expect( p.gateway.oeeSubscribeLine.mock.calls.map( ( [id] )=>id ) ).toEqual( [11, 12, 10] );
		expect( p.shapes().map( s=>[s.name, s.token, s.dash] ) ).toEqual( [
			['Bottling, Connecting, OEE –', 'neutral', 'solid'], ['Packing, Connecting, OEE –', 'neutral', 'solid'],
			['Filler, Connecting, OEE –', 'neutral', 'solid'], ['Capper, Connecting, OEE –', 'neutral', 'solid'], ['Packer, Connecting, OEE –', 'neutral', 'solid']
		] );
		expect( p.text( 'text.label' ) ).toEqual( ['Dock'] );
		await p.push( 1, running( t( 7, 48 ) ) );
		await p.push( 2, stopped( t( 8, 56 ), {small: true} ) );
		await p.push( 4, stopped( t( 8, 50 ), {reason: Jam} ) );
		await p.push( -11, lineStatus( {running: 1, stopped: 1, unknown: 0} ) );
		await p.push( -12, lineStatus( {running: 0, stopped: 1, unknown: 0}, {planned: false} ) );
		const drawn = p.shapes();
		expect( drawn.map( s=>[s.token, s.dash] ) ).toEqual( [['uncoded', 'dashed'], ['neutral', 'dashed'], ['running', 'solid'], ['small', 'solid'], ['Unplanned', 'solid']] );
		expect( drawn[0].texts ).toEqual( ['Bottling', 'OEE 68.4% · 1 of 2 running'] );
		expect( drawn[2].texts ).toEqual( ['Filler', '68.4%'] );//fitted, 6 metres is 114 px wide:  the name and the OEE
		p.map().zoom( 1.25 );
		p.fixture.detectChanges();
		expect( p.shapes()[2].texts ).toEqual( ['Filler', '68.4%', 'A 80.0% · P 90.0% · Q 95.0%', expect.stringMatching( /^Running / )] );//143 by 95 px:  the figures and the state line too
		expect( p.shapes()[3].texts ).toEqual( ['Capper', '68.4%'] );//4 by 4 metres is 95 px:  still the name and the OEE
		await p.push( 2, stopped( t( 8, 56 ), {small: false, reason: Jam} ) );//the small stop passed the threshold:  its reason's colour
		expect( p.shapes()[3].token ).toBe( 'Unplanned' );
	} );
	it( 'ticks the since text and the plant\'s clock each second from a timer, in the line\'s zone an hour off the browser\'s', async ()=>{
		vi.useFakeTimers( {toFake: ['setInterval', 'clearInterval', 'Date']} );
		vi.setSystemTime( new Date( Date.UTC( 2026, 10, 11, 12, 34, 56 ) ) );
		const p = await create( {zone: 'Etc/GMT-1'} );
		await p.push( 1, running( new Date( Date.UTC( 2026, 10, 11, 12, 33, 56 ) ) ) );
		expect( p.shapes()[2].name ).toBe( 'Filler, Running 1m, OEE 68.4%' );
		expect( p.text( '.status-bar .clock' ) ).toEqual( ['13:34:56'] );
		expect( p.text( '.status-bar .zone' ) ).toEqual( ['Etc/GMT-1, UTC+01:00'] );
		vi.advanceTimersByTime( 1000 );
		p.fixture.detectChanges();
		expect( p.shapes()[2].name ).toBe( 'Filler, Running 1m 1s, OEE 68.4%' );
		expect( p.text( '.status-bar .clock' ) ).toEqual( ['13:34:57'] );
		expect( p.gateway.oeeSubscribe ).toHaveBeenCalledTimes( 3 );//nothing is read on the timer
	} );
	it( "heads the floor with the line's shift and the time left, its figures and its state, or No calendar", async ()=>{
		vi.useFakeTimers( {toFake: ['setInterval', 'clearInterval', 'Date']} );
		vi.setSystemTime( t( 12, 34, 10 ) );
		const p = await create();
		expect( p.text( '.status-bar .item' )[0] ).toBe( 'Shift Connecting' );
		await p.push( -10, lineStatus( {running: 2, stopped: 1, unknown: 0} ) );
		expect( p.text( '.status-bar .item' ).slice( 0, 3 ) ).toEqual( ['Shift First, 1h 26m left', 'Plant time 07:34:10 America/New_York, UTC-05:00', 'Line 2 of 3 running'] );//November:  standard time
		expect( p.text( '.status-bar .figure' ) ).toEqual( ['Availability 80.0%', 'Performance 90.0%', 'Quality 95.0%', 'OEE 68.4%'] );
		await p.push( -10, lineStatus( {running: 2, stopped: 1, unknown: 0}, {shift: undefined, planned: false} ) );
		expect( p.text( '.status-bar .item' )[0] ).toBe( 'Shift Between shifts' );
		await p.open( pageData( p.gateway, {calendar: false, zone: 'UTC'} ) );
		expect( p.text( '.status-bar .item' ).slice( 0, 2 ) ).toEqual( ['Shift No calendar', 'Plant time 12:34:10 UTC, UTC'] );
		expect( p.gateway.oeeSubscribeLine ).toHaveBeenCalledTimes( 6 );//subscribed again for the floor opened again
	} );
	it( 'marks a shape whose subscription the gateway refused No access, one ended Ended, and leaves the others', async ()=>{
		const p = await create();
		p.gateway.feed( 2 ).error( {error: {requestId: 7, message: 'Read on the group is withheld', sc: 0, httpStatus: 403}} );
		p.gateway.feed( 4 ).error( {error: {requestId: 8, message: 'The machine was deleted', sc: 0, httpStatus: 400}} );
		await p.push( 1, running( t( 7, 48 ) ) );
		expect( p.shapes().map( s=>[s.name.split( ', ' )[1], s.token, s.dash] ) ).toEqual( [['Connecting', 'neutral', 'solid'], ['Connecting', 'neutral', 'solid'], [expect.stringMatching( /^Running/ ), 'running', 'solid'], ['No access', 'neutral', 'dotted'], ['Ended', 'neutral', 'dotted']] );
		p.page.togglePin( 'm4' );
		await stable( p.fixture );
		expect( p.text( '.panel .state' ) ).toEqual( ['Ended'] );
		expect( p.text( '.panel .note' )[0] ).toBe( 'The machine was deleted' );
		p.page.togglePin( 'm2' );
		await stable( p.fixture );
		expect( p.text( '.panel .note' )[0] ).toMatch( /^Read on its historian group is withheld/ );
	} );
	it( 'opens the side panel on hover, pins it on a pick, and links the machine\'s OEE page, its group and its node', async ()=>{
		const p = await create();
		await p.push( 1, running( t( 7, 48 ) ) );
		await p.push( -11, lineStatus( {running: 1, stopped: 1, unknown: 0} ) );
		expect( p.el().querySelector( '.panel' ) ).toBeNull();
		p.shape( 'Filler' ).dispatchEvent( new MouseEvent( 'mouseenter' ) );
		await stable( p.fixture );
		expect( p.text( '.panel h3' ) ).toEqual( ['Filler'] );
		expect( p.text( '.panel .figures .figure' ) ).toEqual( ['Availability 80.0%', 'Performance 90.0%', 'Quality 95.0%', 'OEE 68.4%'] );
		expect( p.text( '.panel .split tr' ).slice( 0, 4 ) ).toEqual( ['Run 2h 30m 83.3%', 'Unplanned stops 20m 11.1%', 'Planned stops 10m 5.6%', 'Small stops 2m 1.3% of run'] );
		expect( p.text( '.panel .note' ) ).toEqual( ['First, so far', expect.stringMatching( /^Last count / )] );
		const links = ()=>[...p.el().querySelectorAll( '.panel .links a' )].map( a=>a.textContent!.trim() );
		expect( links() ).toEqual( ['insightsOEE page', 'historyGroup', 'account_treeNode'] );
		expect( p.service.group ).toHaveBeenCalledWith( p.gateway, 21 );
		expect( p.gateway.querySingle ).toHaveBeenCalledTimes( 1 );
		expect( p.page.links() ).toEqual( {key: 'm1', pending: false, group: ['../../../groups', 'filler'], node: ['/gateways', 'gw', 'plc1', 'Objects', '2~Machine']} );
		p.shape( 'Filler' ).dispatchEvent( new MouseEvent( 'mouseleave' ) );
		await stable( p.fixture );
		expect( p.el().querySelector( '.panel' ) ).toBeNull();
		p.shape( 'Bottling' ).dispatchEvent( new MouseEvent( 'click' ) );//a pick pins
		await stable( p.fixture );
		expect( p.text( '.panel h3' ) ).toEqual( ['Bottling Line'] );
		expect( p.text( '.panel .state' ) ).toEqual( ['1 of 2 running'] );
		expect( links() ).toEqual( ['insightsOEE page'] );
		p.shape( 'Filler' ).dispatchEvent( new MouseEvent( 'mouseenter' ) );
		await stable( p.fixture );
		expect( p.text( '.panel h3' ) ).toEqual( ['Bottling Line'] );//pinned, so the hover doesn't take it
		expect( p.service.group ).toHaveBeenCalledTimes( 1 );
		p.shape( 'Packer' ).dispatchEvent( new MouseEvent( 'click' ) );
		await stable( p.fixture );
		expect( p.page.links() ).toEqual( {key: 'm4', pending: false, group: ['../../../groups', 'packer'], node: ['/gateways', 'gw', 'plc4']} );//a group rooted at Objects links the connection
		await p.push( 2, stopped( t( 8, 56 ), {reason: Jam} ) );
		p.shape( 'Capper' ).dispatchEvent( new MouseEvent( 'click' ) );
		await stable( p.fixture );
		expect( p.text( '.panel .stop' ) ).toEqual( [expect.stringMatching( /^Stop in progress since .*: Jam Unplanned$/ )] );
		expect( p.page.links() ).toEqual( {key: 'm2', pending: false} );//no group known
	} );
	it( "lists the lines then the machines in the floor's order, each as the dashboard's tile", async ()=>{
		const p = await create();
		await p.push( 1, running( t( 7, 48 ) ) );
		await p.push( -11, lineStatus( {running: 1, stopped: 1, unknown: 0} ) );
		const tiles = p.tiles();
		expect( tiles.map( x=>x.split( ' ' )[0] ) ).toEqual( ['Bottling', 'Packing', 'Filler', 'Capper', 'Packer'] );
		expect( tiles[0] ).toBe( 'Bottling Line 1 of 2 running Availability 80.0% Performance 90.0% Quality 95.0% OEE 68.4% First, so far' );
		expect( tiles[2] ).toMatch( /^Filler Running .* Availability 80\.0% Performance 90\.0% Quality 95\.0% OEE 68\.4% First, so far$/ );
		expect( tiles[3] ).toBe( 'Capper Connecting Availability – Performance – Quality – OEE –' );
		expect( p.page.list().map( t=>t.link.join( '/' ) ) ).toEqual( ['../../../lines/bottling/oee', '../../../lines/packing/oee', '../../../machines/filler/oee', '../../../machines/capper/oee', '../../../machines/packer/oee'] );
	} );
} );