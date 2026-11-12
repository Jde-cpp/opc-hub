import { afterRenderEffect, Component, computed, ElementRef, inject, input, OnDestroy, OnInit, output, signal, untracked, viewChild } from '@angular/core';
import type Highcharts from 'highcharts/esm/highstock';
import { OeeSpan } from '../../../model/oee';
import { HC, HighchartsLoader } from '../../node-detail/node-history/highcharts-loader';
import { ColumnPoint, escapeHtml, GanttPoint, OeeLegendItem } from './oee-chart-data';

//What a chart of the machine's page draws:  the Gantt chart's two rows over the period, or a row of columns - the Pareto's
//minutes, the trend's percent.  The dashboard's Gantt chart (#406) names its `rows`, one per machine, and a band's `y` is its
//row's;  `groups` are the rows that name a line (#409), over its machines' and with no bands.
export type OeeChartSpec = { title:string; description:string; legend:OeeLegendItem[] } & (
	{ kind:'gantt'; range:OeeSpan; planned:GanttPoint[]; state:GanttPoint[]; rows?:string[]; groups?:number[] }
	| { kind:'columns'; categories:string[]; points:ColumnPoint[]; name:string; axis:string; percent:boolean } );

//A chart of the machine's OEE page (#405), through the add-on the History trend loads (HighchartsLoader, #302):  the Gantt
//chart an `xrange` chart, a module of the same folder, not the Gantt product, which is licensed on its own.  Drawn once the
//add-on has loaded;  without it nothing stands here, and the page says so once for its three.  The legend is the template's,
//under the plot:  a band's colour is by what it is, and the columns are one series.
@Component({
	selector: 'oee-chart',
	template: `
		@if( highcharts() ){
			<div #plot class="plot" [class.gantt]="spec().kind=='gantt'" [style.height.px]="height()"></div>
			@if( spec().legend.length ){
				<ul class="legend" aria-label="Legend">
					@for( l of spec().legend; track l.label ){
						<li><span class="swatch" aria-hidden="true" [style.background]="l.color"></span>{{ l.label }}</li>
					}
				</ul>
			}
		}
		@else if( moduleFailed() ){
			<p class="absent" role="status">The Gantt chart could not load the add-on's modules/xrange.js.  Reload the page to try again.</p>
		}`,
	styles: [`
		:host{ display:block; width:100%; }
		.plot{ width:100%; height:280px; } .plot.gantt{ height:150px; }
		.legend{ display:flex; flex-wrap:wrap; gap:.25rem 1rem; margin:.25rem 0 0; padding:0 8px; list-style:none; color:var(--mat-sys-on-surface-variant); font:var(--mat-sys-body-small); }
		.legend li{ display:flex; align-items:center; gap:6px; } .swatch{ width:10px; height:10px; border-radius:2px; }
		p.absent{ margin:.5rem 1rem; color:var(--mat-sys-on-surface-variant); }
	`]
})
export class OeeChart implements OnInit, OnDestroy{
	constructor(){
		//the chart, in the plot once it is laid out, and its data whenever the spec changes
		afterRenderEffect( ()=>{
			const plot = this.plot()?.nativeElement, spec = this.spec(), H = this.highcharts();
			if( !plot || !H )
				return;
			untracked( ()=>{
				if( !this.#chart || this.#kind!=spec.kind ){
					this.#chart?.destroy();
					this.#chart = H.chart( plot, this.#options( spec ) );
					this.#kind = spec.kind;
					this.#height = this.height();
				}
				else
					this.#redraw( spec );
			} );
		} );
	}
	//The chart's data again, in place:  a zoom the user made stands.  While a band is hovered or focused its tooltip stands, and
	//the data waits for it to close - the dashboard's grows every few seconds.
	#redraw( spec:OeeChartSpec ):void{
		const chart = this.#chart!;
		if( (<any>chart).hoverPoint ){
			this.#pending = spec;
			return;
		}
		this.#pending = undefined;
		chart.update( this.#options( spec ), true, true, false );
		if( this.#height!=this.height() )
			chart.reflow();
		this.#height = this.height();
	}
	//a band's tooltip closed:  the data that waited on it, once the pointer has settled on no other band
	#left():void{
		setTimeout( ()=>{
			if( this.#chart && this.#pending )
				this.#redraw( this.#pending );
		} );
	}
	ngOnInit(){
		const gantt = this.spec().kind=='gantt';
		( gantt ? this.#loader.xrange() : this.#loader.load() ).then( H=>this.highcharts.set( H ), ()=>this.moduleFailed.set( gantt && this.#loader.state()=='loaded' ) );
	}
	ngOnDestroy(){
		this.#chart?.destroy();
		this.#chart = undefined;
	}
	spec = input.required<OeeChartSpec>();
	band = output<GanttPoint>();//any band of the state rows, clicked or entered, with its segment and its machine:  a stop's opens its coding, a dashboard's its machine
	//a row per machine is as tall as its rows;  the others' heights are the sheet's
	readonly height = computed( ()=>{ const spec = this.spec(); return spec.kind=='gantt' && spec.rows ? 56+32*Math.max( 1, spec.rows.length ) : null; } );
	plot = viewChild<ElementRef<HTMLDivElement>>( 'plot' );
	highcharts = signal<HC|undefined>( undefined );
	moduleFailed = signal( false );//the add-on is there, and its xrange module didn't load

	//Highcharts' options from the Material tokens the page is drawn in, as the trend's are.
	#options( spec:OeeChartSpec ):Highcharts.Options{
		const css = getComputedStyle( this.#host.nativeElement );
		const token = ( name:string, fallback:string )=>css.getPropertyValue( name ).trim() || fallback;
		const text = token( '--mat-sys-on-surface', '#1b1b1f' ), muted = token( '--mat-sys-on-surface-variant', '#44474f' );
		const grid = token( '--mat-sys-outline-variant', '#c4c6d0' ), surface = token( '--mat-sys-surface-container-low', '#f3f4f9' );
		const self = this;
		const shared:Highcharts.Options = {
			lang: { accessibility: {defaultChartTitle: spec.title} },
			accessibility: { description: spec.description, point: {descriptionFormatter: p=>(<any>p.options).custom?.text ?? ''} },
			title: { text: undefined },
			credits: { style: {color: muted} },
			legend: { enabled: false },
			time: { timezone: Intl.DateTimeFormat().resolvedOptions().timeZone },
			tooltip: { backgroundColor: surface, borderColor: grid, style: {color: text}, formatter(){ return (<any>this).options?.custom?.tip ?? (<any>this).point?.options?.custom?.tip ?? ''; } }
		};
		const axis = { gridLineColor: grid, lineColor: grid, tickColor: grid, labels: {style: {color: muted}} };
		if( spec.kind=='gantt' ){
			const row = { type: <const>'xrange', borderWidth: 0, borderRadius: 2, pointWidth: 24, minPointLength: 3, turboThreshold: 0, dataLabels: {enabled: false} };
			const left = { mouseOut(){ self.#left(); } };
			const groups = spec.groups ?? [];
			return { ...shared,
				chart: { type: 'xrange', backgroundColor: 'transparent', style: {fontFamily: 'inherit'}, animation: false, zooming: {type: 'x'}, spacing: [8, 8, 8, 8] },
				xAxis: { ...axis, type: 'datetime', min: spec.range.start.getTime(), max: spec.range.end.getTime(), gridLineWidth: 1, tickPixelInterval: 140 },
				yAxis: { ...axis, labels: {...axis.labels, formatter(){ return groups.includes( this.pos ) ? `<b>${escapeHtml( this.value )}</b>` : escapeHtml( this.value ); }}, categories: spec.rows ?? ['Planned', 'State'], min: 0, max: ( spec.rows?.length ?? 2 )-1, reversed: true, title: {text: null}, gridLineWidth: 0 },
				plotOptions: { series: {animation: false, states: {inactive: {opacity: 1}}} },
				series: [
					{ ...row, id: 'planned', name: 'Planned time', data: spec.planned, point: {events: left} },
					//the smallest band is three pixels, so a small stop is seen at all
					{ ...row, id: 'state', name: 'State', data: spec.state, cursor: 'pointer', point: {events: {...left, click(){ self.band.emit( <GanttPoint><unknown>this.options ); }}} }
				]
			};
		}
		const labelled = spec.points.length<=12;//a number on every column of a month by shift is noise:  the hover has it
		return { ...shared,
			chart: { type: 'column', backgroundColor: 'transparent', style: {fontFamily: 'inherit'}, animation: false, spacing: [8, 8, 8, 8] },
			xAxis: { ...axis, categories: spec.categories, crosshair: {color: surface} },
			yAxis: { ...axis, min: 0, ...(spec.percent ? {softMax: 100} : {}), title: {text: spec.axis, style: {color: muted}}, labels: {style: {color: muted}, format: spec.percent ? '{value}%' : '{value}'} },
			plotOptions: { series: {animation: false, states: {inactive: {opacity: 1}}} },
			series: [{ type: 'column', id: 'columns', name: spec.name, data: spec.points, borderWidth: 0, borderRadius: 4, maxPointWidth: 48, turboThreshold: 0,
				dataLabels: {enabled: labelled, style: {color: text, textOutline: 'none', fontWeight: 'normal'}, formatter(){ return (<any>this).options?.custom?.label ?? (<any>this).point?.options?.custom?.label ?? ''; }} }]
		};
	}
	#host = inject<ElementRef<HTMLElement>>( ElementRef );
	#loader = inject( HighchartsLoader );
	#chart:Highcharts.Chart|undefined;
	#kind:OeeChartSpec['kind']|undefined;
	#pending:OeeChartSpec|undefined;//one waiting on a tooltip
	#height:number|null = null;
}