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
import { isOeeRangeRefusal, narrowOeeRange, OeeBucket, oeeCount, oeeDuration, OeeLoss, oeeLossSplit, oeePercent, OeePeriod, oeePeriodRange, oeePeriods, OeeSegment, OeeShift, OeeSpan, OeeStop, OeeTimeline } from '../../../model/oee';
import { OeeMachine, OeeMachineStatus } from '../../../model/oee-admin';
import { Gateway } from '../../../services/gateway-service';
import { OeeAdminService } from '../../../services/oee-admin-service';
import { OeeService } from '../../../services/oee-service';
import { GatewayDetailData } from '../../../services/resolvers/client-resolver';
import { HighchartsLoader } from '../../node-detail/node-history/highcharts-loader';
import { OeeChart, OeeChartSpec } from './oee-chart';
import { lossName, oeeDayName, OeeTrendBy, toGantt, toOeeTrend, toPareto } from './oee-chart-data';
import { OeeCodingDialog, OeeReasonsRead } from './oee-coding-dialog';

const stopsPage = 50;
const oeeParts = ['figures', 'timeline', 'losses', 'trend', 'stops'] as const;
type OeePart = typeof oeeParts[number];

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
		this.#shift = undefined;
		this.reasons.set( {list: [], reading: true} );
		this.customFrom.set( '' );
		this.customTo.set( '' );
		this.uncodedOnly.set( false );
		this.statusProblem.set( undefined );
		this.#service.reasons( gateway ).then( list=>{ if( this.#isOpen( machine ) ) this.reasons.set( {list} ); }, e=>{ if( this.#isOpen( machine ) ) this.reasons.set( {list: [], error: errorMessage(e)} ); } );
		//a calendar the gateway's client finds gives the machine shifts:  the page opens on the one it is in, and the trend is by shift
		let status:OeeMachineStatus|undefined, failed:string|undefined;
		try{
			status = await this.#admin.status( gateway, machine.id );
		}
		catch( e ){
			failed = errorMessage( e );
		}
		if( !this.#isOpen( machine ) )
			return;
		const shifts = status?.calendar=='found';
		this.hasShifts.set( shifts );
		this.utcDays.set( !!status && !shifts );//with no calendar the gateway finds, its days are UTC's
		this.statusProblem.set( failed ? `Could not read the machine's calendar, so This shift isn't offered:  ${failed}` : undefined );
		this.period.set( shifts ? 'shift' : 'today' );
		this.trendBy.set( shifts ? 'shift' : 'day' );
		await this.refresh();
	}
	#isOpen( machine:OeeMachine ):boolean{ return this.machine()===machine; }
	#clear():void{
		++this.#epoch;//what is on its way is another period's, or another machine's
		for( const part of oeeParts )
			++this.#reads[part];
		this.range.set( undefined );
		this.#rangeShift = undefined;
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
	utcDays = signal( false );//the trend's days are UTC's:  the machine has no calendar the gateway finds
	statusProblem = signal<string|undefined>( undefined );//why This shift isn't offered, where the status read failed
	customFrom = signal( '' );//as the inputs hold them;  a blank To is now
	customTo = signal( '' );
	range = signal<OeeSpan|undefined>( undefined );//what the page shows, as the last refresh resolved the period
	rangeProblem = signal<string|undefined>( undefined );
	pick( period:OeePeriod ):Promise<void>{
		this.period.set( period );
		return this.refresh();
	}
	//The period at this moment, read whole.  An open period ends now, so Refresh moves it on.  What the page shows stays until
	//each part's answer replaces it;  `keepStops`, a coding's refresh, reads as many stops as the table shows, so it stays where
	//it is read.
	async refresh( keepStops=false ):Promise<void>{
		const machine = this.machine(), gateway = this.gateway, period = this.period(), now = new Date();
		const read = ++this.#epoch, current = ()=>read==this.#epoch;
		this.errors.set( {} );
		this.rangeProblem.set( undefined );
		this.loading.update( n=>n+1 );
		try{
			let shift:OeeShift|undefined;
			if( period=='shift' ){
				try{
					shift = await this.#findShift( gateway, machine, now );
				}
				catch( e ){
					if( current() ){
						this.#clear();
						this.rangeProblem.set( `Could not find the machine's shift:  ${errorMessage(e)}` );
					}
					return;
				}
				if( !current() )
					return;
			}
			const range = oeePeriodRange( period, now, {shift, from: DateUtils.fromLocalInput( this.customFrom() ), to: DateUtils.fromLocalInput( this.customTo() )} );
			if( !range ){
				this.#clear();
				return this.rangeProblem.set( period=='shift' ? "The machine's calendar has no shift in the last day and a half.  Pick another period." : period=='custom' ? 'Enter a From before now, and a To after it or none.' : 'The period is empty.' );
			}
			this.range.set( range );
			this.#rangeShift = shift;
			const limit = keepStops ? Math.max( stopsPage, this.stops().length ) : stopsPage;
			//A part's answer lands while no later read of it, or of the page, has begun:  a toggle's read over a refresh's, and the reverse.
			const part = async <T>( name:OeePart, act:( live:()=>boolean )=>Promise<T>, set:( y:T|undefined )=>void )=>{
				const n = ++this.#reads[name], live = ()=>current() && n==this.#reads[name];
				try{
					const y = await act( live );
					if( live() )
						set( y );
				}
				catch( e ){
					if( live() ){
						set( undefined );
						this.errors.update( errors=>({...errors, [name]: errorMessage(e)}) );
					}
				}
			};
			const figures = this.#service.figures( gateway, machine.id, range );
			await Promise.all( [
				part( 'figures', ()=>figures, buckets=>this.bucket.set( buckets?.[0] ) ),
				part( 'timeline', live=>this.#readTimeline( gateway, machine.id, range, live ), y=>{ this.timeline.set( y?.timeline ); this.narrowed.set( y?.narrowed ? y.range : undefined ); } ),
				part( 'losses', ()=>this.#service.losses( gateway, machine.id, range ), losses=>this.losses.set( losses ?? [] ) ),
				part( 'trend', ()=>this.#readTrend( gateway, machine.id, range, this.trendBy(), shift, ()=>figures ), buckets=>this.trend.set( buckets ?? [] ) ),
				part( 'stops', ()=>this.#service.stops( gateway, machine.id, range, {uncoded: this.uncodedOnly(), limit} ), page=>this.#setStops( page?.stops ?? [], page?.continuation ) )
			] );
		}
		finally{
			this.loading.update( n=>n-1 );
		}
	}
	//This shift's bounds, kept while it runs and read again once now is past its end, when another may have begun.
	async #findShift( gateway:Gateway, machine:OeeMachine, now:Date ):Promise<OeeShift|undefined>{
		if( this.#shift && this.#shift.end>now )
			return this.#shift;
		const shift = await this.#service.shift( gateway, machine.id, now );
		if( this.#isOpen( machine ) )
			this.#shift = shift;
		return shift;
	}
	#shift:OeeShift|undefined;
	#rangeShift:OeeShift|undefined;//the shift the range is, where the period is This shift
	//The Gantt chart's read, which only the chart uses:  none without the add-on.  A range refused as above the gateway's readLimit
	//is halved, its newest half kept, until one is taken, or the page has moved on.
	async #readTimeline( gateway:Gateway, machine:number, asked:OeeSpan, live:()=>boolean ):Promise<{timeline:OeeTimeline; range:OeeSpan; narrowed:boolean}|undefined>{
		if( !await this.#loader.load().catch( ()=>undefined ) )
			return undefined;
		for( let range = asked;; ){
			try{
				return { timeline: await this.#service.timeline( gateway, machine, range ), range, narrowed: range!=asked };
			}
			catch( e ){
				const next = live() && isOeeRangeRefusal( errorMessage(e) ) ? narrowOeeRange( range ) : undefined;
				if( !next )
					throw e;
				range = next;
			}
		}
	}
	loading = signal( 0 );//the reads under way
	errors = signal<Partial<Record<OeePart,string>>>( {} );//each part's refusal, said in its place
	#epoch = 0;//moved by each refresh and each move to another period or machine
	#reads:Record<OeePart,number> = { figures: 0, timeline: 0, losses: 0, trend: 0, stops: 0 };//moved by each read of the part

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
		const buckets = this.trend(), by = this.trendBy(), utc = by=='day' && this.utcDays(), per = utc ? 'day (UTC)' : by;
		return buckets.length ? { kind: 'columns', title: `OEE of ${this.machine().name} by ${per}`, name: 'OEE', axis: 'OEE', percent: true, ...toOeeTrend( buckets, by, oeeDayName( utc ) ),
			description: `OEE in percent for each ${per} of the period.` } : undefined;
	} );
	async setTrendBy( by:OeeTrendBy ):Promise<void>{
		const range = this.range(), gateway = this.gateway, bucket = this.bucket();
		this.trendBy.set( by );
		if( !range )
			return;
		await this.#reread( 'trend', ()=>this.#readTrend( gateway, this.machine().id, range, by, this.#rangeShift, bucket && (()=>Promise.resolve( [bucket] )) ), buckets=>this.trend.set( buckets ), ()=>this.trend.set( [] ) );
	}
	//The trend's buckets.  Over This shift, by shift is the figures' one bucket named for its shift, not a second read of it.
	#readTrend( gateway:Gateway, machine:number, range:OeeSpan, by:OeeTrendBy, shift:OeeShift|undefined, figures:(()=>Promise<OeeBucket[]>)|undefined ):Promise<OeeBucket[]>{
		return by=='shift' && shift && figures
			? figures().then( buckets=>buckets.map( b=>({...b, shift: shift.name}) ) )
			: this.#service.figures( gateway, machine, range, by );
	}

	//The stops
	stops = signal<OeeStop[]>( [] );
	hasMore = signal( false );
	uncodedOnly = signal( false );
	reasons = signal<OeeReasonsRead>( {list: []} );
	readonly stopColumns = ['start', 'end', 'length', 'reason', 'code', 'comment', 'actions'];
	#continuation:string|undefined;
	#setStops( stops:OeeStop[], continuation:string|undefined ):void{
		this.stops.set( stops );
		this.#continuation = continuation;
		this.hasMore.set( !!continuation );
	}
	async setUncodedOnly( uncoded:boolean ):Promise<void>{
		const range = this.range(), gateway = this.gateway;
		this.uncodedOnly.set( uncoded );
		if( !range )
			return;
		await this.#reread( 'stops', ()=>this.#service.stops( gateway, this.machine().id, range, {uncoded, limit: stopsPage} ), page=>this.#setStops( page.stops, page.continuation ), ()=>this.#setStops( [], undefined ) );
	}
	//the page after the oldest stop shown
	async loadMore():Promise<void>{
		const range = this.range(), gateway = this.gateway, continuation = this.#continuation;
		if( !range || !continuation )
			return;
		await this.#reread( 'stops', ()=>this.#service.stops( gateway, this.machine().id, range, {uncoded: this.uncodedOnly(), limit: stopsPage, continuation} ),
			page=>this.#setStops( [...this.stops(), ...page.stops], page.continuation ), ()=>{} );
	}
	//one part read again within the period shown:  dropped where the page, or the part, has been read again since
	async #reread<T>( name:OeePart, act:()=>Promise<T>, set:( y:T )=>void, refused:()=>void ):Promise<void>{
		const epoch = this.#epoch, at = ++this.#reads[name], live = ()=>epoch==this.#epoch && at==this.#reads[name];
		this.loading.update( n=>n+1 );
		this.errors.update( ({[name]: _, ...rest})=>rest );
		try{
			const y = await act();
			if( live() )
				set( y );
		}
		catch( e ){
			if( live() ){
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
	//The dialog reads an open stop again before it codes it.
	async code( stop:OeeStop ):Promise<void>{
		const gateway = this.gateway, machine = this.machine();
		const changed = await OeeCodingDialog.open( this.#dialog, { stop, reasons: this.reasons,
			code: call=>this.#service.code( gateway, machine.id, call ), uncode: id=>this.#service.uncode( gateway, id ),
			reread: ()=>this.#service.stop( gateway, machine.id, stop.id, stop.start ) } );
		if( changed && this.#isOpen( machine ) )
			await this.refresh( true );
	}
	//A band of the Gantt chart:  its stop as the table has it, whole, with its codings.  One the table lacks - filtered out by
	//Uncoded, not paged to, or begun before the period, which cuts its band - is read from the gateway at its first piece.
	async codeSegment( segment:OeeSegment ):Promise<void>{
		const row = this.stops().find( s=>s.id==segment.stop );
		if( row )
			return this.code( row );
		const gateway = this.gateway, machine = this.machine(), first = this.timeline()?.segments.find( s=>s.stop==segment.stop ) ?? segment;
		const said = ( e:string )=>this.errors.update( errors=>({...errors, timeline: e}) );
		let stop:OeeStop|undefined;
		try{
			stop = await this.#service.stop( gateway, machine.id, segment.stop!, first.start );
		}
		catch( e ){
			if( this.#isOpen( machine ) )
				said( `Could not read the stop to code it:  ${errorMessage(e)}` );
			return;
		}
		if( !this.#isOpen( machine ) )
			return;
		if( !stop )
			return said( 'The stop is gone:  the engine has made its stops again.  Refresh to read them.' );
		return this.code( stop );
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