import { DatePipe } from '@angular/common';
import { Component, computed, inject, model, OnDestroy, OnInit, Signal, signal, WritableSignal } from '@angular/core';
import { ActivatedRoute, Router, RouterLink } from '@angular/router';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { Subscription } from 'rxjs';
import { ComponentPageTitle, RouteItem } from 'jde-spa';
import { errorMessage } from 'jde-framework';
import { OeeSegment, OeeSpan } from '../../../model/oee';
import { OeeLiveEvent, OeeLiveStatus } from '../../../model/oee-live';
import { growOeeBands, oeeBetweenShifts, oeeLiveMark, oeeLiveRange, OeeMachineRef, OeeTile, oeeTile, oeeTileNext, OeeTileView, oeeTileView } from '../../../model/oee-live-view';
import { Gateway, gatewayAnswer } from '../../../services/gateway-service';
import { OeeService } from '../../../services/oee-service';
import { WatchPageData } from '../../../services/resolvers/watch-resolver';
import { HighchartsLoader } from '../../node-detail/node-history/highcharts-loader';
import { OeeChart, OeeChartSpec } from './oee-chart';
import { GanttPoint, oeeKindColors, segmentKind, toGanttRows } from './oee-chart-data';

type Machine = OeeMachineRef & { inLine:boolean };
//A machine's tile and its row of the Gantt chart.  The tile is a signal of its own, its view computed from it, so a push redraws
//its own tile alone.  The row:  what its shift covers and its bands, read once the shift is known and grown by each status from
//then on.  `stale`:  the socket dropped, so the bands are read again at the next status.  `ended`:  the gateway ended the
//subscription, so the row stands as it was.
type LiveRow = {
	machine:Machine; tile:WritableSignal<OeeTile>; view:Signal<OeeTileView & {color:string}>; between:Signal<boolean>;
	subscription?:Subscription; range?:OeeSpan; segments:OeeSegment[]; ready:boolean; stale:boolean; ended:boolean; reads:number;
};
export type OeeDashboardBand = { machine:Machine; range:OeeSpan; segments:OeeSegment[]; ended:boolean };

//OEE's dashboard (#406, docs/oee/spec.md *Web*, *The Gantt chart*), `oee` under its gateway's page:  a tile per machine in no
//line, live over Gateway.oeeSubscribe - its state and since when, the stop's reason or why it has none, and the shift's four
//figures - and under them the Gantt chart of the current shift, a row per machine the user may read, each over its own
//calendar's shift, or without one the UTC day.  A row's bands are `oeeTimeline`'s to now, read once;  from then its last band
//grows with each status and a new one opens on a state change, with no further read.  A state change is drawn at once, growth
//every `growEvery` at most.  A machine whose group the user may not read has neither tile nor row;  one whose subscription the
//gateway ends keeps its tile, saying why, and its bands as they stood, drawn while they share the live rows' time.  A tile and a
//band open the machine's OEE page, which opens on its shift.  The chart draws through the History trend's add-on;  without it
//the page is its tiles.
@Component({
	templateUrl: './oee-dashboard.html',
	styleUrls: ['./oee-dashboard.scss'],
	host: {class: 'main-content my-content oee-dashboard'},
	imports: [DatePipe, RouterLink, MatButtonModule, MatIconModule, MatProgressBarModule, OeeChart]
})
export class OeeDashboard implements OnInit, OnDestroy{
	static readonly growEvery = 5000;//ms between the chart's redraws while its bands only grow
	ngOnInit(){
		this.#route.data.subscribe( data=>this.#open( <WatchPageData>data['pageData'] ) );
		this.#loader.load().catch( ()=>{} );//how it settled is the loader's state, for the page's notice
	}
	ngOnDestroy(){ this.#close(); }
	async #open( pageData:WatchPageData ):Promise<void>{
		if( !pageData )//the resolver found no gateway, and is on its way elsewhere
			return;
		this.#close();
		const opened = this.#epoch, gateway = pageData.gateway;
		this.gateway = gateway;
		this.sideNav.set( pageData.routing );
		this.#title.title = `${gateway.name} - OEE`;
		this.loading.set( true );
		try{
			const read = await this.#service.machines( gateway );
			if( opened!=this.#epoch )
				return;
			const machines = [...read.filter( m=>!m.inLine ), ...read.filter( m=>m.inLine )];//the tiles' order, a line's machines after
			const rows = machines.map( machine=>this.#row( machine ) );
			this.#live.set( rows );
			for( const row of rows ){
				this.#rows.set( row.machine.id, row );
				row.subscription = gateway.oeeSubscribe( row.machine.id ).subscribe( {next: e=>this.#on( row, e ), error: e=>this.#ended( row, e )} );
			}
		}
		catch( e ){
			if( opened==this.#epoch )
				this.error.set( errorMessage( e ) );
		}
		finally{
			if( opened==this.#epoch )
				this.loading.set( false );
		}
	}
	#row( machine:Machine ):LiveRow{
		const tile = signal( oeeTile( machine ) );
		const view = computed( ()=>{
			const t = tile();
			return { ...oeeTileView( t ), color: oeeKindColors[t.status ? segmentKind( oeeLiveMark( t.status ) ) : 'unknown'] };
		} );
		const between = computed( ()=>{ const status = tile().status; return !!status && oeeBetweenShifts( status ); } );
		return { machine, tile, view, between, segments: [], ready: false, stale: false, ended: false, reads: 0 };
	}
	//the subscriptions end with the page, and with the gateway it showed
	#close():void{
		++this.#epoch;
		for( const row of this.#rows.values() )
			row.subscription?.unsubscribe();
		this.#rows.clear();
		clearTimeout( this.#timer );
		this.#timer = undefined;
		this.#live.set( [] );
		this.bands.set( [] );
		this.error.set( undefined );
	}

	//The tiles
	#live = signal<LiveRow[]>( [] );//every machine's, the ones in no line first:  a line's machines have a row and no tile
	readonly machines = computed( ()=>this.#live().map( r=>r.machine ) );//those the user may read
	readonly tileViews = computed( ()=>this.#live().filter( r=>!r.machine.inLine ).map( r=>({ machine: r.machine, view: r.view }) ) );
	//the machines between shifts, which the chart has no row for
	readonly between = computed( ()=>this.#live().filter( r=>r.between() ).map( r=>r.machine.name ) );
	#on( row:LiveRow, event:OeeLiveEvent ):void{
		row.tile.update( tile=>oeeTileNext( tile, event ) );
		if( event.kind=='dropped' )
			row.stale = true;//what the outage held isn't pushed:  the bands are read again
		else
			this.#band( row, event.status );
	}
	//The gateway ended the subscription, or refused it.  A group the user may not read is no machine of the page's.  A 401 is the
	//session the gateway doesn't know, not the machine's group:  the tile stays, saying so.
	#ended( row:LiveRow, e:unknown ):void{
		const answer = gatewayAnswer( e );
		if( answer?.httpStatus==403 ){
			this.#rows.delete( row.machine.id );
			this.#live.update( rows=>rows.filter( r=>r!=row ) );
		}
		else{
			row.ended = true;
			const message = answer?.httpStatus==401 ? "The gateway doesn't know your session any more:  sign in again to see this machine." : answer?.message || errorMessage( e );
			row.tile.update( tile=>oeeTileNext( tile, {kind: 'ended', message} ) );
		}
		this.#publish( true );
	}

	//The Gantt chart
	chartState = inject( HighchartsLoader ).state;
	bands = signal<OeeDashboardBand[]>( [] );//each machine's in a shift, as the chart last drew them
	readonly ganttSpec = computed<OeeChartSpec|undefined>( ()=>{
		const rows = this.bands();
		if( !rows.length )
			return undefined;
		return { kind: 'gantt', title: `The current shift on ${this.gateway.name}`, range: axisOf( rows )!, ...toGanttRows( rows.map( r=>({name: r.machine.name, segments: r.segments}) ) ),
			description: "A row per machine over its current shift:  running, each stop in its reason's category, uncoded and small stops, and unknown time.  The last band of a row grows as the machine reports.  A band opens its machine's OEE page." };
	} );
	//A status's place in its machine's row:  another shift's has the row read again from nothing, and the first after a drop has
	//it read again while it stands and grows;  else the bands grow.
	#band( row:LiveRow, status:OeeLiveStatus ):void{
		const range = oeeLiveRange( status, new Date() );
		if( !range ){
			if( row.range ){
				row.range = undefined;
				row.segments = [];
				row.ready = false;
				++row.reads;
				this.#publish( true );
			}
			return;
		}
		const moved = !row.range || row.range.start.getTime()!=range.start.getTime();
		if( moved || row.stale ){
			row.stale = false;
			if( moved ){
				row.segments = [];
				row.ready = false;
			}
			this.#read( row, range );
		}
		row.range = range;
		if( !row.ready )
			return;//the read applies the latest status when it lands
		const before = bandKey( row.segments.at( -1 ) );
		row.segments = growOeeBands( row.segments, status, new Date(), range.start );
		this.#publish( before!=bandKey( row.segments.at( -1 ) ) );
	}
	//The shift so far, from `oeeTimeline`, which only the chart uses:  none without the add-on.  A read refused leaves the row its
	//bands from the status on.
	async #read( row:LiveRow, range:OeeSpan ):Promise<void>{
		const read = ++row.reads, opened = this.#epoch, gateway = this.gateway;
		if( !await this.#loader.load().catch( ()=>undefined ) )
			return;
		let segments:OeeSegment[] = [];
		try{
			const now = new Date();
			if( range.start<now )
				segments = ( await this.#service.timeline( gateway, row.machine.id, {start: range.start, end: now<range.end ? now : range.end} ) ).segments;
		}
		catch( e ){
			console.warn( `Could not read the timeline of '${row.machine.name}':  its row has its bands from now on.`, e );
		}
		if( opened!=this.#epoch || read!=row.reads )
			return;
		const status = row.tile().status;
		row.segments = status ? growOeeBands( segments, status, new Date(), range.start ) : segments;
		row.ready = true;
		this.#publish( true );
	}
	//The rows as the chart draws them:  at once, or with the next redraw.  An ended row is drawn while its range shares the live
	//rows' time, which alone set the axis.
	#publish( now:boolean ):void{
		if( !now ){
			this.#timer ??= setTimeout( ()=>this.#publish( true ), OeeDashboard.growEvery );
			return;
		}
		clearTimeout( this.#timer );
		this.#timer = undefined;
		const drawn = [...this.#rows.values()].filter( r=>r.range && r.ready ).map( r=>({ machine: r.machine, range: r.range!, segments: r.segments, ended: r.ended }) );
		const axis = axisOf( drawn );
		this.bands.set( drawn.filter( r=>!r.ended || (r.range.start<axis!.end && r.range.end>axis!.start) ) );
	}
	//a band of the chart, clicked or entered:  its machine's OEE page
	open( point:GanttPoint ):void{
		const row = this.bands()[point.y];
		if( row )
			this.#router.navigate( ['../machines', row.machine.slug, 'oee'], {relativeTo: this.#route} );
	}

	loading = signal( true );
	error = signal<string|undefined>( undefined );
	sideNav = model.required<RouteItem>();
	gateway!:Gateway;
	#rows = new Map<number,LiveRow>();
	#epoch = 0;//moved as the page closes on a gateway
	#timer:ReturnType<typeof setTimeout>|undefined;
	#route = inject( ActivatedRoute );
	#router = inject( Router );
	#title = inject( ComponentPageTitle );
	#service = inject( OeeService );
	#loader = inject( HighchartsLoader );
}
//what a band is, where it began and whether it has a length to draw yet:  a change of it is drawn at once
function bandKey( s:OeeSegment|undefined ):string{ return s ? `${s.start.getTime()}|${s.state}|${s.reason?.id}|${s.small}|${s.end>s.start}` : ''; }
//the chart's time:  from the earliest start to the latest end of the rows still subscribed, or of every row when none is
function axisOf( rows:OeeDashboardBand[] ):OeeSpan|undefined{
	const live = rows.filter( r=>!r.ended ), over = live.length ? live : rows;
	return over.length ? { start: new Date( Math.min( ...over.map( r=>r.range.start.getTime() ) ) ), end: new Date( Math.max( ...over.map( r=>r.range.end.getTime() ) ) ) } : undefined;
}