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
import { confirm, SnackbarService } from 'jde-framework';
import { editRefused, HistEditArgs, HistEditKind, HistEditResult, HistNodeStatus, HistReadArgs, HistValue, mergeHistValues, pushValue } from '../../../model/hist';
import { Variable } from '../../../model/node';
import { NodeId, NodeKey } from '../../../model/node-id';
import { OpcError } from '../../../model/opc-error';
import { isBad } from '../../../model/status-code';
import { StatusCode } from '../../../model/types';
import { Gateway, SubscriptionResult } from '../../../services/gateway-service';
import { HistoryService } from '../../../services/history-service';
import { NodePageData } from '../../../services/resolvers/node-resolver';
import { HistEditDialog, HistEditPreset } from './hist-edit-dialog';
import { HistTable } from './hist-table';
import { HistTrend } from './hist-trend';

//what the tab shows:  the values, trended and listed, or the modifications made to them, listed with their ModificationInfo
export type HistMode = 'values'|'modified';

//The History tab:  the node's children the server historizes, read through the gateway over
//the caller's own session (spec *Pass-through*), drawn as a trend and listed in a table.  It opens on the latest values - a reverse
//read with only `end` - and Load earlier pages that read by its continuation.  The live tail is the node's /opc
//subscription, joined to the read by source time with duplicates dropped (mergeHistValues).  Modifications is the same read
//with `modified`, listed alone:  an edit isn't published live and isn't a series.  The edits (spec *Edits*) go through the
//dialog, or a row's own Replace and Delete, and the tab reads again what it held once one is taken.
@Component({
	selector: 'node-history',
	templateUrl: './node-history.html',
	styleUrls: ['./node-history.scss'],
	imports: [MatButtonModule, MatButtonToggleModule, MatChipsModule, MatIconModule, MatMenuModule, MatProgressBarModule, MatSlideToggleModule, MatToolbarModule, MatTooltipModule, HistTable, HistTrend]
})
export class NodeHistory implements OnDestroy{
	constructor(){
		//a change of nodes or of mode is a new read:  the values of a node unticked go, and the live tail follows the selection
		effect( ()=>{ const nodes = this.selected(), mode = this.mode(); untracked( ()=>this.#reload(nodes, mode) ); } );
	}
	ngOnDestroy(){ ++this.#generation; this.#unsubscribe(); }//a read still out is stale:  its page is for a tab that is gone

	pageData = input.required<NodePageData>();
	//NodeDetail.historizable, the tab's own gate:  the children the server historizes and this user may read
	candidates = input.required<Variable[]>();
	//the first eight by default:  the trend's palette has eight slots, assigned in order and never cycled
	selectedKeys = linkedSignal<Set<NodeKey>>( ()=>new Set( this.candidates().slice(0, NodeHistory.maxSeries).map(v=>v.key) ) );
	selected = computed<Variable[]>( ()=>this.candidates().filter( v=>this.selectedKeys().has(v.key) ) );
	//the nodes this user may edit, by the server's HistoryWrite bit:  the Edit menu's and the rows' gate
	writable = computed<Variable[]>( ()=>this.selected().filter( v=>v.historyWritable ) );
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

	async #reload( nodes:Variable[], mode:HistMode ){
		const generation = ++this.#generation;
		this.#unsubscribe();
		this.#next = undefined;
		this.#pages = 0;
		this.values.set( [] );
		this.nodeStatuses.set( [] );
		this.hasEarlier.set( nodes.length>0 );
		this.loading.set( false );//a read still out is stale now, and leaves loading as it is
		if( !nodes.length )
			return;
		//the tail first, and the read past it:  a value recorded after the read's end and before the subscription is in neither
		if( this.live() && mode=='values' )
			this.#subscribe( nodes );
		await this.#read( this.#opening( nodes, mode, NodeHistory.pageSize ), generation );
	}
	//"last N":  a reverse read with only `end` (spec *Reads*), modified in the Modifications mode, which the gateway pages by
	//time and count as it pages the values
	#opening( nodes:Variable[], mode:HistMode, limit:number ):HistReadArgs{
		return { nodes: nodes.map( n=>n.nodeId ), end: new Date( Date.now()+NodeHistory.endSlack ), limit, ...(mode=='modified' ? {modified: true} : {}) };
	}
	//paged back by its continuation - the read's exact place, which neither the oldest value held (a live push can be older
	//than the page) nor a Date (cut to the millisecond) can stand in for.  A read for a selection since changed is dropped,
	//not merged.
	async #read( args:HistReadArgs, generation:number ){
		this.loading.set( true );
		try{
			const page = await this.#history.read( this.gateway, {opc: this.cnnctnSlug}, args, m=>console.log(m) );
			if( generation!=this.#generation )
				return;
			++this.#pages;
			this.nodeStatuses.set( page.nodes );
			this.values.update( v=>mergeHistValues( v, page.values ) );
			this.#next = page.continuation==null ? undefined : { ...args, continuation: page.continuation };
			this.hasEarlier.set( this.#next!=undefined );
			this.#fetchNames( [...page.values.map( v=>v.status ), ...page.nodes.map( n=>n.status )] );
		}
		catch( e ){
			if( generation!=this.#generation )
				return;
			this.#next = undefined;
			this.hasEarlier.set( false );
			this.snackbar.exception( "Could not read the history.", e );
		}
		finally{
			if( generation==this.#generation )
				this.loading.set( false );
		}
	}
	loadEarlier(){
		if( this.#next && !this.loading() )
			this.#read( this.#next, this.#generation );
	}
	//what the tab holds, read again after an edit:  one read as long as the pages loaded, since `limit` is outside the
	//continuation's hash, so Load earlier still pages before it.  A fresh list, not a merge - a merge keeps the record it
	//holds at a time, which is the one the edit changed.  The live tail stays.
	#refresh(){
		const nodes = this.selected();
		if( !nodes.length )
			return;
		const generation = ++this.#generation;
		this.#next = undefined;
		const pages = Math.max( this.#pages, 1 );
		this.#pages = 0;
		this.values.set( [] );
		this.nodeStatuses.set( [] );
		this.hasEarlier.set( true );
		this.#read( this.#opening( nodes, this.mode(), pages*NodeHistory.pageSize ), generation );
	}
	onLiveChange( on:boolean ){
		this.live.set( on );
		if( on )
			this.#subscribe( this.selected() );
		else
			this.#unsubscribe();
	}
	//the node's /opc subscription, under an owner key of this tab's own:  the gateway service counts owners per node, so the
	//Children tab's ticks and this tail share the server-side item and neither drops the other's.
	#subscribe( nodes:Variable[] ){
		this.#unsubscribe();
		if( !nodes.length || this.mode()!='values' )
			return;
		const byKey = new Map( nodes.map( n=>[n.key, n] ) );
		this.#subscription = this.gateway.subscribe( this.cnnctnSlug, nodes.map( n=>n.nodeId ), this.#owner ).subscribe({
			next: ( r:SubscriptionResult )=>{
				const node = byKey.get( r.node.key );
				if( !node )
					return;
				if( r.value instanceof OpcError )//a refused subscription, not a reading
					return this.snackbar.exception( `Could not subscribe to ${node.name}.`, r.value );
				this.values.update( v=>mergeHistValues( v, [pushValue( node.nodeId, r, new Date() )] ) );
				if( r.sc )
					this.#fetchNames( [r.sc] );
			},
			error: ( e:Error )=>{ this.#subscription = undefined; this.live.set( false ); this.snackbar.exception( "Subscription error.", e ); },//nothing resubscribes:  the toggle says so
			complete: ()=>{ this.#subscription = undefined; }
		});
	}
	#unsubscribe(){ this.#subscription?.unsubscribe(); this.#subscription = undefined; }
	//a code's name comes from the gateway on request - OpcError.statusCodeText registers it, updateErrorCodes fetches the lot - so
	//one fetch per page, and a repaint when it lands
	#fetchNames( codes:StatusCode[] ){
		if( codes.some( sc=>sc && !OpcError.statusCodeText(sc) ) )
			this.gateway.updateErrorCodes().catch( e=>console.warn(e) );
	}
	//An edit through the dialog, which sends it and closes only on one the server took;  then the tab reads again what it held.
	async openEdit( kind:HistEditKind, preset?:HistEditPreset ){
		const result = await HistEditDialog.open( this.dialog, { kind, nodes: this.writable(), preset, edit: args=>this.#edit( args ), names: codes=>this.#fetchNames( codes ) } );
		if( result ){
			this.snackbar.info( NodeHistory.done[kind] );
			this.#refresh();
		}
	}
	//a row's Replace:  the dialog on the value's node, time, value and status
	replaceValue( v:HistValue ){
		const node = this.writable().find( n=>n.key==v.node.key );
		if( node && v.source )
			this.openEdit( 'replace', {node, time: v.source, value: v.value, status: v.status || undefined} );
	}
	//a row's Delete:  a range purge from the value's time to itself, the one value there on a group, OpcServer and open62541's
	//backend alike (spec *Pass-through*), where a purge at the time is what OpcServer refuses
	async deleteValue( v:HistValue ){
		if( !v.source || !this.writable().some( n=>n.key==v.node.key ) )
			return;
		const name = this.nodeName( v.node );
		if( !await confirm( this.dialog, {title: "Delete this value?", message: `${name} at ${v.source.toLocaleString()} will be deleted from the server's history.  The server keeps the deleted value as a modification.`, confirm: "Delete", destructive: true} ) )
			return;
		try{
			const result = await this.#edit( {kind: 'purgeRange', nodes: [v.node], start: v.source, end: v.source} );
			if( editRefused( result ) ){
				const status = result.nodes.find( n=>isBad(n.status) )?.status ?? result.values.find( x=>isBad(x.status) )?.status ?? 0;
				this.#fetchNames( [status] );
				return this.snackbar.error( `Could not delete the value:  the server answered ${OpcError.text( status )}.` );
			}
			this.snackbar.info( NodeHistory.done.purgeRange );
			this.#refresh();
		}
		catch( e ){
			this.snackbar.exception( "Could not delete the value.", e );
		}
	}
	#edit( args:HistEditArgs ):Promise<HistEditResult>{ return this.#history.edit( this.gateway, {opc: this.cnnctnSlug}, args, m=>console.log(m) ); }
	nodeName( id:NodeId ):string{ return this.candidates().find( c=>c.key==id.key )?.name ?? id.toString(); }
	statusText( sc:StatusCode ):string{ return OpcError.text( sc ); }
	get gateway():Gateway{ return this.pageData().gateway; }
	get cnnctnSlug():string{ return this.pageData().server.connection.slug; }
	get #owner():string{ return `${this.pageData().route.profileKey}/history`; }
	static readonly pageSize = 1000;//values per read:  well under the gateway's readLimit, and a trend's worth at a time
	static readonly endSlack = 60_000;//ms past the browser's now the opening read ends:  for a server clock ahead of the browser's
	static readonly maxSeries = 8;
	static readonly done:Record<HistEditKind,string> = { insert: "Inserted.", replace: "Replaced.", update: "Updated.", purgeRange: "Deleted.", purgeTimes: "Deleted." };

	#history = inject( HistoryService );
	snackbar = inject( SnackbarService );
	dialog = inject( MatDialog );
	#subscription:Subscription|undefined;
	#generation = 0;//which selection the values belong to:  a read or push for an older one is dropped
	#next:HistReadArgs|undefined;//the opening read's arguments with the last page's continuation:  the page before, if any
	#pages = 0;//pages read into `values`, so a refresh reads as far
}