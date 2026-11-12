import { DatePipe } from '@angular/common';
import { Component, computed, inject, model, OnDestroy, OnInit, signal } from '@angular/core';
import { ActivatedRoute, Router, RouterLink } from '@angular/router';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { Subscription } from 'rxjs';
import { ComponentPageTitle, RouteItem } from 'jde-spa';
import { errorMessage } from 'jde-framework';
import { OeeSegment, OeeSpan } from '../../../model/oee';
import { OeeLiveEvent, OeeLiveStatus } from '../../../model/oee-live';
import { growOeeBands, oeeLiveRange, oeeLiveSegment, OeeMachineRef, OeeTile, oeeTile, oeeTileNext, oeeTileView } from '../../../model/oee-live-view';
import { Gateway, gatewayAnswer } from '../../../services/gateway-service';
import { OeeService } from '../../../services/oee-service';
import { WatchPageData } from '../../../services/resolvers/watch-resolver';
import { HighchartsLoader } from '../../node-detail/node-history/highcharts-loader';
import { OeeChart, OeeChartSpec } from './oee-chart';
import { GanttPoint, oeeKindColors, segmentKind, toGanttRows } from './oee-chart-data';

type Machine = OeeMachineRef & { inLine:boolean };
//A machine's row of the Gantt chart:  what its shift covers and its bands, read once the shift is known and grown by each
//status from then on.  `stale`:  the socket dropped, so the bands are read again at the next status.
type LiveRow = { machine:Machine; subscription?:Subscription; range?:OeeSpan; segments:OeeSegment[]; ready:boolean; stale:boolean; reads:number };
export type OeeDashboardBand = { machine:Machine; range:OeeSpan; segments:OeeSegment[] };
const epoch = new Date( 0 );

//OEE's dashboard (#406, docs/oee/spec.md *Web*, *The Gantt chart*), `oee` under its gateway's page:  a tile per machine in no
//line, live over Gateway.oeeSubscribe - its state and since when, the stop's reason or why it has none, and the shift's four
//figures - and under them the Gantt chart of the current shift, a row per machine the user may read, each over its own
//calendar's shift, or without one the UTC day.  A row's bands are `oeeTimeline`'s to now, read once;  from then its last band
//grows with each status and a new one opens on a state change, with no further read.  A state change is drawn at once, growth
//every `growEvery` at most.  A machine whose group the user may not read has neither tile nor row;  one whose subscription the
//gateway ends keeps its tile, saying why, and its bands as they stood.  A tile and a band open the machine's OEE page, which
//opens on its shift.  The chart draws through the History trend's add-on;  without it the page is its tiles.
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
			this.machines.set( machines );
			this.tiles.set( new Map( machines.map( m=>[m.id, oeeTile( m )] ) ) );
			for( const machine of machines ){
				const row:LiveRow = { machine, segments: [], ready: false, stale: false, reads: 0 };
				this.#rows.set( machine.id, row );
				row.subscription = gateway.oeeSubscribe( machine.id ).subscribe( {next: e=>this.#on( row, e ), error: e=>this.#ended( row, e )} );
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
	//the subscriptions end with the page, and with the gateway it showed
	#close():void{
		++this.#epoch;
		for( const row of this.#rows.values() )
			row.subscription?.unsubscribe();
		this.#rows.clear();
		clearTimeout( this.#timer );
		this.#timer = undefined;
		this.machines.set( [] );
		this.tiles.set( new Map() );
		this.bands.set( [] );
		this.error.set( undefined );
	}

	//The tiles
	machines = signal<Machine[]>( [] );//those the user may read, the ones in no line first
	tiles = signal( new Map<number,OeeTile>() );//by machine, every row's:  a line's machines have a row and no tile
	readonly tileViews = computed( ()=>{
		const tiles = this.tiles();
		return this.machines().filter( m=>!m.inLine ).map( machine=>{
			const tile = tiles.get( machine.id )!, status = tile.status;
			return { machine, view: oeeTileView( tile ), color: oeeKindColors[status ? segmentKind( oeeLiveSegment( status, epoch, epoch ) ) : 'unknown'] };
		} );
	} );
	//the machines with no shift now, which the chart has no row for
	readonly between = computed( ()=>{
		const tiles = this.tiles();
		return this.machines().filter( m=>{ const status = tiles.get( m.id )?.status; return status && !oeeLiveRange( status ); } ).map( m=>m.name );
	} );
	#setTile( row:LiveRow, event:Parameters<typeof oeeTileNext>[1] ):OeeTile{
		const tile = oeeTileNext( this.tiles().get( row.machine.id )!, event );
		this.tiles.update( tiles=>new Map( tiles ).set( row.machine.id, tile ) );
		return tile;
	}
	#on( row:LiveRow, event:OeeLiveEvent ):void{
		this.#setTile( row, event );
		if( event.kind=='dropped' )
			row.stale = true;//what the outage held isn't pushed:  the bands are read again
		else
			this.#band( row, event.status );
	}
	//The gateway ended the subscription, or refused it.  A group the user may not read is no machine of the page's.
	#ended( row:LiveRow, e:unknown ):void{
		const answer = gatewayAnswer( e );
		if( answer?.httpStatus==401 || answer?.httpStatus==403 ){
			this.#rows.delete( row.machine.id );
			this.machines.update( machines=>machines.filter( m=>m.id!=row.machine.id ) );
			this.tiles.update( tiles=>{ const y = new Map( tiles ); y.delete( row.machine.id ); return y; } );
		}
		else
			this.#setTile( row, {kind: 'ended', message: answer?.message || errorMessage( e )} );
		this.#publish( true );
	}

	//The Gantt chart
	chartState = inject( HighchartsLoader ).state;
	bands = signal<OeeDashboardBand[]>( [] );//each machine's in a shift, as the chart last drew them
	readonly ganttSpec = computed<OeeChartSpec|undefined>( ()=>{
		const rows = this.bands();
		if( !rows.length )
			return undefined;
		const start = new Date( Math.min( ...rows.map( r=>r.range.start.getTime() ) ) ), end = new Date( Math.max( ...rows.map( r=>r.range.end.getTime() ) ) );
		return { kind: 'gantt', title: `The current shift on ${this.gateway.name}`, range: {start, end}, ...toGanttRows( rows.map( r=>({name: r.machine.name, segments: r.segments}) ) ),
			description: "A row per machine over its current shift:  running, each stop in its reason's category, uncoded and small stops, and unknown time.  The last band of a row grows as the machine reports.  A band opens its machine's OEE page." };
	} );
	//A status's place in its machine's row:  another shift's, or the first after a drop, has the row read again;  else the bands grow.
	#band( row:LiveRow, status:OeeLiveStatus ):void{
		const range = oeeLiveRange( status );
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
		if( !row.range || row.range.start.getTime()!=range.start.getTime() || row.stale ){
			row.range = range;
			row.stale = false;
			row.ready = false;
			this.#read( row, range );
			return;
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
		const status = this.tiles().get( row.machine.id )?.status;
		row.segments = status ? growOeeBands( segments, status, new Date(), range.start ) : segments;
		row.ready = true;
		this.#publish( true );
	}
	//the rows as the chart draws them:  at once, or with the next redraw
	#publish( now:boolean ):void{
		if( !now ){
			this.#timer ??= setTimeout( ()=>this.#publish( true ), OeeDashboard.growEvery );
			return;
		}
		clearTimeout( this.#timer );
		this.#timer = undefined;
		this.bands.set( [...this.#rows.values()].filter( r=>r.range && r.ready ).map( r=>({ machine: r.machine, range: r.range!, segments: r.segments }) ) );
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