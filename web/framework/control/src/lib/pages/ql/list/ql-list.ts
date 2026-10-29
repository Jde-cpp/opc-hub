import { Component, OnInit, input, signal, model, computed, inject } from '@angular/core';
import {ActivatedRoute, Router, RouterLink} from '@angular/router';
import {Sort} from '@angular/material/sort';
import { QLListSettings } from './ql-list-settings/ql-list-settings';
import {SnackbarService} from '../../../shared/snackbar/snackbar-service'
import {IGRAPHQL, IGraphQL, EnumValue } from '../../../services/graphql';
import {TableSchema}  from '../../../model/ql/schema/table-schema';
import {MetaObject}  from '../../../model/ql/schema/meta-object';

import { ComponentPageTitle, Latest, RouteItem, RouteStore, HELP_TOPICS, helpTopics, helpTopicFor, helpTopicRoute } from 'jde-spa';
import { MatIcon } from '@angular/material/icon';
import { MatButton } from '@angular/material/button';
import { CellText, GraphQLTable } from '../../graphql/table/graphql-table';
import { EmptyState, QLListData, QLListResolver, TableSettings } from '../../../services/ql-list-resolver';
import { Failure, failureState } from '../../../utils/errors';
import { FailureState } from '../../../shared/failure-state/failure-state';
import { SelectionModel } from '@angular/cdk/collections';
import { View, ViewField } from '../../../model/ql/view';
import { ViewSet } from '../../../model/ql/view-set';
import { MatDialog } from '@angular/material/dialog';
import { confirm } from '../../../shared/confirm/confirm-dialog';
import { QLRow } from '../../../model/ql/slug-row';
import { verify } from '../../../utils/utils';
import { PageEvent, Paginator } from '../../../shared/paginator/paginator';
import { ViewToolbar } from '../../../shared/view-toolbar/view-toolbar';

@Component({
	selector: 'ql-list',
	styleUrls: ['ql-list.scss'],
	templateUrl: './ql-list.html',
	host: {class:'main-content mat-drawer-container my-content'},
	imports: [FailureState, GraphQLTable, MatButton, MatIcon, Paginator, QLListSettings, RouterLink, ViewToolbar]
})
export class QLList implements OnInit{
	#route = inject( ActivatedRoute );
	#router = inject( Router );
	#componentPageTitle = inject( ComponentPageTitle );
	#snackbar = inject( SnackbarService );
	#dialog = inject( MatDialog );

	async ngOnInit(){
		if( this.listData() )//embedded (QLSelector):  the host resolved the data itself, there is no route resolver to subscribe to
			this.init( {data: this.listData()} );
		else
			this.#route.data.subscribe( (data)=>{ this.init( data ); } );
	}
	//The rows of a reload to show checked:  those `prior` checked, by id - a reload builds new row objects - and one at most on a
	//single-select list, where SelectionModel throws on more.
	static carriedSelection( prior:SelectionModel<QLRow>|undefined, rows:QLRow[], multiple:boolean ):SelectionModel<QLRow>{
		const priorIds = prior?.selected.map( r=>r?.id ).filter( id=>id!=null ) ?? [];
		const selected = rows.filter( r=>priorIds.includes(r.id) );
		return new SelectionModel<QLRow>( multiple, multiple ? selected : selected.slice(0, 1) );
	}
	async init( resolvedValue:Record<string,unknown> ){
		let data = resolvedValue["data"] as QLListData;
		verify( data.profile.current() );
		this.view.set( data.profile.current() );
		const collectionName = data.schema.collectionName;
		this.resolvedData.set( data );

		const rows = data.results[collectionName];
		this.selections.set( QLList.carriedSelection(this.selections(), rows, this.selector() || data.profile.current().showSelector) );
		this.data.set( rows );
		this.skip.set( data.skip ?? 0 );
		if( !this.selector() )
			QLListResolver.saveSkip( data, this.skip() );
		this.more.set( data.more ?? false );
		this.error.set( data.error );//a good load clears the last failure; a resolver that caught the rows query sets it
		this.sideNav.set( data.routing );
		let paths = [];
		for( let x = this.#route; x.routeConfig?.data && x.routeConfig?.data["name"]; x = x.parent! )
			paths.push( x.routeConfig.data['name'] );
		if( paths.length )//guard:  QLList also renders inside a tab (GatewayDetail), where the route has no 'name' - the unguarded assignment wrote undefined over the host page's title
			this.#componentPageTitle.title = paths[0];//.join( " | " ); 	//this.#componentPageTitle.title ? `${this.#componentPageTitle.title} | ${title}` : title;
		else if( this.#route.component==QLList && data.routing?.title )//the routed page itself (/access/resources):  a ':collectionDisplay' route has no title of its own, which left the tab bare
			this.#componentPageTitle.title = data.routing.title;

		this.isLoading.set( false );
	};
	async onSortChange( sort:Sort ){
		this.data.set( [] );
		this.profile().sort( sort );
		await this.#refresh();
	}

	//The soft-delete switch a route can put in the row (ViewFieldSettings.liveToggle).  Where `deleted` is the feature rather
	//than a trash can - a deleted `resource` is one the authorizer does not enforce - the flag is the page's whole point and
	//belongs in the row.  Both directions are confirmed:  enabling enforcement can lock the operator out of the very table
	//they are editing, and disabling it opens one up to everybody.
	async onToggleLive( row:QLRow ){
		const field = this.liveToggleField();
		const settings = field?.liveToggle;
		if( !field || !settings || this.pendingLive().includes(row.id) )
			return;
		const live = row[field.name]==null;
		const name = row["name"] ?? row["slug"] ?? `${row.id}`;
		const verb = live ? settings.disable : settings.enable;
		const message = (live ? settings.disableMessage : settings.enableMessage) ?? `${name} will be changed.`;
		if( !await confirm(this.#dialog, {title: `${verb} ${name}?`, message, confirm: verb}) )
			return;
		this.pendingLive.update( ids=>[...ids, row.id] );
		try{
			await this.#ql().mutate( `${live ? "delete" : "restore"}${this.type()}(id:${row.id})` );
			row[field.name] = live ? new Date() : null;
			this.data.set( [...this.data()] );//a new array:  the row object is mutated in place, and the table only re-renders off the reference
		}
		catch( e ){
			this.#snackbar.exception( `${verb} failed.`, e );
		}
		finally{
			this.pendingLive.update( ids=>ids.filter(id=>id!=row.id) );
		}
	}

	//The try/catch was dead:  router.navigate is ASYNC, so a route that does not exist rejects the promise long after the
	//block has returned.  A row click on a list with no detail route dead-ended in a NavigationError the user never saw -
	//hence both halves here:  don't offer the click where there is nowhere to go (canNavigate off, a rowLink with no segments),
	//and report it through the returned promise where the navigation still fails.
	onRowActivate( row:QLRow ){
		if( this.selector() || !this.canNavigate() )//ql-table already toggled the row; a selector has nowhere to navigate to
			return;
		const rowLink = this.tableSettings().rowLink;
		const link = rowLink ? rowLink.segments( row ) : [`${row.slug}`];
		if( !link )//a row with no page of its own
			return;
		this.#router.navigate( [...this.linkPrefix(), ...link], {relativeTo: this.#route} )
			.then( ok=>{ if( !ok ) this.#snackbar.error( `Could not navigate to '${link.join('/')}'.` ); } )
			.catch( e=>this.#snackbar.exception("Could not navigate to properties", e) );
	}

	onAdd(){
		this.#router.navigate( [...this.linkPrefix(), '$new'], {relativeTo: this.#route} );
	}

	async onRefresh(){
		await this.#reload();//the same page; keep the current rows visible, the progress bar signals the reload
	}

	//refresh() and reload() are only ever driven from template event handlers, so a rejection has nowhere to go but the
	//console:  three call sites did not even await it, and two more awaited with no catch.  The user was left looking at an
	//empty or stale table with nothing said.  Every re-query reports through one of these two now.
	async #refresh(){
		try{ await this.refresh(); }
		catch( e ){ this.#fail( e ); }
	}
	async #reload(){
		try{ await this.reload(); }
		catch( e ){ this.#fail( e ); }
	}
	//the snackbar goes in ten seconds; the page keeps saying it.  The rows are dropped too:  a refresh keeps them on screen
	//while it runs, and leaving stale rows under a failure reads as "current".
	#fail( e:unknown ){
		this.#snackbar.exception( "Could not refresh data.", e );
		this.error.set( e );
		this.data.set( [] );
	}

	async onPage( e:PageEvent ){
		this.skip.set( e.startIndex );
		await this.#reload();
	}

	async onViewSave( view:View ){
		this.data.set( [] );
		try{
			await this.profile().save( view );//awaited, or the catch below is dead code
		}
		catch( e ){
			this.#snackbar.exception( "Could not save view.", e );
		}
		await this.#refresh();
		this.isSettings.set( false );
	}
	async onViewShow( view:View ){
		this.data.set( [] );
		this.isSettings.set( false );
		this.profile().show( view );
		await this.#refresh();
	}
	async onChangeView( index:number ){
		if( this.profile().select(index) )
			await this.#refresh();
	}
	//another view, or this one edited, is a different set of pages:  from the first
	async refresh(){
		this.skip.set( 0 );
		this.resolvedData().schema = new TableSchema( this.resolvedData().schema ); //TODO just copy
		await this.reload();
	}
	//every path that re-queries goes through here so isRefreshing covers the whole load, not just the Refresh button.
	//Only the latest call lands:  page turns and view changes don't wait for each other, and an older query finishing last
	//would put back its own page, or the rows of the view it was sent under.
	async reload(){
		const id = this.#reloads.next();
		this.isRefreshing.set( true );
		try{
			const reload = await QLListResolver.load( this.#ql(), {...this.resolvedData(), skip: this.skip()}, null );
			if( !this.#reloads.is(id) )
				return;
			if( !this.selector() )
				QLListResolver.setChildren( this.routeStore, reload );
			this.init( {data:reload} );
		}
		catch( e ){
			if( this.#reloads.is(id) )
				throw e;
		}
		finally{
			if( this.#reloads.is(id) )
				this.isRefreshing.set( false );
		}
	}
	//As LogDetail:  awaited, said as a delete, and a re-query only where the view on screen went with it - remove() takes nothing but
	//a User view (reviews-issue/web-refactor-review.md #6)
	async onViewDelete( view:View ){
		const current = this.profile().current();
		try{
			await this.profile().remove( view );
		}
		catch( e ){
			this.#snackbar.exception( "Could not delete view.", e );
		}
		if( this.profile().current()!==current )
			await this.#refresh();
		this.isSettings.set( false );
	}

	colSuggestions():Record<string,any[]>{
		let suggestions: Record<string, any[]> = {};
		for( let field of this.view().fields ){
			let values = [];
			if( field.qlField.isNullable ){
				values.push("<null>");
				values.push("<not null>");
			}
			suggestions[field.name] = values;
		}
		for( let row of this.data() ){
			for( let col of Object.keys(row).filter(c=>row[c] && suggestions[c] && !suggestions[c].includes(row[c])) )//guard suggestions[c]: rows can carry keys outside the view's fields
				suggestions[col].push( row[col] );
		}
		for( let col of Object.keys(suggestions) )
			suggestions[col] = suggestions[col].filter( (v,i,a) => a.indexOf(v)===i ).slice(0,100)
				.sort( (a,b)=> typeof a=="number" && typeof b=="number" ? a-b : String(a).localeCompare(String(b)) );//`a-b` was NaN for string values
		return suggestions;
	}

	onViewCancel(){
		this.isSettings.set( false );
	}

	sideNav = model<RouteItem>();//optional:  embedded in a detail page's tab the host owns the sidenav
	listData = input<QLListData>();//pre-resolved by the host instead of the route (QLSelector); `data` is the rows
	//The service the rows come from where the host holds it - a gateway's tabs, the page's Gateway;  else the route's IGRAPHQL.
	//A reload or a live toggle goes to the same one the rows came from.
	ql = input<IGraphQL>();
	#routeQl = inject( IGRAPHQL, {optional: true} );
	#ql():IGraphQL{
		const ql = this.ql() ?? this.#routeQl;
		if( !ql )
			throw new Error( "The list has no ql:  its host gives none and its route provides no IGRAPHQL." );
		return ql;
	}
	linkPrefix = input<string[]>( [] );//the segments between the host's route and a row's slug - a list in a page's tab whose detail route is `templates/:slug`, not `:slug` (gateway-detail's Templates and Groups tabs)
	selector = input<boolean>( false );//pick-rows mode:  always multi-select, no Add, a row click selects instead of navigating
	cellText = input<CellText>();//the host's text for a text cell (ql-table)

	isLoading = signal<boolean>( true );
	isRefreshing = signal<boolean>( false );
	#reloads = new Latest();
	isSettings = signal<boolean>( false );
	selections = model<SelectionModel<QLRow>>( null as any );//rows, not ids - QLSelector maps them to its callers' id selection

	displayedFields = computed<ViewField[]>( ()=>{
		return this.view().fields.filter( v=>v.displayed );
	});
	//The one column the route declared as a switch, if any.
	liveToggleField = computed<ViewField|undefined>( ()=>this.view()?.fields.find(f=>f.liveToggle) );
	pendingLive = signal<unknown[]>( [] );
	canPurge = computed<boolean>( ()=>this.tableSettings().canPurge ?? false );
	collectionName = computed<string>( ()=>this.schema().collectionName );
	columns():Record<string,string>{ return this.resolvedData().columns; }
	data = signal<any[]>([]);
	//Paging:  `skip` is the offset of the rows shown and `more` whether a row follows them (the resolver asks for one past
	//the page).  The server gives no count, so the total is only known on the last page - only then does the pager's range read
	//"of N", and only then does it show Last;  until then Next is simply on.  A single page shows no pager:  the toolbar's row count is then
	//the whole truth, and six disabled buttons under a three-row list would say nothing.
	skip = signal<number>( 0 );
	more = signal<boolean>( false );
	paged = computed<boolean>( ()=>this.more() || this.skip()>0 );
	length = computed<number|undefined>( ()=>this.more() ? undefined : this.skip()+this.data().length );
	excludedColumns = computed<string[]>( ()=>this.tableSettings().excludedColumns ?? [] );
	enums = computed<Map<string, EnumValue[]>>( ()=>this.schema().enums );
	resolvedData = signal<QLListData>(null as any);
	profile = computed<ViewSet<View>>( ()=>this.resolvedData().profile );
	routeStore = inject( RouteStore );
	schema = computed<TableSchema>( ()=>this.resolvedData().schema );
	get sort():Sort{ return this.view().sort?.length ? this.view().sort[0] : {active: "", direction: ""}; }
	canAdd = computed<boolean>( ()=>this.tableSettings().canAdd ?? true );//off the tableSettings, like canPurge - a route that set it anywhere else was silently ignored
	canNavigate = computed<boolean>( ()=>this.tableSettings().canNavigate ?? true );
	tableSettings = computed<TableSettings>( ()=>this.resolvedData().routing.tableSettings );
	type = computed<string>( ()=>MetaObject.toTypeFromCollection(this.collectionName()) );
	view = signal<View>( null as any );//the view the rows were loaded under - profile().current() runs ahead of it while a reload is out

	//MVP first-run:  a list with nothing in it, or one the user may not read, showed a bare header.  Three states below the
	//table now - the route's empty-state words, "no access" for a 403 (the server's line plus what to ask for), and a
	//could-not-load with Retry for anything else - each with the page's help topic where one is registered.
	error = signal<unknown>( undefined );//the rows query was refused or failed:  set by the resolver (QLListData.error) or a re-query, cleared by the next good load
	failure = computed<Failure|undefined>( ()=>{
		const routing = this.resolvedData().routing;
		return failureState( this.error(), routing.tableSettings?.noun ?? routing.title.toLowerCase() );
	});
	//the rows the view's filters narrowed:  they are not the default view's.  Against the default, not against none - the
	//resources page's default filters on criteria <null>, and a database no service has started against must still say so.
	viewFiltered = computed<boolean>( ()=>{
		const home = this.profile().views()[0];
		return !!home && !!this.view() && !View.sameFilters( this.view(), home );
	});
	emptyState = computed<Required<Omit<EmptyState,"add">>>( ()=>QLListResolver.emptyState(this.resolvedData().routing, {filtered: this.viewFiltered(), excluded: !!this.resolvedData().fixedFilters?.length, selector: this.selector()}) );
	showEmpty = computed<boolean>( ()=>!this.isRefreshing() && this.error()===undefined && !this.data().length );//not while a reload has the rows cleared
	addFilled = computed<boolean>( ()=>this.showEmpty() && !this.viewFiltered() );//Add is the one useful action only on a genuinely empty list - not under a filter, a refusal or a failure (#fail and loadOrFail empty the rows too)
	//the help topic for wherever the list is showing - its own page or a detail page's tab - resolved as the navbar's ? does;
	//the fallback topic (the '' route) is navigation help, not this page's, so it is left out
	#helpTopics = helpTopics( inject(HELP_TOPICS, {optional: true}) );
	helpRoute = computed<string[]|undefined>( ()=>{
		const segments = (this.#router.url ?? "").split( "?" )[0].split( "/" ).filter( s=>s.length );
		const topic = helpTopicFor( this.#helpTopics, segments );
		return topic && topic.routes?.some( r=>r.length ) ? helpTopicRoute( topic ) : undefined;
	});
}