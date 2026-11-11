import { ChangeDetectorRef, Component, computed, inject, model, OnDestroy, OnInit, signal } from '@angular/core';
import { ActivatedRoute, RouterModule } from '@angular/router';
import { MatButtonModule } from '@angular/material/button';
import { MatCheckboxModule } from '@angular/material/checkbox';
import { MatDialog } from '@angular/material/dialog';
import { MatIconModule } from '@angular/material/icon';
import { MatSortModule, Sort } from '@angular/material/sort';
import { MatTableModule } from '@angular/material/table';
import { MatTabsModule } from '@angular/material/tabs';
import { MatTooltipModule } from '@angular/material/tooltip';
import { Subscription } from 'rxjs';
import { ComponentPageTitle, LocalPrefs, ProfileStore, RouteItem } from 'jde-spa';
import { cellValues, errorMessage, GroupHeader, GroupHeaderCell, groupRows, isGroupHeader, SliceCount, SlicePanel, SnackbarService, ViewColumnCell, ViewColumnHeader, ViewColumns, ViewToolbar } from 'jde-framework';
import { Gateway, SubscriptionResult } from '../../../services/gateway-service';
import { HistAdminService } from '../../../services/hist-admin-service';
import { OpcStore } from '../../../services/opc-store';
import { WatchStore } from '../../../services/watch-store';
import { NodeSearchProvider } from '../../../services/node-search-provider';
import { WatchPageData } from '../../../services/resolvers/watch-resolver';
import { pushValue } from '../../../model/hist';
import { HistConnection } from '../../../model/hist-admin';
import { pageSegments } from '../../../model/node';
import { OpcError } from '../../../model/opc-error';
import { PushBuffer } from '../../../model/push-buffer';
import { isBad, scHex, statusIcon } from '../../../model/status-code';
import { CnnctnSlug } from '../../../model/server-cnnctn';
import { valueString } from '../../../model/value';
import { discardText, filterText, queueText, samplingText, WatchParameters, WatchRow } from '../../../model/watch';
import { WatchView } from '../../../model/watch-view';
import { trendPalette } from '../../node-detail/node-history/hist-trend-data';
import { StructureTable } from '../../structure-table/structure-table';
import { WatchAddDialog, WatchConnection, WatchPick } from './watch-add-dialog';
import { WatchParametersDialog } from './watch-parameters-dialog';
import { WatchTrend } from './watch-trend';

//The watch list (clients spec *Watch list*, #419):  rows from any of the gateway's connections, each watched under the parameters it
//asks, persisted per user (WatchStore) and read again as the page opens - the node described by its connection, for its name, its
//type and the enumeration that names its value.  One Subscribe per connection carries the rows' parameters;  the ack's revised
//values show beside what was asked, a MonitoredItemModified push replaces them, and every push is the row's reading.  A row is added
//by node id or from the search (WatchAddDialog), or from a node page's row menu, and its parameters are edited in a dialog, which
//subscribes the node again - how a listener changes what it asks.  The view - columns, sort, filters - is saved as the children
//table's is (WatchView).  Two tabs:  the list, and the Trend tab (#420, spec *Live trend*), the ticked rows' pushes over a window
//drawn with the History tab's chart - every row's pushes go to the buffer as they come, so a row ticked shows its window at once.
@Component({
	templateUrl: './gateway-watch.html',
	styleUrls: ['./gateway-watch.scss'],
	host: {class: 'main-content my-content gateway-watch'},
	imports: [RouterModule, MatButtonModule, MatCheckboxModule, MatIconModule, MatSortModule, MatTableModule, MatTabsModule, MatTooltipModule, GroupHeaderCell, SlicePanel, StructureTable, ViewColumnCell, ViewColumnHeader, ViewColumns, ViewToolbar, WatchTrend]
})
export class GatewayWatch implements OnInit, OnDestroy{
	static readonly prefsOwner = 'gatewayWatch';
	static readonly defaultWindow = 10;//minutes
	static readonly maxSeries = trendPalette.length;
	#route = inject( ActivatedRoute );
	#componentPageTitle = inject( ComponentPageTitle );
	#snackbar = inject( SnackbarService );
	#cdRef = inject( ChangeDetectorRef );
	#dialog = inject( MatDialog );
	#store = inject( WatchStore );
	#opcStore = inject( OpcStore );
	#admin = inject( HistAdminService );
	#prefs = inject( LocalPrefs );

	async ngOnInit(){
		await WatchView.loadViews( this.viewSet );
		this.#route.data.subscribe( async data=>{
			const pageData = <WatchPageData>data['pageData'];
			this.subscription = undefined;//the router reuses the page from one gateway to the next
			clearTimeout( this.#retry );
			this.#down = false;
			this.rows.set( [] );
			this.ticked.set( new Set() );
			this.tabIndex.set( 0 );
			this.buffer.clear();
			this.gateway = pageData.gateway;
			this.sideNav.set( pageData.routing );
			this.#componentPageTitle.title = `${this.gateway.slug} - Watch list`;
			await this.load();
		} );
	}
	ngOnDestroy(){
		this.#destroyed = true;
		clearTimeout( this.#retry );
		this.subscription = undefined;
	}
	//still the page `gateway`'s load or add began on:  a describe out when the page was left, or moved to another gateway, would
	//subscribe rows for a page that no longer shows them - and a subscription nothing ends (419-watch #2)
	#isCurrent( gateway:Gateway ):boolean{ return !this.#destroyed && gateway==this.gateway; }
	#destroyed = false;
	//the persisted rows, each described by its connection, then watched
	async load():Promise<void>{
		const gateway = this.gateway;
		this.isLoading.set( true );
		this.error.set( undefined );
		try{
			this.#connectionsRead = this.#loadConnections();
			const items = await this.#store.load( gateway.slug );
			if( !this.#isCurrent( gateway ) )
				return;
			const rows = items.map( i=>WatchRow.fromJson( i ) );
			this.rows.set( rows );
			await this.#describe( rows );
			if( !this.#isCurrent( gateway ) )
				return;
			this.#watch( this.rows() );
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
	#connectionsRead = Promise.resolve();
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
	//the descriptions read again, and a row that had none and now has one is watched (419-watch #5)
	async onRefresh(){
		const gateway = this.gateway, unwatched = this.rows().filter( r=>r.error );
		this.isRefreshing.set( true );
		try{
			await this.#describe( this.rows() );
			if( this.#isCurrent( gateway ) )
				this.#watch( unwatched );
		}
		finally{
			this.isRefreshing.set( false );
		}
	}

	//One Subscribe per connection, the rows' parameters by position:  the first opens the owner's stream, the others add to it.  Only
	//rows with a variable to watch;  the rest show why they have none.
	#watch( rows:WatchRow[] ):void{
		for( const [opc, byOpc] of this.#byConnection( rows.filter( r=>!r.error ) ) )
			this.#send( opc, byOpc );
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
						if( !this.#down )//once an outage:  each retry while the gateway is down errors the stream again (419-watch #8)
							this.#snackbar.exception( "The watch subscription ended.", e );
						this.#down = true;
						clearTimeout( this.#retry );
						this.#retry = setTimeout( ()=>this.#watch( this.rows() ), Gateway.histRetryDelay );
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
	#down = false;//the stream errored, and no result has come since
	//a push is the row's reading, and goes to the trend's buffer under the row's own series key;  a `revised` result is what the server
	//runs the item at - the ack's answer, or a later modify's - and a refused subscribe's OpcError is no reading, so neither is buffered.
	//Only a CHANGE of code asks for its name, as the children table does.
	#onResult( r:SubscriptionResult ):void{
		this.#down = false;
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
			if( !(r.value instanceof OpcError) )
				this.buffer.push( row.key, pushValue( row.node, r, new Date(), row.trendKey ) );
			if( (row.sc && row.sc!=previous && !OpcError.statusCodeText( row.sc )) || row.variable?.valueNamePending( row.value ) )//a StatusCode value's name too (Variable.valueName)
				this.gateway.updateErrorCodes().then( ()=>this.#repaint() );
		}
		this.#repaint();
	}
	//the cells bind to plain fields on the rows:  zoneless runs no pass of its own for a push.  `shown` reads #changes, so its sort
	//and filters follow what changed in place (419-watch #7);  it is computed once a pass, however many pushes came.
	#repaint(){
		this.#changes.update( n=>n+1 );
		this.#cdRef.markForCheck();
	}
	#changes = signal( 0 );

	//rows picked in the dialog - by id, or from the search - each with the defaults:  described, watched and saved
	//- on a connection of the list once it is read, or, where it is refused, one the rows or the node pages know (419-watch #11)
	async onAdd(){
		await this.#connectionsRead;
		const connections = this.#addChoices();
		if( !connections.length )
			return this.#snackbar.warn( "No connection of this gateway could be read to add from." );
		const picks = await WatchAddDialog.open( this.#dialog, {gateway: this.gateway, connections} );
		if( picks?.length )
			await this.add( picks );
	}
	#addChoices():WatchConnection[]{
		if( this.connections().length )
			return this.connections();
		const slugs = new Set( [...this.rows().map( r=>r.opc ), ...this.#opcStore.cnnctnSlugs( this.gateway.slug )] );
		return [...slugs].map( slug=>({slug, name: this.cnnctnName( slug )}) );
	}
	async add( picks:WatchPick[] ):Promise<void>{
		const gateway = this.gateway;
		const fresh:WatchRow[] = [];
		for( const pick of picks ){
			const key = WatchRow.keyOf( pick.opc, pick.node );
			if( !this.rows().some( r=>r.key==key ) && !fresh.some( r=>r.key==key ) )
				fresh.push( new WatchRow( pick.opc, pick.node, {} ) );
		}
		if( !fresh.length )
			return this.#snackbar.info( "Already on the list." );
		this.rows.update( rows=>[...rows, ...fresh] );
		this.#saving( this.#store.append( gateway.slug, fresh.map( r=>r.toJson() ) ) );
		await this.#describe( fresh );
		if( this.#isCurrent( gateway ) )
			this.#watch( fresh );
	}
	async remove( row:WatchRow ){
		this.rows.update( rows=>rows.filter( r=>r!==row ) );
		this.#untick( row );
		this.buffer.remove( row.key );
		this.#saving( this.#store.remove( this.gateway.slug, row.opc, row.node ) );
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
	//list*), and the ack's revised values replace the row's - what the item runs at until then, and after a request that fails
	async onParameters( row:WatchRow ){
		const parameters = await WatchParametersDialog.open( this.#dialog, {name: row.name, parameters: row.parameters} );
		if( parameters )
			await this.setParameters( row, parameters );
	}
	async setParameters( row:WatchRow, parameters:WatchParameters ):Promise<void>{
		row.parameters = parameters;
		this.#saving( this.#store.setParameters( this.gateway.slug, row.opc, row.node, parameters ) );
		if( !row.error )
			this.#send( row.opc, [row] );
		this.#repaint();
	}
	//each change goes to the list as the server holds it, not this page's rows written whole (419-watch #9)
	#saving( write:Promise<unknown> ){
		write.catch( e=>this.#snackbar.exception( "Could not save the watch list.", e ) );
	}

	//The rows the Trend tab draws:  ticked on the list, up to the palette's eight, and only a row with a value to trend - though a ticked
	//row that errors later can still be unticked (420 #2).  Session state, as the History tab's chips are.
	canTick( row:WatchRow ):boolean{ return this.ticked().has( row.key ) || (!row.error && !noValue( row ) && this.ticked().size<GatewayWatch.maxSeries); }
	toggleTick( row:WatchRow ){
		if( this.ticked().has( row.key ) )
			this.#untick( row );
		else if( this.canTick( row ) )
			this.ticked.update( keys=>new Set( keys ).add( row.key ) );
	}
	#untick( row:WatchRow ){
		if( this.ticked().has( row.key ) )
			this.ticked.update( keys=>{ const next = new Set( keys ); next.delete( row.key ); return next; } );
	}
	tickTooltip( row:WatchRow ):string{
		if( this.canTick( row ) )
			return "Draw on the Trend tab";
		return row.error ? "No value to trend." : noValue( row ) ? `No value to trend:  ${OpcError.text( row.sc! )}` : `At most ${GatewayWatch.maxSeries} rows at a time`;
	}
	//the window picked on the Trend tab is the buffer's, and kept between visits;  the default is not written, as a tab index is not
	setTrendWindow( minutes:number ){
		this.trendWindow.set( minutes );
		this.buffer.window = minutes*60_000;
		this.#prefs.set( GatewayWatch.prefsOwner, 'trendWindow', minutes==GatewayWatch.defaultWindow ? null : minutes );
	}

	//the header's click:  the rows follow the view on their own
	onSortChange( sort:Sort ){ this.viewSet.sort( sort ); }
	//the cells' values by column, which the filter box suggests
	filterValues = computed<Record<string,unknown[]>>( ()=>{
		this.#changes();//a describe's names and a push's reading land on the rows in place
		return cellValues( Object.keys(WatchView.columns), this.rows(), ( r, name )=>WatchView.cellValue(r, name, this.names) );
	} );

	//the cells
	readonly names = ( slug:CnnctnSlug )=>this.cnnctnName( slug );
	cell( row:WatchRow, column:string ):string{ return WatchView.cellValue( row, column, this.names )?.toString() ?? ""; }
	valueText( row:WatchRow ):string{ return row.valueName ?? valueString( <any>row.value ); }
	readonly structured = StructureTable.shows;//a value the cell's StructureTable shows (clients spec *Structures*)
	status( row:WatchRow ):string{ return row.error ? row.error : row.sc==undefined ? "" : OpcError.text( row.sc ); }
	statusTooltip( row:WatchRow ):string{ return row.sc ? `${scHex( row.sc )} - ${OpcError.text( row.sc )}` : ""; }
	qualityIcon( row:WatchRow ){ return row.error ? "error" : statusIcon( row.sc ); }
	//the revised values beside the request:  a modify the server refused keeps the item as it was, and the cell says so
	revisedTooltip( row:WatchRow ):string{ return row.revised?.sc ? `The last change failed (${scHex( row.revised.sc )} ${OpcError.text( row.revised.sc )}):  the item runs as it was.` : ""; }
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
	error = signal<string|undefined>( undefined );
	viewSet = WatchView.viewSet( inject( ProfileStore ), inject( LocalPrefs ) );
	view = this.viewSet.current;
	columns = WatchView.columns;
	shown = computed<WatchRow[]>( ()=>{
		this.#changes();
		return this.view() ? this.viewSet.sliced().narrowed( this.#filtered(), this.names ) : [];
	} );
	//The table's rows:  those shown, under a header per group where the view is grouped (views spec *Group by*), a collapsed
	//group's left out.  Every row is here, so a header's count and its sums are its group's, cut again on every push.
	tableRows = computed<(WatchRow|GroupHeader)[]>( ()=>{
		const grouping = this.view()?.grouping( this.columns );
		return grouping ? groupRows( this.shown(), grouping, ( row, column )=>WatchView.cellValue(row, column, this.names), this.viewSet.collapsed() ) : this.shown();
	} );
	readonly isGroup = ( _:number, row:unknown )=>isGroupHeader( row );
	readonly groupColumns = ["jde-group"];
	//the rows the view's filter keeps, which the rows shown and the slice panel's counts share:  read again on every push
	#filtered = computed<WatchRow[]>( ()=>{
		this.#changes();
		return this.view()?.filtered( this.rows(), this.names ) ?? [];
	} );
	//the slice panel's counts, of the rows under the view's filter
	sliceCounts = computed<SliceCount[]>( ()=>this.view()?.sliceCounts( this.#filtered(), this.names ) ?? [] );
	displayedColumns = computed<string[]>( ()=>['trend', ...(this.view()?.displayedColumns ?? []), 'actions'] );
	//not kept between visits:  the ticked rows aren't, so a page left on Trend reopened on an empty one (420 #9)
	tabIndex = signal( 0 );
	trendStepped = signal( true );//the Trend tab's line shape, kept while the tabs switch, as the tab is made again each visit (420 #10)
	ticked = signal( new Set<string>() );//the rows' keys
	//the ticked rows in list order, so a row keeps its colour whatever the sort;  #changes, as a describe names a row in place
	tickedRows = computed<WatchRow[]>( ()=>{ this.#changes(); const ticked = this.ticked(); return this.rows().filter( r=>ticked.has( r.key ) ); } );
	trendWindow = signal<number>( GatewayWatch.windowOf( this.#prefs.get( GatewayWatch.prefsOwner, 'trendWindow', GatewayWatch.defaultWindow ) ) );//minutes
	buffer = new PushBuffer( this.trendWindow()*60_000, key=>this.ticked().has( key ) );//the ticked rows are the last to give way (420 #6)
	static windowOf( minutes:unknown ):number{ return typeof minutes=='number' && PushBuffer.windows.includes( minutes ) ? minutes : GatewayWatch.defaultWindow; }
	statusShown = computed<boolean>( ()=>this.displayedColumns().includes( "status" ) );
	get sort():Sort{ return this.view().sort[0] ?? {active: "", direction: ""}; }//none:  `No sorting` leaves the rows as they came, and no header shows an arrow
	get subscription(){ return this.#subscription; } #subscription:Subscription|undefined;
	set subscription( x ){ if( !x && this.#subscription ) this.#subscription.unsubscribe(); this.#subscription = x; }
}
//a refused subscribe's reading, or a Bad push before any value:  nothing goes to the buffer, so a tick drew no line (420 #7)
const noValue = ( row:WatchRow ):boolean=>row.value===undefined && isBad( row.sc );