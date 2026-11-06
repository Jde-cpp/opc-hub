import { TestBed } from '@angular/core/testing';
import { provideRouter } from '@angular/router';
import type Highcharts from 'highcharts/esm/highstock';
import { PushBuffer } from '../../../model/push-buffer';
import { WatchRow } from '../../../model/watch';
import { HighchartsLoader } from '../../node-detail/node-history/highcharts-loader';
import { WatchTrend } from './watch-trend';
import { stable } from 'jde-spa/testing';
import { BundledHighcharts, histValue, nodeA as A, nodeB as B, variable } from 'jde-opc/testing';

//a row of `opc` on `node`, described as `name` at `path`
const row = ( opc:string, name:string, path:string|null=null, node=A )=>{ const r = new WatchRow( opc, node, {} ); r.describe( variable( 1, name ), path ); return r; };
//a push of the row, as the page buffers it:  under the row's own series
const push = ( buffer:PushBuffer, r:WatchRow, ms:number, value:number )=>buffer.push( r.key, {...histValue( r.node, ms, value ), series: r.trendKey} );
//the tab over `rows` and `buffer`, on the 10 minute window, settled
const create = async ( rows:WatchRow[], buffer:PushBuffer )=>{
	TestBed.configureTestingModule({ providers: [provideRouter( [] ), { provide: HighchartsLoader, useValue: new BundledHighcharts() }] });
	const fixture = TestBed.createComponent( WatchTrend );
	fixture.componentRef.setInput( 'rows', rows );
	fixture.componentRef.setInput( 'buffer', buffer );
	fixture.componentRef.setInput( 'names', ( slug:string )=>slug.toUpperCase() );
	fixture.componentRef.setInput( 'window', 10 );
	return fixture;
};
//the tab once Highcharts has loaded and made the chart
const draw = async ( rows:WatchRow[], buffer:PushBuffer )=>{
	const fixture = await create( rows, buffer );
	const H = <typeof Highcharts>(await import('highcharts/esm/highstock')).default;
	await import( 'highcharts/esm/modules/accessibility' );//loaded here, so the loader's imports are a known number of turns
	await stable( fixture );
	const plot:HTMLElement = fixture.nativeElement.querySelector( '.plot' );
	const chart = H.charts.find( c=>c && c.container.parentElement==plot );
	expect( chart ).toBeDefined();
	const ys = ( r:WatchRow )=>(<any[]>(<Highcharts.SeriesLineOptions>(<Highcharts.Series>chart!.get( r.seriesId )).options).data).map( p=>p.y );
	const lines = ()=>chart!.series.filter( s=>s.options.type=='line' && !s.options.id?.startsWith( 'highcharts-' ) && !s.options.linkedTo ).map( s=>s.name );//the rows' own, not their holds
	return { fixture, plot, chart: chart!, ys, lines };
};

//Clients 7 (#420, spec *Live trend*):  the Trend tab, the History tab's chart over the buffer
describe( 'WatchTrend', ()=>{
	afterEach( ()=>vi.restoreAllMocks() );
	//Two connections may hold one node id, so a row's series is its own, not its node's;  the legend names the connection then.
	it( "draws one line per ticked row from its pushes, two connections' rows of one node id apart", async ()=>{
		const buffer = new PushBuffer( 10*60_000 );
		const a = row( 'local', 'rpm' ), b = row( 'plc', 'rpm' );
		push( buffer, a, 1000, 1 ); push( buffer, b, 1000, 2 ); push( buffer, a, 2000, 3 );
		const { fixture, plot, chart, ys, lines } = await draw( [a, b], buffer );
		expect( ys( a ) ).toEqual( [1, 3] );
		expect( ys( b ) ).toEqual( [2] );
		expect( lines() ).toEqual( ['rpm (LOCAL)', 'rpm (PLC)'] );
		expect( plot.getAttribute( 'aria-label' ) ).toBe( 'Live trend of rpm (LOCAL), rpm (PLC). Highcharts interactive chart.' );
		push( buffer, b, 3000, 4 );//a push lands on its line
		fixture.detectChanges();
		expect( ys( b ) ).toEqual( [2, 4] );
		expect( fixture.nativeElement.querySelector( '.row-count' ).textContent.trim() ).toBe( '4 values' );
		fixture.componentRef.setInput( 'rows', [b] );//a row unticked takes its line, and a name no other row has stands alone
		fixture.detectChanges();
		expect( lines() ).toEqual( ['rpm'] );
		expect( (<Highcharts.Series>chart.get( `${b.seriesId}/flags` )).name ).toBe( 'rpm status' );
		fixture.destroy();
	} );
	//the drop filter takes a `highcharts-` id for Highcharts' own navigator series:  a row of a slug so named was never removed (420 #12)
	it( "takes the line of an unticked row whose slug starts as Highcharts' own ids do", async ()=>{
		const buffer = new PushBuffer( 60_000 ), a = row( 'highcharts-plc', 'rpm' ), b = row( 'local', 'flow', null, B );
		push( buffer, a, 1000, 1 ); push( buffer, a, 2000, 2 );
		const { fixture, chart } = await draw( [a, b], buffer );
		expect( chart.get( a.seriesId ) ).toBeDefined();
		fixture.componentRef.setInput( 'rows', [b] );
		fixture.detectChanges();
		expect( chart.get( a.seriesId ) ).toBeUndefined();
		expect( chart.get( `${a.seriesId}/flags` ) ).toBeUndefined();
		fixture.destroy();
	} );
	//two pumps' motorRpm on one connection read alike:  the path tells them apart, as the list's folder does
	it( 'names rows of one name by their paths, and by the connection where those differ', async ()=>{
		const a = row( 'local', 'rpm', 'Objects/5~pump1/5~rpm' ), b = row( 'local', 'rpm', 'Objects/5~pump2/5~rpm', B ), c = row( 'plc', 'rpm', 'Objects/5~pump1/5~rpm' );
		const { fixture, lines } = await draw( [a, b], new PushBuffer( 60_000 ) );
		expect( lines() ).toEqual( ['Objects/pump1/rpm', 'Objects/pump2/rpm'] );
		fixture.componentRef.setInput( 'rows', [a, b, c] );
		fixture.detectChanges();
		expect( lines() ).toEqual( ['Objects/pump1/rpm (LOCAL)', 'Objects/pump2/rpm (LOCAL)', 'Objects/pump1/rpm (PLC)'] );
		fixture.destroy();
	} );
	//a node the gateway could not place has no path:  two such rows of one name on one connection read alike (420 #11)
	it( 'names a row of a shared name without a path by its node id', async ()=>{
		const a = row( 'local', 'rpm' ), b = row( 'local', 'rpm', null, B );
		const { fixture, lines } = await draw( [a, b], new PushBuffer( 60_000 ) );
		expect( lines() ).toEqual( [`rpm ${A.uaString()}`, `rpm ${B.uaString()}`] );
		fixture.destroy();
	} );
	//the page owns the line shape:  the tab is made again on every visit, and its own reset to Stepped each time (420 #10)
	it( "draws the page's line shape, and hands a pick back", async ()=>{
		const buffer = new PushBuffer( 60_000 ), a = row( 'local', 'rpm' );
		push( buffer, a, 1000, 1 ); push( buffer, a, 2000, 2 );
		const { fixture, chart } = await draw( [a], buffer );
		const emitted:boolean[] = [];
		fixture.componentInstance.stepped.subscribe( v=>emitted.push( v ) );
		fixture.componentRef.setInput( 'stepped', false );
		fixture.detectChanges();
		expect( (<Highcharts.SeriesLineOptions>(<Highcharts.Series>chart.get( a.seriesId )).options).step ).toBeFalsy();
		const buttons:HTMLElement[] = [...fixture.nativeElement.querySelectorAll( '.shape-group mat-button-toggle' )];
		buttons[0].querySelector( 'button' )!.click();
		expect( emitted ).toEqual( [true] );
		fixture.destroy();
	} );
	//the chart opens on the window's range button and follows the toggle, so the view is the window, not every push held.  A push
	//every 10 s:  Highcharts disables a button narrower than five times the closest two points.
	it( 'presses the range button of the window', async ()=>{
		const buffer = new PushBuffer( 10*60_000 ), a = row( 'local', 'rpm' );
		for( let s=0; s<=1200; s+=10 )
			push( buffer, a, s*1000, s );
		const { fixture, chart } = await draw( [a], buffer );
		const selector = (<any>chart).rangeSelector, width = ()=>{ const e = chart.xAxis[0].getExtremes(); return e.max-e.min; };
		expect( selector.selected ).toBe( 1 );
		expect( width() ).toBe( 10*60_000 );
		fixture.componentRef.setInput( 'window', 1 );
		fixture.detectChanges();
		expect( selector.selected ).toBe( 0 );
		expect( width() ).toBe( 60_000 );
		fixture.destroy();
	} );
	//Highcharts drops a button wider than the data, and nothing pressed it again:  a page opened a minute ago stayed on all it held,
	//which a quiet row stretched back to when the page opened (420 #3).  Held, it is pressed once the buffer spans the window, until
	//the user picks another view.
	it( "presses the window's button again once the buffer spans it, until the user moves the view", async ()=>{
		const buffer = new PushBuffer( 10*60_000 ), a = row( 'local', 'rpm' );
		for( let s=0; s<=120; ++s )
			push( buffer, a, s*1000, s );
		const { fixture, chart } = await draw( [a], buffer );
		const selector = (<any>chart).rangeSelector, axis = chart.xAxis[0], width = ()=>{ const e = axis.getExtremes(); return e.max-e.min; };
		expect( selector.selected ).not.toBe( 1 );
		expect( width() ).toBe( 120_000 );
		for( let s=121; s<=720; ++s )
			push( buffer, a, s*1000, s );
		fixture.detectChanges();
		expect( selector.selected ).toBe( 1 );
		expect( width() ).toBe( 10*60_000 );
		axis.setExtremes( 420_000, 720_000, true, false, {trigger: 'navigator'} );//the user's own view
		push( buffer, a, 721_000, 721 );
		fixture.detectChanges();
		expect( selector.selected ).not.toBe( 1 );
		expect( width() ).toBe( 300_000 );
		fixture.destroy();
	} );
	//a row pushing every 30 s:  Highcharts' own minRange, five times the closest points' gap, disabled the 1m button (420 #3)
	it( 'presses the 1 min button over a sparse row', async ()=>{
		const buffer = new PushBuffer( 60_000 ), a = row( 'local', 'rpm' );
		for( let s=0; s<=600; s+=30 )
			push( buffer, a, s*1000, s );
		const { fixture, chart } = await draw( [a], buffer );
		fixture.componentRef.setInput( 'window', 1 );
		fixture.detectChanges();
		const e = chart.xAxis[0].getExtremes();
		expect( (<any>chart).rangeSelector.selected ).toBe( 0 );
		expect( e.max-e.min ).toBe( 60_000 );
		fixture.destroy();
	} );
	//A row reports on change, so a quiet row's line ended at its last push, and a setpoint's one push drew nothing (420 #4).  Each line
	//holds its last value on to the newest push of any row, and a lone point is a marker until a neighbour comes.
	it( "holds each line's last value on to the newest push of any row", async ()=>{
		const buffer = new PushBuffer( 10*60_000 ), a = row( 'local', 'rpm' ), b = row( 'plc', 'flow', null, B );
		push( buffer, a, 1000, 5 );
		for( let s=1; s<=60; ++s )
			push( buffer, b, s*1000, s );
		const { fixture, chart } = await draw( [a, b], buffer );
		const held = ( r:WatchRow )=>(<any[]>(<Highcharts.SeriesLineOptions>(<Highcharts.Series>chart.get( `${r.seriesId}/hold` )).options).data).map( p=>[p.x, p.y] );
		const markers = ( r:WatchRow )=>(<any[]>(<Highcharts.SeriesLineOptions>(<Highcharts.Series>chart.get( r.seriesId )).options).data).map( p=>p.marker?.enabled );
		expect( held( a ) ).toEqual( [[1000, 5], [60_000, 5]] );
		expect( held( b ) ).toEqual( [[60_000, 60]] );//its last push is the newest:  the dot alone
		expect( markers( a ) ).toEqual( [true] );
		push( buffer, b, 61_000, 61 );
		fixture.detectChanges();
		expect( held( a ) ).toEqual( [[1000, 5], [61_000, 5]] );
		push( buffer, a, 30_000, 6 );//a neighbour:  the marker goes
		fixture.detectChanges();
		expect( markers( a ) ).toEqual( [undefined, undefined] );
		expect( held( a ) ).toEqual( [[30_000, 6], [61_000, 6]] );
		buffer.push( a.key, {...histValue( a.node, 40_000, undefined, {status: 0x80000000} ), series: a.trendKey} );//Bad:  its gap goes on
		fixture.detectChanges();
		expect( held( a ) ).toEqual( [] );
		fixture.destroy();
	} );
	//every row is buffered and the page hands new rows with every push:  each one redrew the chart from every ticked point (420 #5)
	it( "redraws for a ticked row's push alone", async ()=>{
		const buffer = new PushBuffer( 10*60_000 ), a = row( 'local', 'rpm' ), other = row( 'plc', 'flow', null, B );
		push( buffer, a, 1000, 1 ); push( buffer, a, 2000, 2 );
		const { fixture, chart, ys } = await draw( [a], buffer );
		const redraw = vi.spyOn( chart, 'redraw' );
		push( buffer, other, 3000, 9 );
		fixture.componentRef.setInput( 'rows', [a] );//the page's tickedRows, a new array with every push
		fixture.detectChanges();
		expect( redraw ).not.toHaveBeenCalled();
		push( buffer, a, 3000, 3 );
		fixture.detectChanges();
		expect( redraw ).toHaveBeenCalledTimes( 1 );
		expect( ys( a ) ).toEqual( [1, 2, 3] );
		fixture.destroy();
	} );
	//a setpoint's one push, then a write:  under jsdom's unmeasured SVG the second draw threw a RangeError (420 N1)
	it( 'draws the next push after a lone one', async ()=>{
		const buffer = new PushBuffer( 10*60_000 ), a = row( 'local', 'rpm' );
		push( buffer, a, 1000, 1 );
		const { fixture, chart, ys } = await draw( [a], buffer );
		push( buffer, a, 2000, 3 );
		fixture.detectChanges();
		expect( ys( a ) ).toEqual( [1, 3] );
		expect( chart.plotWidth ).toBeGreaterThan( 0 );
		fixture.destroy();
	} );
	//A subscribe brought two pushes of one value 1 ms apart:  each the other's neighbour, so no lone marker, and a 1 ms line drew nothing
	//at all (420 W1).  The hold ends in a dot at the newest time, the value now.
	it( "ends each line in a dot at the newest time", async ()=>{
		const buffer = new PushBuffer( 10*60_000 ), a = row( 'local', 'rpm' ), b = row( 'plc', 'flow', null, B );
		push( buffer, a, 1000, 511 ); push( buffer, a, 1001, 511 );
		push( buffer, b, 500, 7 );
		const { fixture, chart } = await draw( [a, b], buffer );
		const hold = ( r:WatchRow )=>(<any[]>(<Highcharts.SeriesLineOptions>(<Highcharts.Series>chart.get( `${r.seriesId}/hold` )).options).data).map( p=>[p.x, p.y, p.marker?.enabled ?? false] );
		expect( hold( a ) ).toEqual( [[1001, 511, true]] );
		expect( hold( b ) ).toEqual( [[500, 7, false], [1001, 7, true]] );
		fixture.destroy();
	} );
	//Highcharts scrolls only a view that ends at the newest data, and presses a button from the view's right edge:  unticking the row
	//with the newest push left the view past the rest, and the next push landed off it (420 W2)
	it( 'follows the newest push once the row that held it is unticked', async ()=>{
		const buffer = new PushBuffer( 60*60_000 ), a = row( 'local', 'rpm' ), b = row( 'plc', 'flow', null, B );
		for( let s=0; s<=720; ++s )
			push( buffer, a, s*1000, s );
		push( buffer, b, 900_000, 1 );
		const { fixture, chart } = await draw( [a, b], buffer );
		const view = ()=>{ const e = chart.xAxis[0].getExtremes(); return [e.min, e.max]; };
		expect( view() ).toEqual( [300_000, 900_000] );
		fixture.componentRef.setInput( 'rows', [a] );
		fixture.detectChanges();
		expect( view() ).toEqual( [120_000, 720_000] );
		push( buffer, a, 721_000, 721 );
		fixture.detectChanges();
		expect( view() ).toEqual( [121_000, 721_000] );
		fixture.componentRef.setInput( 'window', 1 );//a window changed presses from the newest too
		fixture.detectChanges();
		expect( view() ).toEqual( [661_000, 721_000] );
		fixture.destroy();
	} );
	//left with less than the window, all of it in view:  a lone old point, which the view had passed, showed nothing (420 W2)
	it( 'shows what is left whole once it falls short of the window', async ()=>{
		const buffer = new PushBuffer( 60*60_000 ), a = row( 'local', 'rpm' ), b = row( 'plc', 'flow', null, B );
		for( let s=0; s<=720; ++s )
			push( buffer, a, s*1000, s );
		push( buffer, b, 10_000, 1 );
		const { fixture, chart } = await draw( [a, b], buffer );
		fixture.componentRef.setInput( 'rows', [b] );
		fixture.detectChanges();
		const { min, max } = chart.xAxis[0].getExtremes();
		expect( min ).toBeLessThanOrEqual( 10_000 );
		expect( max ).toBeGreaterThanOrEqual( 10_000 );
		expect( (<any>chart).rangeSelector.selected ).toBe( 4 );//All, as Highcharts presses it for data short of a button
		push( buffer, b, 40_000, 2 );//its next push is in view
		fixture.detectChanges();
		expect( chart.xAxis[0].getExtremes().max ).toBe( 40_000 );
		fixture.destroy();
	} );
	//the chart has no Load earlier:  the buffer is all there is, and the window toggle is what bounds it
	it( 'redraws to the window the buffer prunes to, and asks nothing earlier', async ()=>{
		const buffer = new PushBuffer( 10*60_000 );
		const a = row( 'local', 'rpm' );
		for( let s=0; s<=120; s+=10 )
			push( buffer, a, s*1000, s );
		const { fixture, chart, ys } = await draw( [a], buffer );
		expect( ys( a ) ).toHaveLength( 13 );
		const trend = fixture.debugElement.children.find( d=>d.name=='hist-trend' )!.componentInstance;
		expect( trend.hasEarlier() ).toBe( false );
		expect( trend.chartTitle() ).toBe( 'Live trend' );
		expect( fixture.nativeElement.querySelector( 'hist-trend' ).hasAttribute( 'title' ) ).toBe( false );//no native tooltip over the chart (420 #1)
		buffer.window = 60_000;//the page sets it from the toggle
		fixture.detectChanges();
		expect( ys( a ) ).toEqual( [50, 60, 70, 80, 90, 100, 110, 120] );
		expect( chart.get( `${a.seriesId}/flags` ) ).toBeDefined();
		fixture.destroy();
	} );
	it( 'offers the windows, and says which is picked', async ()=>{
		const emitted:number[] = [];
		const { fixture } = await draw( [row( 'local', 'rpm' )], new PushBuffer( 10*60_000 ) );
		fixture.componentInstance.window.subscribe( m=>emitted.push( m ) );
		const buttons:HTMLElement[] = [...fixture.nativeElement.querySelectorAll( '.window-group mat-button-toggle' )];
		expect( buttons.map( b=>b.textContent.trim() ) ).toEqual( ['1 min', '10 min', '1 h'] );
		expect( buttons.map( b=>b.classList.contains( 'mat-button-toggle-checked' ) ) ).toEqual( [false, true, false] );
		buttons[2].querySelector( 'button' )!.click();
		expect( emitted ).toEqual( [60] );
		fixture.destroy();
	} );
	it( 'says to tick rows while none is', async ()=>{
		const fixture = await create( [], new PushBuffer( 60_000 ) );
		await stable( fixture );
		expect( fixture.nativeElement.querySelector( '.jde-empty .title' ).textContent ).toBe( 'No row ticked.' );
		expect( fixture.nativeElement.querySelector( 'hist-trend' ) ).toBeNull();
		fixture.destroy();
	} );
} );