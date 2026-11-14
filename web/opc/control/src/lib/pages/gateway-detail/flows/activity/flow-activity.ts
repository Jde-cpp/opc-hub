import { afterNextRender, Component, ComponentRef, computed, effect, input, OnDestroy, output, signal, untracked, viewChild, ViewContainerRef } from '@angular/core';
import { DatePipe } from '@angular/common';
import { MatButtonModule } from '@angular/material/button';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatSelectModule } from '@angular/material/select';
import { MatSlideToggleModule } from '@angular/material/slide-toggle';
import { MatTooltipModule } from '@angular/material/tooltip';
import { Observable, Subscription } from 'rxjs';
import { errorMessage } from 'jde-framework';
import { Flow, FlowGraph, FlowVersion } from '../../../../model/flow';
import { FlowLiveEvent } from '../../../../model/flow-live';
import { flowHopsShown, flowLiveNext, flowLiveSettle, flowLiveStart, FlowLiveState, FlowWireLive } from '../../../../model/flow-live-view';
import { FlowRecordKind, flowRecordKinds, FlowTraceRecord } from '../../../../model/flow-trace';
import { flowJsonText, flowRecordSummary } from '../../../../model/flow-trace-view';
import { gatewayAnswer } from '../../../../services/gateway-service';
import type { FlowCanvas } from '../canvas/flow-canvas';
import { FlowCanvasDecor, FlowNodeDecor, FlowWireDecor } from '../canvas/flow-canvas-state';
import { loadFlowView } from '../flow-view';

//what the tab subscribes through:  a Gateway, or a stub in a spec
export type FlowLiveGateway = { flowSubscribe( flow:number, hops?:boolean ):Observable<FlowLiveEvent> };
//the subscription as the tab says it:  off while the tab is hidden, connecting until the ack, live from it, dropped with the
//socket until the gateway is back, and ended by the gateway with its reason, a refusal among them
export type FlowLiveStatus = { kind:'off'|'connecting'|'live'|'dropped'|'ended'; message?:string; httpStatus?:number };

//A flow's Activity tab (#456, docs/flows/spec.md *Live*, *Web*):  the canvas read-only under FlowSubscribe, the running
//version's graph, each wire's rate a second as its width and a running dash with its last message on its label, each node's
//status as a badge under it, its drops and errors counted, and the error colour once it has erred;  and the debug panel
//beside it, the trace's records as the pushes carry them with `hops` on, newest first, filtered by node and by kind, paused
//and cleared, each expanding to its json.  The subscription runs while the tab is shown (`active`) and ends when it is left,
//since a push comes every /gateway/flows/live;  a deploy is a new ack, whose version the page is asked for where it lacks it.
@Component({
	selector: 'flow-activity',
	templateUrl: './flow-activity.html',
	styleUrl: './flow-activity.scss',
	imports: [DatePipe, MatButtonModule, MatFormFieldModule, MatIconModule, MatProgressBarModule, MatSelectModule, MatSlideToggleModule, MatTooltipModule]
})
export class FlowActivity implements OnDestroy{
	gateway = input.required<FlowLiveGateway>();
	flow = input.required<Flow>();
	versions = input<FlowVersion[]>( [] );//for the graph of the version running
	active = input( true );//the tab is the one shown
	refresh = output<void>();//the versions lack the ack's
	slot = viewChild.required( 'slot', {read: ViewContainerRef} );

	hops = signal( true );//the debug panel's records asked of the subscription
	paused = signal( false );
	filterNode = signal<string|undefined>( undefined );
	filterKind = signal<FlowRecordKind|undefined>( undefined );
	state = signal<FlowLiveState>( flowLiveStart() );
	status = signal<FlowLiveStatus>( {kind: 'off'} );
	selectedWire = signal<string|undefined>( undefined );
	selectedNode = signal<string|undefined>( undefined );
	expanded = signal<Set<FlowTraceRecord>>( new Set() );
	loadError = signal<string|undefined>( undefined );
	ready = signal( false );
	readonly kinds = flowRecordKinds;
	#frozen = signal<FlowTraceRecord[]>( [] );//the panel's list as it stood at the pause
	#canvas?:ComponentRef<FlowCanvas>;
	#subscription?:Subscription;
	#settle?:ReturnType<typeof setInterval>;
	#askedFor?:number;//the version the page was asked for
	#loaded?:FlowGraph;//what the canvas holds
	#flowId?:number;//whose state is held
	//the flow, by its gateway and id:  a deploy, an enable or a disable gives the page a new row of the same flow
	#shown = computed( ()=>({ gateway: this.gateway(), id: this.flow().id }), {equal: ( a, b )=>a.gateway==b.gateway && a.id==b.id} );

	//the graph drawn:  the ack's version among the page's, none until the page has it
	graph = computed<FlowGraph|undefined>( ()=>{
		const version = this.state().version;
		return version==undefined ? undefined : this.versions().find( v=>v.version==version && !v.unread )?.graph;
	} );
	nodes = computed( ()=>this.graph()?.nodes ?? [] );
	decor = computed<FlowCanvasDecor>( ()=>{
		const state = this.state();
		const nodes = new Map<string,FlowNodeDecor>( [...state.nodes].map( ( [id, n] )=>[id, { status: n.status, counts: n.counts, error: n.error }] ) );
		const wires = new Map<string,FlowWireDecor>( [...state.wires].map( ( [key, w] )=>[key, { rate: w.rate, label: w.rate ? `${FlowActivity.rateText( w.rate )}/s` : w.count ? String( w.count ) : undefined, message: w.msgId ? flowRecordSummary( {json: w.json, kind: 'hop'} ) : undefined }] ) );
		return { nodes, wires };
	} );
	hopsShown = computed( ()=>flowHopsShown( this.paused() ? this.#frozen() : this.state().hops, this.filterNode(), this.filterKind() ) );
	wire = computed( ()=>{ const key = this.selectedWire(); return key ? this.state().wires.get( key ) : undefined; } );
	node = computed( ()=>{ const id = this.selectedNode(); return id ? this.state().nodes.get( id ) : undefined; } );
	totals = computed( ()=>{
		let received = 0, sent = 0, drops = 0, errors = 0;
		for( const n of this.state().nodes.values() ){
			received += n.counts.received;
			sent += n.counts.sent.reduce( ( a, b )=>a+b, 0 );
			drops += n.counts.drops;
			errors += n.counts.errors;
		}
		return { received, sent, drops, errors };
	} );
	static rateText( rate:number ):string{ return rate>=10 ? String( Math.round( rate ) ) : rate.toFixed( 1 ); }

	constructor(){
		afterNextRender( ()=>this.#import() );
		effect( ()=>{//the flow, the tab shown or hidden, or the panel's hops switched:  the subscription anew, or ended
			const {gateway, id} = this.#shown(), active = this.active(), hops = this.hops();
			untracked( ()=>active ? this.#subscribe( gateway, id, hops ) : this.#unsubscribe( 'off' ) );
		} );
		effect( ()=>{//the ack's version missing from the page's:  asked for once
			const version = this.state().version, versions = this.versions();
			if( version!=undefined && !versions.some( v=>v.version==version ) && this.#askedFor!=version ){
				this.#askedFor = version;
				untracked( ()=>this.refresh.emit() );
			}
		} );
		effect( ()=>{
			const graph = this.graph();
			if( this.ready() && graph!=this.#loaded )
				untracked( ()=>this.#load( graph ) );
		} );
		effect( ()=>{
			const decor = this.decor();
			if( this.ready() )
				untracked( ()=>this.#canvas!.setInput( 'decor', decor ) );
		} );
	}
	ngOnDestroy(){ this.#unsubscribe( 'off' ); }
	async #import():Promise<void>{
		try{
			const canvas = await loadFlowView( this.slot() );
			canvas.instance.wireSelected.subscribe( key=>this.selectedWire.set( key ) );
			canvas.instance.selectedChange.subscribe( id=>this.selectedNode.set( id ) );
			this.#canvas = canvas;
			this.#load( this.graph() );
			canvas.setInput( 'decor', this.decor() );
			this.ready.set( true );
		}
		catch( e ){
			this.loadError.set( errorMessage( e, 'Could not load the canvas.' ) );
		}
	}
	#load( graph:FlowGraph|undefined ):void{
		this.#loaded = graph;
		this.#canvas?.instance.load( graph ?? {nodes: []} );
	}
	#subscribe( gateway:FlowLiveGateway, flow:number, hops:boolean ):void{
		this.#unsubscribe( 'connecting' );
		if( flow!=this.#flowId ){//another flow:  nothing of the last is its
			this.#flowId = flow;
			this.state.set( flowLiveStart() );
			this.#frozen.set( [] );
			this.expanded.set( new Set() );
		}
		this.#subscription = gateway.flowSubscribe( flow, hops ).subscribe( {
			next: e=>{
				this.state.update( s=>flowLiveNext( s, e, Date.now() ) );
				if( e.kind=='ack' )
					this.status.set( {kind: 'live'} );
				else if( e.kind=='dropped' )
					this.status.set( {kind: 'dropped'} );
			},
			error: e=>{
				const answer = gatewayAnswer( e );
				this.status.set( {kind: 'ended', message: answer?.message || errorMessage( e ), httpStatus: answer?.httpStatus} );
				this.#subscription = undefined;
				this.#stopSettling();
			}
		} );
		this.#settle = setInterval( ()=>this.state.update( s=>flowLiveSettle( s, Date.now() ) ), 1000 );
	}
	#unsubscribe( kind:FlowLiveStatus['kind'] ):void{
		this.#subscription?.unsubscribe();
		this.#subscription = undefined;
		this.#stopSettling();
		this.status.set( {kind} );
		this.state.update( s=>flowLiveSettle( s, Number.MAX_SAFE_INTEGER ) );
	}
	#stopSettling():void{
		clearInterval( this.#settle );
		this.#settle = undefined;
	}
	//the subscription again, after the gateway ended it:  the flow started since, or the fault is mended
	again():void{ this.#subscribe( this.gateway(), this.flow().id, this.hops() ); }
	setHops( on:boolean ):void{ this.hops.set( on ); }
	pause( on:boolean ):void{
		if( on )
			this.#frozen.set( this.state().hops );
		this.paused.set( on );
	}
	clear():void{
		this.#frozen.set( [] );
		this.expanded.set( new Set() );
		this.state.update( s=>({ ...s, hops: [], dropped: 0 }) );
	}
	toggle( hop:FlowTraceRecord ):void{
		this.expanded.update( set=>{
			const next = new Set( set );
			if( !next.delete( hop ) )
				next.add( hop );
			return next;
		} );
	}
	nodeName( id:string ):string{
		const node = this.nodes().find( n=>n.id==id );
		return node ? node.name || node.type : id;
	}
	//by the wire's ends, not its key, which a node's id holding a `:` or a `>` would split wrong
	wireText( w:Pick<FlowWireLive,'node'|'port'|'to'> ):string{ return `${this.nodeName( w.node )}, output ${w.port+1}, to ${this.nodeName( w.to )}`; }
	summary( hop:FlowTraceRecord ):string{ return flowRecordSummary( hop ); }
	json( v:unknown ):string{ return flowJsonText( v ); }
	hopKindIcon( kind:FlowRecordKind ):string{
		return kind=='error' ? 'error' : kind=='drop' ? 'delete_sweep' : kind=='status' ? 'info' : kind=='birth' ? 'fiber_new' : kind=='clone' ? 'content_copy' : kind=='captured' ? 'inventory' : 'arrow_forward';
	}
	statusText = computed( ()=>{
		const s = this.status(), state = this.state();
		switch( s.kind ){
		case 'off': return 'Not subscribed.';
		case 'connecting': return 'Subscribing…';
		case 'live': return `Live, version ${state.version}, ${state.pushes} ${state.pushes==1 ? 'push' : 'pushes'}.`;
		case 'dropped': return 'Connection to the gateway lost.  The subscription resumes once it is back.';
		default: return s.message ? `Ended:  ${s.message}` : 'Ended.';
		}
	} );
}