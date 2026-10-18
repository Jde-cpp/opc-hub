if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { TestBed } from '@angular/core/testing';
import { provideRouter } from '@angular/router';
import type Highcharts from 'highcharts/esm/highstock';
import { HistValue } from '../../../model/hist';
import { Variable } from '../../../model/node';
import { OpcError } from '../../../model/opc-error';
import { HistTrend } from './hist-trend';
import { TrendPoint, trendPalette } from './hist-trend-data';
import { HighchartsLoader } from './highcharts-loader';

//The add-on the installed site serves by URL, here the package itself:  bundled into the test alone
class BundledHighcharts extends HighchartsLoader{
	protected override importFile( file:string ){ return file=='highstock.js' ? import('highcharts/esm/highstock') : import('highcharts/esm/modules/accessibility'); }
}
//No add-on to import:  the answer to the fetch the loader makes then, stubbed, says why
class UnloadedHighcharts extends HighchartsLoader{
	protected override importFile():Promise<any>{ return Promise.reject( new TypeError( 'Failed to fetch dynamically imported module' ) ); }
}
const variable = ( i:number, name:string )=>new Variable( <any>{ns:2, i, name, browse: {ns:2, name}} );
const at = ( v:Variable, ms:number, value:number ):HistValue=>({ node: v.nodeId, source: new Date(ms), server: null, status: 0, value, bound: false, heartbeat: false });
//A and B drawn, once Highcharts has loaded and made the chart
const draw = async ()=>{
	const A = variable( 1, 'A' ), B = variable( 2, 'B' );
	const fixture = TestBed.createComponent( HistTrend );
	fixture.componentRef.setInput( 'values', [at(A, 1000, 1), at(B, 1000, 2), at(A, 2000, 3), at(B, 2000, 4)] );
	fixture.componentRef.setInput( 'series', [A, B] );
	fixture.detectChanges();
	const H = <typeof Highcharts>(await import('highcharts/esm/highstock')).default;
	let plot!:HTMLElement, chart:Highcharts.Chart|undefined;
	await vi.waitFor( ()=>{ fixture.detectChanges(); plot = fixture.nativeElement.querySelector( '.plot' ); chart = H.charts.find( c=>c && c.container.parentElement==plot ); expect( chart ).toBeDefined(); } );
	return { A, B, fixture, plot, chart: chart! };
};
//A trend whose loader finds no add-on to import, the fetch it makes then answering `status`, once its notice shows
const unloaded = async ( status:number )=>{
	TestBed.overrideProvider( HighchartsLoader, {useValue: new UnloadedHighcharts()} );
	vi.stubGlobal( 'fetch', async ()=>({ status, ok: false, headers: {get: ()=>'text/plain'} }) );
	vi.spyOn( console, 'info' ).mockImplementation( ()=>{} );
	vi.spyOn( console, 'warn' ).mockImplementation( ()=>{} );
	const fixture = TestBed.createComponent( HistTrend );
	fixture.componentRef.setInput( 'values', [] );
	fixture.componentRef.setInput( 'series', [] );
	fixture.detectChanges();
	await vi.waitFor( ()=>{ fixture.detectChanges(); expect( fixture.nativeElement.querySelector( 'p.absent' ) ).not.toBeNull(); } );
	return fixture;
};

describe( 'HistTrend', ()=>{
	//the router is the notice's link's
	beforeEach( ()=>TestBed.configureTestingModule({ providers: [provideRouter( [] ), { provide: HighchartsLoader, useClass: BundledHighcharts }] }) );
	afterEach( ()=>{ vi.unstubAllGlobals(); vi.restoreAllMocks(); } );
	//Removing a line destroys its navigator series, which Highcharts keeps last in chart.series:  a walk that removed as it
	//went read the destroyed series' options and threw, before the legend update and the redraw (historian-web-trend #1).
	it( 'drops an unticked node with its series', async ()=>{
		const { A, B, fixture, chart } = await draw();
		const ids = ()=>chart!.series.map( s=>s.options.id ).filter( id=>!id?.startsWith('highcharts-') );//the navigator's own are highcharts-
		const a = A.nodeId.uaString(), b = B.nodeId.uaString();
		expect( ids() ).toEqual( [a, `${a}/flags`, b, `${b}/flags`] );
		const ys = ( id:string )=>(<any[]>(<Highcharts.SeriesLineOptions>(<Highcharts.Series>chart!.get( id )).options).data).map( p=>p.y );
		expect( ys(a) ).toEqual( [1, 3] );//each line its own node's values
		expect( ys(b) ).toEqual( [2, 4] );
		expect( chart!.options.legend?.enabled ).toBe( true );

		fixture.componentRef.setInput( 'series', [A] );
		fixture.detectChanges();
		expect( ids() ).toEqual( [a, `${a}/flags`] );
		expect( chart!.options.legend?.enabled ).toBe( false );
		fixture.destroy();
	} );
	//A heartbeat's ring is drawn in its line's colour, which is the node's palette slot:  unticking the node before it moved the
	//line to the next colour while setData, finding the points' x, y and status unchanged, left the rings in the old one
	//(historian-group-history-web #2).
	const beat = ( v:Variable, ms:number, value:number )=>({...at( v, ms, value ), heartbeat: true});
	const rings = ( chart:Highcharts.Chart, v:Variable )=>(<TrendPoint[]>(<Highcharts.SeriesLineOptions>(<Highcharts.Series>chart.get( v.nodeId.uaString() )).options).data).map( p=>p.marker?.lineColor );
	it( "redraws a heartbeat's ring when its line changes colour", async ()=>{
		const { A, B, fixture, chart } = await draw();
		fixture.componentRef.setInput( 'values', [at(A, 1000, 1), at(B, 1000, 2), at(A, 2000, 3), beat(B, 2000, 2)] );
		fixture.detectChanges();
		expect( rings(chart, B) ).toEqual( [undefined, trendPalette[1]] );

		fixture.componentRef.setInput( 'series', [B] );
		fixture.detectChanges();
		expect( (<Highcharts.SeriesLineOptions>(<Highcharts.Series>chart.get( B.nodeId.uaString() )).options).color ).toBe( trendPalette[0] );
		expect( rings(chart, B) ).toEqual( [undefined, trendPalette[0]] );
		fixture.destroy();
	} );
	//The same check ignored the heartbeat flag, so a point whose flag alone changed kept or lacked its ring (#2 too).
	it( "draws or drops a heartbeat's ring when its flag alone changes", async ()=>{
		const { A, B, fixture, chart } = await draw();
		fixture.componentRef.setInput( 'values', [at(A, 1000, 1), at(B, 1000, 2), at(A, 2000, 3), beat(B, 2000, 4)] );
		fixture.detectChanges();
		expect( rings(chart, B) ).toEqual( [undefined, trendPalette[1]] );

		fixture.componentRef.setInput( 'values', [at(A, 1000, 1), at(B, 1000, 2), at(A, 2000, 3), at(B, 2000, 4)] );
		fixture.detectChanges();
		expect( rings(chart, B) ).toEqual( [undefined, undefined] );
		fixture.destroy();
	} );
	//A flag kept the text it was drawn with:  setData compared a flag's time and status, not its text, so a status name arriving
	//later never reached it (found fixing historian-web-trend #14).
	it( 'renames a flag when its status name arrives', async ()=>{
		const { A, B, fixture, chart } = await draw();
		fixture.componentRef.setInput( 'values', [at(A, 1000, 1), at(B, 1000, 2), {...at(A, 2000, 3), status: 0x80EF0000}] );
		fixture.detectChanges();
		const flag = ()=>(<any[]>(<Highcharts.SeriesFlagsOptions>(<Highcharts.Series>chart.get( `${A.nodeId.uaString()}/flags` )).options).data)[0]?.text;
		expect( flag() ).toBe( "Bad 0x80EF0000" );
		OpcError.setMessages( [{sc: 0x80EF0000, message: "BadNamedForTheTrend"}] );
		fixture.detectChanges();
		expect( flag() ).toBe( "BadNamedForTheTrend" );
		fixture.destroy();
	} );
	//The tooltip hid a Good by its text;  it names a status where the table marks one, by the same rule (historian-aggregate-picker #13).
	it( "names a point's status in the tooltip where the table marks it", async ()=>{
		const { fixture, chart } = await draw();
		const format = <Function>chart.options.tooltip!.pointFormatter;
		const tip = ( status?:number )=><string>format.call( {options: {custom: status==undefined ? undefined : {status}}, color: '#000', series: {name: 'A'}, y: 1} );
		expect( tip() ).toBe( '<span style="color:#000">●</span> A: <b>1</b><br/>' );
		expect( tip( 0 ) ).not.toContain( '(' );
		expect( tip( 0x401 ) ).not.toContain( '(' );//Good, Calculated:  every aggregate's
		expect( tip( 0x405 ) ).toContain( ' (Good+Partial)' );
		expect( tip( 0x80EF0000 ) ).toContain( ` (${OpcError.text( 0x80EF0000 )})` );//its name, if a test before set one
		fixture.destroy();
	} );
	//The container was role="img" with an aria-label of Angular's, and the accessibility module sets a role and label of its own on
	//the same element at each update:  the role became "region" and the label flipped between "Chart. Highcharts interactive
	//chart." and Angular's (historian-web-trend #13).  The module owns them, named with the nodes.
	it( 'names the chart with its nodes through the accessibility module', async ()=>{
		const { A, fixture, plot } = await draw();
		expect( plot.getAttribute('role') ).toBe( 'region' );
		expect( plot.getAttribute('aria-label') ).toBe( 'History trend of A, B. Highcharts interactive chart.' );
		fixture.componentRef.setInput( 'series', [A] );
		fixture.detectChanges();
		expect( plot.getAttribute('aria-label') ).toBe( 'History trend of A. Highcharts interactive chart.' );
		fixture.destroy();
	} );
	//The accessibility module composes the range selector's keyboard handling only when the RangeSelector is defined as it runs
	//(highcharts-addon #3):  the loader imports it after highstock.
	it( 'gives the range selector buttons their keyboard handling', async ()=>{
		const { fixture, chart } = await draw();
		expect( typeof (<any>chart).highlightRangeSelectorButton ).toBe( 'function' );
		fixture.destroy();
	} );
	//A read again after an edit, or a new selection, empties the values while the series stay:  the axis then had no data and
	//Highcharts asked Intl for the time zone of a NaN, a RangeError thrown from its redraw.
	it( 'draws an empty chart when the values go while the series stay', async ()=>{
		const { A, fixture, chart } = await draw();
		const uncaught:unknown[] = [];
		const onError = ( e:ErrorEvent )=>{ uncaught.push( e.error ?? e.message ); e.preventDefault(); };
		window.addEventListener( 'error', onError );
		try{
			fixture.componentRef.setInput( 'values', [] );
			fixture.detectChanges();
			await new Promise( r=>setTimeout( r, 50 ) );
			expect( uncaught ).toEqual( [] );
			expect( (<Highcharts.SeriesLineOptions>(<Highcharts.Series>chart.get( A.nodeId.uaString() )).options).data ).toEqual( [] );
			fixture.componentRef.setInput( 'values', [at(A, 3000, 5)] );
			fixture.detectChanges();
			await new Promise( r=>setTimeout( r, 50 ) );
			expect( uncaught ).toEqual( [] );
			expect( (<any[]>(<Highcharts.SeriesLineOptions>(<Highcharts.Series>chart.get( A.nodeId.uaString() )).options).data).map( p=>p.y ) ).toEqual( [5] );
		}
		finally{
			window.removeEventListener( 'error', onError );
			fixture.destroy();
		}
	} );
	//The install carries no Highcharts (#302):  where the site has no add-on the loader finds none, and the trend says so in the
	//plot's place, linking the help, with no chart made.
	it( 'says so and draws nothing when Highcharts is not installed', async ()=>{
		const fixture = await unloaded( 404 );
		expect( fixture.nativeElement.querySelector( 'p.absent' ).textContent ).toContain( 'does not include' );
		expect( fixture.nativeElement.querySelector( 'p.absent a' ).getAttribute( 'href' ) ).toBe( '/help/gateways#trend-chart' );
		expect( fixture.nativeElement.querySelector( '.plot' ) ).toBeNull();
		fixture.destroy();
	} );
	//There, and failed to load - the hub restarting as the tab opened, a partial copy:  not "not included", which would send the
	//user to install what is there (highcharts-addon #4).
	it( 'says to reload when Highcharts is there and failed to load', async ()=>{
		const fixture = await unloaded( 503 );
		const notice = fixture.nativeElement.querySelector( 'p.absent' ).textContent;
		expect( notice ).toContain( 'Reload the page' );
		expect( notice ).not.toContain( 'does not include' );
		expect( fixture.nativeElement.querySelector( 'p.absent a' ).getAttribute( 'href' ) ).toBe( '/help/gateways#trend-chart' );
		expect( fixture.nativeElement.querySelector( '.plot' ) ).toBeNull();
		fixture.destroy();
	} );
	//Until the load settles there is nothing to show:  a blank plot there would collapse into the notice on a site without the
	//add-on, moving the table under it (highcharts-addon #6).
	it( 'shows neither a plot nor a notice until the load settles', async ()=>{
		TestBed.overrideProvider( HighchartsLoader, {useValue: new (class extends HighchartsLoader{ protected override importFile(){ return new Promise<any>( ()=>{} ); } })()} );
		const fixture = TestBed.createComponent( HistTrend );
		fixture.componentRef.setInput( 'values', [] );
		fixture.componentRef.setInput( 'series', [] );
		fixture.detectChanges();
		await new Promise( r=>setTimeout( r, 50 ) );
		fixture.detectChanges();
		expect( fixture.nativeElement.querySelector( '.plot' ) ).toBeNull();
		expect( fixture.nativeElement.querySelector( 'p.absent' ) ).toBeNull();
		fixture.destroy();
	} );
} );