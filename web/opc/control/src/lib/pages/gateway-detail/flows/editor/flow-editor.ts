import { afterNextRender, Component, ComponentRef, computed, effect, ElementRef, inject, Injector, input, output, signal, untracked, viewChild, ViewContainerRef } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatDialog } from '@angular/material/dialog';
import { MatIconModule } from '@angular/material/icon';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatTooltipModule } from '@angular/material/tooltip';
import { confirm, errorMessage, SnackbarService } from 'jde-framework';
import { exportFlow, Flow, FlowDeployed, FlowFault, flowFaultText, FlowGraph, FlowNode, sameFlowGraph, toFlowFault } from '../../../../model/flow';
import { flowGraphFault, flowNodeFaults, FlowNodePatch, normalizeFlowGraph } from '../../../../model/flow-nodes';
import { FlowService } from '../../../../services/flow-service';
import { HistPoster } from '../../../../services/history-service';
import type { FlowBlockEditor } from '../blocks/flow-block-editor';
import { FlowBlocksLoader } from '../blocks/flow-blocks-loader';
import type { FlowCanvas } from '../canvas/flow-canvas';
import { FlowDeployDialog } from './flow-deploy-dialog';
import { FlowNodeForm } from './flow-node-form';

//What the editor opens with:  the graph deployed, none before the first deploy, and a draft over it where there is one, an
//import's or a version opened from Versions.  A new object is another load.
export type FlowEditorSource = { deployed:FlowGraph; draft?:FlowGraph };

//A flow's Editor tab (#455, docs/flows/spec.md *Web*):  the canvas with its palette, the selected node's side form, and the
//flow's own actions - Deploy with a comment, which stores the graph as the next version and marks the node a refusal names,
//Revert to the version deployed, Enable and Disable, an inject node's button, and Export.  The canvas and its library are
//imported once the tab is shown (FlowCanvas), as marked is by the help page:  behind the page's loader alone they would sit in
//its chunk, which every tab preloads.  A Function node's block editor (#458) loads the same way once its form asks for it, and
//takes the canvas's room and the form's until its Done or Cancel:  the node's props are one step of the canvas's history at
//Done, and a deploy waits for it, since what the blocks hold is not yet the graph's.
@Component({
	selector: 'flow-editor',
	templateUrl: './flow-editor.html',
	styleUrl: './flow-editor.scss',
	imports: [MatButtonModule, MatIconModule, MatProgressBarModule, MatTooltipModule, FlowNodeForm]
})
export class FlowEditor{
	gateway = input.required<HistPoster>();
	flow = input.required<Flow>();
	source = input.required<FlowEditorSource>();
	deployed = output<FlowDeployed>();
	enabledChange = output<boolean>();
	slot = viewChild.required( 'slot', {read: ViewContainerRef} );
	blocksSlot = viewChild.required( 'blocksSlot', {read: ViewContainerRef} );

	graph = signal<FlowGraph>( {nodes: []} );//what the canvas holds
	base = signal<FlowGraph>( {nodes: []} );//what is deployed
	selected = signal<string|undefined>( undefined );
	refusal = signal<FlowFault|undefined>( undefined );//the last deploy's, or why the version it wrote did not start
	busy = signal( false );
	loadError = signal<string|undefined>( undefined );//the canvas could not be imported
	ready = signal( false );
	blocksOf = signal<string|undefined>( undefined );//the node whose block editor is open
	dirty = computed( ()=>!sameFlowGraph( this.graph(), this.base() ) );
	faults = computed( ()=>flowNodeFaults( this.graph() ) );
	selectedNode = computed( ()=>this.graph().nodes.find( n=>n.id==this.selected() ) );
	//the inject nodes of the version running:  one drawn since is not there to send
	injectable = computed( ()=>new Set( this.flow().enabled && this.flow().version ? this.base().nodes.filter( n=>n.type=='inject' ).map( n=>n.id ) : [] ) );
	canDeploy = computed( ()=>this.ready() && !this.busy() && !this.blocksOf() && (this.dirty() || !this.flow().version) );
	#canvas?:ComponentRef<FlowCanvas>;
	#blocks?:ComponentRef<FlowBlockEditor>;
	#blocksLoader = inject( FlowBlocksLoader );
	#element = inject<ElementRef<HTMLElement>>( ElementRef );
	#injector = inject( Injector );
	#flows = inject( FlowService );
	#dialog = inject( MatDialog );
	#snackbar = inject( SnackbarService );

	constructor(){
		afterNextRender( ()=>this.#import() );
		effect( ()=>{//another flow, or another graph asked onto the canvas
			const source = this.source();
			untracked( ()=>this.#open( source ) );
		} );
		effect( ()=>{
			const faults = this.faults(), refused = this.refusal()?.node, injectable = this.injectable();
			if( this.ready() )
				untracked( ()=>{
					this.#canvas!.setInput( 'faults', faults );
					this.#canvas!.setInput( 'refused', refused );
					this.#canvas!.setInput( 'injectable', injectable );
				} );
		} );
	}
	async #import():Promise<void>{
		try{
			const { FlowCanvas } = await import( '../canvas/flow-canvas' );
			const canvas = this.slot().createComponent( FlowCanvas );
			canvas.instance.graphChange.subscribe( graph=>this.graph.set( graph ) );
			canvas.instance.selectedChange.subscribe( id=>this.selected.set( id ) );
			canvas.instance.inject.subscribe( id=>this.inject( id ) );
			this.#canvas = canvas;
			canvas.instance.load( this.graph() );
			this.ready.set( true );
		}
		catch( e ){
			this.loadError.set( errorMessage( e, 'Could not load the canvas.' ) );
		}
	}
	#open( source:FlowEditorSource ):void{
		this.base.set( normalizeFlowGraph( source.deployed ) );
		this.#load( source.draft ? normalizeFlowGraph( source.draft ) : this.base() );
		this.refusal.set( undefined );
	}
	#load( graph:FlowGraph ):void{
		this.closeBlocks();
		this.graph.set( graph );
		this.selected.set( undefined );
		this.#canvas?.instance.load( graph );
	}

	patch( id:string, patch:FlowNodePatch ):void{ this.#canvas?.instance.patch( id, patch ); }
	//The block editor of a Function node's body, over the canvas and the form:  its Done is the node's `blocks` and the `code`
	//they wrote, together.
	async openBlocks( node:FlowNode ):Promise<void>{
		if( this.blocksOf() )
			return;
		this.blocksOf.set( node.id );
		try{
			const view = await this.#blocksLoader.load( this.blocksSlot() );
			if( this.blocksOf()!=node.id )//let go while it loaded
				return view.destroy();
			view.setInput( 'title', node.name || node.type );
			view.setInput( 'blocks', node.props['blocks'] );
			view.instance.done.subscribe( y=>{
				const now = this.graph().nodes.find( n=>n.id==node.id );
				if( now )
					this.patch( node.id, {props: {...now.props, blocks: y.blocks, code: y.code}} );
				this.closeBlocks();
			} );
			view.instance.closed.subscribe( ()=>this.closeBlocks() );
			this.#blocks = view;
		}
		catch( e ){
			this.blocksOf.set( undefined );
			this.#snackbar.exception( 'Could not load the block editor.', e );
		}
	}
	//the editor gone, and the keyboard back at the form's button for it
	closeBlocks():void{
		if( !this.blocksOf() )
			return;
		this.#blocks?.destroy();
		this.#blocks = undefined;
		this.blocksOf.set( undefined );
		afterNextRender( ()=>this.#element.nativeElement.querySelector<HTMLElement>( '.form .edit-blocks' )?.focus(), {injector: this.#injector} );
	}
	show( id:string ):void{ this.#canvas?.instance.select( id ); }
	//Whether what is on the canvas may go, asked where it is not what is deployed:  a load begins the history again, so no Undo
	//brings it back.
	async mayReplace( title:string ):Promise<boolean>{
		return !this.dirty() || confirm( this.#dialog, {title, message: "What is on the canvas isn't deployed, and Undo won't bring it back.", confirm: 'Replace', destructive: true} );
	}
	async revert():Promise<void>{
		if( this.blocksOf() || !await this.mayReplace( 'Revert to the version deployed?' ) )
			return;
		this.#load( this.base() );
		this.refusal.set( undefined );
	}
	//deployFlow:  the graph as the flow's next version.  A refusal names its node, which the canvas marks and selects.
	async deploy():Promise<void>{
		const flow = this.flow(), graph = this.graph();
		const fault = flowGraphFault( graph );
		const comment = await FlowDeployDialog.open( this.#dialog, { name: flow.name, version: (flow.version ?? 0)+1, enabled: flow.enabled, fault: fault ? flowFaultText( fault, graph ) : undefined } );
		if( comment===undefined )
			return;
		this.busy.set( true );
		try{
			const y = await this.#flows.deploy( this.gateway(), flow.id, graph, comment );
			this.base.set( graph );
			this.refusal.set( y.fault );
			if( y.fault?.node )
				this.show( y.fault.node );
			this.#snackbar.info( `Version ${y.version} deployed${y.running ? ', and running' : y.fault ? ', and did not start' : ':  the flow is disabled'}.${y.dropped ? `  ${y.dropped} ${y.dropped==1 ? 'message' : 'messages'} of the run before dropped.` : ''}` );
			this.deployed.emit( y );
		}
		catch( e ){
			const refusal = toFlowFault( errorMessage( e ) );
			this.refusal.set( refusal );
			if( refusal.node )
				this.show( refusal.node );
		}
		finally{
			this.busy.set( false );
		}
	}
	async setEnabled( enabled:boolean ):Promise<void>{
		this.busy.set( true );
		try{
			await this.#flows.setEnabled( this.gateway(), this.flow().id, enabled );
			this.enabledChange.emit( enabled );
		}
		catch( e ){
			this.#snackbar.exception( `Could not ${enabled ? 'enable' : 'disable'} the flow.`, e );
		}
		finally{
			this.busy.set( false );
		}
	}
	async inject( node:string ):Promise<void>{
		try{
			await this.#flows.inject( this.gateway(), this.flow().id, node );
			this.#snackbar.info( 'Injected.' );
		}
		catch( e ){
			this.#snackbar.exception( 'Could not inject.', e );
		}
	}
	//the canvas as a file, which Import reads
	export():void{
		const flow = this.flow();
		const url = URL.createObjectURL( new Blob( [exportFlow( flow, this.graph() )], {type: 'application/json'} ) );
		const a = document.createElement( 'a' );
		a.href = url;
		a.download = `${flow.slug}.json`;
		document.body.appendChild( a );//Firefox and Safari save from an anchor in the page, and may read the url after the click returns
		a.click();
		a.remove();
		setTimeout( ()=>URL.revokeObjectURL( url ) );
	}
	refusalText = computed( ()=>{ const r = this.refusal(); return r ? flowFaultText( r, this.graph() ) : undefined; } );
}