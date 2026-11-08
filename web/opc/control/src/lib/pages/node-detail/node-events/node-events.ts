import { ChangeDetectorRef, Component, computed, effect, inject, input, OnDestroy, signal, untracked } from '@angular/core';
import { COMMA, ENTER } from '@angular/cdk/keycodes';
import { DatePipe } from '@angular/common';
import { MatButtonModule } from '@angular/material/button';
import { MatChipInputEvent, MatChipsModule } from '@angular/material/chips';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatTableModule } from '@angular/material/table';
import { MatToolbarModule } from '@angular/material/toolbar';
import { MatTooltipModule } from '@angular/material/tooltip';
import { Subscription } from 'rxjs';
import { DateUtils, SnackbarService } from 'jde-framework';
import { LocalPrefs } from 'jde-spa';
import { eventColumns, eventFieldText, EventFields, EventRow, isLocalizedText, selectProblem, toEventRow } from '../../../model/event';
import { UaNode } from '../../../model/node';
import { OpcError } from '../../../model/opc-error';
import { isBad, scHex } from '../../../model/status-code';
import { Gateway, EventPush } from '../../../services/gateway-service';
import { NodePageData } from '../../../services/resolvers/node-resolver';
import { StructureTable } from '../../structure-table/structure-table';

//The Events tab (clients spec *Events*, #424), on a node whose EventNotifier has the SubscribeToEvents bit - the Server object, which
//emits every event, and any object or view a server marks:  an events subscription on the node through the gateway, its fields the
//eight of BaseEventType and the browse paths the chips add - "Rpm", "2:Rpm", "EnabledState/Id" - a table of the events newest first,
//Time and the other base fields as columns, EventId aside, and a column per field added.  **Pause** holds what arrives until **Resume**;
//**Clear** empties the table.  **Load from** fills the table back from a time through historyEvents, a server's own event history, each
//event once by its EventId, with the server's status where it keeps none.  A field list change subscribes the node again, which is how
//a listener changes its list;  it is kept on this browser per connection and node.  A refusal - the node without the bit, a field the
//gateway cannot read - shows its status;  a connection lost subscribes again once the gateway is back.
@Component({
	selector: 'node-events',
	templateUrl: './node-events.html',
	styleUrl: './node-events.scss',
	imports: [DatePipe, MatButtonModule, MatChipsModule, MatFormFieldModule, MatIconModule, MatInputModule, MatProgressBarModule, MatTableModule, MatToolbarModule, MatTooltipModule, StructureTable]
})
export class NodeEvents implements OnDestroy{
	static readonly prefsOwner = 'nodeEvents';
	static readonly maxRows = 1000;//the newest kept;  a server raising faster than that is read is the history's to hold
	#snackbar = inject( SnackbarService );
	#cdRef = inject( ChangeDetectorRef );
	#prefs = inject( LocalPrefs );
	constructor(){
		//the router reuses the page across nodes, and a field list change asks a new item:  each is a new subscription over an empty table
		effect( ()=>{ const node = this.node(), select = this.select(); untracked( ()=>this.#restart( node, select ) ); } );
	}
	ngOnDestroy(){ this.#destroyed = true; this.#unsubscribe(); }
	#destroyed = false;

	pageData = input.required<NodePageData>();
	node = input.required<UaNode>();
	readonly separatorKeysCodes = [ENTER, COMMA];
	//the fields beside the base eight, as the chips show them, kept on this browser per connection and node
	select = signal<string[]>( [] );
	columns = computed( ()=>eventColumns( this.select() ) );
	displayedColumns = computed( ()=>this.columns().map( c=>c.key ) );
	rows = signal<EventRow[]>( [] );//newest first
	paused = signal( false );
	held = signal( 0 );//events held while paused
	#held:EventRow[] = [];
	error = signal<string|undefined>( undefined );//a refusal's status, or why the subscription ended
	live = signal( false );//the subscription is up:  its ack came and nothing ended it
	loading = signal( false );//a history fill out
	//Load from:  a local time to the minute, an hour ago by default, as the datetime-local input holds it
	from = signal<string>( DateUtils.toLocalInput( new Date( Date.now()-3_600_000 ) ).slice( 0, 16 ) );
	historyStatus = signal<string|undefined>( undefined );//what the server answered a fill with, when not Good

	#prefsName( node:UaNode ):string{ return `${this.pageData().gateway.slug}/${this.cnnctnSlug}/${node.uaString()}/select`; }
	#restart( node:UaNode, select:string[] ){
		this.#unsubscribe();
		this.rows.set( [] );
		this.#held = [];
		this.held.set( 0 );
		this.error.set( undefined );
		this.historyStatus.set( undefined );
		if( node!==this.#selectNode ){//a new node:  its own list, before this one's is saved under it
			this.#selectNode = node;
			const kept = this.#prefs.get<string[]>( NodeEvents.prefsOwner, this.#prefsName( node ), [] );
			const list = Array.isArray( kept ) ? kept.filter( p=>typeof p=='string' && !selectProblem( p ) ) : [];
			if( list.join( '\n' )!=select.join( '\n' ) ){
				this.select.set( list );//the effect runs again with it
				return;
			}
		}
		else
			this.#prefs.set( NodeEvents.prefsOwner, this.#prefsName( node ), select.length ? select : null );
		this.#subscribe( node, select );
	}
	#selectNode:UaNode|undefined;
	#subscribe( node:UaNode, select:string[] ){
		const gateway = this.pageData().gateway;
		this.#subscription = gateway.subscribeEvents( this.cnnctnSlug, node.nodeId, select, this.#owner ).subscribe( {
			next: e=>this.#onEvent( e ),
			error: ( e:unknown )=>{
				this.#subscription = undefined;
				this.live.set( false );
				if( e instanceof OpcError ){//refused:  the node, or a field, as the gateway or the server read it - no retry would change it
					this.error.set( `The server refused the subscription:  ${scHex( e.sc )} ${OpcError.text( e.sc )}.` );
					if( !OpcError.statusCodeText( e.sc ) )
						gateway.updateErrorCodes().then( ()=>{ if( !this.#destroyed && this.error() ) this.error.set( `The server refused the subscription:  ${scHex( e.sc )} ${OpcError.text( e.sc )}.` ); } );
				}
				else{//the socket went:  the gateway has forgotten the item, so it is asked for again once it is back
					this.error.set( "Connection to the gateway lost.  The subscription resumes once it is back." );
					clearTimeout( this.#retry );
					this.#retry = setTimeout( ()=>{ if( !this.#destroyed && node===this.node() && !this.#subscription ) this.#subscribe( node, this.select() ); }, Gateway.histRetryDelay );
				}
			}
		} );
		this.live.set( true );
		this.error.set( undefined );
	}
	#subscription:Subscription|undefined;
	#retry:ReturnType<typeof setTimeout>|undefined;
	readonly #owner = `nodeEvents/${Math.random().toString( 36 ).slice( 2 )}`;
	#unsubscribe(){
		clearTimeout( this.#retry );
		this.#subscription?.unsubscribe();
		this.#subscription = undefined;
		this.live.set( false );
	}
	#onEvent( e:EventPush ){
		const row = toEventRow( e.fields );
		if( this.paused() ){
			this.#held.push( row );
			this.held.set( this.#held.length );
			return;
		}
		this.#add( [row] );
	}
	//rows into the table, newest first by Time - the receive time standing in for one without - each EventId once, the newest kept
	#add( rows:EventRow[] ){
		if( !rows.length )
			return;
		this.rows.update( current=>{
			const keys = new Set( current.map( r=>r.key ) );
			const fresh = rows.filter( r=>{ if( keys.has( r.key ) ) return false; keys.add( r.key ); return true; } );
			if( !fresh.length )
				return current;
			const y = [...current, ...fresh].sort( ( a, b )=>NodeEvents.time( b )-NodeEvents.time( a ) );
			return y.length>NodeEvents.maxRows ? y.slice( 0, NodeEvents.maxRows ) : y;
		} );
		this.#cdRef.markForCheck();
	}
	static time( r:EventRow ):number{ return ( r.time ?? r.received ).getTime(); }
	togglePause(){
		const paused = !this.paused();
		this.paused.set( paused );
		if( !paused ){
			const held = this.#held;
			this.#held = [];
			this.held.set( 0 );
			this.#add( held );
		}
	}
	clear(){
		this.rows.set( [] );
		this.#held = [];
		this.held.set( 0 );
		this.historyStatus.set( undefined );
	}
	//a field added from the chip input:  a browse path as the gateway reads one;  one it would refuse is said here and not sent
	addField( e:MatChipInputEvent ){
		const path = e.value.trim();
		e.chipInput.clear();
		if( !path )
			return;
		const problem = selectProblem( path );
		if( problem )
			return this.#snackbar.warn( problem );
		if( this.select().includes( path ) )
			return;
		this.select.update( s=>[...s, path] );
	}
	removeField( path:string ){ this.select.update( s=>s.filter( p=>p!=path ) ); }
	//Load from:  the node's event history from the time typed to now, the fields as the subscription has them, each event once
	async load(){
		const from = DateUtils.fromLocalInput( this.from() );
		if( !from )
			return this.#snackbar.warn( "Type the time to load from." );
		const node = this.node(), gateway = this.pageData().gateway, select = this.select();
		this.loading.set( true );
		this.historyStatus.set( undefined );
		try{
			const page = await gateway.historyEvents( this.cnnctnSlug, node.nodeId, {start: from, select} );
			if( this.#destroyed || node!==this.node() )
				return;
			if( isBad( page.status ) ){
				this.historyStatus.set( `${scHex( page.status )} ${OpcError.text( page.status )}` );
				if( !OpcError.statusCodeText( page.status ) )
					gateway.updateErrorCodes().then( ()=>{ if( !this.#destroyed && this.historyStatus() ) this.historyStatus.set( `${scHex( page.status )} ${OpcError.text( page.status )}` ); } );
			}
			this.#add( page.events.map( ( fields:EventFields )=>toEventRow( fields, from ) ) );
		}
		catch( e ){
			this.#snackbar.exception( "Could not read the event history.", e );
		}
		finally{
			if( !this.#destroyed )
				this.loading.set( false );
		}
	}
	text( r:EventRow, index:number ):string{ return eventFieldText( r.fields[index] ); }
	//a field the cell's StructureTable shows (clients spec *Structures*):  a structure, an array of them or a body no type explains - not a LocalizedText, which is text
	structured( v:unknown ):boolean{ return !isLocalizedText( v ) && StructureTable.shows( v ); }
	get cnnctnSlug():string{ return this.pageData().server.connection.slug; }
}