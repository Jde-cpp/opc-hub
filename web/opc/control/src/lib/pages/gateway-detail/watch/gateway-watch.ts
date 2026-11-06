import { ChangeDetectorRef, Component, computed, inject, model, OnDestroy, OnInit, signal } from '@angular/core';
import { ActivatedRoute, RouterModule } from '@angular/router';
import { MatButtonModule } from '@angular/material/button';
import { MatDialog } from '@angular/material/dialog';
import { MatIconModule } from '@angular/material/icon';
import { MatSortModule, Sort } from '@angular/material/sort';
import { MatTableModule } from '@angular/material/table';
import { MatTooltipModule } from '@angular/material/tooltip';
import { Subscription } from 'rxjs';
import { ComponentPageTitle, LocalPrefs, ProfileStore, RouteItem } from 'jde-spa';
import { errorMessage, QLListSettings, SnackbarService, View, ViewToolbar } from 'jde-framework';
import { Gateway, SubscriptionResult } from '../../../services/gateway-service';
import { HistAdminService } from '../../../services/hist-admin-service';
import { OpcStore } from '../../../services/opc-store';
import { WatchStore } from '../../../services/watch-store';
import { NodeSearchProvider } from '../../../services/node-search-provider';
import { WatchPageData } from '../../../services/resolvers/watch-resolver';
import { HistConnection } from '../../../model/hist-admin';
import { pageSegments } from '../../../model/node';
import { OpcError } from '../../../model/opc-error';
import { scHex, statusIcon } from '../../../model/status-code';
import { CnnctnSlug } from '../../../model/server-cnnctn';
import { valueString } from '../../../model/value';
import { discardText, filterText, queueText, samplingText, WatchParameters, WatchRow } from '../../../model/watch';
import { WatchView } from '../../../model/watch-view';
import { WatchAddDialog, WatchPick } from './watch-add-dialog';
import { WatchParametersDialog } from './watch-parameters-dialog';

//The watch list (clients spec *Watch list*, #419):  rows from any of the gateway's connections, each watched under the parameters it
//asks, persisted per user (WatchStore) and read again as the page opens - the node described by its connection, for its name, its
//type and the enumeration that names its value.  One Subscribe per connection carries the rows' parameters;  the ack's revised
//values show beside what was asked, a MonitoredItemModified push replaces them, and every push is the row's reading.  A row is added
//by node id or from the search (WatchAddDialog), or from a node page's row menu, and its parameters are edited in a dialog, which
//subscribes the node again - how a listener changes what it asks.  The view - columns, sort, filters - is saved as the children
//table's is (WatchView).
@Component({
	templateUrl: './gateway-watch.html',
	styleUrls: ['./gateway-watch.scss'],
	host: {class: 'main-content my-content gateway-watch'},
	imports: [RouterModule, MatButtonModule, MatIconModule, MatSortModule, MatTableModule, MatTooltipModule, QLListSettings, ViewToolbar]
})
export class GatewayWatch implements OnInit, OnDestroy{
	#route = inject( ActivatedRoute );
	#componentPageTitle = inject( ComponentPageTitle );
	#snackbar = inject( SnackbarService );
	#cdRef = inject( ChangeDetectorRef );
	#dialog = inject( MatDialog );
	#store = inject( WatchStore );
	#opcStore = inject( OpcStore );
	#admin = inject( HistAdminService );

	async ngOnInit(){
		await WatchView.loadViews( this.viewSet );
		this.#route.data.subscribe( async data=>{
			const pageData = <WatchPageData>data['pageData'];
			this.subscription = undefined;//the router reuses the page from one gateway to the next
			this.rows.set( [] );
			this.gateway = pageData.gateway;
			this.sideNav.set( pageData.routing );
			this.#componentPageTitle.title = `${this.gateway.slug} - Watch list`;
			await this.load();
		} );
	}
	ngOnDestroy(){
		clearTimeout( this.#retry );
		this.subscription = undefined;
	}
	//the persisted rows, each described by its connection, then watched
	async load():Promise<void>{
		const gateway = this.gateway;
		this.isLoading.set( true );
		this.error.set( undefined );
		try{
			this.#loadConnections();
			const items = await this.#store.load( gateway.slug );
			if( gateway!=this.gateway )
				return;
			const rows = items.map( i=>WatchRow.fromJson( i ) );
			this.rows.set( rows );
			await this.#describe( rows );
			this.#subscribeAll();
		}
		catch( e ){
			this.error.set( errorMessage( e, "Could not load the watch list." ) );
		}
		finally{
			if( gateway==this.gateway )
				this.isLoading.set( false );
		}
	}
	//the connections' names, for the cells and the Add dialog's choice;  the slugs stand in while they are refused or unread
	async #loadConnections():Promise<void>{
		const gateway = this.gateway;
		try{
			const connections = await this.#admin.connections( gateway );
			if( gateway==this.gateway )
				this.connections.set( connections );
		}
		catch( e ){
			console.warn( "Could not list the connections.", e );
		}
	}
	cnnctnName( slug:CnnctnSlug ):string{
		return this.connections().find( c=>c.slug==slug )?.name ?? this.#opcStore.cnnctnName( this.gateway.slug, slug );
	}
	//each row's node as its connection describes it - in parallel, and a connection that fails leaves its rows with the reason
	async #describe( rows:WatchRow[] ):Promise<void>{
		const gateway = this.gateway;
		await Promise.all( rows.map( async row=>{
			try{
				const described = await gateway.nodeById( row.opc, row.node );
				if( !described )
					row.error = `${this.cnnctnName( row.opc )} has no node ${row.node.uaString()}.`;
				else
					row.describe( described.node, described.path );
			}
			catch( e ){
				row.error = errorMessage( e );
			}
		} ) );
		await gateway.updateErrorCodes();
		this.#repaint();
	}
	async onRefresh(){
		this.isRefreshing.set( true );
		try{
			await this.#describe( this.rows() );
		}
		finally{
			this.isRefreshing.set( false );
		}
	}

	//One Subscribe per connection, the rows' parameters by position:  the first opens the owner's stream, the others add to it.  Only
	//rows with a variable to watch;  the rest show why they have none.
	#subscribeAll():void{
		for( const [opc, rows] of this.#byConnection( this.rows().filter( r=>!r.error ) ) )
			this.#send( opc, rows );
	}
	#byConnection( rows:WatchRow[] ):Map<CnnctnSlug,WatchRow[]>{
		const y = new Map<CnnctnSlug,WatchRow[]>();
		for( const row of rows )
			y.set( row.opc, [...(y.get( row.opc ) ?? []), row] );
		return y;
	}
	#send( opc:CnnctnSlug, rows:WatchRow[] ):void{
		if( !rows.length )
			return;
		const nodes = rows.map( r=>r.node ), parameters = rows.map( r=>r.parameters );
		try{
			if( this.subscription )
				this.gateway.addToSubscription( opc, nodes, this.owner, parameters );
			else{
				this.subscription = this.gateway.subscribe( opc, nodes, this.owner, parameters ).subscribe( {
					next: r=>this.#onResult( r ),
					//the socket went:  the gateway has forgotten the rows, so they are asked for again once it is back - every connection's,
					//as the owner's stream carried them all
					error: ( e:Error )=>{
						this.subscription = undefined;
						this.#snackbar.exception( "The watch subscription ended.", e );
						clearTimeout( this.#retry );
						this.#retry = setTimeout( ()=>this.#subscribeAll(), Gateway.histRetryDelay );
					},
					complete: ()=>{ this.subscription = undefined; }
				} );
			}
		}
		catch( e ){
			this.#snackbar.exception( "Could not subscribe.", e );
		}
	}
	#retry:ReturnType<typeof setTimeout>|undefined;
	//a push is the row's reading;  a `revised` result is what the server runs the item at - the ack's answer, or a later modify's.
	//Only a CHANGE of code asks for its name, as the children table does.
	#onResult( r:SubscriptionResult ):void{
		const row = this.rows().find( x=>x.opc==r.opcId && x.node.equals( r.node ) );
		if( !row )
			return console.debug( `watch value for '${r.node}' on ${r.opcId}, which is no longer a row.` );
		if( r.revised ){
			row.revise( r.revised );
			if( r.revised.sc && !OpcError.statusCodeText( r.revised.sc ) )
				this.gateway.updateErrorCodes().then( ()=>this.#repaint() );
		}
		else{
			const previous = row.sc;
			row.setReading( r );
			if( row.sc && row.sc!=previous && !OpcError.statusCodeText( row.sc ) )
				this.gateway.updateErrorCodes().then( ()=>this.#repaint() );
		}
		this.#repaint();
	}
	//the cells bind to plain fields on the rows:  zoneless runs no pass of its own for a push
	#repaint(){ this.#cdRef.markForCheck(); }

	//rows picked in the dialog - by id, or from the search - each with the defaults:  described, watched and saved
	async onAdd(){
		const picks = await WatchAddDialog.open( this.#dialog, {gateway: this.gateway, connections: this.connections()} );
		if( picks?.length )
			await this.add( picks );
	}
	async add( picks:WatchPick[] ):Promise<void>{
		const fresh:WatchRow[] = [];
		for( const pick of picks ){
			const key = WatchRow.keyOf( pick.opc, pick.node );
			if( !this.rows().some( r=>r.key==key ) && !fresh.some( r=>r.key==key ) )
				fresh.push( new WatchRow( pick.opc, pick.node, {} ) );
		}
		if( !fresh.length )
			return this.#snackbar.info( "Already on the list." );
		this.rows.update( rows=>[...rows, ...fresh] );
		this.#save();
		await this.#describe( fresh );
		for( const [opc, rows] of this.#byConnection( fresh.filter( r=>!r.error ) ) )
			this.#send( opc, rows );
	}
	async remove( row:WatchRow ){
		this.rows.update( rows=>rows.filter( r=>r!==row ) );
		this.#save();
		if( !this.rows().some( r=>!r.error ) )
			this.subscription = undefined;//the last watched row:  the stream ends, which unsubscribes the owner's nodes
		else if( !row.error ){
			try{
				await this.gateway.unsubscribe( row.opc, [row.node], this.owner );
			}
			catch( e ){
				this.#snackbar.exception( "Could not remove the subscription.", e );
			}
		}
	}
	//the row's parameters, edited:  the node is subscribed again with them, which is how a listener changes what it asks (spec *Watch
	//list*), and the ack's revised values replace the row's
	async onParameters( row:WatchRow ){
		const parameters = await WatchParametersDialog.open( this.#dialog, {name: row.name, parameters: row.parameters} );
		if( parameters )
			await this.setParameters( row, parameters );
	}
	async setParameters( row:WatchRow, parameters:WatchParameters ):Promise<void>{
		row.parameters = parameters;
		row.revised = undefined;
		this.#save();
		if( !row.error )
			this.#send( row.opc, [row] );
		this.#repaint();
	}
	#save(){
		this.#store.save( this.gateway.slug, this.rows().map( r=>r.toJson() ) ).catch( e=>this.#snackbar.exception( "Could not save the watch list.", e ) );
	}

	//The view group and settings panel, as the children table has them
	onSortChange( sort:Sort ){ this.viewSet.sort( sort ); }
	onViewShow( view:View ){ this.isSettings.set( false ); this.viewSet.show( view ); }
	onViewSave( view:View ){ this.viewSet.save( view ).catch( e=>this.#snackbar.exception( "Could not save view.", e ) ); this.isSettings.set( false ); }
	onViewDelete( view:View ){ this.viewSet.remove( view ).catch( e=>this.#snackbar.exception( "Could not delete view.", e ) ); this.isSettings.set( false ); }
	onChangeView( index:number ){ this.viewSet.select( index ); }
	colSuggestions():Record<string,any[]>{
		const suggestions:Record<string,any[]> = {};
		for( const name of Object.keys( WatchView.columns ) ){
			const values = this.rows().map( r=>WatchView.cellValue( r, name, this.names ) ).filter( v=>v!=null && v!=="" );
			suggestions[name] = [...new Set( values )].slice( 0, 100 ).sort( (a,b)=>typeof a=="number" && typeof b=="number" ? a-b : String(a).localeCompare( String(b) ) );
		}
		return suggestions;
	}

	//the cells
	readonly names = ( slug:CnnctnSlug )=>this.cnnctnName( slug );
	cell( row:WatchRow, column:string ):string{ return WatchView.cellValue( row, column, this.names )?.toString() ?? ""; }
	//the value as text:  an enumeration of the server's own by the name of its value, once resolved
	valueText( row:WatchRow ):string{
		const custom = row.variable?.customDataType;
		if( custom && "enumValues" in custom && typeof row.value=="number" ){
			const named = custom.enumValues.find( v=>v.id==row.value );
			if( named )
				return named.name;
		}
		return valueString( <any>row.value );
	}
	status( row:WatchRow ):string{ return row.error ? row.error : row.sc==undefined ? "" : OpcError.text( row.sc ); }
	statusTooltip( row:WatchRow ):string{ return row.sc ? `${scHex( row.sc )} - ${OpcError.text( row.sc )}` : ""; }
	qualityIcon( row:WatchRow ){ return row.error ? "error" : statusIcon( row.sc ); }
	//the revised values beside the request:  a modify the server refused keeps the item as it was, and the cell says so
	revisedTooltip( row:WatchRow ):string{ return row.revised?.sc ? `The server refused the last change (${scHex( row.revised.sc )} ${OpcError.text( row.revised.sc )}):  the item runs as it was.` : ""; }
	sampling( row:WatchRow ):string{ return samplingText( row.parameters ); }
	queue( row:WatchRow ):string{ return queueText( row.parameters ); }
	discard( row:WatchRow ):string{ return discardText( row.parameters ); }
	filter( row:WatchRow ):string{ return filterText( row.parameters ) || "default"; }
	//the folder the node is under, as the breadcrumbs name it:  `Objects/pump1` for Objects/5~pump1/5~motorRpm
	parentPath( row:WatchRow ):string{
		const segments = row.path ? row.path.split( '/' ).slice( 0, -1 ) : [];
		return segments.length ? NodeSearchProvider.displayPath( segments.join( '/' ) ) : '';
	}
	//the node's page, where the gateway placed it
	nodeLink( row:WatchRow ):string[]|undefined{ return row.path==undefined || !row.uaNode ? undefined : ['/gateways', this.gateway.slug, row.opc, ...pageSegments( row.path, row.uaNode.nodeClass )]; }

	gateway:Gateway = null as any;
	get owner():string{ return `watch/${this.gateway.slug}`; }
	sideNav = model.required<RouteItem>();
	rows = signal<WatchRow[]>( [] );
	connections = signal<HistConnection[]>( [] );
	isLoading = signal( true );
	isRefreshing = signal( false );
	isSettings = signal( false );
	error = signal<string|undefined>( undefined );
	viewSet = WatchView.viewSet( inject( ProfileStore ), inject( LocalPrefs ) );
	view = this.viewSet.current;
	viewCopy = computed<WatchView>( ()=>WatchView.copy( this.view() ) );
	columns = WatchView.columns;
	schema = WatchView.schema;
	shown = computed<WatchRow[]>( ()=>this.view() ? this.view().apply( this.rows(), this.names ) : [] );
	displayedColumns = computed<string[]>( ()=>[...(this.view()?.displayedColumns ?? []), 'actions'] );
	statusShown = computed<boolean>( ()=>this.displayedColumns().includes( "status" ) );
	get sort():Sort{ return this.view().sort[0] ?? {active: "connection", direction: "asc"}; }
	get subscription(){ return this.#subscription; } #subscription:Subscription|undefined;
	set subscription( x ){ if( !x && this.#subscription ) this.#subscription.unsubscribe(); this.#subscription = x; }
}