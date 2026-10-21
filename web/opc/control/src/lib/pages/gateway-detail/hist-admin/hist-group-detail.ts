import { Component, computed, inject, signal } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatSelectModule } from '@angular/material/select';
import { MatTableModule } from '@angular/material/table';
import { MatTabsModule } from '@angular/material/tabs';
import { MatTooltipModule } from '@angular/material/tooltip';
import { confirm, DetailActions, DetailPage, Properties } from 'jde-framework';
import { histPathText, histThresholdColumns, histThresholdFieldText, HistThresholdFields, HistThresholdName, histThresholdNames } from '../../../model/hist';
import { HistConnection, HistGroup, HistGroupMember, HistGroupProps, histNodeLabel, histNodeText, HistRef, HistResolvedMember, HistTemplateMember } from '../../../model/hist-admin';
import { ExNodeId } from '../../../model/ex-node-id';
import { OpcError } from '../../../model/opc-error';
import { Server } from '../../../model/server';
import { StatusCode } from '../../../model/types';
import { Gateway } from '../../../services/gateway-service';
import { HistAdminService } from '../../../services/hist-admin-service';
import { HistoryService } from '../../../services/history-service';
import { GatewayDetailData } from '../../../services/resolvers/client-resolver';
import { OpcStore } from '../../../services/opc-store';
import { HistThresholdsDialog } from '../../node-detail/node-history/hist-thresholds-dialog';
import { HistGroupAccess } from './hist-group-access';
import { HistMembers } from './hist-members';
import { HistNodePickerDialog } from './hist-node-picker-dialog';
import { HistThresholdFieldsEditor } from './hist-threshold-fields';

//A template member's resolution as the page shows it:  resolved to a node, or not and why, from the rows and the last resolve
export type HistResolutionRow = { templateNode:number; path:string; node?:ExNodeId; status?:StatusCode; holder?:HistRef };
//The fields the form leaves out:  the source and the thresholds have controls of their own below it, and the guid is read, never
//typed, so a new row hides it.  Constants, as the binding is read each check and a new array has Properties rebuild its fields.
const excluded = ['root', ...histThresholdNames];
const excludedNew = [...excluded, 'guid'];

//A group's page (#337, spec *Templates*):  on Properties its row - the name, slug and description, its source, one connection with a
//template applied from a root or none for an ad hoc node list, fixed once created, and its thresholds, the defaults for members
//that set none - saved as a connection's is.  Members lists the group's nodes, each with its template member and the columns
//of its own, adds nodes picked from the connection's tree and removes one, each its own mutation, and opens the Thresholds dialog
//for the effective values and overrides (#338).  Access (#339) grants roles on the group's own resource and says which row governs
//it.  Resolution, last as only a group with a template has it, shows each member's node or why it has none, with Resolve, which
//retries the unresolved through resolveHistory.  The gateway is the row's, from HistAdminResolver.
@Component({
	templateUrl: './hist-group-detail.html',
	styleUrls: ['./hist-admin-shared.scss', './hist-group-detail.scss'],
	//the trailing class is load-bearing:  Angular hashes a component's *shape* into its style-encapsulation id and leaves the class
	//name out, so the routed pages that share DetailPage and this host string could collide with NG0912.
	host: {class: 'main-content mat-drawer-container my-content hist-group-detail'},
	imports: [DetailActions, MatButtonModule, MatFormFieldModule, MatIconModule, MatInputModule, MatProgressBarModule, MatSelectModule, MatTableModule, MatTabsModule, MatTooltipModule, Properties, HistGroupAccess, HistThresholdFieldsEditor]
})
export class HistGroupDetail extends DetailPage<HistGroup>{
	constructor(){ super( 'hist-group-detail' ); }

	protected override get ctor(){ return HistGroup; }
	protected override onRow(){
		this.gateway = (<GatewayDetailData<HistGroup>>this.pageData).gateway;
		this.members.rows.set( [] );
		this.templateMembers.set( [] );
		this.resolved.set( undefined );
		this.members.error.set( undefined );
		this.#loadLists();
		if( this.row.id )
			this.refresh();
	}
	protected override upsert():HistGroup{ return new HistGroup( <HistGroupProps>this.properties() ); }
	protected override get title(){ return this.row.name ? `${this.row.name} - Group` : 'New Group'; }
	override get ql(){ return this.gateway; }//per-gateway, as ClientDetail's

	readonly fieldOrder = ['slug', 'name', 'description', 'guid'];
	readonly fieldLabels = { guid: 'Guid' };
	get excludedFields():string[]{ return this.isNew ? excludedNew : excluded; }
	readonly columns = histThresholdColumns;
	readonly memberColumns = ['node', 'member', ...histThresholdNames, 'actions'];
	readonly resolutionColumns = ['path', 'node', 'status'];
	//what a blank default means:  the library's own (spec *Thresholds*)
	readonly thresholdHints:Partial<Record<HistThresholdName,string>> = { exceptionDeviation: 'Blank:  none, every change is stored.', exceptionDeviationFormat: 'Blank:  AbsoluteValue.', minTimeInterval: 'Blank:  0, as fast as the server samples.', maxTimeInterval: 'Blank:  off, no heartbeat.', stepped: 'Blank:  true, a value holds until the next.' };
	get group():HistGroup{ return <HistGroup>this.properties(); }
	connections = signal<HistConnection[]>( [] );
	templates = signal<HistRef[]>( [] );
	server = signal<Server|undefined>( undefined );//the group's connection, for its namespaces:  a node shown by the server's index
	members = new HistMembers<HistGroupMember>( this.snackbar );//its loading and busy are the Resolution tab's too
	templateMembers = signal<HistTemplateMember[]>( [] );
	resolved = signal<Map<number,HistResolvedMember>|undefined>( undefined );//the last resolve's answer, by template member

	//the selects' lists, and the server behind the group's connection
	async #loadLists():Promise<void>{
		const gateway = this.gateway;
		try{
			const [connections, templates] = await Promise.all( [this.admin.connections( gateway ), this.admin.templates( gateway )] );
			if( gateway!=this.gateway )//the router reused the page for another gateway's group meanwhile
				return;
			this.connections.set( connections );
			this.templates.set( templates );
			this.#loadServer();
		}
		catch( e ){
			this.snackbar.exception( 'Could not list the connections and templates.', e );
		}
	}
	async #loadServer():Promise<void>{
		const slug = this.connectionSlug();
		if( !slug )
			return;
		const gateway = this.gateway;
		try{
			const server = await this.opcStore.getConnection( gateway, slug );
			if( gateway==this.gateway )
				this.server.set( server );
		}
		catch( e ){//the nodes show by URI then
			console.warn( `Could not describe '${slug}'.`, e );
		}
	}
	connectionSlug():string|undefined{
		const id = this.group?.serverConnectionId;
		return this.connections().find( c=>c.id==id )?.slug;
	}
	async refresh():Promise<void>{
		const id = this.row?.id, templateId = this.row?.histTemplateId, gateway = this.gateway;
		if( id )
			await this.members.read( ()=>Promise.all( [this.admin.groupMembers( gateway, id ), templateId ? this.admin.templateMembers( gateway, templateId ) : Promise.resolve( [] )] ),
				( [members, templateMembers] )=>{ this.members.rows.set( members ); this.templateMembers.set( templateMembers ); },
				()=>this.row?.id==id && gateway==this.gateway );
	}

	//Members
	//a node by the server's index where the server lists its URI, as a node page names it, else by URI
	nodeLabel( node:ExNodeId ):string{
		const ns = node.nsu ? this.server()?.namespaces.find( n=>n.uri==node.nsu )?.index : node.ns;
		return histNodeLabel( node, ns );
	}
	nodeDetail( node:ExNodeId ):string{ return histNodeText( node ); }
	//the template member a node was resolved from, by its path, or that it was added by hand
	memberText( m:HistGroupMember ):string{
		if( m.templateNodeId==undefined )
			return 'added by hand';
		const t = this.templateMembers().find( t=>t.id==m.templateNodeId );
		return t ? histPathText( t.path ) : `member ${m.templateNodeId}`;
	}
	readonly cellText = histThresholdFieldText;//blank where a node inherits its template member's, then the group's
	//Nodes picked from the connection's tree, from the group's root, each joining by its URI.  A node another group on the connection
	//holds is refused naming that group (spec *Templates*).
	async addNodes():Promise<void>{
		const gateway = this.gateway;
		const slug = this.connectionSlug();
		if( !slug )
			return this.snackbar.error( "The group's connection isn't among the gateway's." );
		const picks = await HistNodePickerDialog.open( this.dialog, { gateway, title: `Add nodes to ${this.row.name}`, mode: 'nodes', connections: this.connections(), connection: slug, root: this.row.rootNode } );
		if( !picks?.length )
			return;
		await this.members.add( picks, 'node', p=>p.name, p=>this.admin.createGroupMember( gateway, this.row.id, p.node ) );
		this.resolved.set( undefined );//the rows changed under it:  a member's reason may not hold now
		await this.refresh();
	}
	async removeNode( m:HistGroupMember ):Promise<void>{
		const name = this.nodeLabel( m.node );
		const member = m.templateNodeId!=undefined ? '  Its template member stays, and Resolve would add the node again.' : '';
		if( !await confirm( this.dialog, {title: `Remove ${name}?`, message: `The node leaves the group:  the history kept of it stays on disk, and another group on the connection may take it.${member}`, confirm: 'Remove', destructive: true} ) )
			return;
		await this.members.mutate( async ()=>{
			await this.admin.purgeGroupMember( this.gateway, m.id );
			this.resolved.set( undefined );
			await this.refresh();
		}, 'Could not remove the node.' );
	}
	//each member's effective thresholds with where each comes from, and the overrides (#338):  the members read again after, since
	//an override is a column of a row shown here
	openThresholds():void{
		const id = this.row.id;
		HistThresholdsDialog.open( this.dialog, { group: id, name: this.row.name, nodes: [],
			load: ()=>this.history.thresholds( this.gateway, id ),
			save: ( rowId, changes )=>this.history.override( this.gateway, rowId, changes )
		} ).then( ()=>this.refresh() );
	}

	//Resolution:  the template's members against the group's rows.  A member with a row is resolved;  the last resolve's answer
	//only says why one without a row has none, so a success it reported for a row since removed can't outrank the rows.
	resolution = computed<HistResolutionRow[]>( ()=>{
		const byMember = new Map<number,HistGroupMember>();
		for( const m of this.members.rows() ){
			if( m.templateNodeId!=undefined )
				byMember.set( m.templateNodeId, m );
		}
		const last = this.resolved();
		return this.templateMembers().map( t=>{
			const m = byMember.get( t.id );
			const answer = m ? undefined : last?.get( t.id );
			const r = answer?.status!==0 ? answer : undefined;
			return { templateNode: t.id, path: histPathText( t.path ), node: m?.node ?? r?.node, status: m ? 0 : r?.status, holder: r?.holder };
		} );
	} );
	unresolved = computed( ()=>this.resolution().filter( r=>r.status!==0 ).length );
	statusKind( r:HistResolutionRow ):'ok'|'bad'|'pending'{ return r.status===0 ? 'ok' : r.status===undefined ? 'pending' : 'bad'; }
	statusText( r:HistResolutionRow ):string{
		if( r.status===undefined )
			return 'Not resolved.  Resolve retries it.';
		if( r.status===0 )
			return 'Resolved';
		const why = OpcError.text( r.status );
		if( r.holder )
			return r.holder.id==this.row.id ? `${why}:  this group holds the node already, as another member or a node added by hand.` : r.holder.name ? `${why}:  group '${r.holder.name}' holds the node.` : `${why}:  group ${r.holder.id} holds the node.`;//no name:  a group the caller may not read
		return why;
	}
	//resolveHistory:  the members with no row are tried again on the connection, and every member answers
	async resolve():Promise<void>{
		await this.members.mutate( async ()=>{
			const answer = await this.admin.resolve( this.gateway, this.row.id );
			this.resolved.set( new Map( answer.map( a=>[a.templateNode, a] ) ) );
			await this.refresh();
			await this.gateway.updateErrorCodes?.();//the statuses' names, before the rows render:  a code the page hasn't met shows as hex until its name is fetched
			const unresolved = answer.filter( a=>a.status!==0 ).length;
			this.snackbar.info( unresolved ? `${answer.length-unresolved} of ${answer.length} members resolved.` : `All ${answer.length} members resolved.` );
		}, 'Could not resolve the members.' );
	}

	//The source and the thresholds, edited onto the record as the properties form edits its fields:  a new row each change, so the
	//page's dirty check sees it.
	setConnection( id:number ):void{
		this.#edit( g=>{ g.serverConnectionId = HistGroup.toId( id ); g.connection = this.connections().find( c=>c.id==id ); } );
		this.#loadServer();
	}
	setTemplate( id:number ):void{ this.#edit( g=>{ g.histTemplateId = HistGroup.toId( id ); g.template = this.templates().find( t=>t.id==id ); } ); }
	setRoot( text:string ):void{ this.#edit( g=>{ g.root = text; } ); }
	setThresholds( fields:HistThresholdFields ):void{ this.#edit( g=>{ g.thresholds = fields; } ); }
	#edit( change:( g:HistGroup )=>void ):void{
		const g = new HistGroup( <HistGroupProps>this.properties() );
		change( g );
		this.properties.set( g );
	}
	rootHint():string{
		return this.group?.rootError ?? ( this.isNew ? 'Where the template\'s paths start:  a NodeId, ns=2;i=5001 or {"nsu":"urn:…","i":5001}, or browse for one.  Blank is the Objects folder.' : 'Fixed when the group was made.' );
	}
	//the root picked from the connection's tree:  an object, by its URI
	async browseRoot():Promise<void>{
		const slug = this.connectionSlug();
		if( !slug )
			return;
		const picks = await HistNodePickerDialog.open( this.dialog, { gateway: this.gateway, title: 'Choose the root', mode: 'object', connections: this.connections(), connection: slug } );
		if( picks?.length )
			this.setRoot( histNodeText( picks[0].node ) );
	}
	admin = inject( HistAdminService );
	history = inject( HistoryService );
	opcStore = inject( OpcStore );
	gateway!:Gateway;
}