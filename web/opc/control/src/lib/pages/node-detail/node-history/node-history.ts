import { Component, computed, effect, inject, input, linkedSignal, OnDestroy, signal, untracked } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatButtonToggleModule } from '@angular/material/button-toggle';
import { MatChipsModule } from '@angular/material/chips';
import { MatDialog } from '@angular/material/dialog';
import { MatIconModule } from '@angular/material/icon';
import { MatMenuModule } from '@angular/material/menu';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatSlideToggleModule } from '@angular/material/slide-toggle';
import { MatToolbarModule } from '@angular/material/toolbar';
import { MatTooltipModule } from '@angular/material/tooltip';
import { Subscription } from 'rxjs';
import { confirm, errorText, SnackbarService } from 'jde-framework';
import { alignUp, applyHistModification, editRefused, HistAggregate, HistAggregation, HistCapabilities, HistEditArgs, HistEditKind, HistEditResult, HistGroupMembers, HistNodeStatus, HistPage, HistPushedModification, HistReadArgs, HistRights, HistSource, histSourceKey, HistValue, isNoData, latestHistTime, mergeHistValues, pushValue } from '../../../model/hist';
import { Variable } from '../../../model/node';
import { NodeId, NodeKey } from '../../../model/node-id';
import { OpcError } from '../../../model/opc-error';
import { isBad } from '../../../model/status-code';
import { StatusCode } from '../../../model/types';
import { Gateway, HistEvent, SubscriptionResult } from '../../../services/gateway-service';
import { HistoryService } from '../../../services/history-service';
import { NodePageData } from '../../../services/resolvers/node-resolver';
import { HistAggregatePicker } from './hist-aggregate-picker';
import { HistEditDialog, HistEditPreset } from './hist-edit-dialog';
import { HistTable } from './hist-table';
import { HistTrend } from './hist-trend';

//what the tab shows:  the values, trended and listed, or the modifications made to them, listed with their ModificationInfo
export type HistMode = 'values'|'modified';
//Where the tab's history comes from:  the server's own, over the connection (spec *Pass-through*), or a group the gateway's
//historian keeps (spec *Reads*), with the children it offers.  `key` is histSourceKey's.
export type HistSourceOption = { key:string; label:string; source:HistSource; nodes:Variable[] };

//The History tab:  the node's children the server historizes, read through the gateway over the caller's own session (spec
//*Pass-through*), and those a group of the gateway's historian holds, read from the group - one source at a time, the server's
//first and a toggle for the rest, since `history` reads one group a call and a node is in one group per connection at most -
//drawn as a trend and listed in a table.  A server source opens on the latest values - a reverse
//read with only `end` - and Load earlier pages that read by its continuation.  Its live tail is the node's /opc
//subscription, joined to the read by source time with duplicates dropped (mergeHistValues).  A group source's compressed
//series would take the raw changes, so its live trend is HistSubscribe's (spec *Subscriptions*):  it opens on the snapshot,
//the last snapshotWindow of the group's history from each node's bound, which has no gap to the stream after it, the records
//as the historian stores them, and a modification pushed redraws the point it targets;  Load earlier is a reverse read ending
//where the snapshot began.  With Live off a group opens on a read, as a server does.  Modifications is the same read
//with `modified`, listed alone:  an edit isn't published live and isn't a series.  The picker reads an aggregate instead of the
//values, one of those the server lists, over intervals of the length picked (spec *Reads*):  the last intervals up to now, and
//Load earlier the intervals before, with no live tail, since a push is a reading, not an aggregate.  The edits (spec *Edits*)
//go through the dialog, or a row's own Replace and Delete, and the tab reads again what it held once one is taken;  on a server
//the node's HistoryWrite bit lets them, on a group the Update and Delete the gateway says it granted (historyCapabilities).
@Component({
	selector: 'node-history',
	templateUrl: './node-history.html',
	styleUrls: ['./node-history.scss'],
	imports: [MatButtonModule, MatButtonToggleModule, MatChipsModule, MatIconModule, MatMenuModule, MatProgressBarModule, MatSlideToggleModule, MatToolbarModule, MatTooltipModule, HistAggregatePicker, HistTable, HistTrend]
})
export class NodeHistory implements OnDestroy{
	constructor(){
		//a change of source, of nodes, of mode or of aggregate is a new read:  the values of a node unticked go, and the live tail follows the selection
		effect( ()=>{ this.histSource(); const nodes = this.selected(), mode = this.mode(), aggregation = this.aggregated(); untracked( ()=>this.#reload(nodes, mode, aggregation) ); } );
		//what the source lists for the picker, and on a group what this user may edit:  once per source
		effect( ()=>{ const key = this.sourceKey(), source = this.histSource(); untracked( ()=>this.#loadSource( key, source ) ); } );
	}
	ngOnDestroy(){ ++this.#generation; this.#unsubscribe(); }//a read still out is stale:  its page is for a tab that is gone

	pageData = input.required<NodePageData>();
	//the gateway and connection read through:  what the aggregates listed, and the one picked, belong to
	connection = computed<string>( ()=>`${this.pageData().gateway.slug}/${this.pageData().server.connection.slug}` );
	//NodeDetail.historizable, the server source's nodes:  the children the server historizes and this user may read
	candidates = input.required<Variable[]>();
	//NodeDetail.groups:  the gateway's groups that hold any of the children, each with its members among them
	groups = input<HistGroupMembers[]>( [] );
	sources = computed<HistSourceOption[]>( ()=>{
		const y:HistSourceOption[] = [];
		if( this.candidates().length )
			y.push( { key: histSourceKey( {opc: this.cnnctnSlug} ), label: 'Server', source: {opc: this.cnnctnSlug}, nodes: this.candidates()} );
		for( const g of this.groups() )
			y.push( { key: histSourceKey( {group: g.id} ), label: g.name, source: {group: g.id}, nodes: g.nodes} );
		return y;
	} );
	//the source picked, while it is still offered - a Children refresh keeps it - else the first
	picked = linkedSignal<HistSourceOption[], string|undefined>( {source: this.sources, computation: ( sources, previous )=>( sources.find( s=>s.key==previous?.value ) ?? sources[0] )?.key} );
	source = computed<HistSourceOption|undefined>( ()=>this.sources().find( s=>s.key==this.picked() ) );
	histSource = computed<HistSource>( ()=>this.source()?.source ?? {opc: this.cnnctnSlug} );
	isGroup = computed( ()=>this.histSource().group!=undefined );
	//what belongs to one source on one gateway and connection:  the aggregates listed, the aggregate picked, a group's capabilities
	sourceKey = computed( ()=>`${this.connection()}/${histSourceKey( this.histSource() )}` );
	pick( key:string ){ this.picked.set( key ); }
	//the source's nodes, for the chips
	nodes = computed<Variable[]>( ()=>this.source()?.nodes ?? [] );
	//the first eight by default:  the trend's palette has eight slots, assigned in order and never cycled
	selectedKeys = linkedSignal<Set<NodeKey>>( ()=>new Set( this.nodes().slice(0, NodeHistory.maxSeries).map(v=>v.key) ) );
	selected = computed<Variable[]>( ()=>this.nodes().filter( v=>this.selectedKeys().has(v.key) ) );
	//a group's capabilities, asked when the tab opens on it (historyCapabilities):  its aggregates, and whether this user may edit it
	capabilities = linkedSignal<string, HistCapabilities|undefined>( {source: this.sourceKey, computation: ()=>undefined} );
	//what the edits are gated by:  on a server each node's HistoryWrite bit stands for both (writable);  on a group, Update and
	//Delete granted on an enforced resource, which the gateway refuses the edits without (spec *Authorization*)
	rights = computed<HistRights>( ()=>this.isGroup() ? { update: !!this.capabilities()?.update, delete: !!this.capabilities()?.delete } : { update: true, delete: true } );
	//the nodes this user may edit:  the Edit menu's and the rows' gate.  Not an array, which the dialog has no editor for, as the
	//Children tab has none.
	writable = computed<Variable[]>( ()=>{
		const rights = this.rights();
		return this.selected().filter( v=>!v.isArray && ( this.isGroup() ? rights.update || rights.delete : v.historyWritable ) );
	} );
	//the live tail:  the node's /opc subscription, the raw values, on a server source;  the historian's stream on a group
	canLive = computed( ()=>this.mode()=='values' && !this.aggregated() );
	//a group's subscription ended by the gateway:  the group removed, or the historian stopped (spec *Subscriptions*)
	ended = signal<string|undefined>( undefined );
	//a group's subscription lost with the socket:  the gateway subscribes again once it is back, from the latest value held
	dropped = signal( false );
	answerer = computed( ()=>this.isGroup() ? "the gateway" : "the server" );
	canSelect( v:Variable ):boolean{ return this.selectedKeys().has( v.key ) || this.selectedKeys().size<NodeHistory.maxSeries; }
	toggle( v:Variable ){
		this.selectedKeys.update( keys=>{
			if( !keys.has(v.key) && keys.size>=NodeHistory.maxSeries )
				return keys;//the same set back:  no change, so no re-read
			const next = new Set( keys );
			if( !next.delete(v.key) )
				next.add( v.key );
			return next;
		} );
	}

	values = signal<HistValue[]>( [] );//in source-time order
	nodeStatuses = signal<HistNodeStatus[]>( [] );
	refused = computed<HistNodeStatus[]>( ()=>this.nodeStatuses().filter( n=>isBad(n.status) ) );//a node the server would not read for this user, with its reason
	loading = signal( false );
	hasEarlier = signal( true );
	stepped = signal( true );
	live = signal( true );
	mode = signal<HistMode>( 'values' );
	aggregates = linkedSignal<string,HistAggregate[]>( {source: this.sourceKey, computation: ()=>[]} );
	//the picker's:  none reads the values.  None on another source, which may not have the aggregate picked:  the read
	//naming it there failed (historian-aggregate-picker #3).
	aggregation = linkedSignal<string,HistAggregation|undefined>( {source: this.sourceKey, computation: ()=>undefined} );
	//what the read is of:  an aggregate in the Values mode alone, since the modifications are records
	aggregated = computed<HistAggregation|undefined>( ()=>this.mode()=='values' ? this.aggregation() : undefined );

	async #reload( nodes:Variable[], mode:HistMode, aggregation:HistAggregation|undefined ){
		const generation = ++this.#generation;
		this.#unsubscribe();
		this.#next = undefined;
		this.#pages = 0;
		this.#tail = undefined;
		this.#earliest = undefined;
		this.values.set( [] );
		this.nodeStatuses.set( [] );
		this.hasEarlier.set( nodes.length>0 );
		this.loading.set( false );//a read still out is stale now, and leaves loading as it is
		if( !nodes.length )
			return;
		if( this.live() && this.canLive() ){
			if( this.isGroup() ){//the snapshot is the opening, and Load earlier reads before it
				const from = new Date( Date.now()-NodeHistory.snapshotWindow );
				this.#next = { nodes: nodes.map( n=>n.nodeId ), end: from, limit: NodeHistory.pageSize };
				this.#subscribeHist( nodes, from );
				return;
			}
			//the tail first, and the read past it:  a value recorded after the read's end and before the subscription is in neither
			this.#subscribe( nodes );
		}
		await this.#read( this.#opening( nodes, mode, aggregation, 1 ), generation );
	}
	//"last N":  a reverse read with only `end` (spec *Reads*), modified in the Modifications mode, which the gateway pages by
	//time and count as it pages the values.  An aggregate read needs both ends and goes forward, since a reverse one stamps each
	//interval at its later edge (spec *Reads*):  the last intervals a load holds, up to the interval boundary past now, so the
	//intervals sit on the clock's and the interval holding now is the last - without the slack, which here would be whole
	//intervals of Bad_NoData at the top of the table, and none before the floor.  `pages`:  how many a refresh reads in one - for
	//an aggregate, the range from the earliest loaded, whatever its loads.
	#opening( nodes:Variable[], mode:HistMode, aggregation:HistAggregation|undefined, pages:number ):HistReadArgs{
		const ids = nodes.map( n=>n.nodeId ), now = Date.now();
		if( !aggregation )
			return { nodes: ids, end: new Date( now+NodeHistory.endSlack ), limit: pages*NodeHistory.pageSize, ...(mode=='modified' ? {modified: true} : {}) };
		const interval = aggregation.interval, end = alignUp( now, interval );
		const start = this.#earliest ?? NodeHistory.start( end, interval, nodes.length );
		return { nodes: ids, start: new Date( start ), end: new Date( end ), interval, aggregate: aggregation.aggregate, limit: Math.ceil( (end-start)/interval )*nodes.length };
	}
	//the intervals a load holds:  one value a node each, within a page
	static intervals( nodes:number ):number{ return Math.max( 1, Math.floor( NodeHistory.pageSize/nodes ) ); }
	//an aggregate range's start:  as many whole intervals before `end` as a load holds and the floor leaves, or the floor when
	//not one does - an interval longer than all the time since
	static start( end:number, interval:number, nodes:number ):number{
		const intervals = Math.min( NodeHistory.intervals( nodes ), Math.floor( (end-NodeHistory.floor)/interval ) );
		return intervals>0 ? end-intervals*interval : NodeHistory.floor;
	}
	//the range before an aggregate read's, ending where it started, or at `end`
	#before( args:HistReadArgs, end=args.start!.getTime() ):HistReadArgs{
		const start = NodeHistory.start( end, args.interval!, args.nodes.length );
		return { ...args, start: new Date( start ), end: new Date( end ), limit: Math.ceil( (end-start)/args.interval! )*args.nodes.length, continuation: undefined };
	}
	//the latest record of the nodes before an aggregate range, raw:  undefined at the start of their history
	async #lastBefore( args:HistReadArgs ):Promise<number|undefined>{
		const page = await this.#history.read( this.gateway, this.histSource(), {nodes: args.nodes, end: args.start, limit: 1}, m=>console.log(m) );
		return page.values.find( v=>v.source )?.source!.getTime();
	}
	//paged back by its continuation - the read's exact place, which neither the oldest value held (a live push can be older
	//than the page) nor a Date (cut to the millisecond) can stand in for - a page at a time, whatever this read's limit.  A
	//read for a selection since changed is dropped, not merged.  `pages`:  how many this read stands for, a refresh's several.
	//An aggregate read is a range a load:  its pages, several only when the gateway capped the limit, are read here at the
	//read's limit, which the gateway caps again - at a page's, a tenth of its cap a round trip (historian-aggregate-picker #6).
	//They are taken in together, since it goes forward and its first page alone is the oldest intervals - a refresh's showed
	//them in place of all held until the rest landed (#4).  Load earlier is the range before, while a whole interval fits
	//above the floor.  Past a range of nothing but Bad_NoData, the answer for an interval with no record, it is the range
	//ending with the interval of the latest record before - a gap, on a server that records on change - and there is none at
	//the start of the history.
	async #read( args:HistReadArgs, generation:number, pages=1 ){
		this.loading.set( true );
		try{
			let page = await this.#page( args, generation );
			if( !page )
				return;
			this.#pages += pages;
			let next:HistReadArgs|undefined = page.continuation==null ? undefined : { ...args, limit: args.aggregate ? args.limit : NodeHistory.pageSize, continuation: page.continuation };
			if( !args.aggregate )
				this.#take( page );
			else{
				const values = [...page.values];
				while( next ){
					page = await this.#page( next, generation );
					if( !page )
						return;
					values.push( ...page.values );
					next = page.continuation==null ? undefined : { ...next, continuation: page.continuation };
				}
				this.#take( {...page, values} );
				this.#earliest = args.start!.getTime();
				if( this.#earliest-NodeHistory.floor<args.interval! )
					next = undefined;
				else if( values.some( v=>!isNoData(v.status) ) )
					next = this.#before( args );
				else{
					const last = await this.#lastBefore( args );
					if( generation!=this.#generation )
						return;
					next = last==undefined || last<NodeHistory.floor ? undefined : this.#before( args, Math.min( alignUp(last+1, args.interval!), this.#earliest ) );
				}
			}
			this.#next = next;
			this.hasEarlier.set( next!=undefined );
		}
		catch( e ){
			if( generation!=this.#generation )
				return;
			this.#next = undefined;
			this.hasEarlier.set( false );
			if( this.#tail ){//what was held is from before the edit
				this.values.set( this.#tail );
				this.nodeStatuses.set( [] );
				this.#tail = undefined;
			}
			this.snackbar.exception( "Could not read the history.", e );
		}
		finally{
			if( generation==this.#generation )
				this.loading.set( false );
		}
	}
	//one page, or undefined for a selection since changed, whose page is dropped
	async #page( args:HistReadArgs, generation:number ):Promise<HistPage|undefined>{
		const page = await this.#history.read( this.gateway, this.histSource(), args, m=>console.log(m) );
		if( generation!=this.#generation )
			return undefined;
		this.#fetchNames( [...page.values.map( v=>v.status ), ...page.nodes.map( n=>n.status )] );
		return page;
	}
	//a page's values merged in, or a refresh's in place of what was held
	#take( page:HistPage ){
		this.nodeStatuses.set( page.nodes );
		if( this.#tail ){
			this.values.set( mergeHistValues( this.#tail, page.values ) );
			this.#tail = undefined;
		}
		else
			this.values.update( v=>mergeHistValues( v, page.values ) );
	}
	loadEarlier(){
		if( this.#next && !this.loading() )
			this.#read( this.#next, this.#generation );
	}
	//what the tab holds, read again after an edit:  one read as long as the pages loaded, since `limit` is outside the
	//continuation's hash, so Load earlier still pages before it.  A fresh list, not a merge - a merge keeps the record it
	//holds at a time, which is the one the edit changed.  The live tail stays.  What is held stays shown until the page
	//lands:  emptied, it sent the table to its first page and the trend blank (historian-web-edits #12).
	#refresh(){
		const nodes = this.selected();
		if( !nodes.length )
			return;
		const generation = ++this.#generation;
		this.#next = undefined;
		const pages = Math.max( this.#pages, 1 );
		this.#pages = 0;
		this.#tail = [];
		this.#read( this.#opening( nodes, this.mode(), this.aggregated(), pages ), generation, pages );
	}
	//what the source lists for the picker:  a server's AggregateFunctions folder, once per connection, or a group's capabilities,
	//its aggregates with this user's rights on it, asked each time the tab opens on it.  A server that lists none, or refuses the
	//browse, and a group the gateway won't describe leave the raw values alone to pick, and a group's edits off.
	async #loadSource( key:string, source:HistSource ){
		let list:HistAggregate[] = [], capabilities:HistCapabilities|undefined;
		try{
			if( source.group!=undefined ){
				capabilities = await this.#history.capabilities( this.gateway, source.group, m=>console.log(m) );
				list = capabilities.aggregates;
			}
			else
				list = await this.#history.aggregates( this.gateway, source.opc, m=>console.log(m) );
		}
		catch( e ){
			console.warn( `Could not describe the history of ${key}.`, e );
		}
		if( key!=this.sourceKey() )
			return;
		this.aggregates.set( list );
		this.capabilities.set( capabilities );
	}
	//Live on again:  a server source's tail is the pushes from here;  a group's subscribes from the latest value held, so the
	//snapshot fills what passed meanwhile, or from the opening's window when nothing is
	onLiveChange( on:boolean ){
		this.live.set( on );
		if( !on )
			this.#unsubscribe();
		else if( this.isGroup() )
			this.#subscribeHist( this.selected(), latestHistTime( this.values() ) ?? new Date( Date.now()-NodeHistory.snapshotWindow ) );
		else
			this.#subscribe( this.selected() );
	}
	//the node's /opc subscription, under an owner key of this tab's own:  the gateway service counts owners per node, so the
	//Children tab's ticks and this tail share the server-side item and neither drops the other's.
	#subscribe( nodes:Variable[] ){
		this.#unsubscribe();
		if( !nodes.length || !this.canLive() || this.isGroup() )
			return;
		const byKey = new Map( nodes.map( n=>[n.key, n] ) );
		this.#subscription = this.gateway.subscribe( this.cnnctnSlug, nodes.map( n=>n.nodeId ), this.#owner ).subscribe({
			next: ( r:SubscriptionResult )=>{
				const node = byKey.get( r.node.key );
				if( !node )
					return;
				if( r.value instanceof OpcError )//a refused subscription, not a reading
					return this.snackbar.exception( `Could not subscribe to ${node.name}.`, r.value );
				const pushed = [pushValue( node.nodeId, r, new Date() )];
				this.values.update( v=>mergeHistValues( v, pushed ) );
				if( this.#tail )
					this.#tail = mergeHistValues( this.#tail, pushed );
				if( r.sc )
					this.#fetchNames( [r.sc] );
			},
			error: ( e:Error )=>{ this.#subscription = undefined; this.live.set( false ); this.snackbar.exception( "Subscription error.", e ); },//nothing resubscribes:  the toggle says so
			complete: ()=>{ this.#subscription = undefined; }
		});
	}
	//The group's subscription (spec *Subscriptions*), from `from`:  the ack's statuses stand where a read's node statuses do, the
	//snapshot's pages merge in with loading on until the last, the stream's values merge in as the tail's pushes do, and a
	//modification pushed is applied to the record it targets.  The gateway subscribes again after a drop, from the latest value it
	//delivered, and says so here meanwhile;  an exception ends it, and Live turns off with the reason shown.
	#subscribeHist( nodes:Variable[], from:Date ){
		this.#unsubscribe();
		const group = this.histSource().group;
		if( !nodes.length || !this.canLive() || group==undefined )
			return;
		const ids = nodes.map( n=>n.nodeId );
		this.ended.set( undefined );
		this.dropped.set( false );
		this.loading.set( true );
		this.#subscription = this.gateway.histSubscribe( {group, nodes: ids, from} ).subscribe({
			next: ( e:HistEvent )=>{
				switch( e.kind ){
					case 'ack':
						this.dropped.set( false );
						this.loading.set( true );
						this.nodeStatuses.set( e.statuses.map( (status, i)=>({node: ids[i], status}) ) );
						this.#fetchNames( e.statuses );
						break;
					case 'snapshot':
						this.#takeLive( e.values );
						if( !e.more )
							this.loading.set( false );
						break;
					case 'records':
						this.#takeLive( e.values );
						for( const m of e.modifications )
							this.#modify( m );
						break;
					case 'dropped':
						this.dropped.set( true );
						this.loading.set( false );//a snapshot cut short:  the rest comes with the next
						break;
				}
			},
			error: ( e:unknown )=>{
				this.#subscription = undefined;
				this.live.set( false );
				this.loading.set( false );
				this.dropped.set( false );
				this.ended.set( `The subscription ended:  ${errorText( e ) ?? "unknown error"}` );
			},
			complete: ()=>{ this.#subscription = undefined; }
		});
	}
	//whether a group's stream is on:  an edit then needs no read, its modification is pushed
	get #liveGroup():boolean{ return this.isGroup() && this.#subscription!=undefined; }
	#takeLive( values:HistValue[] ){
		if( !values.length )
			return;
		this.values.update( v=>mergeHistValues( v, values ) );
		if( this.#tail )
			this.#tail = mergeHistValues( this.#tail, values );
		this.#fetchNames( values.map( v=>v.status ) );
	}
	#modify( m:HistPushedModification ){
		this.values.update( v=>applyHistModification( v, m ) );
		if( this.#tail )
			this.#tail = applyHistModification( this.#tail, m );
		if( m.value?.status )
			this.#fetchNames( [m.value.status] );
	}
	#unsubscribe(){ this.#subscription?.unsubscribe(); this.#subscription = undefined; }
	//a code's name comes from the gateway on request - OpcError.statusCodeText registers it, updateErrorCodes fetches the lot - so
	//one fetch per page, and a repaint when it lands.  Each code registered, not only up to the first unnamed.
	#fetchNames( codes:StatusCode[] ):Promise<void>{
		return codes.filter( sc=>sc && !OpcError.statusCodeText(sc) ).length ? this.gateway.updateErrorCodes().catch( e=>console.warn(e) ) : Promise.resolve();
	}
	//An edit through the dialog, which sends it and closes on one the server took any of;  then the tab reads again what it held,
	//unless a group's stream pushes the modification, and says what was refused.
	async openEdit( kind:HistEditKind, preset?:HistEditPreset ){
		const result = await HistEditDialog.open( this.dialog, { kind, nodes: this.writable(), preset, edit: args=>this.#edit( args ), names: codes=>this.#fetchNames( codes ) } );
		if( !result )
			return;
		if( !this.#liveGroup )
			this.#refresh();
		if( editRefused( result ) )
			this.snackbar.warn( `${NodeHistory.done[kind]}  ${await this.#refusals( result )}` );
		else
			this.snackbar.info( NodeHistory.done[kind] );
	}
	//the parts the server refused, by name:  each value's, or each node's for a range purge, the first few
	async #refusals( r:HistEditResult ):Promise<string>{
		let refused = r.values.filter( v=>isBad(v.status) ).map( v=>({node: v.node, time: v.source, status: v.status}) );
		if( !refused.length )
			refused = r.nodes.filter( n=>isBad(n.status) ).map( n=>({node: n.node, time: null, status: n.status}) );
		await this.#fetchNames( refused.map( x=>x.status ) );
		const shown = refused.slice( 0, NodeHistory.refusalsShown ).map( x=>`${this.nodeName( x.node )}${x.time ? ` at ${x.time.toLocaleString()}` : ""}: ${OpcError.text( x.status )}` );
		const more = refused.length-shown.length;
		return `Refused for ${shown.join( ";  " )}${more ? `, and ${more} more` : ""}.`;
	}
	//a row's Replace:  the dialog on the value's node, time and value, with the status blank, which is Good
	replaceValue( v:HistValue ){
		const node = this.writable().find( n=>n.key==v.node.key );
		if( node && v.source && v.sourceTime && this.rights().update )
			this.openEdit( 'replace', {node, time: v.source, sourceTime: v.sourceTime, value: v.value} );
	}
	//a row's Delete:  a range purge from the value's time to itself, the one value there on a group, OpcServer and open62541's
	//backend alike (spec *Pass-through*), where a purge at the time is what OpcServer refuses.  The time to the tick:  a
	//millisecond's range would miss a record stamped inside it.
	async deleteValue( v:HistValue ){
		if( !v.source || !v.sourceTime || !this.rights().delete || !this.writable().some( n=>n.key==v.node.key ) )
			return;
		const name = this.nodeName( v.node );
		const where = this.isGroup() ? "the gateway's history of the group, which keeps the deleted value as a modification." : "the server's history.  The bundled OPC server keeps the deleted value as a modification.  Another server may not.";
		if( !await confirm( this.dialog, {title: "Delete this value?", message: `${name} at ${v.source.toLocaleString()} will be deleted from ${where}`, confirm: "Delete", destructive: true} ) )
			return;
		try{
			const result = await this.#edit( {kind: 'purgeRange', nodes: [v.node], start: v.sourceTime, end: v.sourceTime} );
			if( editRefused( result ) ){
				const status = result.nodes.find( n=>isBad(n.status) )?.status ?? result.values.find( x=>isBad(x.status) )?.status ?? 0;
				await this.#fetchNames( [status] );//a snackbar's text doesn't repaint when the name lands
				return this.snackbar.error( `Could not delete the value:  ${this.answerer()} answered ${OpcError.text( status )}.` );
			}
			this.snackbar.info( NodeHistory.done.purgeRange );
			if( !this.#liveGroup )
				this.#refresh();
		}
		catch( e ){
			this.snackbar.exception( "Could not delete the value.", e );
		}
	}
	#edit( args:HistEditArgs ):Promise<HistEditResult>{ return this.#history.edit( this.gateway, this.histSource(), args, m=>console.log(m) ); }
	nodeName( id:NodeId ):string{ return this.nodes().find( c=>c.key==id.key )?.name ?? id.toString(); }
	statusText( sc:StatusCode ):string{ return OpcError.text( sc ); }
	get gateway():Gateway{ return this.pageData().gateway; }
	get cnnctnSlug():string{ return this.pageData().server.connection.slug; }
	get #owner():string{ return `${this.pageData().route.profileKey}/history`; }
	static readonly pageSize = 1000;//values per read:  well under the gateway's readLimit, and a trend's worth at a time
	static readonly snapshotWindow = 10*60_000;//ms of a group's history its opening snapshot holds, from the bounds before:  a trend's worth of a feed that changes every second, and Load earlier the rest
	static readonly endSlack = 60_000;//ms past the browser's now the opening read ends:  for a server clock ahead of the browser's
	static readonly floor = 0;//the Unix epoch:  no store holds a value before it, so no aggregate range starts before it (historian-aggregate-picker #2)
	static readonly maxSeries = 8;
	static readonly refusalsShown = 3;
	static readonly done:Record<HistEditKind,string> = { insert: "Inserted.", replace: "Replaced.", update: "Updated.", purgeRange: "Deleted.", purgeTimes: "Deleted." };

	#history = inject( HistoryService );
	snackbar = inject( SnackbarService );
	dialog = inject( MatDialog );
	#subscription:Subscription|undefined;
	#generation = 0;//which selection the values belong to:  a read or push for an older one is dropped
	#next:HistReadArgs|undefined;//the opening read's arguments with the last page's continuation:  the page before, if any
	#pages = 0;//pages read into `values`, so a refresh reads as far
	#earliest:number|undefined;//the start of the earliest aggregate range loaded, so a refresh reads from it
	#tail:HistValue[]|undefined;//the pushes since a refresh's read went out, kept when its page replaces the values held;  undefined but while one is out
}