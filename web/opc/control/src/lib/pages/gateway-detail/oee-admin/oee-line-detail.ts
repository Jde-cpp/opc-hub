import { Component, computed, inject, signal } from '@angular/core';
import { RouterLink } from '@angular/router';
import { MatButtonModule } from '@angular/material/button';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatSelect, MatSelectModule } from '@angular/material/select';
import { MatTableModule } from '@angular/material/table';
import { MatTabsModule } from '@angular/material/tabs';
import { MatTooltipModule } from '@angular/material/tooltip';
import { AppService, CalendarService, DetailActions, DetailPage, errorMessage, PickOption, Properties } from 'jde-framework';
import { oeeLevels, OeeLine, OeeLineMember, OeeLineProps, oeeMemberCandidates, oeeMembersOf, OeePlant } from '../../../model/oee-lines';
import { Gateway } from '../../../services/gateway-service';
import { OeeAdminService } from '../../../services/oee-admin-service';
import { OeeService } from '../../../services/oee-service';
import { GatewayDetailData } from '../../../services/resolvers/client-resolver';
import { HistMembers } from '../hist-admin/hist-members';
import { OeeCalendarField, OeeCalendarList } from './oee-calendars';

const excluded = ['calendarId'];//its control is below the form
const picks:Record<string,PickOption[]> = { level: oeeLevels.map( l=>({value: l, label: l}) ) };
const hints = { level: "Its place in the plant:  a site, an area, a line or a cell.  A member line's is no higher than its own.  It changes no figure." };
//a member as its row says it:  what it is, its page where it is still there, and its place
type MemberRow = { member:OeeLineMember; name:string; kind:string; link?:string[]; first:boolean; last:boolean };

//A line's page (#409, docs/oee/spec.md *Lines*, *Web*):  on Properties its row - the name, slug and description, its level and
//its calendar, the application server's - saved as a machine's is, a refusal said under the form.  Members lists its machines
//and lines in their order, each added, moved and removed by its own mutation, applied as it is made:  the picker offers what
//neither the line nor a line above it reaches, and the lines of a level no higher than its own that it may hold
//(oeeMemberCandidates), and what else the gateway refuses - a right withheld, an edit by another hand - is said on the tab.
//The gateway is the row's, from HistAdminResolver.
@Component({
	templateUrl: './oee-line-detail.html',
	styleUrls: ['../hist-admin/hist-admin-shared.scss', './oee-machine-detail.scss', './oee-line-detail.scss'],
	//the trailing class is load-bearing:  Angular hashes a component's *shape* into its style-encapsulation id and leaves the class
	//name out, so the routed pages that share DetailPage and this host string could collide with NG0912.
	host: {class: 'main-content my-content oee-line-detail'},
	imports: [DetailActions, MatButtonModule, MatFormFieldModule, MatIconModule, MatProgressBarModule, MatSelectModule, MatTableModule, MatTabsModule, MatTooltipModule, OeeCalendarField, Properties, RouterLink]
})
export class OeeLineDetail extends DetailPage<OeeLine>{
	constructor(){ super( 'oee-line-detail' ); }

	protected override get ctor(){ return OeeLine; }
	protected override onRow(){
		const gateway = (<GatewayDetailData<OeeLine>>this.pageData).gateway;
		this.gateway = gateway;
		this.#lineId.set( this.row.id ?? 0 );
		this.plant.set( undefined );
		this.members.error.set( undefined );
		this.calendars.load( gateway.slug );
		if( this.row.id )
			this.#read();
	}
	protected override upsert():OeeLine{ return new OeeLine( <OeeLineProps>this.properties() ); }
	protected override get title(){ return this.row.name ? `${this.row.name} - Line` : 'New Line'; }
	protected override saveFailed( e:unknown ):void{//said on Properties alone, and Save is on every tab
		this.#refusal.set( {of: this.properties(), text: errorMessage(e)} );
		this.tabIndex.set( 0 );
	}
	protected override get deleteWarning(){ return 'A deleted line has no tile, and is out of the figures of the lines it is in.  Restore brings it back.'; }
	protected override get purgeWarning(){ return 'Its memberships go with it:  its members leave it, and it leaves the lines it is in.'; }
	override get ql(){ return this.gateway; }//per-gateway, as ClientDetail's

	readonly fieldOrder = ['slug', 'name', 'description', 'level'];
	readonly excludedFields = excluded;
	readonly picks = picks;
	readonly hints = hints;
	get line():OeeLine{ return <OeeLine>this.properties(); }
	//the server's refusal of the last Save, said while the form is as it was sent:  an edit answers it
	#refusal = signal<{of:unknown; text:string}|undefined>( undefined );
	saveError = computed( ()=>this.#refusal()?.of===this.properties() ? this.#refusal()!.text : undefined );
	calendars = new OeeCalendarList( inject( AppService ), inject( CalendarService ) );
	setCalendar( id:number|undefined ):void{
		const line = new OeeLine( <OeeLineProps>this.properties() );//a new row, so the page's dirty check sees it
		line.calendarId = id || undefined;
		this.properties.set( line );
	}

	//Members
	members = new HistMembers<OeeLineMember>( this.snackbar );//its error is also the gateway's refusal of an edit
	plant = signal<OeePlant|undefined>( undefined );//the gateway's machines, lines and memberships, which the picker filters by
	#lineId = signal( 0 );
	readonly rows = computed<MemberRow[]>( ()=>{
		const plant = this.plant(), base = ['/apps/gateways', this.gateway?.slug];
		if( !plant )
			return [];
		return oeeMembersOf( plant, this.#lineId() ).map( ( member, i, all ):MemberRow=>{
			const machine = plant.machines.find( m=>m.id==member.machineId ), line = plant.lines.find( l=>l.id==member.memberLineId ), place = { first: i==0, last: i==all.length-1 };
			if( member.machineId!=undefined )
				return { member, name: machine?.name ?? `Machine ${member.machineId}, deleted`, kind: 'Machine', link: machine ? [...base, 'machines', machine.slug] : undefined, ...place };
			return { member, name: line?.name ?? `Line ${member.memberLineId}, deleted`, kind: line?.level || 'Line', link: line ? [...base, 'lines', line.slug] : undefined, ...place };
		} );
	} );
	readonly candidates = computed( ()=>{ const plant = this.plant(); return plant ? oeeMemberCandidates( plant, this.#lineId() ) : {machines: [], lines: []}; } );
	readonly memberColumns = ['place', 'name', 'kind', 'actions'];
	//the gateway's machines, lines and memberships:  read whole, since what the picker offers is by what every line holds
	async #read():Promise<void>{
		const id = this.row?.id, gateway = this.gateway;
		if( !id )
			return;
		await this.members.read( ()=>this.#service.plant( gateway, true ), plant=>this.plant.set( plant ), ()=>this.row?.id==id && gateway==this.gateway );
	}
	refresh():Promise<void>{ return this.#read(); }
	//A pick added after the members there are, `m<id>` a machine and `l<id>` a line;  the select goes back to none.
	async add( picked:string, select?:Pick<MatSelect,'writeValue'> ):Promise<void>{
		select?.writeValue( null );
		const id = Number( picked.slice( 1 ) ), pick = picked[0]=='m' ? {machine: id} : {line: id};
		const sort = Math.max( -1, ...this.rows().map( r=>r.member.sort ) )+1;
		await this.#edit( 'Add', ()=>this.#admin.addLineMember( this.gateway, this.row.id, pick, sort ) );
	}
	//A member moved one place up or down:  the members' `sort` made their places, each that isn't already its own update, since
	//rows added by another hand may share one and a removal leaves a gap.  Each sets its own row's, so they go together.
	async move( row:MemberRow, by:-1|1 ):Promise<void>{
		const order = this.rows().map( r=>r.member ), from = order.indexOf( row.member ), to = from+by, gateway = this.gateway;
		if( from<0 || to<0 || to>=order.length )
			return;
		order.splice( to, 0, ...order.splice( from, 1 ) );
		await this.#edit( 'Move', ()=>Promise.all( [...order.entries()].filter( ( [i, m] )=>m.sort!=i ).map( ( [i, m] )=>this.#admin.sortLineMember( gateway, m.id, i ) ) ) );
	}
	async remove( row:MemberRow ):Promise<void>{ await this.#edit( 'Remove', ()=>this.#admin.purgeLineMember( this.gateway, row.member.id ) ); }
	//An edit of the members and the read after it, with `busy` held across both.  A refusal is said on the tab, after the read,
	//which clears it.  On another row the router has opened since, the answer is dropped.
	async #edit( what:string, act:()=>Promise<unknown> ):Promise<void>{
		const id = this.row.id, gateway = this.gateway, current = ()=>this.row?.id==id && this.gateway==gateway;
		this.members.busy.set( true );
		let refusal:string|undefined;
		try{
			await act();
		}
		catch( e ){
			refusal = `${what}:  ${errorMessage(e)}`;
		}
		if( current() )
			await this.#read();
		if( refusal && current() )
			this.members.error.set( refusal );
		this.members.busy.set( false );
	}
	#admin = inject( OeeAdminService );
	#service = inject( OeeService );
	gateway!:Gateway;
}