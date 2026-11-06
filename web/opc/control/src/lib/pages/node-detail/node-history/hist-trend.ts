import { afterRenderEffect, Component, effect, ElementRef, inject, input, OnDestroy, output, untracked, viewChild } from '@angular/core';
import { MatIconModule } from '@angular/material/icon';
import { RouterLink } from '@angular/router';
import type Highcharts from 'highcharts/esm/highstock';
import { HistValue } from '../../../model/hist';
import { NodeKey } from '../../../model/node-id';
import { OpcError } from '../../../model/opc-error';
import { statusIcon } from '../../../model/status-code';
import { badColor, toTrendPoints, TrendFlag, trendKey, trendPalette, TrendPoint, TrendSeries } from './hist-trend-data';
import { HighchartsLoader } from './highcharts-loader';

//The trend:  one line per series - the History tab's nodes, the watch list's rows (#420) - stepped by default, a gap where a reading
//is Bad and a flag naming its status, a triangle on an Uncertain one.  A time axis that pans into more pages:  reaching the left
//edge asks the owner for the page before, where it has one.
//The chart is Highcharts' own object, driven directly:  points arrive one a second per node on the live tail, which is addPoint
//work, not a rebuild of an options object.  Highcharts itself is an add-on the install never carries, imported by URL on first use
//(HighchartsLoader, #302):  the plot is drawn once it has loaded, and until the load settles nothing stands in its place.  Absent,
//or failed to load, a notice does, and the tab works from the table.
@Component({
	selector: 'hist-trend',
	imports: [MatIconModule, RouterLink],
	template: `
		@if( state()=='loaded' ){
			<div #plot class="plot"></div>
		}
		@else if( state()=='absent' ){
			<p class="absent" role="status"><mat-icon aria-hidden="true" fontIcon="show_chart"></mat-icon>The trend draws with Highcharts Stock, which the install does not include.  <a [routerLink]="['/help/gateways']" fragment="trend-chart">How to add it</a></p>
		}
		@else if( state()=='failed' ){
			<p class="absent" role="status"><mat-icon aria-hidden="true" fontIcon="show_chart"></mat-icon>The trend could not load Highcharts Stock.  Reload the page to try again.  <a [routerLink]="['/help/gateways']" fragment="trend-chart">About the add-on</a></p>
		}`,//the plot's role and label are the accessibility module's:  it sets them on every update
	styles: [`
		:host{ display:block; width:100%; }
		.plot{ width:100%; height:360px; }
		p.absent{ display:flex; align-items:center; gap:4px; margin:.5rem 1rem; color:var(--mat-sys-on-surface-variant); } p.absent mat-icon{ flex:none; }
	`]
})
export class HistTrend implements OnDestroy{
	constructor(){
		effect( ()=>{
			const values = this.values(), series = this.series(), stepped = this.stepped();
			OpcError.namesVersion();//a status name arriving re-renders the flags' text, which #draw reads untracked
			untracked( ()=>this.#draw(values, series, stepped) );
		} );
		//the range button pressed for the chart, once it stands:  the watch list's trend moves it with its window (#420), and holds it again
		effect( ()=>{
			const range = this.range();
			untracked( ()=>{ this.#following = this.follow(); this.#press( range ); } );
		} );
		//the chart, in the plot once it is laid out:  Highcharts sizes itself from it
		afterRenderEffect( ()=>{
			const plot = this.plot()?.nativeElement;
			if( plot && !this.#chart )
				untracked( ()=>{ this.#chart = this.#loader.highcharts!.stockChart( plot, this.#options() ); this.#draw( this.values(), this.series(), this.stepped() ); } );
		} );
		this.#loader.load().catch( ()=>{} );//how it settled is the loader's state, and it logged why
	}
	ngOnDestroy(){
		this.#chart?.destroy();
		this.#chart = undefined;
	}

	values = input.required<HistValue[]>();//each series' in source-time order - NodeHistory merges them so, and the watch list's buffer keeps each row's so
	series = input.required<TrendSeries[]>();//the lines drawn, in palette order
	stepped = input( true );
	loading = input( false );
	hasEarlier = input( true );
	range = input( 4 );//the range selector's button pressed:  1m, 10m, 1h, 1d or All - the History tab opens on All, the watch list's trend on its window
	//The watch list's trend (420 #3):  `range` is held, not only pressed.  Highcharts drops a button wider than the data, and one under
	//five times the closest points' gap, and nothing pressed it again;  so it is pressed again once the data spans it, until the user
	//picks another view - pressing `range` itself resumes it - and the axis' minRange is a second, so a sparse row disables no button.
	follow = input( false );
	//The watch list's trend (420 #4):  each line's last value is drawn on to the newest time of any line, by a linked `<id>/hold`
	//series, so the line's own data keeps the append path.  A row reports on change:  a quiet row's line ended at its last push.  The
	//hold ends in a dot, the value now:  a row whose pushes all fall within a pixel - two on subscribe, 1 ms apart - drew nothing (420 W1).
	holdLast = input( false );
	//the chart's name to a screen reader and what it says it shows:  there is no visible title, so the accessibility module's stand-in for one.
	//Not `title`:  a static attribute of that name is written to the host as well, a native tooltip over the whole chart (420 #1).
	chartTitle = input( 'History trend' );
	description = input( 'Historical values of the selected nodes over time.  A Bad reading is a gap in its line, flagged with its status;  an Uncertain one keeps its value under a triangle;  a ring marks a heartbeat, a stored repeat of the value before it.' );
	loadEarlier = output<void>();
	plot = viewChild<ElementRef<HTMLDivElement>>( 'plot' );//once Highcharts has loaded
	state = inject( HighchartsLoader ).state;

	#draw( values:HistValue[], series:TrendSeries[], stepped:boolean ){
		const chart = this.#chart;
		if( !chart )
			return;
		const keep = new Set<string>();
		const step = stepped ? 'left' : undefined;
		const byKey = new Map<NodeKey, HistValue[]>();//one pass, not one per series
		for( const v of values ){
			const key = trendKey( v ), list = byKey.get( key );
			if( list )
				list.push( v );
			else
				byKey.set( key, [v] );
		}
		const drawn = series.map( (s, i)=>{
			const color = trendPalette[Math.min( i, trendPalette.length-1 )];
			return { s, color, ...toTrendPoints( byKey.get(s.key) ?? [], s, stepped, color ) };
		} );
		const holdTo = this.holdLast() ? Math.max( ...drawn.map( d=><number|undefined>d.points.at(-1)?.x ?? Number.NEGATIVE_INFINITY ) ) : undefined;
		drawn.forEach( ({s, color, points, flags})=>{
			const id = s.id, flagsId = `${id}/flags`;
			keep.add( id ); keep.add( flagsId );
			const line = <Highcharts.Series|undefined>chart.get( id );
			if( !line )
				chart.addSeries( {type: 'line', id, name: s.name, color, step, data: points}, false );
			else{
				const options = <Highcharts.SeriesLineOptions>line.options;
				//a rebuild:  a heartbeat's ring carries the line's colour in its own marker.  A name changes with the series' - a watch
				//row's carries its connection only while the ticked rows span several (#420), a node's a DisplayName written.
				if( options.step!=step || options.color!=color || options.name!=s.name ){
					line.update( {type: 'line', step, color, name: s.name}, false );
					line.setData( points, false, false, false );
				}
				else
					HistTrend.setData( line, points );
			}
			const flagSeries = <Highcharts.Series|undefined>chart.get( flagsId ), flagsName = `${s.name} status`;
			if( !flagSeries )
				chart.addSeries( {type: 'flags', id: flagsId, name: flagsName, onSeries: id, linkedTo: id, color: badColor, fillColor: badColor, style: {color: '#ffffff'}, shape: 'circlepin', width: 14, y: -28, data: flags, showInLegend: false}, false );
			else{
				if( flagSeries.options.name!=flagsName )
					flagSeries.update( {type: 'flags', name: flagsName}, false );
				HistTrend.setData( flagSeries, flags );
			}
			if( holdTo!==undefined ){
				const holdId = `${id}/hold`, last = points.at( -1 ), lastX = <number|undefined>last?.x;
				keep.add( holdId );
				const now = {x: holdTo, y: last?.y, marker: {enabled: true}};
				const held = !last || last.y==null ? [] : lastX!<holdTo ? [{x: lastX, y: last.y}, now] : [now];//none after a Bad:  its gap goes on
				const holdSeries = <Highcharts.Series|undefined>chart.get( holdId );
				if( !holdSeries )
					chart.addSeries( {type: 'line', id: holdId, linkedTo: id, color, data: held, marker: {enabled: false}, enableMouseTracking: false, showInNavigator: false, accessibility: {enabled: false}}, false );
				else{
					if( (<Highcharts.SeriesLineOptions>holdSeries.options).color!=color )
						holdSeries.update( {type: 'line', color}, false );
					holdSeries.setData( held, false, false, false );
				}
			}
		} );
		//a series unticked takes its line with it;  the navigator's own series have highcharts- ids.  The ids first, then the removes:
		//removing a line destroys its navigator series, later in chart.series, so a walk that removes as it goes reaches a dead one.
		const drop = chart.series.map( s=>s.options.id ).filter( (id):id is string=>!!id && !id.startsWith('highcharts-') && !keep.has(id) );
		for( const id of drop )
			(<Highcharts.Series|undefined>chart.get( id ))?.remove( false );
		const legend = series.length>1, title = this.#title( series );
		if( chart.options.legend?.enabled!==legend || chart.options.lang?.accessibility?.defaultChartTitle!==title )//not on every push (420 #5)
			chart.update( {legend: {enabled: legend}, lang: {accessibility: {defaultChartTitle: title}}}, false );
		chart.redraw( false );
		this.#keepRange();
	}
	//pressed again where it was dropped, or where the view no longer ends at the newest data - a row unticked that held it, which leaves
	//the view past what is left, and Highcharts scrolls only a view that ends at the newest (420 W2).  Data short of the window is
	//shown whole, as All, until it spans the window.
	#keepRange(){
		const selector = (<any>this.#chart)?.rangeSelector, range = this.range();
		if( !this.#following || !selector )
			return;
		const { max, dataMin, dataMax } = this.#chart!.xAxis[0].getExtremes(), width = selector.buttonOptions[range]?._range;
		if( dataMin!=null && dataMax!=null && width && (max!==dataMax || (dataMax-dataMin>=width && selector.selected!==range)) )
			this.#press( range );
	}
	//A press of ours, which #onAfterSetExtremes tells from the user's by #pressing:  the extremes are set and announced within the press.
	//Held, the press is ours, from the newest data on the chart's own axis:  Highcharts' clickButton keeps the view's right edge, and
	//reads the navigator's extremes, which a lone point's next push left short - All drew the old point alone (420 W2).  Data short of
	//the window is All, as Highcharts presses it.
	#press( range:number ){
		const chart = this.#chart, selector = (<any>chart)?.rangeSelector;
		if( !chart || !selector )
			return;
		this.#pressing = true;
		try{
			const axis = chart.xAxis[0], { dataMin, dataMax } = axis.getExtremes(), width:number|undefined = selector.buttonOptions[range]?._range;
			if( this.#following && dataMin!=null && dataMax!=null ){
				const whole = !width || dataMax-dataMin<width;
				selector.setSelected( whole ? selector.buttonOptions.findIndex( (b:{type:string})=>b.type=='all' ) : range );
				(<any>chart).setFixedRange( whole ? undefined : width );
				axis.setExtremes( whole ? dataMin : dataMax-width!, dataMax, true, false );
			}
			else
				selector.clickButton( range, true );
		}
		finally{
			this.#pressing = false;
		}
	}
	#title( series:TrendSeries[] ):string{ return `${this.chartTitle()} of ${series.map( s=>s.name ).join( ", " ) || "no nodes"}`; }
	//The live tail appends:  a point a second per node, each a setData of every point held is what a chart stutters on, so a tail
	//that only grew is added point by point.  Anything else - a page loaded before the first value, a late push merged into the
	//middle, a line shape change - is a rebuild.
	private static setData( s:Highcharts.Series, data:(TrendPoint|TrendFlag)[] ){
		const prev = <(TrendPoint|TrendFlag)[]|undefined>(<Highcharts.SeriesLineOptions>s.options).data;
		const same = ( a:TrendPoint|TrendFlag, b:TrendPoint|TrendFlag )=>a.x===b.x && (<TrendPoint>a).y===(<TrendPoint>b).y && a.custom?.status===b.custom?.status
			&& (<TrendPoint>a).custom?.heartbeat===(<TrendPoint>b).custom?.heartbeat && (<TrendFlag>a).text===(<TrendFlag>b).text
			&& (<TrendPoint>a).marker?.enabled===(<TrendPoint>b).marker?.enabled;//a lone point's marker goes once a neighbour comes (420 #4)
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
		if( e.trigger && !this.#pressing && this.follow() )
			this.#following = e.trigger=='rangeSelectorButton' && (<any>this.#chart)?.rangeSelector?.buttonOptions.indexOf( (<any>e).rangeSelectorButton )===this.range();
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
			lang: { accessibility: {defaultChartTitle: this.#title( this.series() )} },
			accessibility: { description: this.description() },
			title: { text: undefined },
			credits: { style: {color: muted} },
			time: { timezone: Intl.DateTimeFormat().resolvedOptions().timeZone },
			rangeSelector: {
				buttons: [{type: 'minute', count: 1, text: '1m'}, {type: 'minute', count: 10, text: '10m'}, {type: 'hour', count: 1, text: '1h'}, {type: 'day', count: 1, text: '1d'}, {type: 'all', text: 'All'}],
				selected: this.range(), inputEnabled: false, labelStyle: {color: muted},
				buttonTheme: { fill: 'transparent', stroke: grid, 'stroke-width': 1, style: {color: text}, states: {hover: {fill: surface}, select: {fill: surface, style: {color: text, fontWeight: 'bold'}}} }
			},
			navigator: { height: 30, margin: 12, outlineColor: grid, maskFill: 'rgba(42,120,214,0.12)', handles: {backgroundColor: surface, borderColor: muted}, xAxis: {gridLineColor: grid, labels: {style: {color: muted}}} },
			scrollbar: { enabled: false },
			legend: { enabled: false, itemStyle: {color: text}, itemHoverStyle: {color: text}, itemHiddenStyle: {color: muted} },
			xAxis: { type: 'datetime', ordinal: false, ...(this.follow() ? {minRange: 1000} : {}), gridLineWidth: 1, gridLineColor: grid, lineColor: grid, tickColor: grid, labels: {style: {color: muted}}, crosshair: {color: muted, dashStyle: 'Dot'}, events: {afterSetExtremes: e=>self.#onAfterSetExtremes( e )} },
			yAxis: { opposite: false, gridLineColor: grid, labels: {style: {color: muted}, align: 'right', x: -4}, title: {text: null}, showLastLabel: true },
			tooltip: {
				backgroundColor: surface, borderColor: grid, style: {color: text}, xDateFormat: '%Y-%m-%d %H:%M:%S.%L', shared: false, split: false,
				pointFormatter(){
					const custom = <TrendPoint["custom"]|undefined>this.options.custom;
					const status = statusIcon( custom?.status ) ? ` (${OpcError.text( custom!.status )})` : '';//the table's rule:  none for a Good with nothing to say, an aggregate's Calculated one too
					const held = custom?.hold ? ', held' : '';
					const heartbeat = custom?.heartbeat ? ', heartbeat' : '';
					return `<span style="color:${this.color}">●</span> ${this.series.name}: <b>${this.y}</b>${status}${held}${heartbeat}<br/>`;
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
	#loader = inject( HighchartsLoader );
	#chart:Highcharts.Chart|undefined;
	#following = false;//`range` is held (follow), until the user picks another view
	#pressing = false;
}