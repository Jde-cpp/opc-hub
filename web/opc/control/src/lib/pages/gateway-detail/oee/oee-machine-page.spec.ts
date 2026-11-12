import { Component, input, output, signal } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { By } from '@angular/platform-browser';
import { MatDialog } from '@angular/material/dialog';
import { provideRouter } from '@angular/router';
import { BehaviorSubject } from 'rxjs';
import { OeeCodingCall, OeeSpan, toOeeBucket, toOeeStop, toOeeTimeline, toOeeTimelines } from '../../../model/oee';
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
	band = output<any>();
}
const row = { id: 4, name: 'Pump 1', slug: 'pump1', histGroup: {id: 5, name: 'Pumps'}, countUnit: 'bottles' };
const Jam = {id: 3, name: 'Jam', category: 'Unplanned'};
const lineRow = { id: 10, name: 'Packaging', slug: 'packaging', level: 'Area', calendarId: 2 };//a line's page is the same page, its route's kind `lines`
const iso = ( h:number, m=0 )=>new Date( Date.UTC( 2026, 10, 11, h, m ) ).toISOString();
const stopJson = ( id:number, extra:object={} )=>({ id, start: iso( 8, 12 ), end: iso( 8, 40 ), ms: 1680000, small: false, open: false, uncoded: true, codings: [], ...extra });
//The page over stubs:  `service` records each read and answers what the test set on `answers`;  `status` is the machine's, a
//calendar found by default;  `addOn` is how the add-on settled;  `dialog` is what the coding dialog was opened with, and
//`closeWith` what it answers.  `open` moves the page to another machine, as the router does.  `line` opens it on a line, whose
//figures carry two members and whose timeline is a machine each.
const create = async ( {calendar='found', addOn='loaded', status, line=false}:{calendar?:string; addOn?:'loaded'|'absent'; status?:Error; line?:boolean}={} )=>{
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
		figures: vi.fn( async ( _g:unknown, of:unknown, _range:OeeSpan, by?:string, members?:boolean )=>{
			if( answers.figures && !by )
				throw answers.figures;
			const ofLine = typeof of=='object' ? {machines: 2, coverage: {performance: 2, quality: 1}, measured: ['availability', 'performance']} : {};
			return [toOeeBucket( {...bucket, ...ofLine, shift: by=='shift' ? 'First' : undefined, ...(members ? {members: [{...bucket, oee: 0.5, machine: {id: 5, name: 'Pump 2', slug: 'pump2'}}, {...bucket, oee: undefined, partial: false, line: {id: 11, name: 'Line A', slug: 'line-a', level: 'Line'}}]} : {})} )];
		} ),
		timelines: vi.fn( async ( _g:unknown, _line:number, _range:OeeSpan )=>toOeeTimelines( [
			{machine: {id: 4, name: 'Pump 1'}, planned: [], segments: [{start: iso( 6 ), end: iso( 8 ), state: 'running', small: false, uncoded: false}]},
			{machine: {id: 5, name: 'Pump 2'}, planned: [], segments: [{start: iso( 9 ), end: iso( 9, 5 ), state: 'stopped', stop: 99, small: false, uncoded: true}]}
		] ) ),
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
	const admin = { status: vi.fn( async ()=>{ if( status ) throw status; return {enabled: true, calendar, engine: 'live'}; } ), lineStatus: vi.fn( async ()=>({enabled: true, calendar}) ) };
	const loader = { state: signal( addOn ), load: vi.fn( async ()=>addOn=='loaded' ? {} : undefined ) };
	const dialog = vi.spyOn( OeeCodingDialog, 'open' ).mockImplementation( async ()=>answers.closeWith );
	const gateway = { slug: 'g' };
	const data = new BehaviorSubject<object>( line ? {kind: 'lines', pageData: {row: lineRow, routing: {}, schema: {}, gateway}} : {pageData: {row, routing: {}, schema: {}, gateway}} );
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
		const [seven, other] = p.page.timelines()![0].segments;
		const spec = p.charts()[0].spec();
		const band = spec.kind=='gantt' && spec.state.find( b=>b.custom.segment==seven );
		expect( band && band.custom.machine ).toBeUndefined();//a band of the machine's own chart carries none:  it is the page's
		p.charts()[0].band.emit( band );
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
	//#409:  a line's OEE page is this page, the line in the machine's place
	it( "opens a line headed by its level and name, every read the line's, with its members' tiles and what its factors are over", async ()=>{
		const p = await create( {line: true} );
		expect( p.title.title ).toBe( 'Area Packaging - OEE' );
		expect( p.text( '.line-heading' ) ).toEqual( ['Area Packaging'] );
		expect( [p.admin.lineStatus.mock.calls.length, p.admin.status.mock.calls.length, p.page.period()] ).toEqual( [1, 0, 'shift'] );
		const range = p.page.range()!;
		expect( p.service.shift.mock.calls[0][1] ).toEqual( {line: 10} );
		expect( p.service.figures.mock.calls[0].slice( 1 ) ).toEqual( [{line: 10}, range, undefined, true] );
		for( const read of [p.service.stops, p.service.losses] )
			expect( read.mock.calls[0].slice( 1, 3 ) ).toEqual( [{line: 10}, range] );
		expect( [p.service.timelines.mock.calls[0].slice( 1 ), p.service.timeline.mock.calls.length] ).toEqual( [[10, range], 0] );
		expect( p.text( '#oee-members + .tiles .tile' ) ).toEqual( ['Pump 2MachineAvailability80.0%Performance104.0%Quality95.0%OEE50.0%Partial', 'Line ALineAvailability80.0%Performance104.0%Quality95.0%OEE–'] );
		expect( p.page.members().map( m=>m.link ) ).toEqual( [['../../../machines', 'pump2', 'oee'], ['../..', 'line-a', 'oee']] );
		expect( p.page.counts() ).toEqual( [{label: 'Machines', text: '2'}, {label: 'Performance over', text: '2 of 2'}, {label: 'Quality over', text: '1 of 2'}] );
		expect( p.text( '.split table th[scope="col"]' ).slice( -2 ) ).toEqual( ['Coverage', 'Machines'] );
		expect( p.page.factors()[3].hint ).toBe( 'Not measured on every machine:  a machine needs a Good or Reject signal.  Coverage says how many it is over.' );//the line's words, not a machine's
		const trend = p.charts().map( c=>c.spec() ).find( c=>c.title.startsWith( 'OEE of' ) )!;
		expect( trend.kind=='columns' && trend.points[0].custom.text ).not.toContain( 'good of' );//its machines' counts don't add
		expect( p.text( '.period-bar a' ) ).toEqual( ['monitoringDashboard', 'settingsLine'] );//each with its icon's name
	} );
	it( "draws a line's chart a pair of rows per machine, lists its stops with their machines, and codes each on its machine", async ()=>{
		const p = await create( {line: true} );
		p.answers.stops = [stopJson( 7, {machine: {id: 4, name: 'Pump 1'}} )];
		await p.page.refresh();
		await stable( p.fixture );
		const spec = p.charts()[0].spec();
		expect( spec.kind=='gantt' && [spec.rows, spec.state.map( b=>b.y )] ).toEqual( [['Pump 1, planned', 'Pump 1', 'Pump 2, planned', 'Pump 2'], [1, 3]] );
		expect( p.page.stopColumns()[0] ).toBe( 'machine' );
		expect( p.rows( 'mat-row' )[0] ).toMatch( /^Pump 1 \| / );
		await p.page.code( p.page.stops()[0] );
		await p.dialogData().code( {start: 'a', end: 'b', reason: 3} );
		expect( p.service.code.mock.calls.at( -1 )!.slice( 1 ) ).toEqual( [4, {start: 'a', end: 'b', reason: 3}] );//on the stop's machine, not the line
		p.charts()[0].band.emit( spec.kind=='gantt' && spec.state[0] );//running time opens nothing
		await flush( 2 );
		expect( p.service.stop ).not.toHaveBeenCalled();
		p.charts()[0].band.emit( spec.kind=='gantt' && spec.state[1] );//Pump 2's stop, which the table lacks:  read on its machine
		await flush( 4 );
		expect( p.service.stop.mock.calls.at( -1 )!.slice( 1, 3 ) ).toEqual( [5, 99] );
		expect( p.dialogData().stop ).toMatchObject( {id: 99, machine: {id: 5, name: 'Pump 2'}} );
		await p.dialogData().code( {start: 'a', end: 'b', reason: 3} );
		expect( p.service.code.mock.calls.at( -1 )![1] ).toBe( 5 );
		const drawn = spec.kind=='gantt' && spec.state[1];//Pump 2's band, as the chart drew it before a read put Pump 2's pair first
		const [pump1, pump2] = await p.service.timelines( p.gateway, 10, p.page.range()! );
		p.service.timelines.mockResolvedValueOnce( [pump2, pump1] );
		await p.page.refresh();
		p.charts()[0].band.emit( drawn );
		await flush( 4 );
		expect( p.service.stop.mock.calls.at( -1 )!.slice( 1, 3 ) ).toEqual( [5, 99] );//the band's own machine, not its row's now
	} );
	it( "has no Code for a line's stop that names no machine", async ()=>{
		const p = await create( {line: true} );
		p.answers.stops = [stopJson( 8 )];
		await p.page.refresh();
		await stable( p.fixture );
		expect( p.fixture.nativeElement.querySelectorAll( 'mat-row button' ) ).toHaveLength( 0 );
		await p.page.code( p.page.stops()[0] );
		expect( p.dialog ).not.toHaveBeenCalled();
	} );
	it( 'goes from a line to a machine as the router moves it, with no line left of it', async ()=>{
		const p = await create( {line: true} );
		await p.open( row );
		expect( [p.page.line(), p.title.title, p.page.members(), p.page.stopColumns()[0]] ).toEqual( [undefined, 'Pump 1 - OEE', [], 'start'] );
		expect( p.service.figures.mock.calls.at( -1 )!.slice( 1 ) ).toHaveLength( 2 );
		expect( p.text( '.line-heading' ) ).toEqual( [] );
	} );
} );