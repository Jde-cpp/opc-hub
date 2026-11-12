import { TestBed } from '@angular/core/testing';
import type Highcharts from 'highcharts/esm/highstock';
import { toOeeLoss, toOeeTimeline } from '../../../model/oee';
import { HighchartsLoader } from '../../node-detail/node-history/highcharts-loader';
import { OeeChart, OeeChartSpec } from './oee-chart';
import { oeeKindColors, toGantt, toPareto } from './oee-chart-data';
import { silence, stable } from 'jde-spa/testing';
import { BundledHighcharts } from 'jde-opc/testing';

const t = ( h:number, m=0 )=>new Date( Date.UTC( 2026, 10, 11, h, m ) );
const range = { start: t( 6 ), end: t( 14 ) };
const timeline = toOeeTimeline( [{
	planned: [{shift: 1, name: 'First', start: t( 6 ).toISOString(), end: t( 14 ).toISOString(), breaks: []}],
	segments: [
		{start: t( 6 ).toISOString(), end: t( 8 ).toISOString(), state: 'running', small: false, uncoded: false},
		{start: t( 8 ).toISOString(), end: t( 8, 30 ).toISOString(), state: 'stopped', stop: 7, small: false, uncoded: true},
		{start: t( 8, 30 ).toISOString(), end: t( 14 ).toISOString(), state: 'running', small: false, uncoded: false}] }] );
const gantt:OeeChartSpec = { kind: 'gantt', title: 'Timeline of Pump 1', description: 'd', range, ...toGantt( timeline, range ) };
const pareto:OeeChartSpec = { kind: 'columns', title: 'Losses of Pump 1', description: 'd', name: 'Lost time', axis: 'Minutes', percent: false, ...toPareto( [toOeeLoss( {kind: 'uncoded', ms: 1800000, stops: 1} )] ) };
//the chart drawn with the package's own Highcharts, once it has loaded
const draw = async ( spec:OeeChartSpec, loader:HighchartsLoader = new BundledHighcharts() )=>{
	TestBed.configureTestingModule({ providers: [{ provide: HighchartsLoader, useValue: loader }] });
	const fixture = TestBed.createComponent( OeeChart );
	fixture.componentRef.setInput( 'spec', spec );
	const H = <typeof Highcharts>(await import('highcharts/esm/highstock')).default;
	await import( 'highcharts/esm/modules/accessibility' );
	await import( 'highcharts/esm/modules/xrange' );//loaded here, so the loader's imports are a known number of turns
	await stable( fixture );
	const plot:HTMLElement = fixture.nativeElement.querySelector( '.plot' );
	const chart = H.charts.find( c=>c && c.container.parentElement==plot );
	const picked:unknown[] = [];
	fixture.componentInstance.pick.subscribe( s=>picked.push( s ) );
	return { fixture, plot, chart, picked };
};

describe( 'OeeChart', ()=>{
	afterEach( ()=>{ vi.unstubAllGlobals(); vi.restoreAllMocks(); } );
	it( 'draws the Gantt chart as two xrange rows over the period, with its legend', async ()=>{
		const { fixture, chart } = await draw( gantt );
		expect( chart ).toBeDefined();
		expect( chart!.series.map( s=>[s.options.id, s.type, s.points.length] ) ).toEqual( [['planned', 'xrange', 1], ['state', 'xrange', 3]] );
		expect( (<any>chart!.series[1].options).minPointLength ).toBe( 3 );//a small stop is seen at all
		expect( [chart!.xAxis[0].options.min, chart!.xAxis[0].options.max] ).toEqual( [range.start.getTime(), range.end.getTime()] );
		expect( chart!.series[1].points[1].color ).toBe( oeeKindColors.uncoded );
		expect( [...fixture.nativeElement.querySelectorAll( '.legend li' )].map( ( li:any )=>li.textContent.trim() ) ).toEqual( ['Planned time', 'Running', 'Uncoded stop'] );
		fixture.destroy();
	} );
	it( "emits a stop's segment when its band is clicked, and nothing for running time", async ()=>{
		const { fixture, chart, picked } = await draw( gantt );
		const [running, stop] = chart!.series[1].points;
		(<any>running).firePointEvent( 'click' );
		expect( picked ).toEqual( [] );
		(<any>stop).firePointEvent( 'click' );
		expect( picked ).toEqual( [timeline.segments[1]] );
		fixture.destroy();
	} );
	it( 'redraws in place when the data changes, and anew when the kind does', async ()=>{
		const { fixture, chart, plot } = await draw( gantt );
		const later = { start: t( 7 ), end: t( 14 ) };
		fixture.componentRef.setInput( 'spec', {...gantt, range: later, ...toGantt( {planned: [], segments: timeline.segments.slice( 1 )}, later )} );
		await stable( fixture );
		expect( chart!.series.map( s=>s.points.length ) ).toEqual( [0, 2] );
		expect( chart!.xAxis[0].options.min ).toBe( later.start.getTime() );
		fixture.componentRef.setInput( 'spec', pareto );
		await stable( fixture );
		const H = <typeof Highcharts>(await import('highcharts/esm/highstock')).default;
		const columns = H.charts.find( c=>c && c.container.parentElement==plot );
		expect( columns ).not.toBe( chart );
		expect( columns!.series.map( s=>[s.type, s.points.map( p=>p.y )] ) ).toEqual( [['column', [30]]] );
		fixture.destroy();
	} );
	it( 'draws nothing without the add-on, and says so where the xrange module alone failed', async ()=>{
		const absent = { state: ()=>'absent', load: async ()=>undefined, xrange: async ()=>undefined };
		const none = await draw( gantt, <any>absent );
		expect( none.plot ).toBeNull();
		expect( none.fixture.nativeElement.textContent.trim() ).toBe( '' );
		TestBed.resetTestingModule();//a second chart in the one test
		silence();
		const failed = await draw( gantt, <any>{ state: ()=>'loaded', load: async ()=>({}), xrange: async ()=>{ throw new TypeError( 'Failed to fetch' ); } } );
		expect( failed.plot ).toBeNull();
		expect( failed.fixture.nativeElement.textContent ).toContain( 'modules/xrange.js' );
	} );
} );