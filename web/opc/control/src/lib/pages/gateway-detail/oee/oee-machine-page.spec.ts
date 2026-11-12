import { Component, input, output, signal } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { By } from '@angular/platform-browser';
import { MatDialog } from '@angular/material/dialog';
import { provideRouter } from '@angular/router';
import { BehaviorSubject } from 'rxjs';
import { OeeCodingCall, OeeSpan, toOeeBucket, toOeeStop, toOeeTimeline } from '../../../model/oee';
import { OeeAdminService } from '../../../services/oee-admin-service';
import { OeeService } from '../../../services/oee-service';
import { HighchartsLoader } from '../../node-detail/node-history/highcharts-loader';
import { OeeChart, OeeChartSpec } from './oee-chart';
import { OeeCodingDialog, OeeCodingDialogData } from './oee-coding-dialog';
import { OeeMachinePage } from './oee-machine-page';
import { flush, pageTitleStub, provideRoute, stable } from 'jde-spa/testing';

//the chart, which the page hands a spec:  Highcharts' own drawing is oee-chart.spec's
@Component({ selector: 'oee-chart', template: '' })
class ChartStub{
	spec = input.required<OeeChartSpec>();
	pick = output<any>();
}
const row = { id: 4, name: 'Pump 1', slug: 'pump1', histGroup: {id: 5, name: 'Pumps'}, countUnit: 'bottles' };
const Jam = {id: 3, name: 'Jam', category: 'Unplanned'};
const iso = ( h:number, m=0 )=>new Date( Date.UTC( 2026, 10, 11, h, m ) ).toISOString();
const stopJson = ( id:number, extra:object={} )=>({ id, start: iso( 8, 12 ), end: iso( 8, 40 ), ms: 1680000, small: false, open: false, uncoded: true, codings: [], ...extra });
//The page over stubs:  `service` records each read and answers what the test set on `answers`;  `status` is the machine's, a
//calendar found by default;  `addOn` is how the add-on settled;  `dialog` is what the coding dialog was opened with, and
//`closeWith` what it answers.  `open` moves the page to another machine, as the router does.
const create = async ( {calendar='found', addOn='loaded', status}:{calendar?:string; addOn?:'loaded'|'absent'; status?:Error}={} )=>{
	const answers = {
		timelineRefusals: 0,
		stops: [stopJson( 7 )], continuation: <string|undefined>undefined,
		figures: <Error|undefined>undefined,
		closeWith: false
	};
	const bucket = { start: iso( 6 ), end: iso( 14 ), planned: 23400000, run: 18720000, unplanned: 2880000, plannedStop: 1800000, small: 240000, excluded: 3600000, unknown: 600000, total: 16848, good: 16006, reject: 842, outside: 12, ideal: 16848000,
		availability: 0.8, performance: 1.04, quality: 0.95, oee: 0.684, teep: 0.556, measured: ['availability', 'performance', 'quality'], partial: true };
	const service = {
		reasons: vi.fn( async ()=>[Jam] ),
		shift: vi.fn( async ( _g:unknown, _id:number, now:Date )=>( {start: new Date( now.getTime()-3600000 ), end: new Date( now.getTime()+3600000 ), name: 'First'} ) ),
		figures: vi.fn( async ( _g:unknown, _id:number, _range:OeeSpan, by?:string )=>{
			if( answers.figures && !by )
				throw answers.figures;
			return [toOeeBucket( {...bucket, shift: by=='shift' ? 'First' : undefined} )];
		} ),
		stops: vi.fn( async ( _g:unknown, _id:number, _range:OeeSpan, _args:{uncoded:boolean; limit:number; continuation?:string} )=>({ stops: answers.stops.map( toOeeStop ), continuation: answers.continuation }) ),
		losses: vi.fn( async ( _g:unknown, _id:number, _range:OeeSpan )=>[{kind: <const>'reason', reason: Jam, ms: 1680000, stops: 1}, {kind: <const>'small', ms: 240000, stops: 2}] ),
		timeline: vi.fn( async ( _g:unknown, _id:number, _range:OeeSpan )=>{
			if( service.timeline.mock.calls.length<=answers.timelineRefusals )
				throw new Error( '(400)It is 12000 segments, more than readLimit, 10000:  narrow the range.' );
			return toOeeTimeline( [{planned: [], segments: [{start: iso( 8, 12 ), end: iso( 8, 40 ), state: 'stopped', stop: 7, small: false, uncoded: true}, {start: iso( 9 ), end: iso( 9, 5 ), state: 'stopped', stop: 99, small: false, uncoded: true}]}] );
		} ),
		stop: vi.fn( async ( _g:unknown, _id:number, id:number, _at:Date )=>id==99 ? toOeeStop( stopJson( 99, {start: iso( 9 ), end: iso( 9, 5 ), codings: [{id: 12, start: iso( 9 ), end: iso( 9, 2 ), reason: Jam, userName: 'op'}]} ) ) : undefined ),
		code: vi.fn( async ( _g:unknown, _id:number, _call:OeeCodingCall )=>undefined ),
		uncode: vi.fn( async ( _g:unknown, _id:number )=>{} )
	};
	const admin = { status: vi.fn( async ()=>{ if( status ) throw status; return {enabled: true, calendar, engine: 'live'}; } ) };
	const loader = { state: signal( addOn ), load: vi.fn( async ()=>addOn=='loaded' ? {} : undefined ) };
	const dialog = vi.spyOn( OeeCodingDialog, 'open' ).mockImplementation( async ()=>answers.closeWith );
	const gateway = { slug: 'g' };
	const data = new BehaviorSubject<object>( {pageData: {row, routing: {}, schema: {}, gateway}} );
	const title = { title: '' };
	TestBed.configureTestingModule({ providers: [
		provideRouter( [] ), provideRoute( {data} ), { ...pageTitleStub, useValue: title },
		{ provide: OeeService, useValue: service }, { provide: OeeAdminService, useValue: admin }, { provide: HighchartsLoader, useValue: loader }, { provide: MatDialog, useValue: {} }
	]});
	TestBed.overrideComponent( OeeMachinePage, {remove: {imports: [OeeChart]}, add: {imports: [ChartStub]}} );
	const fixture = TestBed.createComponent( OeeMachinePage );
	fixture.componentRef.setInput( 'sideNav', undefined );
	await stable( fixture );
	const page = fixture.componentInstance;
	const text = ( selector:string )=>[...fixture.nativeElement.querySelectorAll( selector )].map( ( e:any )=>e.textContent.trim().replace( /\s+/g, ' ' ) );
	const rows = ( selector:string )=>[...fixture.nativeElement.querySelectorAll( selector )].map( ( tr:any )=>[...tr.children].map( ( c:any )=>c.textContent.trim() ).join( ' | ' ) );//a table's rows, cell by cell
	const charts = ()=>fixture.debugElement.queryAll( By.directive( ChartStub ) ).map( d=><ChartStub>d.componentInstance );
	const open = async ( next:object, nextGateway:unknown=gateway )=>{
		data.next( {pageData: {row: next, routing: {}, schema: {}, gateway: nextGateway}} );
		await stable( fixture );
	};
	const dialogData = ()=><OeeCodingDialogData>dialog.mock.calls.at( -1 )![1];
	return { page, fixture, service, admin, answers, gateway, title, text, rows, charts, open, dialog, dialogData };
};

describe( 'OeeMachinePage', ()=>{
	afterEach( ()=>vi.restoreAllMocks() );
	it( 'opens a machine with a calendar on its shift, reading every part over it', async ()=>{
		const p = await create();
		expect( p.title.title ).toBe( 'Pump 1 - OEE' );
		expect( p.page.period() ).toBe( 'shift' );
		expect( p.page.periods().map( x=>x.id ) ).toEqual( ['shift', 'today', 'yesterday', 'week', 'lastWeek', 'custom'] );
		const range = p.page.range()!;
		expect( range.end.getTime()-range.start.getTime() ).toBe( 3600000 );//the shift so far:  it ends now
		for( const read of [p.service.figures, p.service.stops, p.service.losses, p.service.timeline] )
			expect( read.mock.calls[0].slice( 0, 3 ) ).toEqual( [p.gateway, 4, range] );
		expect( p.service.figures.mock.calls.map( c=>c[3] ) ).toEqual( [undefined] );//the trend by shift is the figures' one bucket
		expect( p.page.trend().map( b=>b.shift ) ).toEqual( ['First'] );
		expect( p.service.stops.mock.calls[0][3] ).toEqual( {uncoded: false, limit: 50} );
		expect( p.page.errors() ).toEqual( {} );
		expect( p.page.loading() ).toBe( 0 );
	} );
	it( 'opens a machine with no calendar on today, with no shift to pick or trend by', async ()=>{
		const p = await create( {calendar: 'none'} );
		expect( p.page.period() ).toBe( 'today' );
		expect( p.page.periods().map( x=>x.id ) ).not.toContain( 'shift' );
		expect( p.service.shift ).not.toHaveBeenCalled();
		expect( p.page.trendBy() ).toBe( 'day' );
		expect( p.page.range()!.start ).toEqual( new Date( new Date().getFullYear(), new Date().getMonth(), new Date().getDate() ) );
	} );
	it( 'shows the figures with the loss split, the counts in the unit, and marks a performance above 100%', async ()=>{
		const p = await create();
		expect( p.text( '.factor dt' ) ).toEqual( ['OEE', 'Availability', 'Performance', 'Quality', 'TEEP'] );
		expect( p.page.factors().map( f=>f.value ) ).toEqual( ['68.4%', '80.0%', '104.0%', '95.0%', '55.6%'] );
		expect( p.page.factors().map( f=>!!f.over ) ).toEqual( [false, false, true, false, false] );
		expect( p.fixture.nativeElement.querySelectorAll( '.factor .over' ) ).toHaveLength( 1 );
		expect( p.rows( '.split table:first-child tbody tr' ) ).toEqual( ['Run | 5h 12m | 80.0%', 'Unplanned stops | 48m | 12.3%', 'Planned stops | 30m | 7.7%'] );
		expect( p.page.counts() ).toEqual( [{label: 'Total', text: `${( 16848 ).toLocaleString()} bottles`}, {label: 'Good', text: `${( 16006 ).toLocaleString()} bottles`}, {label: 'Reject', text: '842 bottles'}, {label: 'Outside planned time', text: '12 bottles'}] );
		expect( p.text( 'section .notice' ).some( t=>t.includes( 'Partial' ) ) ).toBe( true );
	} );
	it( 'narrows a period the timeline refuses to its newest half until it is taken, and says what the chart shows', async ()=>{
		const p = await create();
		p.answers.timelineRefusals = p.service.timeline.mock.calls.length+2;
		await p.page.pick( 'yesterday' );
		await stable( p.fixture );
		const range = p.page.range()!, asked = p.service.timeline.mock.calls.slice( -3 ).map( c=>c[2] ), quarter = ( range.end.getTime()-range.start.getTime() )/4;
		expect( asked.map( r=>r.end ) ).toEqual( [range.end, range.end, range.end] );
		expect( asked.map( r=>range.end.getTime()-r.start.getTime() ) ).toEqual( [quarter*4, quarter*2, quarter] );
		expect( p.page.narrowed() ).toEqual( asked[2] );
		expect( p.text( 'section .notice' ).some( t=>t.includes( 'more stops than the gateway reads at once' ) ) ).toBe( true );
		expect( p.charts()[0].spec() ).toMatchObject( {kind: 'gantt', range: asked[2]} );
		expect( p.service.figures.mock.calls.at( -1 )![2] ).toEqual( range );//the figures are the whole period's
		await p.page.pick( 'today' );
		expect( p.page.narrowed() ).toBeUndefined();
	} );
	it( "says a part's refusal in its place and keeps the rest", async ()=>{
		const p = await create();
		p.answers.figures = new Error( 'Forbidden.' );
		await p.page.refresh();
		await stable( p.fixture );
		expect( p.page.errors() ).toEqual( {figures: 'Forbidden.', trend: 'Forbidden.'} );//the trend by shift over This shift is the figures' bucket
		expect( p.page.bucket() ).toBeUndefined();
		expect( p.text( '#oee-figures ~ .refused' ) ).toEqual( ['Forbidden.'] );
		expect( p.page.stops() ).toHaveLength( 1 );
		expect( p.page.losses() ).toHaveLength( 2 );
		await p.page.pick( 'today' );
		expect( p.page.errors() ).toEqual( {figures: 'Forbidden.'} );
		expect( p.page.trend() ).toHaveLength( 1 );
	} );
	it( 'is its figures and tables without the add-on:  no chart, no timeline read, the trend as rows', async ()=>{
		const p = await create( {addOn: 'absent'} );
		expect( p.charts() ).toEqual( [] );
		expect( p.service.timeline ).not.toHaveBeenCalled();
		expect( p.fixture.nativeElement.querySelector( '#oee-timeline' ) ).toBeNull();
		expect( p.text( '.notice' ).some( t=>t.includes( 'which the install does not include' ) ) ).toBe( true );
		expect( p.text( '.factor dt' ) ).toHaveLength( 5 );
		expect( p.rows( 'table[aria-label="Losses"] tbody tr' ) ).toEqual( ['Jam | Unplanned | 28m | 87.5% | 1', 'Small stops |  | 4m | 12.5% | 2'] );
		expect( p.fixture.nativeElement.querySelectorAll( 'table[aria-label="OEE by bucket"] tbody tr' ) ).toHaveLength( 1 );
		expect( p.fixture.nativeElement.querySelectorAll( 'mat-row' ) ).toHaveLength( 1 );
	} );
	it( 'hands the charts the Gantt, the Pareto and the trend', async ()=>{
		const p = await create();
		expect( p.charts().map( c=>[c.spec().kind, c.spec().title] ) ).toEqual( [['gantt', 'Timeline of Pump 1'], ['columns', 'Losses of Pump 1'], ['columns', 'OEE of Pump 1 by shift']] );
		await p.page.setTrendBy( 'day' );
		await stable( p.fixture );
		expect( p.service.figures.mock.calls.at( -1 )![3] ).toBe( 'day' );
		expect( p.charts()[2].spec().title ).toBe( 'OEE of Pump 1 by day' );
	} );
	it( 'reads the stops again for the Uncoded filter, and the page after for Earlier stops', async ()=>{
		const p = await create();
		p.answers.continuation = 'next';
		await p.page.setUncodedOnly( true );
		expect( p.service.stops.mock.calls.at( -1 )![3] ).toEqual( {uncoded: true, limit: 50} );
		expect( p.page.hasMore() ).toBe( true );
		p.answers.stops = [stopJson( 6 )];
		p.answers.continuation = undefined;
		await p.page.loadMore();
		expect( p.service.stops.mock.calls.at( -1 )![3] ).toEqual( {uncoded: true, limit: 50, continuation: 'next'} );
		expect( p.page.stops().map( s=>s.id ) ).toEqual( [7, 6] );
		expect( p.page.hasMore() ).toBe( false );
	} );
	it( "codes a stop from its row and reads the page again once a coding landed", async ()=>{
		const p = await create();
		const reads = p.service.figures.mock.calls.length;
		await p.page.code( p.page.stops()[0] );
		expect( p.dialogData().stop ).toBe( p.page.stops()[0] );
		expect( p.dialogData().reasons() ).toEqual( {list: [Jam]} );
		await p.dialogData().reread();
		expect( p.service.stop ).toHaveBeenCalledWith( p.gateway, 4, 7, p.page.stops()[0].start );
		expect( p.service.figures.mock.calls.length ).toBe( reads );//closed with nothing sent:  nothing read
		await p.dialogData().code( {start: 'a', end: 'b', reason: 3} );
		expect( p.service.code ).toHaveBeenCalledWith( p.gateway, 4, {start: 'a', end: 'b', reason: 3} );
		await p.dialogData().uncode( 9 );
		expect( p.service.uncode ).toHaveBeenCalledWith( p.gateway, 9 );
		p.answers.closeWith = true;
		await p.page.code( p.page.stops()[0] );
		expect( p.service.figures.mock.calls.length ).toBe( reads+1 );//the figures, which the trend by shift shares
		expect( p.service.losses.mock.calls.length ).toBe( 2 );
	} );
	it( "opens the dialog from a band on the table's stop, or on the gateway's of one the table lacks", async ()=>{
		const p = await create();
		const [seven, other] = p.page.timeline()!.segments;
		p.charts()[0].pick.emit( seven );
		expect( p.dialogData().stop ).toBe( p.page.stops()[0] );
		expect( p.service.stop ).not.toHaveBeenCalled();
		await p.page.codeSegment( other );
		expect( p.service.stop ).toHaveBeenCalledWith( p.gateway, 4, 99, other.start );
		expect( p.dialogData().stop ).toMatchObject( {id: 99, codings: [{id: 12}]} );//whole, with its codings
		const opened = p.dialog.mock.calls.length;
		await p.page.codeSegment( {...other, stop: 98} );
		expect( p.dialog.mock.calls.length ).toBe( opened );
		expect( p.page.errors().timeline ).toMatch( /^The stop is gone/ );
	} );
	it( "drops a refresh's stops that land after the Uncoded toggle's, and the toggle's that land after a later refresh's", async ()=>{
		const p = await create();
		let answer!:( page:any )=>void;
		p.service.stops.mockImplementationOnce( ()=>new Promise<any>( r=>answer = r ) );
		const refresh = p.page.refresh();
		await flush( 2 );
		await p.page.setUncodedOnly( true );//answers at once
		answer( {stops: [toOeeStop( stopJson( 1 ) ), toOeeStop( stopJson( 2 ) )], continuation: 'all'} );
		await refresh;
		expect( p.page.stops().map( s=>s.id ) ).toEqual( [7] );//the toggle's
		expect( p.page.hasMore() ).toBe( false );
		p.service.stops.mockImplementationOnce( ()=>new Promise<any>( r=>answer = r ) );
		const toggle = p.page.setUncodedOnly( false );
		await p.page.refresh();
		answer( {stops: [toOeeStop( stopJson( 1 ) )]} );
		await toggle;
		expect( p.page.stops().map( s=>s.id ) ).toEqual( [7] );//the refresh's
	} );
	it( "keeps the stops paged in through a coding's refresh", async ()=>{
		const p = await create();
		p.answers.stops = Array.from( {length: 50}, ( _, i )=>stopJson( 100-i ) );
		p.answers.continuation = 'next';
		await p.page.refresh();
		p.answers.stops = Array.from( {length: 10}, ( _, i )=>stopJson( 50-i ) );
		await p.page.loadMore();
		expect( p.page.stops() ).toHaveLength( 60 );
		p.answers.closeWith = true;
		await p.page.code( p.page.stops()[55] );
		expect( p.service.stops.mock.calls.at( -1 )![3] ).toEqual( {uncoded: false, limit: 60} );
		await p.page.refresh();
		expect( p.service.stops.mock.calls.at( -1 )![3] ).toEqual( {uncoded: false, limit: 50} );//Refresh reads the first page
	} );
	it( 'stops narrowing a refused timeline once the page has moved on', async ()=>{
		const p = await create();
		const refusal = new Error( '(400)It is 12000 segments, more than readLimit, 10000:  narrow the range.' );
		let refuse!:( e:Error )=>void;
		const before = p.service.timeline.mock.calls.length;
		p.service.timeline.mockImplementationOnce( async ()=>{ throw refusal; } ).mockImplementationOnce( ()=>new Promise<any>( ( _r, reject )=>refuse = reject ) );
		const yesterday = p.page.pick( 'yesterday' );
		await flush( 2 );
		const today = p.page.pick( 'today' );
		await flush( 2 );
		refuse( refusal );
		await Promise.all( [yesterday, today] );
		const midnight = new Date( new Date().getFullYear(), new Date().getMonth(), new Date().getDate() ).getTime();
		expect( p.service.timeline.mock.calls.slice( before ).filter( c=>c[2].end.getTime()==midnight ) ).toHaveLength( 2 );
		expect( p.page.loading() ).toBe( 0 );
		expect( p.page.narrowed() ).toBeUndefined();
		expect( p.page.errors() ).toEqual( {} );
	} );
	it( "keeps This shift's bounds while it runs, and says a refused shift read as the error", async ()=>{
		const p = await create();
		await p.page.refresh();
		expect( p.service.shift ).toHaveBeenCalledTimes( 1 );//still running:  its end is past now
		p.service.shift.mockRejectedValueOnce( new Error( 'Timed out.' ) );
		await p.open( {id: 5, name: 'Pump 2', slug: 'pump2'} );
		expect( p.service.shift ).toHaveBeenCalledTimes( 2 );//another machine's
		expect( p.page.rangeProblem() ).toBe( "Could not find the machine's shift:  Timed out." );
		expect( p.page.bucket() ).toBeUndefined();
	} );
	it( 'says a refused status read, and opens on today with no shift', async ()=>{
		const p = await create( {status: new Error( 'Forbidden.' )} );
		expect( p.page.period() ).toBe( 'today' );
		expect( p.page.periods().map( x=>x.id ) ).not.toContain( 'shift' );
		expect( p.page.statusProblem() ).toBe( "Could not read the machine's calendar, so This shift isn't offered:  Forbidden." );
		expect( p.text( '.notice' ).some( t=>t.includes( "This shift isn't offered" ) ) ).toBe( true );
		expect( p.page.utcDays() ).toBe( false );
	} );
	it( "names a machine with no calendar's days as UTC's", async ()=>{
		const p = await create( {calendar: 'none'} );
		expect( p.page.utcDays() ).toBe( true );
		expect( p.charts()[2].spec().title ).toBe( 'OEE of Pump 1 by day (UTC)' );
		TestBed.resetTestingModule();//a second page in the one test, without the add-on
		const absent = await create( {calendar: 'none', addOn: 'absent'} );
		expect( absent.text( 'table[aria-label="OEE by bucket"] th[scope="col"]' )[0] ).toBe( 'Day (UTC)' );
	} );
	it( "drops another machine's answers when the router moves the page on", async ()=>{
		const p = await create();
		let answer!:( rows:any[] )=>void;
		p.service.losses.mockImplementationOnce( ()=>new Promise<any[]>( r=>answer = r ) );
		const refresh = p.page.refresh();
		await flush( 2 );//past the shift's read, to the parts'
		await p.open( {id: 5, name: 'Pump 2', slug: 'pump2'} );
		answer( [{kind: 'uncoded', ms: 1, stops: 1}] );
		await refresh;
		expect( p.title.title ).toBe( 'Pump 2 - OEE' );
		expect( p.page.losses().map( l=>l.kind ) ).toEqual( ['reason', 'small'] );//Pump 2's own read
		expect( p.service.losses.mock.calls.at( -1 )![1] ).toBe( 5 );
		expect( p.page.uncodedOnly() ).toBe( false );
	} );
	it( "names a stop's reason, the one its parts share, or why it has none", async ()=>{
		const p = await create();
		const coding = ( id:number, reason:object, comment?:string )=>({ id, start: iso( 8, 12 ), end: iso( 8, 40 ), reason, comment, userName: 'op' });
		const NoFilm = {id: 4, name: 'No film', category: 'Unplanned'};
		const text = ( extra:object )=>p.page.reasonText( toOeeStop( stopJson( 1, {uncoded: false, ...extra} ) ) );
		expect( text( {reason: Jam} ) ).toBe( 'Jam' );
		expect( text( {uncoded: true} ) ).toBe( 'Uncoded' );
		expect( text( {small: true} ) ).toBe( 'Small stop' );
		expect( text( {small: true, codings: [coding( 1, Jam ), coding( 2, Jam )]} ) ).toBe( 'Jam' );
		expect( text( {codings: [coding( 1, Jam ), coding( 2, NoFilm )]} ) ).toBe( 'Coded in parts' );
		expect( p.page.comments( toOeeStop( stopJson( 1, {codings: [coding( 1, Jam, 'film' ), coding( 2, NoFilm )]} ) ) ) ).toEqual( ['Jam film (op)', 'No film (op)'] );
		expect( p.page.comments( toOeeStop( stopJson( 1, {codings: [coding( 1, Jam, 'film' )]} ) ) ) ).toEqual( ['film (op)'] );
	} );
	it( 'says so where a custom range is no range, and reads nothing', async ()=>{
		const p = await create();
		const reads = p.service.figures.mock.calls.length;
		await p.page.pick( 'custom' );
		expect( p.page.rangeProblem() ).toBe( 'Enter a From before now, and a To after it or none.' );
		expect( p.page.range() ).toBeUndefined();
		expect( p.page.bucket() ).toBeUndefined();
		expect( p.service.figures.mock.calls.length ).toBe( reads );
	} );
} );