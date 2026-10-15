import { Component, computed, inject, signal } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatTableModule } from '@angular/material/table';
import { MatTabsModule } from '@angular/material/tabs';
import { MatTooltipModule } from '@angular/material/tooltip';
import { RouterLink } from '@angular/router';
import { confirm, DetailPage, errorText, Properties } from 'jde-framework';
import { deviationFormats, histPathText, histThresholdArgs, histThresholdBlank, histThresholdColumns, HistThresholdFields, HistThresholdName, histThresholdNames, histThresholdProblem } from '../../../model/hist';
import { HistTemplate, HistTemplateGroup, HistTemplateMember, HistTemplateProps } from '../../../model/hist-admin';
import { Gateway, GatewayService } from '../../../services/gateway-service';
import { HistAdminService } from '../../../services/hist-admin-service';
import { HistNodePickerDialog } from './hist-node-picker-dialog';
import { HistThresholdFieldsEditor } from './hist-threshold-fields';

//A template's page (#337, spec *Templates*):  its row on Properties, saved as a connection's is, and its members on Members, each
//a browse path from a group's root with the thresholds it sets, edited one at a time - a member added resolves in every group of
//the template at once, one removed takes its node out of each - so each is its own mutation, applied as it is made, not held
//for Save.  Groups lists the groups made from it.  The gateway is the url's, apps/gateways/<instance>/templates/<slug>.
@Component({
	templateUrl: './hist-template-detail.html',
	styleUrls: ['./hist-admin-shared.scss', './hist-template-detail.scss'],
	//the trailing class is load-bearing:  Angular hashes a component's *shape* into its style-encapsulation id and leaves the class
	//name out, so the routed pages that share DetailPage and this host string could collide with NG0912.
	host: {class: 'main-content mat-drawer-container my-content hist-template-detail'},
	imports: [MatButtonModule, MatIconModule, MatProgressBarModule, MatTableModule, MatTabsModule, MatTooltipModule, Properties, RouterLink, HistThresholdFieldsEditor]
})
export class HistTemplateDetail extends DetailPage<HistTemplate>{
	constructor(){
		super( 'hist-template-detail' );
		const segments = this.router.url.split( '?' )[0].split( '/' );
		this.instance = decodeURIComponent( segments[segments.length-3] ?? '' );
		this.#gateway = this.gatewayService.gateway( this.instance ).then( g=>this.gateway = g );
	}

	protected override get ctor(){ return HistTemplate; }
	protected override onRow(){
		this.members.set( [] );
		this.groups.set( [] );
		this.editing.set( undefined );
		this.error.set( undefined );
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
	members = signal<HistTemplateMember[]>( [] );
	groups = signal<HistTemplateGroup[]>( [] );
	loading = signal( false );
	busy = signal( false );
	error = signal<string|undefined>( undefined );//the gateway's refusal of the list, or of an edit
	editing = signal<HistTemplateMember|undefined>( undefined );
	fields = signal<HistThresholdFields>( histThresholdBlank );
	problem = computed( ()=>histThresholdProblem( this.fields() ) );
	changes = computed( ()=>{ const m = this.editing(); return m && !this.problem() ? histThresholdArgs( this.fields(), m.thresholds ) : {}; } );
	changed = computed( ()=>Object.keys( this.changes() ).length>0 );

	async refresh():Promise<void>{
		const id = this.row?.id;
		if( !id )
			return;
		this.loading.set( true );
		this.error.set( undefined );
		try{
			const gateway = await this.#gateway;
			const [members, groups] = await Promise.all( [this.admin.templateMembers( gateway, id ), this.admin.templateGroups( gateway, id )] );
			if( this.row?.id!=id )//the router reused the page for another template meanwhile
				return;
			this.members.set( members );
			this.groups.set( groups );
		}
		catch( e ){
			this.error.set( errorText( e ) ?? 'Unknown error' );
		}
		finally{
			this.loading.set( false );
		}
	}
	pathText( m:HistTemplateMember ):string{ return histPathText( m.path ); }
	//a member's own column, or blank where it leaves a group node to inherit
	cellText( m:HistTemplateMember, name:HistThresholdName ):string{
		const t = m.thresholds[name];
		return t==='' ? '' : name=='exceptionDeviationFormat' ? deviationFormats[Number( t )] ?? t : t;
	}
	edit( m:HistTemplateMember ):void{
		this.editing.set( m );
		this.fields.set( {...m.thresholds} );
		this.error.set( undefined );
	}
	cancelEdit():void{ this.editing.set( undefined ); }
	//the member's own thresholds changed:  the gateway re-resolves each group node made from it before answering
	async saveEdit():Promise<void>{
		const m = this.editing();
		const changes = this.changes();
		if( !m || !Object.keys( changes ).length || this.busy() )
			return;
		this.busy.set( true );
		this.error.set( undefined );
		try{
			await this.admin.updateTemplateMember( await this.#gateway, m.id, changes, msg=>console.log( msg ) );
			this.editing.set( undefined );
			await this.refresh();
		}
		catch( e ){
			this.error.set( errorText( e ) ?? 'Unknown error' );
		}
		finally{
			this.busy.set( false );
		}
	}
	async remove( m:HistTemplateMember ):Promise<void>{
		const groups = this.groups().length;
		const where = groups ? `, and its node leaves each of the ${groups} ${groups==1 ? 'group' : 'groups'} made from the template.  The history each kept of it stays on disk` : '';
		if( !await confirm( this.dialog, {title: `Remove ${this.pathText( m )}?`, message: `The member leaves the template${where}.`, confirm: 'Remove', destructive: true} ) )
			return;
		this.busy.set( true );
		try{
			await this.admin.purgeTemplateMember( await this.#gateway, m.id, msg=>console.log( msg ) );
			if( this.editing()?.id==m.id )
				this.editing.set( undefined );
			await this.refresh();
		}
		catch( e ){
			this.snackbar.exception( 'Could not remove the member.', e );
		}
		finally{
			this.busy.set( false );
		}
	}
	//Members picked from a connection's tree, under a root of the user's choosing, since a template names no server:  each pick's path
	//from that root becomes a member.  One refused - the same path again, a right withheld - doesn't stop the rest;  the refusals are
	//said together.
	async add():Promise<void>{
		const gateway = await this.#gateway;
		let connections;
		try{
			connections = await this.admin.connections( gateway, msg=>console.log( msg ) );
		}
		catch( e ){
			return this.snackbar.exception( 'Could not list the connections.', e );
		}
		const picks = await HistNodePickerDialog.open( this.dialog, { gateway, title: `Add members to ${this.row.name}`, mode: 'paths', connections, rootEditable: true } );
		if( !picks?.length )
			return;
		this.busy.set( true );
		const refusals:string[] = [];
		try{
			for( const p of picks ){
				try{
					await this.admin.createTemplateMember( gateway, this.row.id, p.path, {}, msg=>console.log( msg ) );
				}
				catch( e ){
					refusals.push( `${histPathText( p.path )}:  ${errorText( e ) ?? 'Unknown error'}` );
				}
			}
		}
		finally{
			this.busy.set( false );
		}
		if( refusals.length )
			this.snackbar.error( `${picks.length-refusals.length} of ${picks.length} added.  ${refusals.slice( 0, HistTemplateDetail.refusalsShown ).join( '  ' )}` );
		else
			this.snackbar.info( `${picks.length} ${picks.length==1 ? 'member' : 'members'} added.` );
		await this.refresh();
	}
	groupLink( g:HistTemplateGroup ):string[]{ return ['/apps/gateways', this.instance, 'groups', g.slug]; }
	static readonly refusalsShown = 3;
	gatewayService = inject( GatewayService );
	admin = inject( HistAdminService );
	gateway!:Gateway;
	instance:string;
	#gateway:Promise<Gateway>;
}