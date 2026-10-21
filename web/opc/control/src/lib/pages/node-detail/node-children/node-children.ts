import { SelectionModel, SelectionChange } from '@angular/cdk/collections';
import { ChangeDetectorRef, Component, computed, effect, inject, model, OnDestroy, OnInit, output, signal } from '@angular/core';
import {MatCheckboxChange, MatCheckboxModule} from '@angular/material/checkbox';
import {MatChipsModule} from '@angular/material/chips';
import {MatIconModule} from '@angular/material/icon';
import {MatSelectChange, MatSelectModule} from '@angular/material/select';
import { MatSortModule, Sort } from "@angular/material/sort";
import {MatTooltipModule} from '@angular/material/tooltip';
import {RouterModule, ActivatedRoute} from '@angular/router';
import { Gateway, SubscriptionResult } from '../../../services/gateway-service';
import { LocalPrefs, ProfileStore } from 'jde-spa';
import { DateUtils, QLListSettings, SnackbarService, ProtoUtils, Timestamp, View, ViewToolbar } from 'jde-framework'
import { EAccess, ETypes } from '../../../model/types';
import {  MatTableModule } from '@angular/material/table';
import { Subscription } from 'rxjs';
import { ComponentPageTitle } from 'jde-spa';
import { NodePageData } from '../../../services/resolvers/node-resolver';
import { OpcStore } from '../../../services/opc-store';
import { Reading, Value, valueString } from '../../../model/value';
import { OpcError } from '../../../model/opc-error';
import { scHex, statusIcon } from '../../../model/status-code';
import { ENodeClass, Variable, UaNode }  from '../../../model/node';
import { NodeView } from '../../../model/node-view';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatDatepickerInputEvent, MatDatepickerModule } from '@angular/material/datepicker';
import { MatInputModule } from '@angular/material/input';
import { Server } from '../../../model/server';
import { NodeId } from '../../../model/node-id';

@Component({
	selector: 'node-children',
	templateUrl: './node-children.html',
	styleUrls: ['./node-children.scss'],
	imports: [RouterModule,MatCheckboxModule,MatChipsModule,MatDatepickerModule,MatFormFieldModule,MatIconModule,MatInputModule,MatSortModule,MatTableModule,MatTooltipModule,MatSelectModule,QLListSettings,ViewToolbar]
})
export class NodeChildren implements OnInit, OnDestroy {
	#route = inject( ActivatedRoute );
	#snackbar = inject( SnackbarService );
	#componentPageTitle = inject( ComponentPageTitle );
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
			if( this.pageData )
				//the previous node's settings, on the way to the next one:  warn rather than snackbar a page being navigated away from.
				this.#profileStore.save<UserSettings>( this.pageData.route.profileKey, this.profile )
					.catch( e=>console.warn("Could not save the node settings.", e) );
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
			this.isLoading.set( false );
			this.#componentPageTitle.title = this.server.connection.name + (this.node().id==85 ? '' : `/${this.node().name}`);
		});
	}

	ngOnDestroy() {
		this.selections.clear();
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
		//A re-browse can drop a row the live subscription still holds - deleted or renamed server-side, or `displayed` flipped.
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
			//REST answers a Bad reading with the code alone, so a refresh in the middle of a fault would blank the value the row is
			//holding:  the new row takes it over, as a push would have left it.
			for( const fresh of <Variable[]>references.filter(r=>r.isVariable) ){
				if( fresh.stale && fresh.value===undefined )
					fresh.value = this.variables.find( v=>v.key==fresh.key )?.value;
			}
			this.setNodes( references.filter( r=>r.displayed ), false );
			this.refreshed.emit( this.pageData.nodes );
		}
		catch( e ){
			this.#snackbar.exception( "Could not refresh nodes.", e );
		}
		finally{
			this.isRefreshing.set( false );
		}
	}

	//The view group and settings panel, as ql-list/log-detail have them;  the rows re-derive from view() on their own.
	onSortChange( sort:Sort ){
		this.viewSet.sort( sort );
	}
	onViewShow( view:View ){
		this.isSettings.set( false );
		this.viewSet.show( view );
	}
	onViewSave( view:View ){
		this.viewSet.save( view ).catch( e=>this.#snackbar.exception("Could not save view.", e) );
		this.isSettings.set( false );
	}
	onViewDelete( view:View ){
		this.viewSet.remove( view ).catch( e=>this.#snackbar.exception("Could not save view.", e) );
		this.isSettings.set( false );
	}
	onChangeView( index:number ){
		this.viewSet.select( index );
	}
	colSuggestions():Record<string,any[]>{
		const suggestions:Record<string,any[]> = {};
		for( const name of Object.keys(NodeView.columns) ){
			const values = this.nodes().map( n=>NodeView.cellValue(n, name) ).filter( v=>v!=null && v!=="" );
			suggestions[name] = [...new Set(values)].slice( 0, 100 ).sort( (a,b)=>typeof a=="number" && typeof b=="number" ? a-b : String(a).localeCompare(String(b)) );
		}
		return suggestions;
	}

	toDate( value:Timestamp|undefined ):Date|null{
		const date = value ? ProtoUtils.toDate( value ) : null;
		return date ? DateUtils.asUtc( date ) : null;
	}
	//[value] on a MatDatepickerInput is dirty-checked by reference, and its setter reformats the input element - so binding a
	//freshly-built Date ran that setter on EVERY change-detection pass and rewrote the box from the model.  Any pass during an
	//edit (the click that closes the calendar, the keystrokes of a typed date) therefore wiped what was just entered and put
	//the pre-write value back.  Hand out the same Date while the timestamp is unchanged so the binding goes quiet;  #repaint
	//drops the entry where the box does have to be re-formatted from the model.
	dateValue( n:Variable ):Date|null{
		const date = this.toDate( <Timestamp|undefined>n.value );
		const cached = this.#dates.get( n );
		if( this.#dates.has(n) && (cached?.getTime() ?? null)==(date?.getTime() ?? null) )
			return cached!;
		this.#dates.set( n, date );
		return date;
	}
	//the value cells bind to plain fields on the nodes, not signals, so an async write (or a subscription push) changes nothing
	//Angular is watching:  zoneless runs no pass of its own, and the cell kept the old value until an unrelated event forced one.
	#repaint( n?:Variable ){
		if( n )
			this.#dates.delete( n );//a new reference, so the datepicker re-formats the box:  a failed write leaves the model as it was, but not the text in the box
		this.#cdRef.markForCheck();
	}
	#dates = new WeakMap<Variable,Date|null>();
	//every reading a row takes - a push, a write's echo, a re-read - so the quality travels with the value.  Only a CHANGE of
	//code asks for its name:  a fault holds its code on every tick it lasts, and the socket has no REST call behind it to bring one.
	#setReading( row:Variable, r:Reading ){
		const previous = row.sc;
		row.setReading( r );
		if( row.sc && row.sc!=previous && !OpcError.statusCodeText(row.sc) )
			this._iot.updateErrorCodes().then( ()=>this.#repaint() );
	}

	toObject( x:ENodeClass ):string{ return ENodeClass[x]; }
	toString( value:Value|undefined ){ return valueString(value); }//Variable.value is optional - a node the read could not answer for has none
	dataType( n:UaNode ):string{ return NodeView.cellValue( n, "dataType" )?.toString() ?? ""; }
	//the Access cell is a chip per flag, so it takes the list the cell text is joined from rather than the text
	access( n:UaNode ):string[]{ return NodeView.accessList( n.isVariable ? n as Variable : undefined ); }
	checkboxLabel(row?: UaNode): string {
		return row
			? `${this.selections.isSelected(row) ? 'deselect' : 'select'} ${row.name}`
			: `${this.isAllSelected() ? 'select' : 'deselect'} all`;
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
				if( !this.subscription){
					this.subscription = this._iot.subscribe( this.cnnctnSlug, nodes, this.Key ).subscribe({
						next:(value: SubscriptionResult) =>{
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
			this.profile.subscriptions = this.profile.subscriptions.filter( s=>!nodes.some(n=>n.key==s.key) );//compare by value: r.removed nodeIds are fresh instances, so reference includes() never matched
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

	//the header box covers the rows on screen that can be subscribed - an object has no value, and its own box is disabled
	get selectableRows():UaNode[]{ return this.rows().filter( n=>n.isVariable ); }
	isAllSelected():boolean{ const rows = this.selectableRows; return rows.length>0 && rows.every( n=>this.selections.isSelected(n) ); }//a plain method:  a computed() over the non-signal SelectionModel cached its first value forever
	toggleAllRows() {
		if( this.isAllSelected() )
			this.selections.deselect( ...this.selectableRows );
		else
			this.selections.select( ...this.selectableRows );
	}

	toggleValue( n:Variable, e:MatCheckboxChange ){
		return this.#write( n, e.checked, ()=>e.source.checked = !!n.value );
	}
	changeDouble( n:Variable, e:Event ){
		const box = <HTMLInputElement>e.target;
		return this.#write( n, +box.value, ()=>box.value = n.value?.toString() ?? "" );
	}
	changeString( n:Variable, e:Event ){
		const box = <HTMLInputElement>e.target;
		return this.#write( n, box.value, ()=>box.value = n.value?.toString() ?? "" );
	}
	changeEnum( n:Variable, e:MatSelectChange<number> ){
		return this.#write( n, e.value, ()=>e.source.value = <number>n.value );
	}
	changeDate( n:Variable, e:MatDatepickerInputEvent<Date, any> ){
		if( !e.value )//unparseable text, or a cleared box:  beginningOfDay(null) is TODAY, and the old value would have been overwritten with it
			return this.#repaint( n );
		return this.#write( n, <Timestamp>ProtoUtils.fromDate(DateUtils.beginningOfDay(e.value)) );//#repaint(n) re-formats the box
	}
	//The editor ends showing the row's reading - the echo, or after a failure a re-read of the value - rather than what was
	//typed:  a binding is re-applied only when its value changes, so a refused write, or an echo of the old value, kept the text.
	async #write( row:Variable, value:Value, show?:()=>void ){
		try{
			this.#setReading( row, await this._iot.write(this.cnnctnSlug, row.nodeId, value) );
		}
		catch( e ){
			this.#snackbar.exception( `Could not write ${row.name}.`, e );
			await this._iot.read( this.cnnctnSlug, row.nodeId ).then( r=>this.#setReading(row, r), x=>console.warn(`Could not re-read ${row.name}.`, x) );
		}
		show?.();
		this.#repaint( row );
	}

	routerLink(n:UaNode):string[]{
		return [ `./${n.browseFQ(this.server.connection.defaultBrowseNs)}` ];
	}
	ETypes = ETypes;
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
	readOnly( r:Variable ):boolean{ return r.stale || (r.userAccessLevel!=undefined && !(r.userAccessLevel & EAccess.Write)); }//a stale value is the last one known, not one to edit
	//why an editor is locked - the tooltip and aria-label on the cell.  #35: a disabled box with no explanation is what cost
	//the walk the afternoon;  the level is spelled out so an operator can tell "the server says read-only" from "the value is
	//stale" without the log.
	readOnlyReason( r:Variable ):string{
		if( !r.isVariable || this.readDenied(r) || !this.readOnly(r) )
			return "";//no editor to explain, or one that is live
		if( r.stale )
			return `The last reading was ${this.status(r) || "bad"}, so this is the last value known and not one to edit.`;
		const names = NodeView.accessList( r );
		return `The server reports this node read-only for you (access: ${names.length ? names.join(", ") : "none"}).`;
	}
	readDenied( r:UaNode ):boolean{ return NodeView.readDenied( r ); }//the Snapshot cell's "no read access" - the model's rule, reachable from the template
	isLoading = signal<boolean>( true );
	isRefreshing = signal<boolean>( false );
	isSettings = signal<boolean>( false );
	get Key():string{ return this.pageData.route.profileKey; }
	node = model.required<UaNode>();
	refreshed = output<UaNode[]>();//the nodes a Refresh browsed:  pageData.nodes is assigned, which no signal sees
	get nodeId(){ return this.node().nodeId; }
	get server():Server{ return this.pageData.server; }
	get cnnctnSlug():string{ return this.server.connection.slug; }
	pageData!:NodePageData;
	profile!:UserSettings;
	nodes = signal<UaNode[]>( [] );//every child the browse returned;  rows() is the view's cut of them
	rows = computed<UaNode[]>( ()=>this.view() ? this.view().apply( this.nodes() ) : [] );
	displayedColumns = computed<string[]>( ()=>this.view()?.displayedColumns ?? [] );
	statusShown = computed<boolean>( ()=>this.displayedColumns().includes("status") );//the Snapshot cell's quality icon stands in for the Status column, so it shows only where the view hides that
	get variables():Variable[]{ return <Variable[]>this.nodes().filter((x)=>x.nodeClass==ENodeClass.Variable); }
	selections = new SelectionModel<UaNode>(true, []);
	//sideNav = model.required<NodeRoute>();
	get sort():Sort{ return this.view().sort[0] ?? {active: "name", direction: "asc"}; }
	get subscription(){return this.#subscription;} #subscription:Subscription|undefined;
	set subscription(x){ if(!x && this.subscription) this.subscription.unsubscribe(); this.#subscription=x; }
	viewSet = NodeView.viewSet( inject(ProfileStore), inject(LocalPrefs) );
	view = this.viewSet.current;
	viewCopy = computed<NodeView>( ()=>NodeView.copy(this.view()) );//the settings panel gets a copy to edit, never the one on screen - and one copy per view, not one per change-detection pass as a getter would hand it
	columns = NodeView.columns;
	schema = NodeView.schema;

	#profileStore = inject( ProfileStore );
	#opcStore = inject( OpcStore );
}
//per node (keyed by profileKey):  the subscriptions.  The tab index used to live here too, but is LocalPrefs' now;  the columns and sort are the view's now, shared by every node.
class UserSettings{
	subscriptions:NodeId[] = [];
//	access:NodeAccessProfile = new NodeAccessProfile();
}