import { Location } from '@angular/common';
import { TestBed } from '@angular/core/testing';
import { By } from '@angular/platform-browser';
import { provideRouter } from '@angular/router';
import { BehaviorSubject } from 'rxjs';
import { SnackbarService } from 'jde-framework';
import { OeeFloorRights, toOeeFloorShape } from '../../../model/oee-floor';
import { OeeLiveEvent, OeeLiveStatus } from '../../../model/oee-live';
import { OeeFloorService } from '../../../services/oee-floor-service';
import { OpcStore } from '../../../services/opc-store';
import { OeeFloorPageData } from '../../../services/resolvers/oee-floor-resolver';
import { OeeFloorEditor } from './oee-floor-editor';
import { OeeFloorMap } from './oee-floor-map';
import { OeeFloorPage } from './oee-floor-page';
import { flush, pageTitleStub, provideRoute, silence, stable } from 'jde-spa/testing';
import { snackbarStub } from 'jde-framework/testing';
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
const none:OeeFloorRights = { createFloor: false, updateFloor: false, createShape: false, updateShape: false, purgeShape: false };
const all:OeeFloorRights = { createFloor: true, updateFloor: true, createShape: true, updateShape: true, purgeShape: true };
//The page over stubs:  a gateway whose oeeSubscribe opens a feed per machine and oeeSubscribeLine one per line, under its id
//negated, `push` a status on one;  `service` answers a machine's group, the caller's `rights` on the floor's tables, none by
//default, or their refusal, and the floor read again.  `made` is a page navigated to from a floor's making.  `open` moves the
//page to another floor, as the router does.  `root` is the Filler's group's, by the server's index unless given;  `opcStore`
//answers a connection's namespaces, urn:jde:demo the server's second.
const create = async ( {zone='America/New_York', rights=none, made=false, root='ns=2;i=5001'}:{zone?:string; rights?:OeeFloorRights|Error; made?:boolean; root?:string}={} )=>{
	await import( './oee-floor-editor' );//loaded here, so the page's import of it is a known number of turns
	const feeds = new Map<number,Feed<OeeLiveEvent,number>>();
	const feed = ( id:number )=>feeds.get( id ) ?? feeds.set( id, new Feed<OeeLiveEvent,number>() ).get( id )!;
	const gateway = { name: 'gw', slug: 'gw', feed, oeeSubscribe: vi.fn( ( machine:number )=>feed( machine ).open( machine ) ), oeeSubscribeLine: vi.fn( ( line:number )=>feed( -line ).open( -line ) ), querySingle: vi.fn( async ()=>({ name: 'Machine', nodeClass: 1, path: 'Objects/2~Machine' }) ) };
	const first = pageData( gateway, {zone} );
	const service = {
		group: vi.fn( async ( _g:unknown, id:number )=>id==21 ? { slug: 'filler', connection: 'plc1', root } : id==24 ? { slug: 'packer', connection: 'plc4' } : undefined ),
		rights: vi.fn( async ( ..._:unknown[] )=>{ if( rights instanceof Error ) throw rights; return rights; } ),
		floor: vi.fn( async ( ..._:unknown[] ):Promise<object|undefined>=>({ floor: first.floor, shapes: first.shapes }) ), plant: vi.fn( async ( ..._:unknown[] )=>first.plant ), save: vi.fn( async ()=>{} )
	};
	const data = new BehaviorSubject<object>( {pageData: first} );
	const title = { title: '' }, snackbar = snackbarStub();
	const opcStore = { getConnection: vi.fn( async ( ..._:unknown[] )=>({ namespaces: [{index: 0, uri: 'http://opcfoundation.org/UA/'}, {index: 2, uri: 'urn:jde:demo'}] }) ) };
	TestBed.configureTestingModule({ providers: [provideRouter( [] ), provideRoute( {data} ), { ...pageTitleStub, useValue: title }, { provide: OeeFloorService, useValue: service }, { provide: SnackbarService, useValue: snackbar }, { provide: OpcStore, useValue: opcStore }] });
	if( made ){//the history entry's state, as the router keeps it
		const location = TestBed.inject( Location );
		let state:unknown = {edit: true, navigationId: 2};
		vi.spyOn( location, 'getState' ).mockImplementation( ()=>state );
		vi.spyOn( location, 'replaceState' ).mockImplementation( ( _path, _query, next )=>{ state = next; } );
	}
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
	const lineTexts = ()=>[...el().querySelectorAll( 'g.line-text' )].map( g=>[...g.querySelectorAll( 'text' )].map( x=>x.textContent!.trim() ) );
	const layers = ()=>[...el().querySelectorAll( 'g.shape, g.line-text' )].map( g=>g.classList.contains( 'line-text' ) ? 'text' : g.classList.contains( 'machine' ) ? 'machine' : 'line' );
	const map = ()=><OeeFloorMap>fixture.debugElement.query( By.directive( OeeFloorMap ) ).componentInstance;
	const open = async ( next:OeeFloorPageData )=>{
		data.next( {pageData: next} );
		await stable( fixture );
	};
	const editor = ()=><OeeFloorEditor|undefined>fixture.debugElement.query( By.directive( OeeFloorEditor ) )?.componentInstance;
	const button = ( label:string )=><HTMLButtonElement|undefined>[...el().querySelectorAll( 'button' )].find( b=>b.textContent!.trim().endsWith( label ) );
	return { page, fixture, gateway, service, title, push, shapes, text, tiles, shape, lineTexts, layers, map, open, el, editor, button, first, snackbar, opcStore };
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
		expect( drawn[0].texts ).toEqual( [] );//a line's text is drawn after the machines, over the one at its box's corner
		expect( p.lineTexts() ).toEqual( [['Bottling', 'OEE 68.4% · 1 of 2 running'], ['Packing', 'OEE 68.4% · 0 of 1 running']] );
		expect( p.layers() ).toEqual( ['line', 'line', 'machine', 'machine', 'machine', 'text', 'text'] );
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
		p.page.togglePin( 'm1' );
		await stable( p.fixture );
		expect( p.text( '.panel .state' ) ).toEqual( ['Running 1m'] );
		expect( p.text( '.status-bar .clock' ) ).toEqual( ['13:34:56'] );
		expect( p.text( '.status-bar .zone' ) ).toEqual( ['Etc/GMT-1, UTC+01:00'] );
		vi.advanceTimersByTime( 1000 );
		p.fixture.detectChanges();
		expect( p.text( '.panel .state' ) ).toEqual( ['Running 1m 1s'] );
		expect( p.text( '.status-bar .clock' ) ).toEqual( ['13:34:57'] );
		expect( p.shapes()[2].name ).toBe( 'Filler, Running, OEE 68.4%' );//words that hold still, or a screen reader says them again each second
		expect( p.text( '.panel .live' ) ).toEqual( ['Filler, Running'] );
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
		await p.push( -10, lineStatus( {running: 3, stopped: 0, unknown: 0}, {shift: undefined, figures: {...figures, oee: 0.5}} ) );
		expect( p.text( '.status-bar .figure' )[3] ).toBe( 'OEE 50.0%' );//the floor opened again's own line, not the first's
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
		expect( p.text( '.panel h3' ) ).toEqual( [] );//the panel stands, so a hover never takes the map's room
		expect( p.text( '.panel .prompt' ) ).toEqual( ['Point at, focus or tap a machine or line for its figures, its stop and its links.'] );
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
		expect( p.page.links() ).toEqual( {key: 'm1', group: ['../../../groups', 'filler'], node: ['/gateways', 'gw', 'plc1', 'Objects', '2~Machine']} );
		p.shape( 'Filler' ).dispatchEvent( new MouseEvent( 'mouseleave' ) );
		await stable( p.fixture );
		expect( p.text( '.panel h3' ) ).toEqual( [] );
		expect( p.text( '.panel .prompt' ).length ).toBe( 1 );
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
		expect( p.page.links() ).toEqual( {key: 'm4', group: ['../../../groups', 'packer'], node: ['/gateways', 'gw', 'plc4']} );//a group rooted at Objects links the connection
		await p.push( 2, stopped( t( 8, 56 ), {reason: Jam} ) );
		p.shape( 'Capper' ).dispatchEvent( new MouseEvent( 'click' ) );
		await stable( p.fixture );
		expect( p.text( '.panel .stop' ) ).toEqual( [expect.stringMatching( /^Stop in progress since .*: Jam Unplanned$/ )] );
		expect( p.page.links() ).toEqual( {key: 'm2'} );//no group known
	} );
	//the seed's groups, and a template's:  the gateway's `node` takes an id by the server's index, and asked by the URI it answered
	//no path, so the panel had no Node link
	it( "places a root spelled by its namespace's URI by the index the server has for it", async ()=>{
		const p = await create( {root: '{"nsu":"urn:jde:demo","i":5001}'} );
		p.shape( 'Filler' ).dispatchEvent( new MouseEvent( 'click' ) );
		await stable( p.fixture );
		expect( p.opcStore.getConnection ).toHaveBeenCalledWith( p.gateway, 'plc1' );
		expect( p.gateway.querySingle ).toHaveBeenCalledWith( expect.stringContaining( 'node(' ), {opc: 'plc1', id: {ns: 2, i: 5001}} );
		expect( p.page.links() ).toEqual( {key: 'm1', group: ['../../../groups', 'filler'], node: ['/gateways', 'gw', 'plc1', 'Objects', '2~Machine']} );
	} );
	it( 'links no node for a root in a namespace the server lacks, and asks for none', async ()=>{
		const p = await create( {root: '{"nsu":"urn:elsewhere","i":5001}'} );
		p.shape( 'Filler' ).dispatchEvent( new MouseEvent( 'click' ) );
		await stable( p.fixture );
		expect( p.gateway.querySingle ).not.toHaveBeenCalled();
		expect( p.page.links() ).toEqual( {key: 'm1', group: ['../../../groups', 'filler']} );
	} );
	it( "reads a machine's links once, though it is selected again while the read is in flight", async ()=>{
		const p = await create();
		for( const [name, links] of <[string, unknown][]>[['Filler', {key: 'm1'}], ['Bottling', undefined], ['Filler', {key: 'm1'}]] ){
			p.shape( name ).dispatchEvent( new MouseEvent( 'mouseenter' ) );
			p.fixture.detectChanges();
			expect( p.page.links() ).toEqual( links );//none yet, and a line has none
		}
		await stable( p.fixture );
		expect( p.service.group ).toHaveBeenCalledTimes( 1 );
		expect( p.gateway.querySingle ).toHaveBeenCalledTimes( 1 );
		expect( p.page.links()?.group ).toEqual( ['../../../groups', 'filler'] );
	} );
	it( 'pins a shape on a press that stays put, and captures the pointer only for a drag, which pins nothing', async ()=>{
		const p = await create();
		const capture = vi.fn();
		Object.assign( p.el().querySelector( 'svg' )!, { setPointerCapture: capture, hasPointerCapture: ()=>false } );
		const at = ( x:number, buttons=1 )=><PointerEvent><unknown>{ pointerId: 1, pointerType: 'mouse', button: 0, buttons, clientX: x, clientY: 100 };
		const press = async ( to:number )=>{
			p.map().down( at( 100 ) );
			p.map().move( at( to ) );
			p.map().up( at( to, 0 ) );
			p.shape( 'Filler' ).dispatchEvent( new MouseEvent( 'click' ) );//where the press began and ended, as the browser sends it
			await stable( p.fixture );
		};
		await press( 103 );
		expect( capture ).not.toHaveBeenCalled();//a capture would send the click to the svg
		expect( p.page.pinned() ).toBe( 'm1' );
		await press( 140 );
		expect( capture ).toHaveBeenCalledWith( 1 );
		expect( p.page.pinned() ).toBe( 'm1' );//a drag, so no pick to unpin it
		p.map().down( at( 100 ) );
		p.map().move( at( 104, 0 ) );//released outside the svg before it captured the pointer
		const view = p.map().view();
		p.map().move( at( 160, 0 ) );
		expect( p.map().view() ).toEqual( view );//a hover after it pans nothing
	} );
	it( "says what the dashboard's tiles say:  between shifts, a calendar not found, and a connection lost", async ()=>{
		const p = await create();
		await p.push( 1, running( t( 7, 48 ), {shift: undefined, planned: false} ) );//between shifts, with the last shift's figures
		await p.push( 2, running( t( 7, 48 ), {shift: undefined, calendarMissing: true} ) );
		p.page.togglePin( 'm1' );
		await stable( p.fixture );
		expect( p.text( '.panel .note' ) ).toEqual( ['Between shifts.', expect.stringMatching( /^Last count / )] );//no period
		const tiles = p.tiles();
		expect( tiles[2] ).toMatch( /OEE 68\.4% Between shifts\.$/ );
		expect( tiles[3] ).toMatch( /OEE 68\.4% UTC day, so far Its calendar isn't found, so the figures are the UTC day's\.$/ );
		expect( p.el().querySelector( '.map-wrap .notice' ) ).toBeNull();
		p.gateway.feed( 1 ).next( {kind: 'dropped'} );
		await stable( p.fixture );
		expect( p.text( '.panel .note' )[0] ).toMatch( /^The connection to the gateway was lost: this is the last it said\.$/ );
		expect( p.text( '.map-wrap .notice' ) ).toEqual( [expect.stringMatching( /The connection to the gateway was lost: the map shows the last it said\.$/ )] );
		await p.push( 1, running( t( 7, 48 ) ) );
		expect( p.el().querySelector( '.map-wrap .notice' ) ).toBeNull();//its next status
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
	it( "offers Edit to a caller who may update the floor's shapes, and stands the editor in the map's place until its Cancel", async ()=>{
		const p = await create( {rights: {...none, updateShape: true}} );
		expect( p.service.rights ).toHaveBeenCalledWith( p.gateway );
		expect( p.editor() ).toBeUndefined();//imported once Edit is pressed
		p.button( 'Edit' )!.click();
		await stable( p.fixture, 5 );
		const editor = p.editor()!;
		expect( document.activeElement ).toBe( p.el().querySelector( 'oee-floor-editor .sizes input' ) );//the focus, into the editor:  Edit is gone
		expect( [editor.line().name, editor.source().floor.id, editor.source().shapes.length, editor.plant().machines.length, editor.rights().updateShape, editor.rights().createShape] ).toEqual( ['Plant', 1, 4, 3, true, false] );
		expect( [...p.el().querySelectorAll( 'oee-floor-editor g.shape' )].map( g=>g.getAttribute( 'aria-label' ) ) ).toEqual( ['Filler', 'Capper', 'Packer', 'Label Dock'] );
		expect( [p.el().querySelector( 'oee-floor-map' ), p.el().querySelector( '.status-bar' ), p.el().querySelector( '.legend' ), p.button( 'Edit' )] ).toEqual( [null, null, null, undefined] );
		expect( p.text( '.line-heading' ) ).toEqual( ['Site Plant'] );//the header stays
		p.button( 'Cancel' )!.click();
		await stable( p.fixture );
		expect( p.editor() ).toBeUndefined();
		expect( p.el().querySelector( 'oee-floor-map' ) ).not.toBeNull();
		expect( document.activeElement ).toBe( p.button( 'Edit' ) );//the focus, back where it left
		expect( p.service.floor ).not.toHaveBeenCalled();//nothing saved, nothing to read
		expect( p.gateway.oeeSubscribe ).toHaveBeenCalledTimes( 3 );//and the subscriptions stood through it
	} );
	it( "offers no Edit without Update on the shapes, or where the gateway doesn't say", async ()=>{
		const viewer = await create( {rights: {...all, updateShape: false}} );
		expect( viewer.button( 'Edit' ) ).toBeUndefined();
		TestBed.resetTestingModule();//built twice:  a gateway that has no oeeFloorRights
		const warn = silence();
		const old = await create( {rights: new Error( "Could not find view 'oeeFloorRights'" )} );
		expect( old.button( 'Edit' ) ).toBeUndefined();
		expect( warn ).toHaveBeenCalledTimes( 1 );
		expect( old.shapes().length ).toBe( 5 );//the map stands
	} );
	it( "reads the floor again after the editor's save, and draws it", async ()=>{
		const p = await create( {rights: all} );
		p.button( 'Edit' )!.click();
		await stable( p.fixture, 5 );
		const moved = [...p.first.shapes.slice( 0, 3 ), toOeeFloorShape( {id: 5, oeeFloorId: 1, kind: 'Label', points: [[38, 15]], label: 'Dock'} )!, toOeeFloorShape( {id: 6, oeeFloorId: 1, kind: 'Label', points: [[2, 18]], label: 'Exit', sort: 1} )!];
		p.service.floor.mockResolvedValueOnce( {floor: {...p.first.floor, width: 50}, shapes: moved} );
		p.editor()!.closed.emit( true );
		await stable( p.fixture, 5 );
		expect( p.service.floor ).toHaveBeenCalledWith( p.gateway, 10 );
		expect( p.service.plant ).toHaveBeenCalledWith( p.gateway );
		expect( p.editor() ).toBeUndefined();
		expect( p.text( 'text.label' ) ).toEqual( ['Dock', 'Exit'] );
		expect( p.map().floor().width ).toBe( 50 );
		expect( p.gateway.oeeSubscribe ).toHaveBeenCalledTimes( 6 );//the floor read is opened as a floor is
		expect( p.service.rights ).toHaveBeenCalledTimes( 1 );//the rights are the gateway's, asked once
		expect( p.button( 'Edit' ) ).not.toBeUndefined();
		p.button( 'Edit' )!.click();
		await stable( p.fixture, 5 );
		expect( p.editor()!.source().shapes.length ).toBe( 5 );//the editor again, over what was read
		p.service.floor.mockRejectedValueOnce( new Error( 'gone' ) );
		p.editor()!.closed.emit( true );
		await stable( p.fixture, 5 );
		expect( p.snackbar.exception ).toHaveBeenCalledWith( 'The floor is saved, but could not be read again', expect.any( Error ) );
		expect( p.text( 'text.label' ) ).toEqual( ['Dock', 'Exit'] );//the map as it was
	} );
	it( 'opens a floor just made in its editor, for a caller who may edit it', async ()=>{
		const p = await create( {rights: all, made: true} );
		await stable( p.fixture, 5 );
		expect( p.editor() ).not.toBeUndefined();
		expect( TestBed.inject( Location ).getState() ).toEqual( {navigationId: 2} );//taken from the entry, so a Back or a reload to it opens the map
		TestBed.resetTestingModule();//built twice:  the same navigation, without Update on the shapes
		const viewer = await create( {made: true} );
		await stable( viewer.fixture, 5 );
		expect( viewer.editor() ).toBeUndefined();
		expect( viewer.el().querySelector( 'oee-floor-map' ) ).not.toBeNull();
	} );
} );