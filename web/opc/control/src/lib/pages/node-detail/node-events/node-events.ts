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
import { DateUtils, SnackbarService } from 'jde-framework';
import { LocalPrefs } from 'jde-spa';
import { eventColumns, eventFieldText, EventFields, EventRow, eventTime, isLocalizedText, keptSelect, repeatsField, selectProblem, toEventRow } from '../../../model/event';
import { UaNode } from '../../../model/node';
import { OpcError } from '../../../model/opc-error';
import { isBad, scHex } from '../../../model/status-code';
import { NodePageData } from '../../../services/resolvers/node-resolver';
import { StructureTable } from '../../structure-table/structure-table';
import { EventStream } from '../event-stream';

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
	ngOnDestroy(){ this.#destroyed = true; this.#stream.destroy(); }
	#destroyed = false;

	pageData = input.required<NodePageData>();
	node = input.required<UaNode>();
	#stream = new EventStream( {
		gateway: ()=>this.pageData().gateway,
		connection: ()=>this.cnnctnSlug,
		node: ()=>this.node(),
		select: ()=>this.select(),
		owner: `nodeEvents/${Math.random().toString( 36 ).slice( 2 )}`,
		onEvent: fields=>this.#onEvent( fields )
	} );
	readonly separatorKeysCodes = [ENTER, COMMA];
	//the fields beside the base eight, as the chips show them, kept on this browser per connection and node
	select = signal<string[]>( [] );
	columns = computed( ()=>eventColumns( this.select() ) );
	displayedColumns = computed( ()=>this.columns().map( c=>c.key ) );
	rows = signal<EventRow[]>( [] );//newest first
	paused = signal( false );
	held = signal( 0 );//events held while paused
	#held:EventRow[] = [];
	readonly live = this.#stream.live;
	readonly error = this.#stream.error;
	loading = signal( false );//a history fill out
	//Load from:  a local time to the minute, an hour ago by default, as the datetime-local input holds it
	from = signal<string>( DateUtils.toLocalInput( new Date( Date.now()-3_600_000 ) ).slice( 0, 16 ) );
	historyStatus = signal<string|undefined>( undefined );//what the server answered a fill with, when not Good

	#prefsName( node:UaNode ):string{ return `${this.pageData().gateway.slug}/${this.cnnctnSlug}/${node.uaString()}/select`; }
	#restart( node:UaNode, select:string[] ){
		this.#stream.close();
		this.rows.set( [] );
		this.#held = [];
		this.held.set( 0 );
		this.#stream.error.set( undefined );
		this.historyStatus.set( undefined );
		if( node!==this.#selectNode ){//a new node:  its own list, before this one's is saved under it
			this.#selectNode = node;
			const list = keptSelect( this.#prefs.get<string[]>( NodeEvents.prefsOwner, this.#prefsName( node ), [] ) );
			if( list.join( '\n' )!=select.join( '\n' ) ){
				this.select.set( list );//the effect runs again with it
				return;
			}
		}
		else
			this.#prefs.set( NodeEvents.prefsOwner, this.#prefsName( node ), select.length ? select : null );
		this.#stream.open( node );
	}
	#selectNode:UaNode|undefined;
	#onEvent( fields:EventFields ){
		const row = toEventRow( fields );
		if( this.paused() ){
			this.#held.push( row );
			if( this.#held.length>NodeEvents.maxRows )
				this.#held.shift();
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
		if( repeatsField( path, this.select() ) )
			return this.#snackbar.warn( `'${path}' is a column already.` );
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
			let continuation:string|undefined;
			do{
				const page = await gateway.historyEvents( this.cnnctnSlug, node.nodeId, {start: from, select, continuation} );
				if( this.#destroyed || node!==this.node() || select!==this.select() )//its fields are the old list's, or the page has moved on
					return;
				if( isBad( page.status ) ){
					this.historyStatus.set( `${scHex( page.status )} ${OpcError.text( page.status )}` );
					if( !OpcError.statusCodeText( page.status ) )
						gateway.updateErrorCodes().then( ()=>{ if( !this.#destroyed && this.historyStatus() ) this.historyStatus.set( `${scHex( page.status )} ${OpcError.text( page.status )}` ); } );
				}
				this.#add( page.events.map( ( fields:EventFields )=>toEventRow( fields, from ) ) );
				continuation = page.continuation;
			}while( continuation );
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
	readonly eventTime = eventTime;
	//a field the cell's StructureTable shows (clients spec *Structures*):  a structure, an array of them or a body no type explains - not a LocalizedText, which is text
	structured( v:unknown ):boolean{ return !isLocalizedText( v ) && StructureTable.shows( v ); }
	get cnnctnSlug():string{ return this.pageData().server.connection.slug; }
}