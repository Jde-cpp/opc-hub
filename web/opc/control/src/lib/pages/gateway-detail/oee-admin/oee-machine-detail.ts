import { Component, computed, inject, signal } from '@angular/core';
import { RouterLink } from '@angular/router';
import { MatButtonModule } from '@angular/material/button';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatSelect, MatSelectModule } from '@angular/material/select';
import { MatTabsModule } from '@angular/material/tabs';
import { AppService, CalendarRef, CalendarService, DetailActions, DetailPage, errorMessage, Properties } from 'jde-framework';
import { HistGroupMember, histNodeText } from '../../../model/hist-admin';
import { msPerMinute, OeeIdealMode, oeeIdealText, OeeMachine, OeeMachineProps, OeeMachineStatus, OeeMutationResult, OeeRole, oeeRunningText, OeeSignal, oeeStatusText, oeeWidthMinutes, parseOeeIdeal, parseOeeRunning, toOeeMutationResult } from '../../../model/oee-admin';
import { Gateway } from '../../../services/gateway-service';
import { HistAdminService } from '../../../services/hist-admin-service';
import { OeeAdminService, OeeGroup } from '../../../services/oee-admin-service';
import { GatewayDetailData } from '../../../services/resolvers/client-resolver';
import { HistMembers } from '../hist-admin/hist-members';
import { readOeeCalendars } from './oee-calendars';

//The fields the form leaves out:  the group, the ideal, the interval and the calendar have controls of their own below it, and
//`processed` is the engine's.  A constant, as the binding is read each check and a new array has Properties rebuild its fields.
const excluded = ['histGroupId', 'calendarId', 'idealCycle', 'width', 'processed'];
const units = { smallStop: 'ms', idleTimeout: 'ms' };
const hints = { smallStop: 'A stop shorter than this is a small stop, a performance loss:  300000 is five minutes.', idleTimeout: 'Without a State signal, how long without a count before the machine is stopped.  Blank with one.', countUnit: 'What a count is:  bottles, cases.' };
//a role as the Signals tab says it
const roles:{role:OeeRole; hint:string}[] = [
	{ role: 'State', hint: 'Running or stopped:  a Boolean, or a code with the values that mean running.' },
	{ role: 'Total', hint: 'A counter of every unit made.' },
	{ role: 'Rate', hint: 'A rate, integrated into the count - in place of Total, not beside it.' },
	{ role: 'Good', hint: 'A counter of the good units.  Without it, good is Total less Reject.' },
	{ role: 'Reject', hint: 'A counter of the rejects.' },
	{ role: 'Reason', hint: "The stop's reason code, which a reason's Code names." }
];
const defaultRunning = '[true]';//what a State with no list runs on
const ratePeriods = [ {ms: 1000, label: 'A second'}, {ms: msPerMinute, label: 'A minute'}, {ms: 3600000, label: 'An hour'} ];

//A machine's page (#404, docs/oee/spec.md *Machines*, *Web*):  on Properties its row - the name, slug and description, its group,
//fixed once created, the ideal as a cycle time or a rate, stored as the cycle, the small-stop threshold, the idle timeout, the
//interval and the calendar, the application server's - saved as a group's is, a refusal said under the form.  Signals gives the
//group's members their roles, each pick its own mutation, applied as it is made, with the warnings its answer carries beside it,
//says how the engine stands, and has Recompute, which makes a range of the machine's rows again.  The gateway is the row's, from
//HistAdminResolver.
@Component({
	templateUrl: './oee-machine-detail.html',
	styleUrls: ['../hist-admin/hist-admin-shared.scss', './oee-machine-detail.scss'],
	//the trailing class is load-bearing:  Angular hashes a component's *shape* into its style-encapsulation id and leaves the class
	//name out, so the routed pages that share DetailPage and this host string could collide with NG0912.
	host: {class: 'main-content my-content oee-machine-detail'},
	imports: [DetailActions, MatButtonModule, MatFormFieldModule, MatIconModule, MatInputModule, MatProgressBarModule, MatSelectModule, MatTabsModule, Properties, RouterLink]
})
export class OeeMachineDetail extends DetailPage<OeeMachine>{
	constructor(){ super( 'oee-machine-detail' ); }

	protected override get ctor(){ return OeeMachine; }
	protected override onRow(){
		const gateway = (<GatewayDetailData<OeeMachine>>this.pageData).gateway;
		if( gateway.slug!=this.gateway?.slug ){//another gateway's lists, which #loadLists leaves where its reads fail
			this.groups.set( [] );
			this.calendars.set( undefined );
			this.calendarsUrl.set( undefined );
		}
		this.gateway = gateway;
		this.signals.rows.set( [] );
		this.signals.error.set( undefined );
		this.members.set( [] );
		this.warnings.set( {} );
		this.status.set( undefined );
		this.recompute.set( false );
		this.idealTyped.set( undefined );
		this.recomputeFrom.set( '' );
		this.recomputeTo.set( '' );
		this.#loadLists();
		if( this.row.id )
			this.#read( true );
	}
	protected override upsert():OeeMachine{ return new OeeMachine( <OeeMachineProps>this.properties() ); }
	protected override get title(){ return this.row.name ? `${this.row.name} - Machine` : 'New Machine'; }
	protected override saveFailed( e:unknown ):void{//said on Properties alone, and Save is on every tab
		this.#refusal.set( {of: this.properties(), text: errorMessage(e)} );
		this.tabIndex.set( 0 );
	}
	protected override get purgeWarning(){ return 'Its stops, intervals, codings and signals go with it, and it leaves its lines.'; }
	override get ql(){ return this.gateway; }//per-gateway, as ClientDetail's
	//what the gateway warns of in its answer:  an idle timeout changed on a machine whose count is its state
	protected override saved( answers:unknown ):void{
		const warnings = ( Array.isArray( answers ) ? answers : [answers] ).flatMap( a=>toOeeMutationResult( a ).warnings );
		if( warnings.length )
			this.snackbar.warn( warnings.join( '  ' ) );
	}

	readonly fieldOrder = ['slug', 'name', 'description', 'countUnit', 'smallStop', 'idleTimeout'];
	readonly excludedFields = excluded;
	readonly units = units;
	readonly hints = hints;
	readonly roles = roles;
	get machine():OeeMachine{ return <OeeMachine>this.properties(); }
	//the server's refusal of the last Save, said while the form is as it was sent:  an edit answers it
	#refusal = signal<{of:unknown; text:string}|undefined>( undefined );
	saveError = computed( ()=>this.#refusal()?.of===this.properties() ? this.#refusal()!.text : undefined );
	groups = signal<OeeGroup[]>( [] );
	calendars = signal<CalendarRef[]|undefined>( undefined );//the application server's;  undefined until it has answered, or when it can't
	calendarsUrl = signal<string[]|undefined>( undefined );//the page the calendars are a tab of:  a hub's own, else the application server's
	idealMode = signal<OeeIdealMode>( 'cycle' );
	idealTyped = signal<string|undefined>( undefined );//the ideal as typed, until the row or the mode changes:  the cycle read back would rewrite it mid-number

	//the selects' lists:  the gateway's groups, and the application server's calendars with where their pages are
	async #loadLists():Promise<void>{
		const gateway = this.gateway;
		this.#admin.groups( gateway ).then( groups=>{ if( gateway==this.gateway ) this.groups.set( groups ); }, e=>this.snackbar.exception( 'Could not list the groups.', e ) );
		try{
			const { calendars, url } = await readOeeCalendars( this.#app, this.#calendarService, gateway.slug );
			if( gateway!=this.gateway )
				return;
			this.calendars.set( calendars );
			this.calendarsUrl.set( url );
		}
		catch( e ){//the calendar shows by its id then
			console.warn( 'Could not list the calendars.', e );
		}
	}
	//the signals and how the engine stands, and with `withMembers` the group's members they pick from, read as the group's page
	//reads them:  a signal's edit leaves the members as they were
	async #read( withMembers:boolean ):Promise<void>{
		const id = this.row?.id, groupId = this.row?.histGroupId, gateway = this.gateway;
		if( !id )
			return;
		await this.signals.read( ()=>Promise.all( [this.#admin.signals( gateway, id ), !withMembers ? undefined : groupId ? this.#hist.groupMembers( gateway, groupId ) : [], this.#admin.status( gateway, id ).catch( ()=>undefined )] ),
			( [signals, members, status] )=>{ this.signals.rows.set( signals ); if( members ) this.members.set( members ); this.status.set( status ); },
			()=>this.row?.id==id && gateway==this.gateway );
	}

	//Properties
	groupLink():string[]|undefined{
		const slug = this.groups().find( g=>g.id==this.machine?.histGroupId )?.slug;
		return slug ? ['/apps/gateways', this.gateway.slug, 'groups', slug] : undefined;
	}
	//the calendar's page, once the calendars are read and the row's is among them
	calendarLink():string[]|undefined{
		const slug = this.calendars()?.find( c=>c.id==this.machine?.calendarId )?.slug, url = this.calendarsUrl();
		return slug && url ? [...url, slug] : undefined;
	}
	//the row's calendar where the application server's list lacks it - deleted there, or the list unread
	missingCalendar():number|undefined{
		const id = this.machine?.calendarId;
		return id && !this.calendars()?.some( c=>c.id==id ) ? id : undefined;
	}
	//the widths offered, in minutes, with the row's own where it is another the engine takes
	widths = computed( ()=>{
		const own = (this.machine?.width ?? msPerMinute)/msPerMinute;
		return oeeWidthMinutes.includes( own ) ? oeeWidthMinutes : [...oeeWidthMinutes, own].sort( ( a, b )=>a-b );
	} );
	idealText():string{ return this.idealTyped() ?? oeeIdealText( this.machine?.idealCycle, this.idealMode() ); }
	//the ideal in the other form, under the field
	idealHint():string{
		const cycle = this.machine?.idealCycle;
		if( this.machine?.idealInvalid )
			return 'A number above zero, or blank to measure no performance.';
		if( cycle==undefined )
			return 'Blank measures no performance.';
		return this.idealMode()=='cycle' ? `${oeeIdealText( cycle, 'rate' )} a minute` : `A cycle of ${oeeIdealText( cycle, 'cycle' )} s`;
	}
	//the stored cycle in the other form:  what was typed and refused goes with it
	setIdealMode( mode:OeeIdealMode ):void{
		this.idealMode.set( mode );
		this.idealTyped.set( undefined );
		if( this.machine?.idealInvalid )
			this.#edit( m=>{ m.idealInvalid = false; } );
	}
	setIdeal( text:string ):void{
		this.idealTyped.set( text );
		const cycle = parseOeeIdeal( text, this.idealMode() );
		const invalid = Number.isNaN( cycle );
		this.#edit( m=>{ m.idealInvalid = invalid; if( !invalid ) m.idealCycle = cycle; } );
	}
	setGroup( id:number ):void{ this.#edit( m=>{ m.histGroupId = id; m.group = this.groups().find( g=>g.id==id ); } ); }
	setCalendar( id:number ):void{ this.#edit( m=>{ m.calendarId = id || undefined; } ); }
	setWidth( minutes:number ):void{ this.#edit( m=>{ m.width = minutes*msPerMinute; } ); }
	//a new row each change, so the page's dirty check sees it
	#edit( change:( m:OeeMachine )=>void ):void{
		const m = new OeeMachine( <OeeMachineProps>this.properties() );
		change( m );
		this.properties.set( m );
	}

	//Signals
	signals = new HistMembers<OeeSignal>( this.snackbar );//its error is also the gateway's refusal of a pick
	members = signal<HistGroupMember[]>( [] );
	warnings = signal<Partial<Record<OeeRole,string[]>>>( {} );//what the last edit of each role's signal answered
	status = signal<OeeMachineStatus|undefined>( undefined );
	statusText = computed( ()=>{ const s = this.status(); return s ? oeeStatusText( s ) : undefined; } );
	recompute = signal( false );//an edit answered `recompute`:  the rows before now were made with the old input
	signalOf( role:OeeRole ):OeeSignal|undefined{ return this.signals.rows().find( s=>s.role==role ); }
	memberLabel( m:HistGroupMember ):string{ return m.attributes.displayName || histNodeText( m.node ); }
	runningText( s:OeeSignal ):string{ return oeeRunningText( s.running ); }
	//the periods a Rate's select offers, with its own where it is another the column holds
	ratePeriodsOf( s:OeeSignal ):{ms:number; label:string}[]{
		const own = s.ratePeriod;
		return !own || ratePeriods.some( p=>p.ms==own ) ? ratePeriods
			: [...ratePeriods, {ms: own, label: own%1000 ? `${own} ms` : `${own/1000} seconds`}].sort( ( a, b )=>a.ms-b.ms );
	}
	//A role's member picked, changed, or taken away, 0 for none:  the signal's create, update or purge, sent at once.  A refusal - a
	//Rate beside a Total, the last signal of a machine with no idle timeout - is said on the tab, and the select goes back.
	async setSignal( role:OeeRole, nodeId:number, select?:Pick<MatSelect,'writeValue'> ):Promise<void>{
		const current = this.signalOf( role ), gateway = this.gateway;
		if( (current?.nodeId ?? 0)==nodeId )
			return;
		await this.#send( role, ()=>!nodeId ? this.#admin.purgeSignal( gateway, current!.id ).then( ()=>undefined )
			: current ? this.#admin.updateSignal( gateway, current.id, {histGroupNodeId: nodeId} )
			: this.#admin.createSignal( gateway, this.row.id, role, nodeId ),
			()=>select?.writeValue( this.signalOf( role )?.nodeId ?? 0 ) );
	}
	//a State's running values, as typed:  `true`, or `2, 3` for a code
	async setRunning( s:OeeSignal, text:string, input?:{value:string} ):Promise<void>{
		const running = parseOeeRunning( text );
		if( running===undefined )
			return this.signals.error.set( `State:  '${text}' is no list of values - true, or 2, 3 for a code.` );
		if( (running ?? defaultRunning)!=(s.running ?? defaultRunning) )
			await this.#send( s.role, ()=>this.#admin.updateSignal( this.gateway, s.id, {running} ), ()=>{ if( input ) input.value = this.runningText( this.signalOf( s.role ) ?? s ); } );
	}
	async setRatePeriod( s:OeeSignal, ratePeriod:number, select?:Pick<MatSelect,'writeValue'> ):Promise<void>{
		if( ratePeriod!=s.ratePeriod )
			await this.#send( s.role, ()=>this.#admin.updateSignal( this.gateway, s.id, {ratePeriod} ), ()=>select?.writeValue( (this.signalOf( s.role ) ?? s).ratePeriod ) );
	}
	//An edit of `role`'s signal and the read after it, with `busy` held across both.  A refusal puts the control back, to the value
	//the read found, since its binding's value is the same as before and Angular writes nothing.  The answer is the row's it was
	//sent for:  on another the router has opened since, it is dropped.
	async #send( role:OeeRole, act:()=>Promise<OeeMutationResult|undefined>, putBack:()=>void ):Promise<void>{
		const id = this.row.id, gateway = this.gateway, current = ()=>this.row?.id==id && this.gateway==gateway;
		this.signals.busy.set( true );
		let refusal:string|undefined;
		try{
			const answer = await act();
			if( current() ){
				this.warnings.update( w=>({...w, [role]: answer?.warnings ?? []}) );
				if( answer?.recompute )
					this.recompute.set( true );
			}
		}
		catch( e ){
			refusal = `${role}:  ${errorMessage(e)}`;
		}
		if( current() )
			await this.#read( false );
		if( refusal && current() ){
			this.signals.error.set( refusal );//after the read, which clears it
			putBack();
		}
		this.signals.busy.set( false );
	}

	//Recompute:  the range as the browser's wall clock.  A blank To is now when Recompute is pressed:  a computed would keep the
	//moment From was typed.
	recomputeFrom = signal( '' );
	recomputeTo = signal( '' );
	recomputeRange = computed<{start:Date; end?:Date}|undefined>( ()=>{
		const start = new Date( this.recomputeFrom() ), end = this.recomputeTo() ? new Date( this.recomputeTo() ) : undefined;
		return Number.isFinite( start.getTime() ) && (!end || (Number.isFinite( end.getTime() ) && start<end)) ? {start, end} : undefined;
	} );
	async recomputeRows():Promise<void>{
		const range = this.recomputeRange();
		if( !range )
			return;
		const span = {start: range.start, end: range.end ?? new Date()};
		if( span.start>=span.end )
			return this.signals.error.set( 'Recompute:  From is after now.  Give a To, or a From before now.' );
		await this.signals.mutate( async ()=>{
			const queued = await this.#admin.recompute( this.gateway, this.row.id, span.start, span.end );
			this.recompute.set( false );
			this.snackbar.info( `Recompute queued:  ${queued.start.toLocaleString()} to ${queued.end.toLocaleString()}.` );
		}, 'Could not recompute.' );
	}
	#admin = inject( OeeAdminService );
	#hist = inject( HistAdminService );
	#calendarService = inject( CalendarService );
	#app = inject( AppService );
	gateway!:Gateway;
}