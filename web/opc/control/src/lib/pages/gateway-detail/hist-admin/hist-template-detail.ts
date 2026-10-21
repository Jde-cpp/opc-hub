import { Component, computed, inject, signal } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatTableModule } from '@angular/material/table';
import { MatTabsModule } from '@angular/material/tabs';
import { MatTooltipModule } from '@angular/material/tooltip';
import { RouterLink } from '@angular/router';
import { confirm, DetailActions, DetailPage, errorMessage, Properties } from 'jde-framework';
import { histPathText, histThresholdArgs, histThresholdBlank, histThresholdColumns, histThresholdFieldText, HistThresholdFields, HistThresholdName, histThresholdNames, histThresholdProblem } from '../../../model/hist';
import { HistTemplate, HistTemplateGroup, HistTemplateMember, HistTemplateProps } from '../../../model/hist-admin';
import { Gateway } from '../../../services/gateway-service';
import { HistAdminService } from '../../../services/hist-admin-service';
import { GatewayDetailData } from '../../../services/resolvers/client-resolver';
import { HistMembers } from './hist-members';
import { HistNodePickerDialog } from './hist-node-picker-dialog';
import { HistThresholdFieldsEditor } from './hist-threshold-fields';

//A template's page (#337, spec *Templates*):  its row on Properties, saved as a connection's is, and its members on Members, each
//a browse path from a group's root with the thresholds it sets, edited one at a time - a member added resolves in every group of
//the template at once, one removed takes its node out of each - so each is its own mutation, applied as it is made, not held
//for Save.  Groups lists the groups made from it.  The gateway is the row's, from HistAdminResolver.
@Component({
	templateUrl: './hist-template-detail.html',
	styleUrls: ['./hist-admin-shared.scss', './hist-template-detail.scss'],
	//the trailing class is load-bearing:  Angular hashes a component's *shape* into its style-encapsulation id and leaves the class
	//name out, so the routed pages that share DetailPage and this host string could collide with NG0912.
	host: {class: 'main-content mat-drawer-container my-content hist-template-detail'},
	imports: [DetailActions, MatButtonModule, MatIconModule, MatProgressBarModule, MatTableModule, MatTabsModule, MatTooltipModule, Properties, RouterLink, HistThresholdFieldsEditor]
})
export class HistTemplateDetail extends DetailPage<HistTemplate>{
	constructor(){ super( 'hist-template-detail' ); }

	protected override get ctor(){ return HistTemplate; }
	protected override onRow(){
		this.gateway = (<GatewayDetailData<HistTemplate>>this.pageData).gateway;
		this.members.rows.set( [] );
		this.groups.set( [] );
		this.editing.set( undefined );
		this.members.error.set( undefined );
		if( this.row.id )
			this.refresh();
	}
	protected override upsert():HistTemplate{ return new HistTemplate( <HistTemplateProps>{...this.properties()} ); }
	protected override get title(){ return this.row.name ? `${this.row.name} - Template` : 'New Template'; }
	override get ql(){ return this.gateway; }//per-gateway, as ClientDetail's

	readonly fieldOrder = ['slug', 'name', 'description'];
	readonly columns = histThresholdColumns;
	readonly displayedColumns = ['path', ...histThresholdNames, 'actions'];
	//what a blank column of a member means:  the group's value, else the default
	readonly memberHints:Partial<Record<HistThresholdName,string>> = { exceptionDeviation: "Blank:  the group's deviation, else none.", exceptionDeviationFormat: "Blank:  the group's format, else AbsoluteValue.", minTimeInterval: "Blank:  the group's, else 0.", maxTimeInterval: "Blank:  the group's, else off.", stepped: "Blank:  the group's, else true." };
	get template(){ return this.row; }
	members = new HistMembers<HistTemplateMember>( this.snackbar );//its error is also the gateway's refusal of an edit
	groups = signal<HistTemplateGroup[]>( [] );
	editing = signal<HistTemplateMember|undefined>( undefined );
	fields = signal<HistThresholdFields>( histThresholdBlank );
	problem = computed( ()=>histThresholdProblem( this.fields() ) );
	changes = computed( ()=>{ const m = this.editing(); return m && !this.problem() ? histThresholdArgs( this.fields(), m.thresholds ) : {}; } );
	changed = computed( ()=>Object.keys( this.changes() ).length>0 );

	async refresh():Promise<void>{
		const id = this.row?.id, gateway = this.gateway;
		if( id )
			await this.members.read( ()=>Promise.all( [this.admin.templateMembers( gateway, id ), this.admin.templateGroups( gateway, id )] ),
				( [members, groups] )=>{ this.members.rows.set( members ); this.groups.set( groups ); },
				()=>this.row?.id==id && gateway==this.gateway );
	}
	pathText( m:HistTemplateMember ):string{ return histPathText( m.path ); }
	readonly cellText = histThresholdFieldText;//blank where a member leaves its group nodes to inherit
	edit( m:HistTemplateMember ):void{
		this.editing.set( m );
		this.fields.set( {...m.thresholds} );
		this.members.error.set( undefined );
	}
	cancelEdit():void{ this.editing.set( undefined ); }
	//the member's own thresholds changed:  the gateway re-resolves each group node made from it before answering
	async saveEdit():Promise<void>{
		const m = this.editing();
		const changes = this.changes();
		if( !m || !Object.keys( changes ).length || this.members.busy() )
			return;
		this.members.busy.set( true );
		this.members.error.set( undefined );
		try{
			await this.admin.updateTemplateMember( this.gateway, m.id, changes );
			this.editing.set( undefined );
			await this.refresh();
		}
		catch( e ){
			this.members.error.set( errorMessage(e) );
		}
		finally{
			this.members.busy.set( false );
		}
	}
	async remove( m:HistTemplateMember ):Promise<void>{
		const groups = this.groups().length;
		const deleted = this.groups().filter( g=>g.deleted ).length;
		const which = groups==1 ? `the ${deleted ? 'deleted ' : ''}group made from the template` : `the ${groups} groups made from the template${deleted ? `, ${deleted} of them deleted` : ''}`;
		const where = groups ? `, and its node leaves ${which}.  The history each kept of it stays on disk` : '';
		if( !await confirm( this.dialog, {title: `Remove ${this.pathText( m )}?`, message: `The member leaves the template${where}.`, confirm: 'Remove', destructive: true} ) )
			return;
		await this.members.mutate( async ()=>{
			await this.admin.purgeTemplateMember( this.gateway, m.id );
			if( this.editing()?.id==m.id )
				this.editing.set( undefined );
			await this.refresh();
		}, 'Could not remove the member.' );
	}
	//Members picked from a connection's tree, under a root of the user's choosing, since a template names no server:  each pick's path
	//from that root becomes a member.
	async add():Promise<void>{
		const gateway = this.gateway;
		let connections;
		try{
			connections = await this.admin.connections( gateway );
		}
		catch( e ){
			return this.snackbar.exception( 'Could not list the connections.', e );
		}
		const picks = await HistNodePickerDialog.open( this.dialog, { gateway, title: `Add members to ${this.row.name}`, mode: 'paths', connections, rootEditable: true } );
		if( !picks?.length )
			return;
		await this.members.add( picks, 'member', p=>histPathText( p.path ), p=>this.admin.createTemplateMember( gateway, this.row.id, p.path ) );
		await this.refresh();
	}
	groupLink( g:HistTemplateGroup ):string[]{ return ['/apps/gateways', this.gateway.slug, 'groups', g.slug]; }
	admin = inject( HistAdminService );
	gateway!:Gateway;
}