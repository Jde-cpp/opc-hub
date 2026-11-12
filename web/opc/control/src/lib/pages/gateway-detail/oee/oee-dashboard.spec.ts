import { Component, input, output, signal } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { By } from '@angular/platform-browser';
import { provideRouter, Router } from '@angular/router';
import { BehaviorSubject } from 'rxjs';
import { OeeSpan, toOeeTimeline } from '../../../model/oee';
import { OeeLiveEvent, OeeLiveStatus } from '../../../model/oee-live';
import { OeeService } from '../../../services/oee-service';
import { HighchartsLoader } from '../../node-detail/node-history/highcharts-loader';
import { OeeChart, OeeChartSpec } from './oee-chart';
import { OeeDashboard } from './oee-dashboard';
import { flush, pageTitleStub, provideRoute, silence, stable } from 'jde-spa/testing';
import { Feed } from 'jde-opc/testing';

//the chart, which the page hands a spec:  Highcharts' own drawing is oee-chart.spec's
@Component({ selector: 'oee-chart', template: '' })
class ChartStub{
	spec = input.required<OeeChartSpec>();
	band = output<any>();
}
const t = ( h:number, m=0 )=>new Date( Date.UTC( 2026, 10, 11, h, m ) );
const iso = ( h:number, m=0 )=>t( h, m ).toISOString();
const Jam = {id: 3, name: 'Jam', category: 'Unplanned'};
const shift = { name: 'First', start: t( 6 ), end: t( 14 ) };
const figures = ( at:Date )=>({ start: shift.start, end: at, availability: 0.8, performance: 0.9, quality: 0.95, oee: 0.684, partial: false });
const running = ( since:Date, at:Date ):OeeLiveStatus=>({ state: 'running', since, planned: true, shift, figures: figures( at ), calendarMissing: false });
const stopped = ( since:Date, at:Date, stop:object={} ):OeeLiveStatus=>({ state: 'stopped', since, planned: true, shift, figures: figures( at ), calendarMissing: false, stop: {start: since, small: false, ...stop} });
const all = [{id: 4, name: 'Pump 1', slug: 'pump1', inLine: false}, {id: 5, name: 'Pump 2', slug: 'pump2', inLine: false}, {id: 6, name: 'Filler', slug: 'filler', inLine: true}];
//The page over stubs:  a gateway whose oeeSubscribe opens a feed per machine, `push` a status on one;  `service` answers the
//machines and each machine's timeline, read to 09:00;  `addOn` is how the add-on settled.  `open` moves the page to another
//gateway, as the router does.
const create = async ( {machines=all, addOn='loaded', timeline}:{machines?:typeof all; addOn?:'loaded'|'absent'; timeline?:Error}={} )=>{
	const gatewayOf = ( name:string )=>{
		const feeds = new Map<number,Feed<OeeLiveEvent,number>>();
		const feed = ( id:number )=>feeds.get( id ) ?? feeds.set( id, new Feed<OeeLiveEvent,number>() ).get( id )!;
		return { name, slug: name, feed, oeeSubscribe: vi.fn( ( machine:number )=>feed( machine ).open( machine ) ) };
	};
	const gateway = gatewayOf( 'gw' );
	const service = {
		machines: vi.fn( async ()=>machines ),
		timeline: vi.fn( async ( _g:unknown, _id:number, _range:OeeSpan )=>{
			if( timeline )
				throw timeline;
			return toOeeTimeline( [{planned: [], segments: [{start: iso( 6 ), end: iso( 8, 12 ), state: 'running', small: false, uncoded: false}, {start: iso( 8, 12 ), end: iso( 8, 40 ), state: 'stopped', stop: 7, reason: Jam, small: false, uncoded: false}, {start: iso( 8, 40 ), end: iso( 9 ), state: 'running', small: false, uncoded: false}]}] );
		} )
	};
	const loader = { state: signal( addOn ), load: vi.fn( async ()=>addOn=='loaded' ? {} : undefined ) };
	const data = new BehaviorSubject<object>( {pageData: {gateway, routing: {}}} );
	const title = { title: '' };
	TestBed.configureTestingModule({ providers: [
		provideRouter( [] ), provideRoute( {data} ), { ...pageTitleStub, useValue: title },
		{ provide: OeeService, useValue: service }, { provide: HighchartsLoader, useValue: loader }
	]});
	TestBed.overrideComponent( OeeDashboard, {remove: {imports: [OeeChart]}, add: {imports: [ChartStub]}} );
	const navigate = vi.spyOn( TestBed.inject( Router ), 'navigate' ).mockResolvedValue( true );
	const fixture = TestBed.createComponent( OeeDashboard );
	fixture.componentRef.setInput( 'sideNav', undefined );
	await stable( fixture );
	const page = fixture.componentInstance;
	const push = async ( machine:number, status:OeeLiveStatus, of=gateway )=>{
		of.feed( machine ).next( {kind: 'status', status} );
		await flush( 4 );
		fixture.detectChanges();
	};
	const tiles = ()=>[...fixture.nativeElement.querySelectorAll( '.tile' )].map( ( a:any )=>[...a.querySelectorAll( '*' )].filter( ( e:any )=>!e.children.length ).map( ( e:any )=>e.textContent.trim().replace( /\s+/g, ' ' ) ).filter( ( x:string )=>!!x ).join( ' ' ) );//a tile's texts, leaf by leaf
	const chart = ()=><ChartStub|undefined>fixture.debugElement.query( By.directive( ChartStub ) )?.componentInstance;
	const spans = ( machine:number )=>page.bands().find( b=>b.machine.id==machine )?.segments.map( s=>[s.state, s.start.toISOString().slice( 11, 16 ), s.end.toISOString().slice( 11, 16 )] );
	const open = async ( next:ReturnType<typeof gatewayOf> )=>{
		data.next( {pageData: {gateway: next, routing: {}}} );
		await stable( fixture );
	};
	return { page, fixture, gateway, gatewayOf, service, title, navigate, push, tiles, chart, spans, open };
};

describe( 'OeeDashboard', ()=>{
	afterEach( ()=>{ vi.useRealTimers(); vi.restoreAllMocks(); } );
	it( 'subscribes to every machine, and has a tile for each in no line', async ()=>{
		const p = await create();
		expect( p.title.title ).toBe( 'gw - OEE' );
		expect( p.gateway.oeeSubscribe.mock.calls.map( ([id])=>id ) ).toEqual( [4, 5, 6] );
		expect( p.tiles() ).toEqual( ['Pump 1 Connecting Availability – Performance – Quality – OEE –', 'Pump 2 Connecting Availability – Performance – Quality – OEE –'] );
		await p.push( 4, running( t( 8, 40 ), t( 9, 1 ) ) );
		await p.push( 5, stopped( t( 8, 50 ), t( 9, 1 ) ) );
		expect( p.tiles()[0] ).toMatch( /^Pump 1 Running since .*, 21m Availability 80\.0% Performance 90\.0% Quality 95\.0% OEE 68\.4% First, so far$/ );
		expect( p.tiles()[1] ).toMatch( /^Pump 2 Stopped since .*, 11m Uncoded Availability 80\.0%/ );
		await p.push( 5, stopped( t( 8, 50 ), t( 9, 2 ), {reason: Jam} ) );
		expect( p.tiles()[1] ).toMatch( /, 12m Jam Unplanned Availability/ );
	} );
	it( "reads a machine's shift so far once, then grows its last band with each status and opens one on a state change", async ()=>{
		const p = await create();
		await p.push( 4, running( t( 8, 40 ), t( 9, 1 ) ) );
		expect( p.service.timeline ).toHaveBeenCalledTimes( 1 );
		expect( p.service.timeline.mock.calls[0].slice( 1 ) ).toEqual( [4, {start: t( 6 ), end: t( 14 )}] );//now is past this shift's end
		expect( p.spans( 4 ) ).toEqual( [['running', '06:00', '08:12'], ['stopped', '08:12', '08:40'], ['running', '08:40', '09:01']] );
		vi.useFakeTimers();
		const next = ( status:OeeLiveStatus )=>p.gateway.feed( 4 ).next( {kind: 'status', status} );//a ready row grows in the push's own turn
		next( running( t( 8, 40 ), t( 9, 2 ) ) );
		expect( p.spans( 4 )!.at( -1 ) ).toEqual( ['running', '08:40', '09:01'] );//growth waits for the next redraw
		vi.advanceTimersByTime( OeeDashboard.growEvery );
		expect( p.spans( 4 )!.at( -1 ) ).toEqual( ['running', '08:40', '09:02'] );
		next( running( t( 8, 40 ), t( 9, 3 ) ) );
		next( stopped( t( 9, 4 ), t( 9, 4 ), {small: true} ) );//a state change is drawn at once, with what grew before it
		expect( p.spans( 4 )!.slice( -2 ) ).toEqual( [['running', '08:40', '09:04'], ['stopped', '09:04', '09:04']] );
		expect( p.service.timeline ).toHaveBeenCalledTimes( 1 );
		vi.useRealTimers();
		p.fixture.detectChanges();
		const spec = p.chart()!.spec();
		expect( spec.kind=='gantt' && [spec.rows, spec.range, spec.state.map( b=>b.y ), spec.planned] ).toEqual( [['Pump 1'], {start: t( 6 ), end: t( 14 )}, [0, 0, 0], []] );//the stop has no length yet
		p.gateway.feed( 4 ).next( {kind: 'status', status: stopped( t( 9, 4 ), t( 9, 5 ), {small: true} )} );//its first is drawn at once
		expect( p.spans( 4 )!.at( -1 ) ).toEqual( ['stopped', '09:04', '09:05'] );
	} );
	it( "draws a row for a line's machine too, after the tiles' machines, and opens a band's machine", async ()=>{
		const p = await create();
		await p.push( 6, running( t( 8, 40 ), t( 9, 1 ) ) );
		await p.push( 5, {...running( t( 8, 40 ), t( 9, 1 ) ), shift: {name: 'Late', start: t( 8 ), end: t( 16 )}} );
		const spec = p.chart()!.spec();
		expect( spec.kind=='gantt' && [spec.rows, spec.range] ).toEqual( [['Pump 2', 'Filler'], {start: t( 6 ), end: t( 16 )}] );
		p.chart()!.band.emit( {y: 1} );
		expect( p.navigate.mock.calls.at( -1 )![0] ).toEqual( ['../machines', 'filler', 'oee'] );
	} );
	it( 'reads the row again for another shift, and has no row between shifts', async ()=>{
		const p = await create();
		await p.push( 4, running( t( 8, 40 ), t( 9, 1 ) ) );
		await p.push( 4, {state: 'stopped', since: t( 14 ), planned: false, calendarMissing: false, stop: {start: t( 14 ), small: false}} );
		expect( p.page.bands() ).toEqual( [] );
		expect( p.page.between() ).toEqual( ['Pump 1'] );
		expect( p.fixture.nativeElement.textContent ).toContain( 'Between shifts: Pump 1' );
		expect( p.tiles()[0] ).toContain( 'Between shifts.' );
		await p.push( 4, {...running( t( 14 ), t( 14, 1 ) ), shift: {name: 'Second', start: t( 14 ), end: t( 22 )}} );
		expect( p.service.timeline ).toHaveBeenCalledTimes( 2 );
		expect( p.page.bands()[0].range ).toEqual( {start: t( 14 ), end: t( 22 )} );
	} );
	it( 'keeps the tile of a machine whose subscription ends, saying why, with its bands as they stood', async ()=>{
		const p = await create();
		await p.push( 4, running( t( 8, 40 ), t( 9, 1 ) ) );
		p.gateway.feed( 4 ).error( {error: {requestId: 1, message: "Machine 'Pump 1' was deleted, which ends the subscription.", sc: 0, httpStatus: 404}} );
		p.fixture.detectChanges();
		expect( p.tiles()[0] ).toBe( "Pump 1 Ended Availability – Performance – Quality – OEE – Machine 'Pump 1' was deleted, which ends the subscription." );
		expect( p.spans( 4 )!.at( -1 ) ).toEqual( ['running', '08:40', '09:01'] );
		expect( p.tiles() ).toHaveLength( 2 );
	} );
	it( 'has neither tile nor row for a machine whose group the user may not read', async ()=>{
		const p = await create();
		p.gateway.feed( 5 ).error( {error: {requestId: 2, message: 'No Read on the group.', sc: 0, httpStatus: 403}} );
		p.fixture.detectChanges();
		expect( p.tiles().map( x=>x.split( ' ' ).slice( 0, 2 ).join( ' ' ) ) ).toEqual( ['Pump 1'] );
		expect( p.page.machines().map( m=>m.id ) ).toEqual( [4, 6] );
	} );
	it( 'marks a tile whose socket dropped, and reads its row again at the next status', async ()=>{
		const p = await create();
		await p.push( 4, running( t( 8, 40 ), t( 9, 1 ) ) );
		p.gateway.feed( 4 ).next( {kind: 'dropped'} );
		p.fixture.detectChanges();
		expect( p.tiles()[0] ).toContain( 'The connection to the gateway was lost' );
		await p.push( 4, running( t( 9, 20 ), t( 9, 30 ) ) );
		expect( p.service.timeline ).toHaveBeenCalledTimes( 2 );
		expect( p.tiles()[0] ).not.toContain( 'was lost' );
	} );
	it( 'is its tiles without the add-on:  no timeline read, no chart, and a notice', async ()=>{
		const p = await create( {addOn: 'absent'} );
		await p.push( 4, running( t( 8, 40 ), t( 9, 1 ) ) );
		expect( p.service.timeline ).not.toHaveBeenCalled();
		expect( p.chart() ).toBeUndefined();
		expect( p.tiles()[0] ).toContain( 'Running' );
		expect( p.fixture.nativeElement.textContent ).toContain( 'Highcharts Stock, which the install does not include' );
	} );
	it( 'starts a row at the status where its timeline is refused', async ()=>{
		const warn = silence();
		const p = await create( {timeline: new Error( '(400)more than readLimit' )} );
		await p.push( 4, running( t( 5 ), t( 9, 1 ) ) );
		expect( p.spans( 4 ) ).toEqual( [['running', '06:00', '09:01']] );
		expect( warn ).toHaveBeenCalledTimes( 1 );
	} );
	it( 'ends its subscriptions when it moves to another gateway, and when it goes', async ()=>{
		const p = await create();
		await p.push( 4, running( t( 8, 40 ), t( 9, 1 ) ) );
		const other = p.gatewayOf( 'other' );
		await p.open( other );
		expect( [4, 5, 6].map( id=>p.gateway.feed( id ).subscribers.length ) ).toEqual( [0, 0, 0] );
		expect( other.oeeSubscribe ).toHaveBeenCalledTimes( 3 );
		expect( [p.title.title, p.page.bands()] ).toEqual( ['other - OEE', []] );
		p.fixture.destroy();
		expect( [4, 5, 6].map( id=>other.feed( id ).subscribers.length ) ).toEqual( [0, 0, 0] );
	} );
	it( 'says so where there is no machine, and where the machines cannot be read', async ()=>{
		const none = await create( {machines: []} );
		expect( none.fixture.nativeElement.textContent ).toContain( 'No machines to show.' );
		TestBed.resetTestingModule();//a second page in the one test
		const p = await create();
		p.service.machines.mockRejectedValueOnce( new Error( '(403)No Read on oeeMachines.' ) );
		await p.open( p.gatewayOf( 'other' ) );
		expect( p.fixture.nativeElement.textContent ).toContain( 'Could not read the machines.' );
		expect( p.fixture.nativeElement.textContent ).toContain( 'No Read on oeeMachines.' );
	} );
} );