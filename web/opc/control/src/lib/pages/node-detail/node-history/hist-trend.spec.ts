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
import type Highcharts from 'highcharts/esm/highstock';
import { HistValue } from '../../../model/hist';
import { Variable } from '../../../model/node';
import { OpcError } from '../../../model/opc-error';
import { HistTrend } from './hist-trend';

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
	const plot:HTMLElement = fixture.nativeElement.querySelector( '.plot' );
	let chart:Highcharts.Chart|undefined;
	await vi.waitFor( ()=>{ chart = H.charts.find( c=>c?.container.parentElement==plot ); expect( chart ).toBeDefined(); } );
	return { A, B, fixture, plot, chart: chart! };
};

describe( 'HistTrend', ()=>{
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
} );