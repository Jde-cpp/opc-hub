import { Component, computed, inject, signal, viewChild } from '@angular/core';
import { CdkCopyToClipboard } from '@angular/cdk/clipboard';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatTabsModule } from '@angular/material/tabs';
import { MatTooltipModule } from '@angular/material/tooltip';
import { DetailActions, DetailPage, errorMessage, Properties } from 'jde-framework';
import { Latest } from 'jde-spa';
import { Flow, FlowDeployed, FlowGraph, FlowNode, FlowProps, FlowVersion } from '../../../model/flow';
import { FlowService } from '../../../services/flow-service';
import { Gateway } from '../../../services/gateway-service';
import { GatewayDetailData } from '../../../services/resolvers/client-resolver';
import { AccessSubject, HistGroupAccess } from '../hist-admin/hist-group-access';
import { FlowEditor, FlowEditorSource } from './editor/flow-editor';
import { FlowContext } from './flow-context';
import { FlowDrafts } from './flow-drafts';
import { FlowVersions } from './flow-versions';

//A flow's own resource (docs/flows/spec.md *Rights*), as the gateway tests it:  Acl()->Test( "gateway", "flows", <flow guid>, … )
//(FlowMAwait), governed as a group's is, so the group's tab on the flow's row.
export const flowAccessSubject:AccessSubject = {
	key: { schemaName: 'gateway', slug: 'flows' }, table: 'flows', noun: 'flow',
	rights: { 0: 'None', 2: 'Read', 4: 'Update', 8: 'Delete', 16: 'Purge', 32: 'Administer' },
	hint: "Who may see the flow and change it:  Read for its row, its versions, its trace and its activity, Update to edit it, deploy, enable, disable, inject, set its context and replay, Delete and Purge their own, and Administer to change these grants once the flow's own resource governs it.  The flows table's resource, switched on the Resources page, turns checking on;  under it the flow answers to its own resource once it has one that is enforced, else to the table's.  What a running flow may do is its deployer's:  a write needs their Update on the group that holds the node.",
	open: name=>`Anyone signed in reads ${name}, edits it, deploys it and purges it`,
	held: name=>`read ${name}, change it, deploy it or delete it`,
	stopped: 'anyone signed in reads the flow, edits it, deploys it and purges it'
};
//the fields the form leaves out:  `enabled` is the Editor's Enable and Disable, `version` a deploy's, and the guid is read, never typed
const excluded = ['guid', 'enabled', 'version'];
const tracePicks = { trace: [ {value: null, label: "The gateway's level"}, {value: 'off', label: 'Off:  nothing written'}, {value: 'counts', label: "Counts:  a minute's figures a node"}, {value: 'errors', label: 'Errors:  the path of a message that fails'}, {value: 'hops', label: 'Hops:  every message at every node'} ] };

//A flow's page (#455, docs/flows/spec.md *Web*), under the gateway's Flows tab.  Editor:  the canvas, its palette and the
//selected node's form, with Deploy, Enable, Disable and Inject.  Versions:  the deploys, a diff of two, and the replays.
//Context:  the values its nodes keep.  Access:  the group's tab, on the flow's row.  Properties, last:  the row's name,
//description and trace level, saved as a group's is, with Delete, Restore and Purge;  the slug names the trace's directory and
//never changes.  Activity and History are phase 10's.  The row and its gateway are HistAdminResolver's;  a flow is made by the
//tab's Create and Import, so the page never holds a new one.
@Component({
	templateUrl: './flow-detail.html',
	styleUrls: ['../hist-admin/hist-admin-shared.scss', './flow-detail.scss'],
	//the trailing class is load-bearing, as HistGroupDetail's is (NG0912)
	host: {class: 'main-content my-content flow-detail'},
	imports: [CdkCopyToClipboard, DetailActions, MatButtonModule, MatIconModule, MatProgressBarModule, MatTabsModule, MatTooltipModule, Properties, FlowContext, FlowEditor, FlowVersions, HistGroupAccess]
})
export class FlowDetail extends DetailPage<Flow>{
	constructor(){ super( 'flow-detail' ); }

	protected override get ctor(){ return Flow; }
	protected override onRow(){
		this.gateway = (<GatewayDetailData<Flow>>this.pageData).gateway;
		this.versions.set( [] );
		this.source.set( undefined );
		const draft = this.#drafts.take( this.row.guid );
		if( draft || !this.row.version )//a flow just made or imported, or one with nothing deployed:  the canvas, whichever tab the last flow was left on
			this.tabIndex.set( 0 );
		this.#opening = { draft };
		this.refreshVersions();
	}
	protected override upsert():Flow{ return new Flow( <FlowProps>this.properties() ); }
	protected override get title(){ return `${this.row.name} - Flow`; }
	protected override get onlyPropertiesTab(){ return false; }
	protected override get deleteWarning(){ return 'The flow stops.  Its versions, its context and its trace stay, and Restore brings it back, started again if it is enabled.'; }
	protected override get purgeWarning(){ return 'Its versions, its replays, its context values and its trace go with it.'; }
	override get ql(){ return this.gateway; }//per-gateway, as ClientDetail's

	readonly accessSubject = flowAccessSubject;
	readonly fieldOrder = ['slug', 'name', 'description', 'trace'];
	readonly excludedFields = excluded;
	readonly picks = tracePicks;
	readonly hints = { trace: 'What the flow writes of its messages.  A change restarts a running flow.' };
	versions = signal<FlowVersion[]>( [] );//newest first
	versionsLoading = signal( false );
	versionsError = signal<string|undefined>( undefined );
	source = signal<FlowEditorSource|undefined>( undefined );//what the editor opens with, once the versions are read
	//the current version's nodes, which Context names a node's values by
	nodes = computed<FlowNode[]>( ()=>this.#current( this.versions(), this.row?.version )?.nodes ?? [] );

	#current( versions:FlowVersion[], version:number|undefined ):FlowGraph|undefined{ return versions.find( v=>v.version==version && !v.unread )?.graph; }
	//What the editor opens with once a read of the versions lands, an import's draft kept until then, so Retry has it:  the
	//current version's graph, under the draft.  A read that fails opens no editor:  an empty canvas would deploy over a version
	//it never showed.
	#opening?:{ draft?:FlowGraph };
	#reads = new Latest();//the newest read alone lands, whatever order the answers come in, and a read for the last flow not at all
	retry():void{ this.refreshVersions(); }
	async refreshVersions():Promise<void>{
		const read = this.#reads.next();
		this.versionsLoading.set( true );
		this.versionsError.set( undefined );
		try{
			const versions = await this.#flows.versions( this.gateway, this.row.id );
			if( !this.#reads.is( read ) )
				return;
			this.versions.set( versions );
			if( this.#opening ){
				this.source.set( { deployed: this.#current( versions, this.row.version ) ?? {nodes: []}, draft: this.#opening.draft } );
				this.#opening = undefined;
			}
		}
		catch( e ){
			if( this.#reads.is( read ) )
				this.versionsError.set( errorMessage( e, 'Could not read the versions.' ) );
		}
		finally{
			if( this.#reads.is( read ) )
				this.versionsLoading.set( false );
		}
	}
	//the row as the gateway has it now, without a read:  the form keeps what is typed in it
	#changed( change:Partial<Pick<Flow,'version'|'enabled'>> ):void{
		this.row = Object.assign( new Flow( this.row ), change );
		this.properties.set( Object.assign( new Flow( <FlowProps>this.properties() ), change ) );
	}
	onDeployed( deployed:FlowDeployed ):void{
		this.#changed( {version: deployed.version} );
		this.refreshVersions();
	}
	onEnabled( enabled:boolean ):void{ this.#changed( {enabled} ); }
	//a version's graph back on the canvas, over the one deployed, for Deploy to make the next, once what the canvas holds may go
	async openVersion( version:FlowVersion ):Promise<void>{
		if( !await (this.editor()?.mayReplace( `Open version ${version.version} on the canvas?` ) ?? true) )
			return;
		this.source.set( { deployed: this.#current( this.versions(), this.row.version ) ?? {nodes: []}, draft: version.version==this.row.version ? undefined : version.graph } );
		this.tabIndex.set( 0 );
		this.snackbar.info( version.version==this.row.version ? `Version ${version.version}, the current one, is on the canvas.` : `Version ${version.version} is on the canvas, not deployed:  Deploy makes it the next version.` );
	}
	copied( ok:boolean ):void{
		if( ok )
			this.snackbar.info( 'Guid copied.' );
		else
			this.snackbar.error( 'Could not copy the guid.' );
	}
	editor = viewChild( FlowEditor );
	#flows = inject( FlowService );
	#drafts = inject( FlowDrafts );
	gateway!:Gateway;
}