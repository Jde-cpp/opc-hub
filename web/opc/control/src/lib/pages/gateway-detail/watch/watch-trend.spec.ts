import { TestBed } from '@angular/core/testing';
import { provideRouter } from '@angular/router';
import type Highcharts from 'highcharts/esm/highstock';
import { PushBuffer } from '../../../model/push-buffer';
import { WatchRow } from '../../../model/watch';
import { HighchartsLoader } from '../../node-detail/node-history/highcharts-loader';
import { WatchTrend } from './watch-trend';
import { stable } from 'jde-spa/testing';
import { histValue, nodeA as A, nodeB as B, variable } from 'jde-opc/testing';

//The add-on the installed site serves by URL, here the package itself:  bundled into the test alone (hist-trend.spec)
class BundledHighcharts extends HighchartsLoader{
	protected override importFile( file:string ){ return file=='highstock.js' ? import('highcharts/esm/highstock') : import('highcharts/esm/modules/accessibility'); }
}
//a row of `opc` on `node`, described as `name` at `path`
const row = ( opc:string, name:string, path:string|null=null, node=A )=>{ const r = new WatchRow( opc, node, {} ); r.describe( variable( 1, name ), path ); return r; };
//a push of the row, as the page buffers it:  under the row's own series
const push = ( buffer:PushBuffer, r:WatchRow, ms:number, value:number )=>buffer.push( r.key, {...histValue( r.node, ms, value ), series: r.trendKey} );
//the tab over `rows` and `buffer`, once Highcharts has loaded and made the chart
const draw = async ( rows:WatchRow[], buffer:PushBuffer )=>{
	TestBed.configureTestingModule({ providers: [provideRouter( [] ), { provide: HighchartsLoader, useValue: new BundledHighcharts() }] });
	const fixture = TestBed.createComponent( WatchTrend );
	fixture.componentRef.setInput( 'rows', rows );
	fixture.componentRef.setInput( 'buffer', buffer );
	fixture.componentRef.setInput( 'names', ( slug:string )=>slug.toUpperCase() );
	fixture.componentRef.setInput( 'window', 10 );
	const H = <typeof Highcharts>(await import('highcharts/esm/highstock')).default;
	await import( 'highcharts/esm/modules/accessibility' );//loaded here, so the loader's imports are a known number of turns
	await stable( fixture );
	const plot:HTMLElement = fixture.nativeElement.querySelector( '.plot' );
	const chart = H.charts.find( c=>c && c.container.parentElement==plot );
	expect( chart ).toBeDefined();
	const ys = ( r:WatchRow )=>(<any[]>(<Highcharts.SeriesLineOptions>(<Highcharts.Series>chart!.get( r.key )).options).data).map( p=>p.y );
	const lines = ()=>chart!.series.filter( s=>s.options.type=='line' && !s.options.id?.startsWith( 'highcharts-' ) ).map( s=>s.name );
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
		expect( (<Highcharts.Series>chart.get( `${b.key}/flags` )).name ).toBe( 'rpm status' );
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
		expect( trend.title() ).toBe( 'Live trend' );
		buffer.window = 60_000;//the page sets it from the toggle
		fixture.detectChanges();
		expect( ys( a ) ).toEqual( [50, 60, 70, 80, 90, 100, 110, 120] );
		expect( chart.get( `${a.key}/flags` ) ).toBeDefined();
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
		TestBed.configureTestingModule({ providers: [provideRouter( [] ), { provide: HighchartsLoader, useValue: new BundledHighcharts() }] });
		const fixture = TestBed.createComponent( WatchTrend );
		fixture.componentRef.setInput( 'rows', [] );
		fixture.componentRef.setInput( 'buffer', new PushBuffer( 60_000 ) );
		fixture.componentRef.setInput( 'names', ( slug:string )=>slug );
		fixture.componentRef.setInput( 'window', 1 );
		await stable( fixture );
		expect( fixture.nativeElement.querySelector( '.jde-empty .title' ).textContent ).toBe( 'No row ticked.' );
		expect( fixture.nativeElement.querySelector( 'hist-trend' ) ).toBeNull();
		fixture.destroy();
	} );
} );