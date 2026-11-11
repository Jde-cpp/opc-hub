import { Component, ElementRef, OnInit, input, signal, model, computed, inject } from '@angular/core';
import {ActivatedRoute, Router, RouterLink} from '@angular/router';
import {Sort} from '@angular/material/sort';
import {SnackbarService} from '../../../shared/snackbar/snackbar-service'
import {IGRAPHQL, IGraphQL, EnumValue } from '../../../services/graphql';
import {TableSchema}  from '../../../model/ql/schema/table-schema';
import {MetaObject}  from '../../../model/ql/schema/meta-object';

import { ComponentPageTitle, Latest, RouteItem, RouteStore, HELP_TOPICS, helpTopics, helpTopicFor, helpTopicRoute } from 'jde-spa';
import { MatIcon } from '@angular/material/icon';
import { MatButton } from '@angular/material/button';
import { MatTabNavPanel } from '@angular/material/tabs';
import { NgTemplateOutlet } from '@angular/common';
import { CellText, GraphQLTable } from '../../graphql/table/graphql-table';
import { EmptyState, QLListData, QLListResolver, TableSettings } from '../../../services/ql-list-resolver';
import { Failure, failureState } from '../../../utils/errors';
import { FailureState } from '../../../shared/failure-state/failure-state';
import { SelectionModel } from '@angular/cdk/collections';
import { View, ViewField } from '../../../model/ql/view';
import { cellValues, heldValues, takesFilterTerm } from '../../../model/ql/filter-query';
import { ViewSet } from '../../../model/ql/view-set';
import { MatDialog } from '@angular/material/dialog';
import { confirm } from '../../../shared/confirm/confirm-dialog';
import { QLRow } from '../../../model/ql/slug-row';
import { verify } from '../../../utils/utils';
import { PageEvent, Paginator } from '../../../shared/paginator/paginator';
import { ViewToolbar } from '../../../shared/view-toolbar/view-toolbar';
import { SlicePanel } from '../../../shared/slice-panel/slice-panel';
import { SliceCount } from '../../../model/ql/slice';
import { Grouping } from '../../../model/ql/group';

@Component({
	selector: 'ql-list',
	templateUrl: './ql-list.html',
	host: {class:'main-content my-content'},
	imports: [FailureState, GraphQLTable, MatButton, MatIcon, MatTabNavPanel, NgTemplateOutlet, Paginator, RouterLink, SlicePanel, ViewToolbar]
})
export class QLList implements OnInit{
	#route = inject( ActivatedRoute );
	#router = inject( Router );
	#componentPageTitle = inject( ComponentPageTitle );
	#snackbar = inject( SnackbarService );
	#dialog = inject( MatDialog );
	#el = inject( ElementRef );

	async ngOnInit(){
		if( this.listData() )//embedded (QLSelector):  the host resolved the data itself, there is no route resolver to subscribe to
			this.#resolved( {data: this.listData()} );
		else
			this.#route.data.subscribe( (data)=>this.#resolved(data) );
	}
	//the rows resolved are the view's
	#resolved( data:Record<string,unknown> ){
		this.init( data );
		this.#queried = this.profile().sliced();
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
		this.sliceCounts.set( data.sliceCounts );
		this.sliceCountsError.set( data.sliceCountsError );
		this.#countedBy.set( data.sliceCountsOf?.by );
		this.#groupCounts.set( data.groupCounts );
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
		await this.#reload( true );//the same page; keep the current rows visible, the progress bar signals the reload
	}

	//refresh() and reload() are only ever driven from template event handlers, so a rejection has nowhere to go but the
	//console:  three call sites did not even await it, and two more awaited with no catch.  The user was left looking at an
	//empty or stale table with nothing said.  Every re-query reports through one of these two now.
	async #refresh(){
		try{ await this.refresh(); }
		catch( e ){ this.#fail( e ); }
	}
	async #reload( recount=false ){
		try{ await this.reload( recount ); }
		catch( e ){ this.#fail( e ); }
	}
	//the snackbar goes in ten seconds; the page keeps saying it.  The rows are dropped too:  a refresh keeps them on screen
	//while it runs, and leaving stale rows under a failure reads as "current".  So are the slice panel's counts.
	#fail( e:unknown ){
		this.#snackbar.exception( "Could not refresh data.", e );
		this.error.set( e );
		this.data.set( [] );
		this.sliceCounts.set( undefined );
		this.sliceCountsError.set( undefined );
		this.#groupCounts.set( undefined );
	}

	async onPage( e:PageEvent ){
		Paginator.showFirstRow( this.#el.nativeElement, this.skip(), e.startIndex );
		this.skip.set( e.startIndex );
		await this.#reload();
	}

	//The toolbar or a header put another view on screen:  its rows, unless those shown or on their way hold them - the view
	//renamed, a copy of it, a column hidden, moved or resized, or its filter changed only in a term the box cannot read - where
	//the page stays as it is, its columns the view's even while a read is out.  A column shown is read on the same page, as is
	//another slice or group column, for its counts, and a list showing a failure reads whatever the change.  The slice panel says the same
	//of its value, which narrows the rows as a filter's term does.
	async onChangeView(){
		const view = this.profile().current(), asked = this.profile().sliced(), queried = this.#queried ?? this.view();
		if( View.answers(queried, asked) && queried.sliceBy==asked.sliceBy && queried.groupBy==asked.groupBy && !this.error() ){
			this.view.set( view );
			const multiple = this.selector() || view.showSelector;//the Fields checklist's Select
			if( multiple!=this.selections().isMultipleSelection() )
				this.selections.set( QLList.carriedSelection(this.selections(), this.data(), multiple) );
		}
		else if( View.sameRows(queried, asked) )
			await this.#reload();
		else
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
	//would put back its own page, or the rows of the view it was sent under.  The slice panel's counts are read again where the
	//filter or the column changed, or `recount` - a Refresh - asks, and the groups' with them.
	async reload( recount=false ){
		const id = this.#reloads.next();
		this.#queried = this.profile().sliced();
		this.isRefreshing.set( true );
		try{
			const reload = await QLListResolver.load( this.#ql(), {...this.resolvedData(), skip: this.skip(), ...( recount ? {sliceCounts: undefined, groupCounts: undefined} : {} )}, null );
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
	//the loaded rows' values by column, which the filter box suggests after a field's colon and a header's `Filter by…` lists
	filterValues = computed<Record<string,unknown[]>>( ()=>cellValues( this.view().fields.map( f=>f.name ), this.data(), ( row, column )=>row[column] ) );
	//The chip columns a click filters on, each with the values the filter holds as `field:a,b`:  a click adds the value, and a
	//click on a value it holds takes it out.  A column the box cannot filter - one the gateway grafts on - is not among them.
	chipFilters = computed<Map<string,Set<string>>>( ()=>{
		const view = this.profile().current(), host = view.filterHost();
		if( !host )
			return new Map();
		const held = heldValues( view.filter, host );
		return new Map( view.fields.filter( f=>f.chip && takesFilterTerm(f, host) ).map( f=>[f.name, held.get( f.name ) ?? new Set<string>()] ) );
	} );
	async onChipClick( chip:{column:string, value:string} ){
		this.profile().edit( draft=>draft.toggleFilterValue(chip.column, chip.value) );
		await this.onChangeView();
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
	enums = computed<Map<string, EnumValue[]>>( ()=>this.schema().enums );
	resolvedData = signal<QLListData>(null as any);
	profile = computed<ViewSet<View>>( ()=>this.resolvedData().profile );
	routeStore = inject( RouteStore );
	schema = computed<TableSchema>( ()=>this.resolvedData().schema );
	//the draft's, as the columns and the later sorts' places are:  the header shows the sort asked for while its rows are read
	get sort():Sort{ return this.profile().current().sort[0] ?? {active: "", direction: ""}; }
	canAdd = computed<boolean>( ()=>this.tableSettings().canAdd ?? true );//off the tableSettings, like canPurge - a route that set it anywhere else was silently ignored
	canNavigate = computed<boolean>( ()=>this.tableSettings().canNavigate ?? true );
	tableSettings = computed<TableSettings>( ()=>this.resolvedData().routing.tableSettings );
	type = computed<string>( ()=>MetaObject.toTypeFromCollection(this.collectionName()) );
	view = signal<View>( null as any );//the view the rows were loaded under - profile().current() runs ahead of it while a reload is out
	#queried:View|undefined;//the view of the last reload sent, whose rows are shown or on their way

	//MVP first-run:  a list with nothing in it, or one the user may not read, showed a bare header.  Three states below the
	//table now - the route's empty-state words, "no access" for a 403 (the server's line plus what to ask for), and a
	//could-not-load with Retry for anything else - each with the page's help topic where one is registered.
	error = signal<unknown>( undefined );//the rows query was refused or failed:  set by the resolver (QLListData.error) or a re-query, cleared by the next good load
	failure = computed<Failure|undefined>( ()=>{
		const routing = this.resolvedData().routing;
		return failureState( this.error(), routing.tableSettings?.noun ?? routing.title.toLowerCase() );
	});
	//the rows the view's filters, or its slice, narrowed:  they are not the default view's.  Against the default, not against none - the
	//resources page's default filters on criteria <null>, and a database no service has started against must still say so.
	//The route's default, not the first tab, which is whichever view the user put there.
	viewFiltered = computed<boolean>( ()=>{
		const home = this.profile().routeDefault;
		return !!home && !!this.view() && ( !View.sameFilters(this.view(), home) || this.profile().slice()!==undefined );
	});
	//the slice panel's counts, read with the rows (QLListResolver.load):  undefined where the view is not sliced, or they failed
	sliceCounts = signal<SliceCount[]|undefined>( undefined );
	sliceCountsError = signal<unknown>( undefined );
	#countedBy = signal<string|undefined>( undefined );//the column they are of
	//what the panel shows:  the counts of the column it lists alone, so another column, picked while a read is out, shows none
	panelCounts = computed<SliceCount[]|undefined>( ()=>this.#countedBy()==this.profile().current().sliceBy ? this.sliceCounts() : undefined );
	panelFailure = computed<unknown>( ()=>this.#countedBy()==this.profile().current().sliceBy ? this.sliceCountsError() : undefined );
	//What the rows are grouped by (views spec *Group by*), as the view they were read under has it, with each group's count over
	//the whole list where it was read:  a header then says *12 of 40* on a page that holds a part of its group.
	#groupCounts = signal<SliceCount[]|undefined>( undefined );
	grouping = computed<Grouping|undefined>( ()=>this.view()?.grouping( this.columns(), this.#groupCounts() ) );
	emptyState = computed<Required<Omit<EmptyState,"add">>>( ()=>QLListResolver.emptyState(this.resolvedData().routing, {filtered: this.viewFiltered(), excluded: !!this.resolvedData().fixedFilter?.includes( "-id:" ), selector: this.selector()}) );
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