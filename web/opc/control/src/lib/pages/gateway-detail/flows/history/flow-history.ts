import { afterNextRender, Component, ComponentRef, computed, effect, inject, input, OnDestroy, signal, untracked, viewChild, ViewContainerRef } from '@angular/core';
import { DatePipe } from '@angular/common';
import { MatButtonModule } from '@angular/material/button';
import { MatButtonToggleModule } from '@angular/material/button-toggle';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatSelectModule } from '@angular/material/select';
import { MatTableModule } from '@angular/material/table';
import { MatTooltipModule } from '@angular/material/tooltip';
import { errorMessage } from 'jde-framework';
import { Latest } from 'jde-spa';
import { Flow, FlowGraph, FlowRun, FlowVersion } from '../../../../model/flow';
import { FlowCountRow, FlowRecordKind, flowRecordKinds, FlowTraceRecord, flowWireKey } from '../../../../model/flow-trace';
import { flowCountStep, flowDeployMarks, flowGraphCounts, flowJsonText, flowLineagePath, flowOwnRecords, FlowPlayer, flowRecordSummary, flowVersionAt } from '../../../../model/flow-trace-view';
import { FlowService } from '../../../../services/flow-service';
import { HistReader } from '../../../../services/history-service';
import type { FlowCanvas } from '../canvas/flow-canvas';
import { FlowCanvasDecor, FlowLit, FlowNodeDecor, FlowWireDecor } from '../canvas/flow-canvas-state';
import { localInput } from '../flow-replay-dialog';
import { loadFlowView } from '../flow-view';

//a run asked onto the tab by Versions:  `n` counts the asks, so the same run asked twice is taken twice
export type FlowHistoryRequest = { run:FlowRun; n:number };
export type FlowRange = { start:Date; end:Date };
//what is wrong with a range typed, or nothing:  each a time, in order
export function rangeProblem( from:string, to:string ):string|undefined{
	const start = new Date( from ), end = new Date( to );
	if( !from || isNaN( start.getTime() ) )
		return 'From is a time.';
	if( !to || isNaN( end.getTime() ) )
		return 'To is a time.';
	return start>=end ? 'From is before To.' : undefined;
}
export const flowHistoryPage = 200;//records a read of the list
export const flowPlayPage = 1000;//records a read for Play
export const flowFiredFor = 600;//ms a fired node or wire stays lit
export const flowPlayTick = 100;//ms between the player's steps

//A flow's History tab (#456, docs/flows/spec.md *History*, *Web*):  the canvas read-only over a range picked as the node
//History tab picks one, the live trace's or a replay's, with flowCounts' figures on the wires and under the nodes, the version
//in force at the scrubber's time drawn from flow_versions with the deploys marked along the range, the message list from
//flowTrace by node, kind or message, a message selected lighting its path from flowMessage, its own records listed with its
//json at each node where it changed, its ancestors' and descendants' paths dashed, and Play stepping the range's records
//through the canvas at one, ten or a hundred times, the scrubber following.  A run opened from Versions is a range of its own.
@Component({
	selector: 'flow-history',
	templateUrl: './flow-history.html',
	styleUrl: './flow-history.scss',
	imports: [DatePipe, MatButtonModule, MatButtonToggleModule, MatFormFieldModule, MatIconModule, MatInputModule, MatProgressBarModule, MatSelectModule, MatTableModule, MatTooltipModule]
})
export class FlowHistory implements OnDestroy{
	gateway = input.required<HistReader>();
	flow = input.required<Flow>();
	versions = input<FlowVersion[]>( [] );
	request = input<FlowHistoryRequest|undefined>( undefined );
	slot = viewChild.required( 'slot', {read: ViewContainerRef} );
	readonly kinds = flowRecordKinds;
	readonly columns = ['time', 'node', 'kind', 'msg', 'summary'];
	readonly speeds = [1, 10, 100];

	//the range typed, and the one read
	from = signal( localInput( new Date( Date.now()-3_600_000 ) ) );
	to = signal( localInput( new Date() ) );
	problem = computed( ()=>rangeProblem( this.from(), this.to() ) );
	range = signal<FlowRange|undefined>( undefined );
	run = signal<FlowRun|undefined>( undefined );//none is the live trace
	runs = signal<FlowRun[]>( [] );
	//the list's narrowing
	filterNode = signal<string|undefined>( undefined );
	filterKind = signal<FlowRecordKind|undefined>( undefined );
	filterMsg = signal( '' );
	counts = signal<FlowCountRow[]>( [] );
	countsError = signal<string|undefined>( undefined );
	records = signal<FlowTraceRecord[]>( [] );
	continuation = signal<string|undefined>( undefined );
	listError = signal<string|undefined>( undefined );
	loading = signal( 0 );//reads out
	selected = signal<FlowTraceRecord|undefined>( undefined );
	lineage = signal<{ records:FlowTraceRecord[]; more:boolean }|undefined>( undefined );
	lineageError = signal<string|undefined>( undefined );
	at = signal( 0 );//the scrubber, ms
	playing = signal( false );
	speed = signal( 1 );
	playError = signal<string|undefined>( undefined );
	fired = signal<Map<string,number>>( new Map() );//a node's id or a wire's key, when it fired
	ready = signal( false );
	loadError = signal<string|undefined>( undefined );
	#canvas?:ComponentRef<FlowCanvas>;
	#reads = new Latest();//the counts and the list, of one range
	#lineageReads = new Latest();
	#player?:FlowPlayer;
	#playReading = false;
	#ticker?:ReturnType<typeof setInterval>;
	#flows = inject( FlowService );
	#taken?:number;//the request answered
	#loaded?:FlowGraph;//what the canvas holds
	//the flow, by its gateway and id:  a deploy, an enable or a disable gives the page a new row of the same flow
	#shown = computed( ()=>({ gateway: this.gateway(), id: this.flow().id }), {equal: ( a, b )=>a.gateway==b.gateway && a.id==b.id} );

	//the version in force at the scrubber, its graph drawn
	version = computed<FlowVersion|undefined>( ()=>flowVersionAt( this.versions().filter( v=>!v.unread ), new Date( this.at() ) ) );
	graph = computed<FlowGraph|undefined>( ()=>this.version()?.graph );
	marks = computed( ()=>{ const r = this.range(); return r ? flowDeployMarks( this.versions(), r.start, r.end ) : []; } );
	//the nodes the filter offers:  every version's, by id, named by the latest
	nodes = computed( ()=>{
		const y = new Map<string,string>();
		for( const v of [...this.versions()].sort( ( a, b )=>a.version-b.version ) )
			for( const n of v.graph.nodes )
				y.set( n.id, n.name || n.type );
		return [...y].map( ( [id, name] )=>({ id, name }) );
	} );
	graphCounts = computed( ()=>{ const g = this.graph(); return g ? flowGraphCounts( this.counts(), g ) : undefined; } );
	path = computed( ()=>{
		const s = this.selected(), l = this.lineage(), g = this.graph();
		return s?.msgId && l && g ? flowLineagePath( l.records, s.msgId, g ) : undefined;
	} );
	own = computed( ()=>{ const s = this.selected(), l = this.lineage(); return s?.msgId && l ? flowOwnRecords( l.records, s.msgId ) : []; } );
	kin = computed( ()=>{ const l = this.lineage(), own = this.own().length; return l ? l.records.filter( r=>r.msgId ).length-own : 0; } );
	decor = computed<FlowCanvasDecor>( ()=>{
		const counts = this.graphCounts(), path = this.path(), fired = this.fired();
		const nodes = new Map<string,FlowNodeDecor>(), wires = new Map<string,FlowWireDecor>();
		if( counts ){
			for( const [id, c] of counts.nodes )
				nodes.set( id, { counts: c } );
			for( const [key, n] of counts.wires )
				wires.set( key, { label: String( n ) } );
		}
		const lit = ( map:Map<string,{lit?:FlowLit}>, key:string, how:FlowLit )=>map.set( key, { ...(map.get( key ) ?? {}), lit: how } );
		if( path ){
			path.kin.nodes.forEach( n=>lit( nodes, n, 'kin' ) );
			path.kin.wires.forEach( w=>lit( wires, w, 'kin' ) );
			path.own.nodes.forEach( n=>lit( nodes, n, 'own' ) );
			path.own.wires.forEach( w=>lit( wires, w, 'own' ) );
		}
		for( const key of fired.keys() )
			lit( key.includes( '>' ) ? wires : nodes, key, 'fired' );
		return { nodes, wires };
	} );
	total = computed( ()=>{
		let received = 0, errors = 0, drops = 0;
		for( const row of this.counts() ){
			received += row.counts.received;
			errors += row.counts.errors;
			drops += row.counts.drops;
		}
		return { received, errors, drops };
	} );

	constructor(){
		afterNextRender( ()=>this.#import() );
		effect( ()=>{//another flow:  its runs, and the last hour of its live trace
			const {gateway, id} = this.#shown();
			untracked( ()=>{
				this.run.set( undefined );
				this.#loadRuns( gateway, id );
				this.read();
			} );
		} );
		effect( ()=>{//a run asked by Versions
			const request = this.request();
			if( request && request.n!=this.#taken ){
				this.#taken = request.n;
				untracked( ()=>this.openRun( request.run ) );
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
	ngOnDestroy(){ this.stop(); }
	async #import():Promise<void>{
		try{
			const canvas = await loadFlowView( this.slot() );
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
	async #loadRuns( gateway:HistReader, flow:number ):Promise<void>{
		try{
			this.runs.set( await this.#flows.runs( gateway, flow ) );
		}
		catch{
			this.runs.set( [] );//the select offers the live trace alone
		}
	}
	//the last hour, day or week, to now
	preset( ms:number ):void{
		const now = new Date();
		this.from.set( localInput( new Date( now.getTime()-ms ) ) );
		this.to.set( localInput( now ) );
		this.run.set( undefined );
		this.read();
	}
	//a run picked:  its range, and its trace
	pickRun( id:number|undefined ):void{
		const run = id==undefined ? undefined : this.runs().find( r=>r.id==id );
		this.openRun( run );
	}
	openRun( run:FlowRun|undefined ):void{
		this.run.set( run );
		if( run?.start && run.finish ){
			this.from.set( localInput( run.start ) );
			this.to.set( localInput( new Date( run.finish.getTime()+60_000 ) ) );//to the minute, inclusive of the run's last
		}
		this.read();
	}
	//The range read anew:  the counts over it, in pages of whole buckets, and the list's first page.  The scrubber goes to the
	//end, the version in force there drawn, and what was selected or playing is let go.
	async read():Promise<void>{
		if( this.problem() )
			return;
		const range:FlowRange = { start: new Date( this.from() ), end: new Date( this.to() ) };
		this.stop();
		this.#player = undefined;
		this.range.set( range );
		this.at.set( range.end.getTime() );
		this.selected.set( undefined );
		this.lineage.set( undefined );
		this.records.set( [] );
		this.continuation.set( undefined );
		this.counts.set( [] );
		this.countsError.set( undefined );
		this.listError.set( undefined );
		const read = this.#reads.next();
		await Promise.all( [this.#readCounts( read, range ), this.#readList( read, range )] );
	}
	#args(){ return { flow: this.flow().id, run: this.run()?.id }; }
	async #readCounts( read:number, range:FlowRange ):Promise<void>{
		this.loading.update( n=>n+1 );
		try{
			const by = flowCountStep( range.start, range.end ).by;
			let continuation:string|undefined, rows:FlowCountRow[] = [];
			do{
				const page = await this.#flows.counts( this.gateway(), { ...this.#args(), start: range.start, end: range.end, by, continuation } );
				if( !this.#reads.is( read ) )
					return;
				rows = rows.concat( page.counts );
				continuation = page.continuation;
			}while( continuation );
			this.counts.set( rows );
		}
		catch( e ){
			if( this.#reads.is( read ) )
				this.countsError.set( errorMessage( e ) );
		}
		finally{
			this.loading.update( n=>n-1 );
		}
	}
	async #readList( read:number, range:FlowRange, continuation?:string ):Promise<void>{
		this.loading.update( n=>n+1 );
		try{
			const msg = this.filterMsg().trim();
			const page = await this.#flows.trace( this.gateway(), { ...this.#args(), start: range.start, end: range.end, node: this.filterNode(), kind: this.filterKind(), msg: msg || undefined, limit: flowHistoryPage, continuation } );
			if( !this.#reads.is( read ) )
				return;
			this.records.update( r=>continuation ? r.concat( page.records ) : page.records );
			this.continuation.set( page.continuation );
		}
		catch( e ){
			if( this.#reads.is( read ) )
				this.listError.set( errorMessage( e ) );
		}
		finally{
			this.loading.update( n=>n-1 );
		}
	}
	//the list again under its filters, the counts kept
	async filter():Promise<void>{
		const range = this.range();
		if( !range )
			return;
		this.records.set( [] );
		this.continuation.set( undefined );
		this.listError.set( undefined );
		await this.#readList( this.#reads.next(), range );
	}
	async more():Promise<void>{
		const range = this.range(), continuation = this.continuation();
		if( range && continuation )
			await this.#readList( this.#reads.current, range, continuation );
	}
	//a record picked:  its message's lineage, the path lit and the scrubber at its time
	async select( record:FlowTraceRecord ):Promise<void>{
		if( this.selected()==record ){
			this.selected.set( undefined );
			this.lineage.set( undefined );
			return;
		}
		this.selected.set( record );
		this.lineage.set( undefined );
		this.lineageError.set( undefined );
		this.at.set( record.time.getTime() );
		if( !record.msgId )
			return;
		const read = this.#lineageReads.next(), range = this.range();
		try{
			const lineage = await this.#flows.message( this.gateway(), { flow: this.flow().id, id: record.msgId, run: this.run()?.id, start: range?.start, end: range?.end } );
			if( this.#lineageReads.is( read ) )
				this.lineage.set( lineage );
		}
		catch( e ){
			if( this.#lineageReads.is( read ) )
				this.lineageError.set( errorMessage( e ) );
		}
	}
	seek( ms:number ):void{
		this.at.set( ms );
		this.#player?.seek( ms );
	}
	//Play:  the range's records read ahead a page at a time and stepped through the canvas at the speed, each lighting its node
	//and the wires it sent on for a moment, the scrubber following.
	play():void{
		const range = this.range();
		if( !range || this.playing() )
			return;
		if( !this.#player || this.#player.start.getTime()!=range.start.getTime() || this.#player.end.getTime()!=range.end.getTime() ){
			this.#player = new FlowPlayer( range.start, range.end );
			this.#playReading = false;
			this.#playContinuation = undefined;
		}
		this.#player.speed = this.speed();
		if( this.#player.done || this.at()>=range.end.getTime() )
			this.#player.seek( range.start.getTime() );
		else
			this.#player.seek( this.at() );
		this.at.set( this.#player.at );
		this.#player.play( Date.now() );
		this.playing.set( true );
		this.playError.set( undefined );
		this.#ticker = setInterval( ()=>this.tick( Date.now() ), flowPlayTick );
		this.#feed();
	}
	pause():void{
		this.#player?.pause();
		this.playing.set( false );
		clearInterval( this.#ticker );
		this.#ticker = undefined;
	}
	stop():void{
		this.pause();
		this.fired.set( new Map() );
	}
	setSpeed( speed:number ):void{
		this.speed.set( speed );
		if( this.#player )
			this.#player.speed = speed;
	}
	//a step of the wall clock:  what the player passed lights up, what lit a while ago goes dark, and the next page is read
	//once the player nears the end of what it holds
	tick( now:number ):void{
		const player = this.#player;
		if( !player )
			return;
		const graph = this.graph();
		const fired = new Map( [...this.fired()].filter( ( [, at] )=>now-at<flowFiredFor ) );
		for( const r of player.tick( now ) ){
			fired.set( r.node, now );
			const node = graph?.nodes.find( n=>n.id==r.node );
			for( const port of r.ports )
				for( const to of node?.wires[port] ?? [] )
					fired.set( flowWireKey( r.node, port, to ), now );
		}
		this.fired.set( fired );
		this.at.set( player.at );
		if( !player.playing ){
			this.playing.set( false );
			clearInterval( this.#ticker );
			this.#ticker = undefined;
		}
		else if( player.held<flowPlayPage/2 )
			this.#feed();
	}
	#playContinuation?:string;
	async #feed():Promise<void>{
		const player = this.#player, range = this.range();
		if( !player || !range || player.complete || this.#playReading )
			return;
		this.#playReading = true;
		try{
			const page = await this.#flows.trace( this.gateway(), { ...this.#args(), start: range.start, end: range.end, limit: flowPlayPage, continuation: this.#playContinuation } );
			if( this.#player!=player )
				return;
			this.#playContinuation = page.continuation;
			player.add( page.records, !page.continuation );
		}
		catch( e ){
			this.playError.set( errorMessage( e ) );
			this.pause();
		}
		finally{
			this.#playReading = false;
		}
	}
	nodeName( id:string ):string{
		const node = this.graph()?.nodes.find( n=>n.id==id );
		return node ? node.name || node.type : this.nodes().find( n=>n.id==id )?.name ?? id;
	}
	summary( r:FlowTraceRecord ):string{ return flowRecordSummary( r ); }
	json( v:unknown ):string{ return flowJsonText( v ); }
	runText( r:FlowRun ):string{ return `Replay ${r.id}, version ${r.version}${r.status ? `, ${r.status.split( ':' )[0]}` : ', under way'}`; }
	stepText = computed( ()=>{ const r = this.range(); return r ? flowCountStep( r.start, r.end ).by : ''; } );
}