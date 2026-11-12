import { afterRenderEffect, Component, ElementRef, inject, input, OnDestroy, OnInit, output, signal, untracked, viewChild } from '@angular/core';
import type Highcharts from 'highcharts/esm/highstock';
import { OeeSegment, OeeSpan } from '../../../model/oee';
import { HC, HighchartsLoader } from '../../node-detail/node-history/highcharts-loader';
import { codable, ColumnPoint, GanttPoint, OeeLegendItem } from './oee-chart-data';

//What a chart of the machine's page draws:  the Gantt chart's two rows over the period, or a row of columns - the Pareto's
//minutes, the trend's percent.
export type OeeChartSpec = { title:string; description:string; legend:OeeLegendItem[] } & (
	{ kind:'gantt'; range:OeeSpan; planned:GanttPoint[]; state:GanttPoint[] }
	| { kind:'columns'; categories:string[]; points:ColumnPoint[]; name:string; axis:string; percent:boolean } );

//A chart of the machine's OEE page (#405), through the add-on the History trend loads (HighchartsLoader, #302):  the Gantt
//chart an `xrange` chart, a module of the same folder, not the Gantt product, which is licensed on its own.  Drawn once the
//add-on has loaded;  without it nothing stands here, and the page says so once for its three.  The legend is the template's,
//under the plot:  a band's colour is by what it is, and the columns are one series.
@Component({
	selector: 'oee-chart',
	template: `
		@if( highcharts() ){
			<div #plot class="plot" [class.gantt]="spec().kind=='gantt'"></div>
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
				}
				else
					this.#chart.update( this.#options( spec ), true, true, false );
			} );
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
	pick = output<OeeSegment>();//a stop's band clicked, or entered from the keyboard
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
			return { ...shared,
				chart: { type: 'xrange', backgroundColor: 'transparent', style: {fontFamily: 'inherit'}, animation: false, zooming: {type: 'x'}, spacing: [8, 8, 8, 8] },
				xAxis: { ...axis, type: 'datetime', min: spec.range.start.getTime(), max: spec.range.end.getTime(), gridLineWidth: 1, tickPixelInterval: 140 },
				yAxis: { ...axis, categories: ['Planned', 'State'], reversed: true, title: {text: null}, gridLineWidth: 0 },
				plotOptions: { series: {animation: false, states: {inactive: {opacity: 1}}} },
				series: [
					{ ...row, id: 'planned', name: 'Planned time', data: spec.planned },
					//the smallest band is three pixels, so a small stop is seen at all;  a stop's band opens its coding
					{ ...row, id: 'state', name: 'State', data: spec.state, cursor: 'pointer', point: {events: {click(){ const s = <OeeSegment|undefined>(<any>this.options).custom?.segment; if( s && codable( s ) ) self.pick.emit( s ); }}} }
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
}