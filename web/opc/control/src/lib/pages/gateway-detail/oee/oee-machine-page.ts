import { DatePipe } from '@angular/common';
import { Component, computed, inject, model, OnInit, signal } from '@angular/core';
import { ActivatedRoute, RouterLink } from '@angular/router';
import { MatButtonModule } from '@angular/material/button';
import { MatButtonToggleModule } from '@angular/material/button-toggle';
import { MatDialog } from '@angular/material/dialog';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatSlideToggleModule } from '@angular/material/slide-toggle';
import { MatTableModule } from '@angular/material/table';
import { MatTooltipModule } from '@angular/material/tooltip';
import { ComponentPageTitle, RouteItem } from 'jde-spa';
import { DateUtils, errorMessage } from 'jde-framework';
import { isOeeRangeRefusal, narrowOeeRange, OeeBucket, oeeCount, oeeDuration, OeeLoss, oeeLossSplit, oeePercent, OeePeriod, oeePeriodRange, oeePeriods, OeeReasonRef, OeeSegment, OeeSpan, OeeStop, OeeTimeline } from '../../../model/oee';
import { OeeMachine } from '../../../model/oee-admin';
import { Gateway } from '../../../services/gateway-service';
import { OeeAdminService } from '../../../services/oee-admin-service';
import { OeeService } from '../../../services/oee-service';
import { GatewayDetailData } from '../../../services/resolvers/client-resolver';
import { HighchartsLoader } from '../../node-detail/node-history/highcharts-loader';
import { OeeChart, OeeChartSpec } from './oee-chart';
import { lossName, OeeTrendBy, toGantt, toOeeTrend, toPareto } from './oee-chart-data';
import { OeeCodingDialog, OeeCodingStop } from './oee-coding-dialog';

const stopsPage = 50;

//A machine's OEE page (#405, docs/oee/spec.md *Web*, *The Gantt chart*), `machines/<slug>/oee` under its gateway's page:  a
//period - this shift, where the machine has a calendar, today, yesterday, this week, last week or a custom range - and over it
//the figures with the loss split, the Gantt chart, a row for the planned time and one for the state, the losses as a Pareto,
//the trend by shift or day, and the stops, newest first, where they are coded:  from a row, or from a stop's band of the chart.
//Each part is a read of its own, so one refused says so in its place and the rest stand;  a period the timeline refuses as more
//than the gateway reads at once is halved, newest half kept, until it is taken, and a notice says what the chart shows.  The
//charts draw through the History trend's add-on;  without it the page is its figures and its tables.  A day and a week are
//the browser's, midnight and Monday;  a shift is the calendar's.  The machine and its gateway are the row's, from
//HistAdminResolver, and the router reuses the page from one machine to the next.
@Component({
	templateUrl: './oee-machine-page.html',
	styleUrls: ['./oee-machine-page.scss'],
	host: {class: 'main-content my-content oee-machine-page'},
	imports: [DatePipe, RouterLink, MatButtonModule, MatButtonToggleModule, MatFormFieldModule, MatIconModule, MatInputModule, MatProgressBarModule, MatSlideToggleModule, MatTableModule, MatTooltipModule, OeeChart]
})
export class OeeMachinePage implements OnInit{
	ngOnInit(){
		this.#route.data.subscribe( data=>this.#open( <GatewayDetailData<OeeMachine>>data['pageData'] ) );
		this.#loader.load().catch( ()=>{} );//how it settled is the loader's state, for the page's one notice
	}
	async #open( pageData:GatewayDetailData<OeeMachine> ):Promise<void>{
		if( !pageData )//the resolver found no row, and is on its way elsewhere
			return;
		const machine = new OeeMachine( pageData.row ), gateway = pageData.gateway;
		this.gateway = gateway;
		this.machine.set( machine );
		this.sideNav.set( pageData.routing );
		this.#title.title = `${machine.name} - OEE`;
		this.#clear();
		this.reasons.set( [] );
		this.customFrom.set( '' );
		this.customTo.set( '' );
		this.uncodedOnly.set( false );
		this.#service.reasons( gateway ).then( reasons=>{ if( this.#isOpen( machine ) ) this.reasons.set( reasons ); }, e=>console.warn( 'Could not list the reasons.', e ) );
		//a calendar the gateway's client finds gives the machine shifts:  the page opens on the one it is in, and the trend is by shift
		const status = await this.#admin.status( gateway, machine.id ).catch( ()=>undefined );
		if( !this.#isOpen( machine ) )
			return;
		const shifts = status?.calendar=='found';
		this.hasShifts.set( shifts );
		this.period.set( shifts ? 'shift' : 'today' );
		this.trendBy.set( shifts ? 'shift' : 'day' );
		await this.refresh();
	}
	#isOpen( machine:OeeMachine ):boolean{ return this.machine()===machine; }
	#clear():void{
		++this.#reads;//what is on its way is another period's, or another machine's
		this.range.set( undefined );
		this.rangeProblem.set( undefined );
		this.bucket.set( undefined );
		this.timeline.set( undefined );
		this.narrowed.set( undefined );
		this.losses.set( [] );
		this.trend.set( [] );
		this.stops.set( [] );
		this.#continuation = undefined;
		this.hasMore.set( false );
		this.errors.set( {} );
	}

	//The period
	readonly periods = computed( ()=>oeePeriods.filter( p=>p.id!='shift' || this.hasShifts() ) );
	period = signal<OeePeriod>( 'today' );
	hasShifts = signal( false );
	customFrom = signal( '' );//as the inputs hold them;  a blank To is now
	customTo = signal( '' );
	range = signal<OeeSpan|undefined>( undefined );//what the page shows, as the last refresh resolved the period
	rangeProblem = signal<string|undefined>( undefined );
	pick( period:OeePeriod ):Promise<void>{
		this.period.set( period );
		return this.refresh();
	}
	//The period at this moment, read whole.  An open period ends now, so Refresh moves it on.  What the page shows stays until
	//each part's answer replaces it, so a coding's refresh leaves the table where it is read.
	async refresh():Promise<void>{
		const machine = this.machine(), gateway = this.gateway, period = this.period(), now = new Date();
		const read = ++this.#reads, current = ()=>read==this.#reads;
		this.errors.set( {} );
		this.rangeProblem.set( undefined );
		this.loading.update( n=>n+1 );
		try{
			const shift = period=='shift' ? await this.#service.shift( gateway, machine.id, now ).catch( ()=>undefined ) : undefined;
			if( !current() )
				return;
			const range = oeePeriodRange( period, now, {shift, from: DateUtils.fromLocalInput( this.customFrom() ), to: DateUtils.fromLocalInput( this.customTo() )} );
			if( !range ){
				this.#clear();
				return this.rangeProblem.set( period=='shift' ? "The machine's calendar has no shift in the last day and a half.  Pick another period." : period=='custom' ? 'Enter a From before now, and a To after it or none.' : 'The period is empty.' );
			}
			this.range.set( range );
			const part = async <T>( name:OeePart, act:()=>Promise<T>, set:( y:T|undefined )=>void )=>{
				try{
					const y = await act();
					if( current() )
						set( y );
				}
				catch( e ){
					if( current() ){
						set( undefined );
						this.errors.update( errors=>({...errors, [name]: errorMessage(e)}) );
					}
				}
			};
			await Promise.all( [
				part( 'figures', ()=>this.#service.figures( gateway, machine.id, range ), buckets=>this.bucket.set( buckets?.[0] ) ),
				part( 'timeline', ()=>this.#readTimeline( gateway, machine.id, range ), y=>{ this.timeline.set( y?.timeline ); this.narrowed.set( y?.narrowed ? y.range : undefined ); } ),
				part( 'losses', ()=>this.#service.losses( gateway, machine.id, range ), losses=>this.losses.set( losses ?? [] ) ),
				part( 'trend', ()=>this.#service.figures( gateway, machine.id, range, this.trendBy() ), buckets=>this.trend.set( buckets ?? [] ) ),
				part( 'stops', ()=>this.#service.stops( gateway, machine.id, range, {uncoded: this.uncodedOnly(), limit: stopsPage} ), page=>this.#setStops( page?.stops ?? [], page?.continuation ) )
			] );
		}
		finally{
			this.loading.update( n=>n-1 );
		}
	}
	//The Gantt chart's read, which only the chart uses:  none without the add-on.  A range refused as above the gateway's readLimit
	//is halved, its newest half kept, until one is taken.
	async #readTimeline( gateway:Gateway, machine:number, asked:OeeSpan ):Promise<{timeline:OeeTimeline; range:OeeSpan; narrowed:boolean}|undefined>{
		if( !await this.#loader.load().catch( ()=>undefined ) )
			return undefined;
		for( let range = asked;; ){
			try{
				return { timeline: await this.#service.timeline( gateway, machine, range ), range, narrowed: range!=asked };
			}
			catch( e ){
				const next = isOeeRangeRefusal( errorMessage(e) ) ? narrowOeeRange( range ) : undefined;
				if( !next )
					throw e;
				range = next;
			}
		}
	}
	loading = signal( 0 );//the reads under way
	errors = signal<Partial<Record<OeePart,string>>>( {} );//each part's refusal, said in its place
	#reads = 0;

	//The figures
	bucket = signal<OeeBucket|undefined>( undefined );
	readonly factors = computed( ()=>{
		const b = this.bucket();
		if( !b )
			return [];
		const measures = ( name:string )=>b.measured.includes( name );
		return [
			{ label: 'OEE', value: oeePercent( b.oee ), hint: b.measured.length ? `The product of ${b.measured.join( ', ' )}.` : 'No factor is measured.', lead: true },
			{ label: 'Availability', value: oeePercent( b.availability ), hint: 'Run time over planned time.' },
			{ label: 'Performance', value: oeePercent( b.performance ), hint: measures( 'performance' ) ? 'The count at the ideal cycle over run time.' : 'Not measured:  the machine has no count, or no ideal.', over: (b.performance ?? 0)>1 },
			{ label: 'Quality', value: oeePercent( b.quality ), hint: measures( 'quality' ) ? 'Good over total.' : 'Not measured:  the machine has no Good or Reject signal.' },
			{ label: 'TEEP', value: oeePercent( b.teep ), hint: "OEE times planned time over the period's whole length." }
		];
	} );
	readonly split = computed( ()=>{ const b = this.bucket(); return b ? oeeLossSplit( b ) : undefined; } );
	readonly counts = computed( ()=>{
		const b = this.bucket(), unit = this.machine()?.countUnit;
		return !b ? [] : [ {label: 'Total', value: b.total}, {label: 'Good', value: b.good}, {label: 'Reject', value: b.reject}, {label: 'Outside planned time', value: b.outside} ]
			.map( c=>({ label: c.label, text: `${oeeCount( c.value )}${unit ? ` ${unit}` : ''}` }) );
	} );
	duration = oeeDuration;
	percent = oeePercent;

	//The charts
	chartState = inject( HighchartsLoader ).state;
	timeline = signal<OeeTimeline|undefined>( undefined );
	narrowed = signal<OeeSpan|undefined>( undefined );//what the Gantt chart shows, where the period was refused whole
	readonly ganttSpec = computed<OeeChartSpec|undefined>( ()=>{
		const timeline = this.timeline(), range = this.narrowed() ?? this.range();
		if( !timeline || !range )
			return undefined;
		return { kind: 'gantt', title: `Timeline of ${this.machine().name}`, range, ...toGantt( timeline, range ),
			description: "The machine's planned time on the first row and its state on the second:  running, each stop in its reason's category, uncoded and small stops, and unknown time.  A stop's band opens its coding." };
	} );
	losses = signal<OeeLoss[]>( [] );
	readonly lostMs = computed( ()=>this.losses().reduce( ( sum, l )=>sum+l.ms, 0 ) );
	readonly paretoSpec = computed<OeeChartSpec|undefined>( ()=>{
		const losses = this.losses();
		return losses.some( l=>l.ms>0 ) ? { kind: 'columns', title: `Losses of ${this.machine().name}`, name: 'Lost time', axis: 'Minutes', percent: false, ...toPareto( losses ),
			description: 'The time lost to each stop reason, to uncoded stops and to small stops, longest first.' } : undefined;
	} );
	lossName = lossName;
	trendBy = signal<OeeTrendBy>( 'day' );
	trend = signal<OeeBucket[]>( [] );
	readonly trendSpec = computed<OeeChartSpec|undefined>( ()=>{
		const buckets = this.trend(), by = this.trendBy();
		return buckets.length ? { kind: 'columns', title: `OEE of ${this.machine().name} by ${by}`, name: 'OEE', axis: 'OEE', percent: true, ...toOeeTrend( buckets, by ),
			description: `OEE in percent for each ${by} of the period.` } : undefined;
	} );
	async setTrendBy( by:OeeTrendBy ):Promise<void>{
		const range = this.range(), read = this.#reads, gateway = this.gateway;
		this.trendBy.set( by );
		if( !range )
			return;
		await this.#reread( 'trend', read, ()=>this.#service.figures( gateway, this.machine().id, range, by ), buckets=>this.trend.set( buckets ), ()=>this.trend.set( [] ) );
	}

	//The stops
	stops = signal<OeeStop[]>( [] );
	hasMore = signal( false );
	uncodedOnly = signal( false );
	reasons = signal<OeeReasonRef[]>( [] );
	readonly stopColumns = ['start', 'end', 'length', 'reason', 'code', 'comment', 'actions'];
	#continuation:string|undefined;
	#setStops( stops:OeeStop[], continuation:string|undefined ):void{
		this.stops.set( stops );
		this.#continuation = continuation;
		this.hasMore.set( !!continuation );
	}
	async setUncodedOnly( uncoded:boolean ):Promise<void>{
		const range = this.range(), read = this.#reads, gateway = this.gateway;
		this.uncodedOnly.set( uncoded );
		if( !range )
			return;
		await this.#reread( 'stops', read, ()=>this.#service.stops( gateway, this.machine().id, range, {uncoded, limit: stopsPage} ), page=>this.#setStops( page.stops, page.continuation ), ()=>this.#setStops( [], undefined ) );
	}
	//the page after the oldest stop shown
	async loadMore():Promise<void>{
		const range = this.range(), read = this.#reads, gateway = this.gateway, continuation = this.#continuation;
		if( !range || !continuation )
			return;
		await this.#reread( 'stops', read, ()=>this.#service.stops( gateway, this.machine().id, range, {uncoded: this.uncodedOnly(), limit: stopsPage, continuation} ),
			page=>this.#setStops( [...this.stops(), ...page.stops], page.continuation ), ()=>{} );
	}
	//one part read again within the period shown:  dropped where the page has moved on since
	async #reread<T>( name:OeePart, read:number, act:()=>Promise<T>, set:( y:T )=>void, refused:()=>void ):Promise<void>{
		this.loading.update( n=>n+1 );
		this.errors.update( ({[name]: _, ...rest})=>rest );
		try{
			const y = await act();
			if( read==this.#reads )
				set( y );
		}
		catch( e ){
			if( read==this.#reads ){
				refused();
				this.errors.update( errors=>({...errors, [name]: errorMessage(e)}) );
			}
		}
		finally{
			this.loading.update( n=>n-1 );
		}
	}
	//what a stop is put down to:  its reason - the one its parts share, where it is coded in parts - or why it has none
	reasonText( s:OeeStop ):string{
		const parts = new Set( s.codings.map( c=>c.reason?.name ) );
		return s.reason?.name ?? ( s.uncoded ? 'Uncoded' : parts.size==1 && s.codings[0].reason ? s.codings[0].reason.name : parts.size ? 'Coded in parts' : s.small ? 'Small stop' : '' );
	}
	//the codings' comments with who made them, a part to a line
	comments( s:OeeStop ):string[]{
		return s.codings.map( c=>[s.codings.length>1 ? c.reason?.name : undefined, c.comment, c.userName ? `(${c.userName})` : undefined].filter( x=>!!x ).join( ' ' ) ).filter( x=>!!x );
	}
	codeText( s:OeeStop ):string{ return s.code==undefined ? '' : typeof s.code=='object' ? JSON.stringify( s.code ) : String( s.code ); }

	//The coding dialog on a stop, and the page read again where a coding landed:  a reason moves the figures and the losses too.
	async code( stop:OeeCodingStop ):Promise<void>{
		const gateway = this.gateway, machine = this.machine();
		const changed = await OeeCodingDialog.open( this.#dialog, { stop, reasons: this.reasons(),
			code: call=>this.#service.code( gateway, machine.id, call ), uncode: id=>this.#service.uncode( gateway, id ) } );
		if( changed && this.#isOpen( machine ) )
			await this.refresh();
	}
	//A band of the Gantt chart:  its stop as the table has it, whole, with its codings.  One the table hasn't paged to is the
	//stop's pieces on the chart, which the period cuts.
	codeSegment( segment:OeeSegment ):Promise<void>{
		const pieces = this.timeline()?.segments.filter( s=>s.stop==segment.stop ) ?? [segment], first = pieces[0] ?? segment, last = pieces.at( -1 ) ?? segment;
		return this.code( this.stops().find( s=>s.id==segment.stop ) ?? { start: first.start, startText: first.startText, end: last.end, endText: last.endText, codings: [] } );
	}

	sideNav = model.required<RouteItem>();
	machine = signal<OeeMachine>( null as any );
	gateway!:Gateway;
	#route = inject( ActivatedRoute );
	#title = inject( ComponentPageTitle );
	#dialog = inject( MatDialog );
	#service = inject( OeeService );
	#admin = inject( OeeAdminService );
	#loader = inject( HighchartsLoader );
}
type OeePart = 'figures'|'timeline'|'losses'|'trend'|'stops';