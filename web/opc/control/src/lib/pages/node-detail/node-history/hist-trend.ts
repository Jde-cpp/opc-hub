import { AfterViewInit, Component, computed, effect, ElementRef, inject, input, OnDestroy, output, untracked, viewChild } from '@angular/core';
import type Highcharts from 'highcharts/esm/highstock';
import { HistValue } from '../../../model/hist';
import { Variable } from '../../../model/node';
import { OpcError } from '../../../model/opc-error';
import { badColor, toTrendPoints, TrendFlag, trendPalette, TrendPoint } from './hist-trend-data';

type HC = typeof Highcharts;
let highcharts:Promise<HC>|undefined;
//Highcharts Stock and its accessibility module, loaded on first use and once:  the libraries are not lazy (web/CLAUDE.md), so a
//static import would put the whole of Highcharts in the initial bundle for every page.
function loadHighcharts():Promise<HC>{
	return highcharts ??= Promise.all( [import('highcharts/esm/highstock'), import('highcharts/esm/modules/accessibility')] ).then( ([m])=>m.default );
}

//The trend (plan Phase 4):  one line per node, stepped by default, a gap where a reading is Bad and a flag naming its status, a
//triangle on an Uncertain one.  A time axis that pans into more pages:  reaching the left edge asks the owner for the page before.
//The chart is Highcharts' own object, driven directly:  points arrive one a second per node on the live tail, which is addPoint
//work, not a rebuild of an options object.
@Component({
	selector: 'hist-trend',
	template: `<div #plot class="plot" role="img" [attr.aria-label]="ariaLabel()"></div>`,
	styles: [`:host{ display:block; width:100%; } .plot{ width:100%; height:360px; }`]
})
export class HistTrend implements AfterViewInit, OnDestroy{
	constructor(){
		effect( ()=>{
			const values = this.values(), series = this.series(), stepped = this.stepped();
			this.namesVersion();//a status name arriving re-renders the flags' text
			untracked( ()=>this.#draw(values, series, stepped) );
		} );
	}
	async ngAfterViewInit(){
		const H = await loadHighcharts();
		if( this.#destroyed )
			return;
		this.#chart = H.stockChart( this.plot().nativeElement, this.#options() );
		this.#draw( this.values(), this.series(), this.stepped() );
	}
	ngOnDestroy(){
		this.#destroyed = true;
		this.#chart?.destroy();
		this.#chart = undefined;
	}

	values = input.required<HistValue[]>();//in source-time order - NodeHistory merges them so
	series = input.required<Variable[]>();//the nodes drawn, in palette order
	stepped = input( true );
	loading = input( false );
	hasEarlier = input( true );
	namesVersion = input( 0 );
	loadEarlier = output<void>();
	plot = viewChild.required<ElementRef<HTMLDivElement>>( 'plot' );
	ariaLabel = computed( ()=>`History trend of ${this.series().map(s=>s.name).join(", ") || "no nodes"}` );

	#draw( values:HistValue[], series:Variable[], stepped:boolean ){
		const chart = this.#chart;
		if( !chart )
			return;
		const keep = new Set<string>();
		const step = stepped ? 'left' : undefined;
		series.forEach( (node, i)=>{
			const id = node.nodeId.uaString(), flagsId = `${id}/flags`;//Highcharts ids are strings;  NodeId.key is a Symbol
			keep.add( id ); keep.add( flagsId );
			const {points, flags} = toTrendPoints( values, node.nodeId, stepped );
			const color = trendPalette[Math.min( i, trendPalette.length-1 )];
			const line = <Highcharts.Series|undefined>chart.get( id );
			if( !line )
				chart.addSeries( {type: 'line', id, name: node.name, color, step, data: points}, false );
			else{
				const options = <Highcharts.SeriesLineOptions>line.options;
				if( options.step!=step || options.color!=color )
					line.update( {type: 'line', step, color}, false );
				HistTrend.setData( line, points );
			}
			const flagSeries = <Highcharts.Series|undefined>chart.get( flagsId );
			if( !flagSeries )
				chart.addSeries( {type: 'flags', id: flagsId, name: `${node.name} status`, onSeries: id, linkedTo: id, color: badColor, fillColor: badColor, style: {color: '#ffffff'}, shape: 'circlepin', width: 14, y: -28, data: flags, showInLegend: false}, false );
			else
				HistTrend.setData( flagSeries, flags );
		} );
		for( const s of [...chart.series] ){//a node unticked takes its series with it;  the navigator's own series have highcharts- ids
			const id = s.options.id;
			if( id && !id.startsWith('highcharts-') && !keep.has(id) )
				s.remove( false );
		}
		chart.update( {legend: {enabled: series.length>1}}, false );
		chart.redraw( false );
	}
	//The live tail appends:  a point a second per node, each a setData of every point held is what a chart stutters on, so a tail
	//that only grew is added point by point.  Anything else - a page loaded before the first value, a late push merged into the
	//middle, a line shape change - is a rebuild.
	private static setData( s:Highcharts.Series, data:(TrendPoint|TrendFlag)[] ){
		const prev = <(TrendPoint|TrendFlag)[]|undefined>(<Highcharts.SeriesLineOptions>s.options).data;
		const same = ( a:TrendPoint|TrendFlag, b:TrendPoint|TrendFlag )=>a.x===b.x && (<TrendPoint>a).y===(<TrendPoint>b).y && a.custom?.status===b.custom?.status;
		if( prev?.length && data.length>prev.length && same(prev[0], data[0]) && same(prev[prev.length-1], data[prev.length-1]) ){
			for( let i=prev.length; i<data.length; ++i )
				s.addPoint( data[i], false );
		}
		else if( !prev || prev.length!=data.length || !prev.every((p,i)=>same(p, data[i])) )
			s.setData( data, false, false, false );
	}
	//the user reached the left edge of what is loaded:  ask for the page before.  Only a user's move (e.trigger) - a redraw after a
	//load sets the extremes too, and would ask again on its own.
	#onAfterSetExtremes( e:Highcharts.AxisSetExtremesEventObject ){
		const axis = this.#chart?.xAxis[0];
		if( !axis || !e.trigger || this.loading() || !this.hasEarlier() )
			return;
		const dataMin = axis.getExtremes().dataMin;
		if( dataMin!=null && e.min<=dataMin+0.02*(e.max-e.min) )
			this.loadEarlier.emit();
	}
	//Highcharts' options from the Material tokens the page is drawn in:  text in the text tokens, grid and axes recessive, no
	//surface of its own.  Stock's ordinal axis is off:  it closes the gaps between points, and a trend's gaps are the point.
	#options():Highcharts.Options{
		const css = getComputedStyle( this.#host.nativeElement );
		const token = ( name:string, fallback:string )=>css.getPropertyValue( name ).trim() || fallback;
		const text = token( '--mat-sys-on-surface', '#1b1b1f' ), muted = token( '--mat-sys-on-surface-variant', '#44474f' );
		const grid = token( '--mat-sys-outline-variant', '#c4c6d0' ), surface = token( '--mat-sys-surface-container-low', '#f3f4f9' );
		const self = this;
		return {
			chart: { backgroundColor: 'transparent', style: {fontFamily: 'inherit'}, animation: false, zooming: {type: 'x'}, panning: {enabled: true, type: 'x'}, panKey: 'shift', spacing: [8, 8, 8, 8] },
			accessibility: { description: 'Historical values of the selected nodes over time.  A Bad reading is a gap in its line, flagged with its status;  an Uncertain one keeps its value under a triangle.' },
			title: { text: undefined },
			credits: { style: {color: muted} },
			time: { timezone: Intl.DateTimeFormat().resolvedOptions().timeZone },
			rangeSelector: {
				buttons: [{type: 'minute', count: 1, text: '1m'}, {type: 'minute', count: 10, text: '10m'}, {type: 'hour', count: 1, text: '1h'}, {type: 'day', count: 1, text: '1d'}, {type: 'all', text: 'All'}],
				selected: 4, inputEnabled: false, labelStyle: {color: muted},
				buttonTheme: { fill: 'transparent', stroke: grid, 'stroke-width': 1, style: {color: text}, states: {hover: {fill: surface}, select: {fill: surface, style: {color: text, fontWeight: 'bold'}}} }
			},
			navigator: { height: 30, margin: 12, outlineColor: grid, maskFill: 'rgba(42,120,214,0.12)', handles: {backgroundColor: surface, borderColor: muted}, xAxis: {gridLineColor: grid, labels: {style: {color: muted}}} },
			scrollbar: { enabled: false },
			legend: { enabled: false, itemStyle: {color: text}, itemHoverStyle: {color: text}, itemHiddenStyle: {color: muted} },
			xAxis: { type: 'datetime', ordinal: false, gridLineWidth: 1, gridLineColor: grid, lineColor: grid, tickColor: grid, labels: {style: {color: muted}}, crosshair: {color: muted, dashStyle: 'Dot'}, events: {afterSetExtremes: e=>self.#onAfterSetExtremes( e )} },
			yAxis: { opposite: false, gridLineColor: grid, labels: {style: {color: muted}, align: 'right', x: -4}, title: {text: null}, showLastLabel: true },
			tooltip: {
				backgroundColor: surface, borderColor: grid, style: {color: text}, xDateFormat: '%Y-%m-%d %H:%M:%S.%L', valueDecimals: 3, shared: false, split: false,
				pointFormatter(){
					const custom = <TrendPoint["custom"]|undefined>this.options.custom;
					const status = custom?.status ? ` (${OpcError.text( custom.status )})` : '';
					const held = custom?.hold ? ', held' : '';
					return `<span style="color:${this.color}">●</span> ${this.series.name}: <b>${this.y}</b>${status}${held}<br/>`;
				}
			},
			plotOptions: {
				series: { animation: false, dataGrouping: {enabled: false}, turboThreshold: 0, lineWidth: 2, connectNulls: false, marker: {enabled: false, radius: 4, states: {hover: {enabled: true}}}, states: {hover: {lineWidthPlus: 0}}, showInNavigator: true },
				flags: { tooltip: {pointFormat: '{point.text}'}, style: {fontWeight: 'bold'} }
			},
			series: []
		};
	}
	#host = inject<ElementRef<HTMLElement>>( ElementRef );
	#chart:Highcharts.Chart|undefined;
	#destroyed = false;
}