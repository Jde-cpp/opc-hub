import { Component, computed, effect, input, output, untracked, viewChild, ViewEncapsulation } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatTooltipModule } from '@angular/material/tooltip';
import { FCanvasComponent, FCreateConnectionEvent, FCreateNodeEvent, FFlowComponent, FFlowModule, FSelectionChangeEvent, FZoomDirective, injectFlowState, provideFFlow, withA11y, withFlowState } from '@foblex/flow';
import { FlowGraph } from '../../../../model/flow';
import { flowCategories, FlowNodePatch, flowNodeTypes, flowOutputs, newFlowNode } from '../../../../model/flow-nodes';
import { canvasRecord, CanvasNode, CanvasWire, inNode, outPort, toCanvas, toGraph, wire, wireId } from './flow-canvas-state';
import { FlowNodeView } from './flow-node-view';

const canvasNode = ( type:string, x:number, y:number ):CanvasNode=>canvasRecord( newFlowNode( type, x, y ) );

//The flow editor's canvas (#455), on @foblex/flow (#446):  a palette by category, a node a component drawn by its type, wires
//dragged from port to port, zoom, the minimap, and undo and redo from the library's managed state, whose records are mapped to the
//graph and back (flow-canvas-state.ts).  Selection and the view are kept out of the history.  It is the one file that imports the
//library, and the editor imports it once its tab is shown, so the library is in neither main nor the page's chunk.  The sheet is
//the editor's own on Material's tokens, unencapsulated:  a wire's path and the minimap are the library's elements.
@Component({
	selector: 'flow-canvas',
	templateUrl: './flow-canvas.html',
	styleUrl: './flow-canvas.scss',
	encapsulation: ViewEncapsulation.None,
	host: {class: 'flow-canvas'},
	providers: [ provideFFlow(
		withFlowState( {
			selectionInHistory: false, canvasTransformInHistory: false,
			//a wire is named by its ends, so the same wire drawn twice is one
			connectionFactory: ( e:FCreateConnectionEvent )=>e.targetId ? {id: wireId( e.sourceId, e.targetId ), sourceId: e.sourceId, targetId: e.targetId} : null,
			nodeFactory: ( e:FCreateNodeEvent<{type:string}> )=>canvasNode( e.data.type, e.externalItemRect.x, e.externalItemRect.y )
		} ),
		withA11y()
	) ],
	imports: [FFlowModule, MatButtonModule, MatIconModule, MatTooltipModule, FlowNodeView]
})
export class FlowCanvas{
	state = injectFlowState<CanvasNode,CanvasWire>();
	flow = viewChild.required( FFlowComponent );
	canvas = viewChild.required( FCanvasComponent );
	zoom = viewChild.required( FZoomDirective );
	faults = input<Map<string,string>>( new Map() );//by node, what is wrong with it
	refused = input<string>();//the node a deploy's refusal names
	injectable = input<Set<string>>( new Set() );//the inject nodes the running flow has
	graphChange = output<FlowGraph>();
	selectedChange = output<string|undefined>();//the one node selected, for the side form
	inject = output<string>();
	readonly palette = flowCategories.map( c=>({ ...c, types: flowNodeTypes.filter( t=>t.category==c.category ) }) );
	graph = computed<FlowGraph>( ()=>toGraph( {nodes: this.state.nodes(), connections: this.state.connections()} ) );
	selection = computed( ()=>this.state.selection().nodeIds.length+this.state.selection().connectionIds.length );
	#rendered = false;
	#loads = 0;

	constructor(){
		effect( ()=>{
			const graph = this.graph();
			untracked( ()=>this.graphChange.emit( graph ) );
		} );
		effect( ()=>{
			const {nodeIds, connectionIds} = this.state.selection();
			const id = nodeIds.length==1 && !connectionIds.length ? nodeIds[0] : undefined;
			untracked( ()=>this.selectedChange.emit( id ) );
		} );
	}

	//another graph on the canvas, its history begun again
	load( graph:FlowGraph ):void{
		this.state.load( toCanvas( graph ) );
		this.state.clearHistory();
		++this.#loads;
		if( this.#rendered )
			this.#fitWhenDrawn( this.#loads, 120 );
	}
	//fFullRendered comes for the flow's first render and again when an undo empties the history, never for a load:  the first
	//fits the view, and a later load fits once every wire has its path.
	onFullRendered():void{
		if( this.#rendered )
			return;
		this.#rendered = true;
		this.fit( false );
	}
	#fitWhenDrawn( load:number, frames:number ):void{
		if( load!=this.#loads )
			return;
		const paths = Array.from( document.querySelectorAll( '.flow-canvas f-connection path.f-connection-path' ) );
		if( frames<=0 || (paths.length>=this.state.connections().length && paths.every( x=>x.getAttribute( 'd' ) )) )
			this.fit( false );
		else
			requestAnimationFrame( ()=>this.#fitWhenDrawn( load, frames-1 ) );
	}
	//the flow in the view, and no larger than life:  a few nodes fill no window
	fit( animated=true ):void{
		if( this.state.nodes().length )
			this.canvas().fitToScreen( {x: 40, y: 40}, animated, false, 1 );
	}
	remove():void{
		const {nodeIds, connectionIds} = this.state.selection();
		this.state.removeItems( nodeIds, connectionIds );
	}
	//A palette item taken by the keyboard or a click, where a drag isn't to be had:  the node goes below the lowest one, and is selected.
	add( type:string ):void{
		const nodes = this.state.nodes();
		const node = canvasNode( type, nodes.length ? Math.min( ...nodes.map( n=>n.position.x ) ) : 0, nodes.length ? Math.max( ...nodes.map( n=>n.position.y ) )+70 : 0 );
		this.state.addNodes( node );
		this.select( node.id );
	}
	//The library's selection, and the state's with it:  the state hears a selection made by hand, not one made here.
	select( id:string|undefined ):void{
		const ids = id && this.state.getNode( id ) ? [id] : [];
		this.flow().select( ids, [], false );
		this.state.applySelectionChange( new FSelectionChangeEvent( ids, [], [] ) );
	}
	//A node changed by its form, one step of the history:  with its wires where its ports moved or went.
	patch( id:string, patch:FlowNodePatch ):void{
		const node = this.state.getNode( id );
		if( !node )
			return;
		const changed:CanvasNode = { ...node, ...( patch.name!==undefined ? {name: patch.name} : {} ), ...( patch.props ? {props: patch.props} : {} ) };
		const outputs = flowOutputs( changed );
		const from = this.state.connections().filter( c=>outPort( c.sourceId ).node==id );
		//each wire where its port is now, or nowhere:  those that stay as they are, those that go, and those to draw
		const rewired = from.map( c=>{
			const was = outPort( c.sourceId ).port, port = patch.ports ? patch.ports.indexOf( was ) : was;
			return port>=0 && port<outputs ? wire( id, port, inNode( c.targetId ) ) : undefined;
		} );
		const kept = new Set( from.filter( ( c, i )=>rewired[i]?.id==c.id ).map( c=>c.id ) );
		const stale = from.filter( c=>!kept.has( c.id ) ).map( c=>c.id );
		const drawn = new Map( rewired.filter( ( c ):c is CanvasWire=>!!c && !kept.has( c.id ) ).map( c=>[c.id, c] ) );
		this.state.batch( ()=>{
			this.state.updateNode( id, {name: changed.name, props: changed.props} );
			if( stale.length )
				this.state.removeConnections( stale );
			if( drawn.size )
				this.state.addConnections( ...drawn.values() );
		} );
	}
}