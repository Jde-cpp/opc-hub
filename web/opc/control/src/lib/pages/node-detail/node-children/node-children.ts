import { SelectionModel, SelectionChange } from '@angular/cdk/collections';
import { ChangeDetectorRef, Component, computed, effect, inject, model, OnDestroy, OnInit, output, Signal, signal } from '@angular/core';
import {MatButtonModule} from '@angular/material/button';
import {MatCheckboxModule} from '@angular/material/checkbox';
import {MatChipsModule} from '@angular/material/chips';
import {MatDialog} from '@angular/material/dialog';
import {MatIconModule} from '@angular/material/icon';
import {MatMenuModule} from '@angular/material/menu';
import {MatSlideToggleModule} from '@angular/material/slide-toggle';
import { MatSortModule, Sort } from "@angular/material/sort";
import {MatTabsModule} from '@angular/material/tabs';
import {MatTooltipModule} from '@angular/material/tooltip';
import {RouterModule, ActivatedRoute} from '@angular/router';
import { Gateway, SubscriptionResult } from '../../../services/gateway-service';
import { LocalPrefs, ProfileStore } from 'jde-spa';
import { cellValues, SnackbarService, ViewColumnCell, ViewColumnHeader, ViewColumns, ViewToolbar } from 'jde-framework'
import { EAccess } from '../../../model/types';
import {  MatTableModule } from '@angular/material/table';
import { Subscription } from 'rxjs';
import { NodePageData } from '../../../services/resolvers/node-resolver';
import { OpcStore } from '../../../services/opc-store';
import { WatchStore } from '../../../services/watch-store';
import { Reading } from '../../../model/value';
import { OpcError } from '../../../model/opc-error';
import { scHex, statusIcon } from '../../../model/status-code';
import { ENodeClass, Method, Variable, UaNode }  from '../../../model/node';
import { MethodCallDialog } from './method-call-dialog';
import { WriteDialog } from './write-dialog';
import { NodeView } from '../../../model/node-view';
import { Server } from '../../../model/server';
import { NodeId } from '../../../model/node-id';
import { StructureTable } from '../../structure-table/structure-table';

@Component({
	selector: 'node-children',
	templateUrl: './node-children.html',
	styleUrls: ['./node-children.scss'],
	imports: [RouterModule,MatButtonModule,MatCheckboxModule,MatChipsModule,MatIconModule,MatMenuModule,MatSlideToggleModule,MatSortModule,MatTableModule,MatTabsModule,MatTooltipModule,StructureTable,ViewColumnCell,ViewColumnHeader,ViewColumns,ViewToolbar]
})
export class NodeChildren implements OnInit, OnDestroy {
	#route = inject( ActivatedRoute );
	#snackbar = inject( SnackbarService );
	#cdRef = inject( ChangeDetectorRef );
	constructor(){
		//the sidenav siblings sort by the active view (OpcStore.setRoute), and a node switch is resolved before this component hears
		//of it - so the store learns of every view change here, header sorts included, ahead of the next navigation.
		effect( ()=>{ if( this.view() ) this.#opcStore.nodeView.set( this.view() ); } );
	}

	async ngOnInit() {
		//the views are the table's, not a node's:  loaded once, ahead of the first node.  route.data replays its current
		//value on subscribe, so a navigation during the load is not missed.
		await NodeView.loadViews( this.viewSet );
		this.#route.data.subscribe( async (data)=>{
			this.pageData = data["pageData"];
			//A node switch is a param change, so Angular reuses this component and ngOnDestroy never runs: without this the
			//previous node's rows stayed selected, and since `nodes` is a fresh UaNode array per navigation, re-selecting the
			//persisted subscriptions below added a SECOND entry for the same node rather than being a no-op (SelectionModel
			//dedupes by object identity).  Three round trips left four copies of one node, and isAllSelected() compares
			//`selected.length` to `nodes.length`, so the header checkbox went wrong.
			//A NEW model rather than selections.clear(): clear() emits `removed`, which onSubscriptionChange cannot tell from
			//the user unticking the rows - it would unsubscribe and drop the persisted subscriptions.  Dropping the live
			//subscription with it matches what ngOnDestroy does, and keeps it paired with the owner key, which is per node.
			this.subscription = undefined;
			this.profile = await this.#profileStore.load<UserSettings>( this.pageData.route.profileKey, new UserSettings() );
			this.profile.subscriptions = (this.profile.subscriptions ?? []).map( (s)=>new NodeId(s) );//revive persisted plain objects into NodeId instances (s.equals/s.key below would otherwise throw)
			this.setNodes( this.pageData.nodes, true );
			this.isLoading.set( false );//the page's title is NodeDetail's:  set here too from the `node` input, which lags a navigation - and a hidden tab's never updates - it named a node already left (the #416 browser walk)
		});
	}

	//No selections.clear():  it emits `removed`, which unticked every row in the profile - a switch to the History tab, which
	//destroys this one, came back to none.
	ngOnDestroy() {
		this.selections.changed.unsubscribe();
		this.subscription = undefined;
	}

	//fresh UaNode objects - a navigation, or a re-browse of the same node.  The persisted subscriptions pick out the rows to
	//show ticked.  On a navigation the live subscription was dropped above, so select() them and let `added` recreate it at
	//the gateway;  a re-browse keeps the live subscription (it is keyed by NodeId), so the new model is built with them
	//pre-selected instead - the constructor does not emit `changed`, so nothing is re-subscribed, and nothing unsubscribed.
	private setNodes( nodes:UaNode[], resubscribe:boolean ){
		this.pageData.nodes = nodes;
		this.nodes.set( nodes );
		const persisted = nodes.filter( n=>this.profile.subscriptions.some(s=>s.key==n.key) );
		//A re-browse can drop a row the live subscription still holds - deleted or renamed server-side.
		//Nothing else unsubscribed it (a new SelectionModel emits no `changed`), so the server went on publishing values for a
		//node with no row and the next-handler below threw on every tick.  The persisted INTENT stays put, so the node
		//re-subscribes on its own if a later browse brings it back.
		if( !resubscribe && this.subscription ){
			const orphaned = this.profile.subscriptions.filter( s=>!nodes.some(n=>n.key==s.key) );
			if( orphaned.length )
				this._iot.unsubscribe( this.cnnctnSlug, orphaned, this.Key ).catch( e=>this.#snackbar.exception("Could not remove subscription.", e) );
		}
		this.selections = new SelectionModel<UaNode>( true, resubscribe ? [] : persisted );
		this.selections.changed.subscribe( this.onSubscriptionChange.bind(this) );
		if( resubscribe )
			this.selections.select( ...persisted );
	}
	async onRefresh(){
		this.isRefreshing.set( true );
		try{
			const references = await this._iot.browseObjectsFolder( this.cnnctnSlug, this.node(), true );
			for( const fresh of <Variable[]>references.filter(r=>r.isVariable) ){
				const previous = this.variables.find( v=>v.key==fresh.key );
				if( previous )
					fresh.takeOver( previous );
			}
			if( this.nodes().some( n=>this.isSelf(n) ) ){//a Variable page's own row, re-read as its children were, stays first (NodeResolver)
				const self = <Variable>this.node();
				this.#setReading( self, await this._iot.read( this.cnnctnSlug, self.nodeId ) );
				references.unshift( self );
			}
			this.setNodes( references, false );
			this.refreshed.emit( this.pageData.nodes );
		}
		catch( e ){
			this.#snackbar.exception( "Could not refresh nodes.", e );
		}
		finally{
			this.isRefreshing.set( false );
		}
	}

	//the header's click:  the rows re-derive from view() on their own
	onSortChange( sort:Sort ){
		this.viewSet.sort( sort );
	}
	//the cells' values by column, which the filter box suggests
	filterValues = computed<Record<string,unknown[]>>( ()=>{
		this.#changes();//a push's reading lands on the node in place
		return cellValues( Object.keys(NodeView.columns), this.nodes(), NodeView.cellValue );
	} );

	//the value cells bind to plain fields on the nodes, not signals, so an async write (or a subscription push) changes nothing
	//Angular is watching:  zoneless runs no pass of its own, and the cell kept the old value until an unrelated event forced one.
	#repaint(){
		this.#changes.update( n=>n+1 );
		this.#cdRef.markForCheck();
	}
	#changes = signal( 0 );
	//every reading a row takes - a push, a write's echo, a re-read - so the quality travels with the value.  Only a CHANGE of
	//code asks for its name:  a fault holds its code on every tick it lasts, and the socket has no REST call behind it to bring one.
	//A StatusCode value whose name hasn't come asks too (Variable.valueName).
	#setReading( row:Variable, r:Reading ){
		const previous = row.sc;
		row.setReading( r );
		if( (row.sc && row.sc!=previous && !OpcError.statusCodeText(row.sc)) || row.valueNamePending() )
			this._iot.updateErrorCodes().then( ()=>this.#repaint() );
	}

	toObject( x:ENodeClass ):string{ return ENodeClass[x]; }
	readonly structured = StructureTable.shows;//a value the cell's StructureTable shows (clients spec *Structures*)
	dataType( n:UaNode ):string{ return this.cell( n, "dataType" ); }
	cell( n:UaNode, column:string ):string{ return NodeView.cellValue( n, column )?.toString() ?? ""; }//a column's text, as the view sorts and filters it
	//the Access cell is a chip per flag, so it takes the list the cell text is joined from rather than the text
	access( n:UaNode ):string[]{ return NodeView.accessList( n.isVariable ? n as Variable : undefined ); }
	checkboxLabel(row?: UaNode): string {
		return row
			? `${this.selections.isSelected(row) ? 'deselect' : 'select'} ${row.name}`
			: `${this.isAllSelected() ? 'deselect' : 'select'} all`;
	}
	async onSubscriptionChange( r:SelectionChange<UaNode> ){
		if( r.added.length>0 ){
			try {
				let nodes = r.added.map( r=>r.nodeId );
				//Dedupe by key.  Restoring persisted subscriptions re-selects those rows, and `selections.changed` is subscribed
				//before the awaited profile load resumes, so that restore re-enters here — a blind push re-added every
				//already-persisted NodeId on every visit and the array grew by N each time until an unsubscribe pruned the key.
				//Compare by key like the removal branch below: these are fresh NodeId instances, so identity never matches.
				const unsaved = nodes.filter( n=>!this.profile.subscriptions.some(s=>s.key==n.key) );
				this.profile.subscriptions.push( ...unsaved );//`nodes` (not `unsaved`) still drives the gateway calls below - after a reload the live subscription does not exist yet even for persisted keys
				if( unsaved.length )
					this.#saveProfile();
				if( !this.subscription){
					this.subscription = this._iot.subscribe( this.cnnctnSlug, nodes, this.Key ).subscribe({
						next:(value: SubscriptionResult) =>{
							if( value.revised )//what the server revised the node's item to (spec *Watch list*):  the watch list's to show, no reading
								return;
							const row = this.variables.find( (r)=>r.nodeId.equals(value.node) );
							if( !row )
								return console.debug( `subscription value for '${value.node}', which is no longer a row on this page.` );
							this.#setReading( row, value );
							this.#repaint();
						},
						error:(e: Error) =>{
							//An errored Subscription is dead but still TRUTHY, so `if( !this.subscription )` sent the next tick down
							//the addToSubscription branch - which builds a fresh gateway Subject with no observer on it.  Every value
							//was then dropped while the rows still showed as live, until the user navigated away and back.  Clearing
							//it first (before anything that could throw) makes the next tick re-subscribe for real.
							this.subscription = undefined;
							this.#snackbar.exception( "Subscription error.", e );
						},
						complete:()=>{ this.subscription = undefined; console.debug( "complete" );}
					});
				}
				else
					this._iot.addToSubscription( this.cnnctnSlug, nodes, this.Key );
			} catch (e) {
				this.#snackbar.exception( "Could not add subscription.", e );
			}
		}
		if( r.removed.length>0 ){
			let nodes = r.removed.map( r=>r.nodeId );
			const kept = this.profile.subscriptions.filter( s=>!nodes.some(n=>n.key==s.key) );//compare by value: r.removed nodeIds are fresh instances, so reference includes() never matched
			if( kept.length!=this.profile.subscriptions.length ){
				this.profile.subscriptions = kept;
				this.#saveProfile();
			}
			if( !this.selections.selected.length )
				this.subscription = undefined;
			else{
				try{
					this._iot.unsubscribe( this.cnnctnSlug, nodes, this.Key );
				}
				catch( e ) {
					this.#snackbar.exception( "Could not remove subscription.", e );
				}
			}
		}
	}

	//As the tick is made:  the save used to wait for the next node, so a tab switch, a reload or leaving the page lost it.
	#saveProfile(){
		this.#profileStore.save<UserSettings>( this.Key, this.profile ).catch( e=>this.#snackbar.exception("Could not save the subscriptions.", e) );
	}

	//the header box covers the rows on screen that can be subscribed - an object has no value, and its own box is disabled
	get selectableRows():UaNode[]{ return this.rows().filter( n=>n.isVariable ); }
	isAllSelected():boolean{ const rows = this.selectableRows; return rows.length>0 && rows.every( n=>this.selections.isSelected(n) ); }//a plain method:  a computed() over the non-signal SelectionModel cached its first value forever
	toggleAllRows() {
		if( this.isAllSelected() )
			this.selections.deselect( ...this.selectableRows );
		else
			this.selections.select( ...this.selectableRows );
	}

	//a Variable row's Write (clients spec *Values and writes*, #423):  the dialog types the value as the DataType says - every built-in
	//type, an array whole or by index range, a structure by member - sends it through the gateway and closes with the value read back,
	//which the row shows with its quality;  a refusal stays in the dialog for another go, and the row keeps its reading.
	async write( n:Variable ){
		const reading = await WriteDialog.open( this.#dialog, { node: n, write: ( value, indexRange )=>this._iot.write( this.cnnctnSlug, n.nodeId, value, indexRange ) } );
		if( !reading )
			return;
		this.#setReading( n, reading );
		this.#repaint();
	}

	//a Method row's Call (clients spec *Methods*):  the dialog reads the method's arguments through the gateway and calls it on this
	//page's node, the object the method belongs to.  The codes it answers are registered before the names are fetched - the
	//fetch asks only for codes already pending, and the dialog's template would register them after it.
	call( m:Method ){
		MethodCallDialog.open( this.#dialog, {
			object: this.node(), method: m,
			arguments: ()=>this._iot.methodArguments( this.cnnctnSlug, m.nodeId ),
			call: args=>this._iot.call( this.cnnctnSlug, this.nodeId, m.nodeId, args ),
			names: codes=>{
				if( codes.filter( sc=>!OpcError.statusCodeText(sc) ).length )
					this._iot.updateErrorCodes();
			}
		} );
	}
	//a Variable row's Watch (clients spec *Watch list*, #419):  added to the gateway's watch list with the defaults, where its parameters are set
	async watch( n:Variable ){
		try{
			const added = await this.#watchStore.add( this._iot.slug, this.cnnctnSlug, n.nodeId );
			this.#snackbar.info( added ? `Watching ${n.name}.` : `${n.name} is on the watch list already.` );
		}
		catch( e ){
			this.#snackbar.exception( `Could not watch ${n.name}.`, e );
		}
	}
	get watchLink():string[]{ return ['/apps', 'gateways', this._iot.slug, 'watch']; }
	routerLink(n:UaNode):string[]{
		return [ `./${n.browseFQ(this.server.connection.defaultBrowseNs, this.nodes(), !!this.pageData.route.node?.isRoot)}` ];//the route's node, as isSelf's
	}
	isSelf( n:UaNode ):boolean{ return n.key==this.pageData.route.node?.key; }//a Variable page's own row (NodeResolver):  its name is no link.  The route's node, which `node` is too:  a row renders before the model is set
	get _iot():Gateway{ return this.pageData.gateway; }
	//the quality of a row's reading (OPC 10000-4 7.38) - the Status cell's text, and the icon that stands in beside the value where a view hides the column
	status( r:UaNode ):string{ return NodeView.cellValue( r, "status" )?.toString() ?? ""; }
	//the icon's tooltip carries the name, as the icon is all there is;  the Status cell's is the code alone, beside a name the
	//cell already spells out.  Neither for plain Good - it adds nothing, and an empty matTooltip does not open.
	statusTooltip( r:Variable ):string{ return r.sc ? `${scHex( r.sc )} - ${this.status( r )}` : ""; }
	qualityIcon( r:UaNode ){ return this.status( r ) ? statusIcon( (r as Variable).sc ) : undefined; }
	//install-issues #35.  Two bugs lived on the `< EAccess.Write` this replaces.  It was a magnitude test on a bitmask, so
	//Read|HistoryRead (5) and Read|StatusWrite (0x21) - read-only nodes both - compared "greater than Write" and offered an
	//editor the server would refuse;  and `null < 2` is true, so a server that reports no access level at all (see
	//Variable.accessLevelOf) locked every cell it had.  Now: a level we were given decides by its CurrentWrite bit, and one we
	//were not given is unknown, not a denial - the editor stays live and the server's own refusal is the answer, which is a
	//status the user can see rather than a dimmed box that says nothing.
	readOnly( r:Variable ):boolean{ return r.stale || (r.userAccessLevel!=undefined && !(r.userAccessLevel & EAccess.Write)); }//a stale value is the last one known, not one to write over
	//why a cell has no Write - the tooltip and aria-label on the lock beside it.  #35: a locked cell with no explanation is what cost
	//the walk the afternoon;  the level is spelled out so an operator can tell "the server says read-only" from "the value is
	//stale" without the log.
	readOnlyReason( r:Variable ):string{
		if( !r.isVariable || this.readDenied(r) || !this.readOnly(r) )
			return "";//nothing to explain, or a cell with its Write
		if( r.stale )
			return `The last reading was ${this.status(r) || "bad"}, so this is the last value known and not one to edit.`;
		const names = NodeView.accessList( r );
		return `The server reports this node read-only for you (access: ${names.length ? names.join(", ") : "none"}).`;
	}
	readDenied( r:UaNode ):boolean{ return NodeView.readDenied( r ); }//the Snapshot cell's "no read access" - the model's rule, reachable from the template
	isLoading = signal<boolean>( true );
	isRefreshing = signal<boolean>( false );
	get Key():string{ return this.pageData.route.profileKey; }
	node = model.required<UaNode>();
	refreshed = output<UaNode[]>();//the nodes a Refresh browsed:  pageData.nodes is assigned, which no signal sees
	get nodeId(){ return this.node().nodeId; }
	get server():Server{ return this.pageData.server; }
	get cnnctnSlug():string{ return this.server.connection.slug; }
	pageData!:NodePageData;
	profile!:UserSettings;
	nodes = signal<UaNode[]>( [] );//every child the browse returned;  rows() is the view's cut of them, less the system nodes while the toggle is off
	//the *system nodes* toggle (clients spec *Address space*):  the store's, which keeps it per browser and sets the sidenav by it
	get showSystem():Signal<boolean>{ return this.#opcStore.showSystem; }
	setShowSystem( show:boolean ):void{ this.#opcStore.setShowSystem( show ); }
	rows = computed<UaNode[]>( ()=>{
		const sorted = this.view() ? this.view().apply( this.nodes().filter( n=>this.showSystem() || !n.isSystem || this.isSelf(n) ) ) : [];//a ns-0 Variable page keeps its own row
		const self = sorted.find( n=>this.isSelf(n) );//a Variable page's own row stays first, whatever the view's sort
		return self ? [self, ...sorted.filter( n=>n!==self )] : sorted;
	} );
	//every row the view would show is a system node the toggle hides - the Types folder's, by default:  the empty state says so, not
	//that the view matches nothing (reviews-issue/416-address-space.md #12)
	hiddenBySystem = computed<boolean>( ()=>!this.showSystem() && !this.rows().length && !!this.view() && this.view().apply( this.nodes() ).length>0 );
	displayedColumns = computed<string[]>( ()=>[...(this.view()?.displayedColumns ?? []), 'menu'] );//the row menu last, whatever the view shows
	statusShown = computed<boolean>( ()=>this.displayedColumns().includes("status") );//the Snapshot cell's quality icon stands in for the Status column, so it shows only where the view hides that
	snapshotShown = computed<boolean>( ()=>this.displayedColumns().includes("snapshot") );//a Method row's Call is in the Snapshot cell, and in its Name cell where the view hides that (reviews-issue/318-opc-calls.md #6)
	get variables():Variable[]{ return <Variable[]>this.nodes().filter((x)=>x.nodeClass==ENodeClass.Variable); }
	selections = new SelectionModel<UaNode>(true, []);
	//sideNav = model.required<NodeRoute>();
	get sort():Sort{ return this.view().sort[0] ?? {active: "", direction: ""}; }//none:  `No sorting` leaves the rows as they came, and no header shows an arrow
	get subscription(){return this.#subscription;} #subscription:Subscription|undefined;
	set subscription(x){ if(!x && this.subscription) this.subscription.unsubscribe(); this.#subscription=x; }
	viewSet = NodeView.viewSet( inject(ProfileStore), inject(LocalPrefs) );
	view = this.viewSet.current;
	columns = NodeView.columns;

	#profileStore = inject( ProfileStore );
	#opcStore = inject( OpcStore );
	#watchStore = inject( WatchStore );
	#dialog = inject( MatDialog );
}
//per node (keyed by profileKey):  the subscriptions.  The tab index used to live here too, but is LocalPrefs' now;  the columns and sort are the view's now, shared by every node.
class UserSettings{
	subscriptions:NodeId[] = [];
//	access:NodeAccessProfile = new NodeAccessProfile();
}