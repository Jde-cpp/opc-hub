import { Component, OnInit, OnDestroy, ViewChild, input, signal, model, computed, Injectable, inject } from '@angular/core';
import { CommonModule } from '@angular/common';
import {ActivatedRoute, Route, Router, RouterLink, Routes, UrlSegment} from '@angular/router';
import {Sort} from '@angular/material/sort';
import { MatTable } from '@angular/material/table';
import { QLListSettings } from './ql-list-settings/ql-list-settings';
import {SnackbarService} from '../../../shared/snackbar/snackbar-service'
import {IGRAPHQL, IGraphQL, EnumValue } from '../../../services/graphql';
import {Field} from '../../../model/ql/schema/field';
import {TableSchema}  from '../../../model/ql/schema/table-schema';
import {MetaObject}  from '../../../model/ql/schema/meta-object';

import { ComponentPageTitle, RouteItem, RouteStore, IRouteService, RouteService, HELP_TOPICS, helpTopics, helpTopicFor } from 'jde-spa';
import { MatIcon } from '@angular/material/icon';
import { MatIconButton, MatButton } from '@angular/material/button';
import { MatButtonToggle, MatButtonToggleGroup } from '@angular/material/button-toggle';
import { MatCheckbox } from '@angular/material/checkbox';
import { MatProgressBar } from '@angular/material/progress-bar';
import { MatToolbar } from '@angular/material/toolbar';
import { MatTooltip } from '@angular/material/tooltip';
import { ProfileStore } from 'jde-spa';
import { GraphQLTable } from '../../graphql/table/graphql-table';
import { EmptyState, QLListData, QLListResolver, TableSettings } from '../../../services/ql-list-resolver';
import { errorText, httpStatus } from '../../../utils/errors';
import { SelectionModel } from '@angular/cdk/collections';
import { View, ViewField, ViewType } from '../../../model/ql/view';
import { MatDialog } from '@angular/material/dialog';
import { confirm } from '../../../shared/confirm/confirm-dialog';
import { QLRow } from '../../../model/ql/slug-row';
import { PageProfile } from '../../graphql/model/page-settings';
import { verify } from '../../../utils/utils';

@Component({
	selector: 'ql-list',//.main-content.mat-drawer-container.my-content
	styleUrls: ['ql-list.scss'],
	templateUrl: './ql-list.html',
	host: {class:'main-content mat-drawer-container my-content'},
	imports: [CommonModule, GraphQLTable, MatButton, MatButtonToggle, MatButtonToggleGroup, MatCheckbox, MatIcon, MatIconButton, MatProgressBar, MatToolbar, MatTooltip, QLListSettings, RouterLink]
})
export class QLList implements OnInit, OnDestroy{
	private route = inject( ActivatedRoute );
	private router = inject( Router );
	private componentPageTitle = inject( ComponentPageTitle );
	private ql:IGraphQL = inject( IGRAPHQL );
	private snackbar = inject( SnackbarService );
	private dialog = inject( MatDialog );

	ngOnDestroy(){
		//this.profileStore.save(this.collectionName(), this.profile);
		//The selector forces showDeleted off and a live-toggle page forces it on - both hide the checkbox, and saving either
		//forced value would reset the collection's own list page to something the user never chose.
		if( !this.selector() && !this.liveToggleField() )
			ProfileStore.setShowDeleted( this.collectionName(), this.showDeleted() );
	}

	async ngOnInit(){
		if( this.listData() )//embedded (QLSelector):  the host resolved the data itself, there is no route resolver to subscribe to
			this.init( {data: this.listData()} );
		else
			this.route.data.subscribe( (data)=>{ this.init( data ); } );
	}
	async init( resolvedValue:Record<string,unknown> ){
		let data = resolvedValue["data"] as QLListData;
		verify( data.profile.view );
		const priorIds = this.selections()?.selected.map( r=>r?.id ).filter( id=>id!=null ) ?? [];//a reload builds new row objects, so carry the selection over by id
		this.view.set( data.profile.view );
		const collectionName = data.schema.collectionName;
		this.resolvedData.set( data );
		//if( !this.profile )
			//this.profile = await this.profileStore.load(collectionName, QLList.defaultProfile );

		const rows = data.results[collectionName];
		const multiple = this.selector() || data.profile.view.showSelector;
		let selected = rows.filter( (r:QLRow)=>priorIds.includes(r.id) );
		if( !multiple )
			selected = selected.slice( 0, 1 );//SelectionModel throws on multiple values in single-select mode
		this.selections.set( new SelectionModel<QLRow>(multiple, selected) );
		this.data.set( rows );
		this.error.set( data.error );//a good load clears the last failure; a resolver that caught the rows query sets it
		this.sideNav.set( data.routing );
		let paths = [];
		for( let x = this.route; x.routeConfig?.data && x.routeConfig?.data["name"]; x = x.parent! )
			paths.push( x.routeConfig.data['name'] );
		if( paths.length )//guard:  QLList also renders inside a tab (GatewayDetail), where the route has no 'name' - the unguarded assignment wrote undefined over the host page's title
			this.componentPageTitle.title = paths[0];//.join( " | " ); 	//this.componentPageTitle.title ? `${this.componentPageTitle.title} | ${title}` : title;
		else if( this.route.component==QLList && data.routing?.title )//the routed page itself (/access/resources):  a ':collectionDisplay' route has no title of its own, which left the tab bare
			this.componentPageTitle.title = data.routing.title;

/*		const order = ["name", "created", "updated", "deleted", "slug", "description"];
		this.displayedFields = Field.filterSort( this.schema().fields, order, [...this.excludedColumns(), "description"], this.showDeleted() );
		if( !this.excludedColumns().find(x=>x=="description") )
			this.displayedFields.push( this.schema().fields.find(x=>x.name=="description") );
*/
		this.isLoading.set( false );
	};
	onSortChange( sort:Sort ){
		//let updateView = this.view().isUser && this.view().sort.length<2;
		let newView = new View( this.view() );
		newView.sort = [sort, ...newView.sort.filter(s=>s.active!=sort.active)];
		this.onViewShow( newView );
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
		if( !await confirm(this.dialog, {title: `${verb} ${name}?`, message, confirm: verb}) )
			return;
		this.pendingLive.update( ids=>[...ids, row.id] );
		try{
			await this.ql.mutate( `${live ? "delete" : "restore"}${this.type()}(id:${row.id})`, (m)=>console.log(m) );
			row[field.name] = live ? new Date() : null;
			this.data.set( [...this.data()] );//a new array:  the row object is mutated in place, and the table only re-renders off the reference
		}
		catch( e ){
			this.snackbar.exception( `${verb} failed.`, e );
		}
		finally{
			this.pendingLive.update( ids=>ids.filter(id=>id!=row.id) );
		}
	}

	restore(){
		this.ql.mutate( `restore${this.type()}(id:${this.selection().id})`, (m)=>console.log(m) ).then( ()=>this.selection().deleted=null ).catch( (e)=>console.log(e) );
	}

	//The try/catch was dead:  router.navigate is ASYNC, so a route that does not exist rejects the promise long after the
	//block has returned.  /access/resources has no 'resources/:slug' route, so every row click there dead-ended in a
	//NavigationError the user never saw - hence both halves here:  don't offer the click where there is nowhere to go, and
	//report it through the returned promise where the navigation still fails.
	onRowActivate( row:QLRow ){
		if( this.selector() || !this.canNavigate() )//ql-table already toggled the row; a selector has nowhere to navigate to
			return;
		this.router.navigate( [row.slug], {relativeTo: this.route} )
			.then( ok=>{ if( !ok ) this.snackbar.error( `Could not navigate to '${row.slug}'.` ); } )
			.catch( e=>this.snackbar.exception("Could not navigate to properties", e) );
	}

	onAdd(){
		this.router.navigate( ['$new'], {relativeTo: this.route} );
	}

	async onRefresh(){
		await this.#refresh( this.resolvedData().profile );//keep the current rows visible; the progress bar signals the reload
	}

	//refresh() and reload() are only ever driven from template event handlers, so a rejection has nowhere to go but the
	//console:  three call sites did not even await it, and two more awaited with no catch.  The user was left looking at an
	//empty or stale table with nothing said.  Every re-query reports through one of these two now.
	async #refresh( profile: PageProfile ){
		try{ await this.refresh( profile ); }
		catch( e ){ this.#fail( e ); }
	}
	async #reload(){
		try{ await this.reload(); }
		catch( e ){ this.#fail( e ); }
	}
	//the snackbar goes in ten seconds; the page keeps saying it.  The rows are dropped too:  a refresh keeps them on screen
	//while it runs, and leaving stale rows under a failure reads as "current".
	#fail( e:unknown ){
		this.snackbar.exception( "Could not refresh data.", e );
		this.error.set( e );
		this.data.set( [] );
	}

	async delete(){
		const purge = this.selection().deleted!=null;
		const type = purge ? "purge" : "delete";
		try{
			await this.ql.mutate(`${type}${this.type()}(id:${this.selection().id})`, (m)=>console.log(m) );
			if( !purge && this.showDeleted() )
				this.selection().deleted = new Date();
			else{
				const values = this.data().slice();
				const index = values.findIndex( (x)=>x["id"]==this.selection()["id"] );
				values.splice( index, 1 );
				this.selections.set( new SelectionModel<QLRow>(false, []) );
				this.data.set( values );
			}
		}
		catch( e ){
			this.snackbar.exception( "Could not delete entry.", e );
		}
	}
	selection = computed<any>( ()=>{
		return this.selections().selected.length==1 ? this.selections().selected[0] : null;
	});
	async onViewSave(view:View){
		this.data.set( [] );
		let profile = this.resolvedData().profile;
		if( view.isSystem && !profile.views.find(v=>v.name==view.name && v.isSystem) )
			view.type = ViewType.User;
		profile.upsertView( view, this.collectionName(), this.profileStore );
		if( view.type==ViewType.User ){
			try{
				await this.profileStore.save( `qlList/${this.collectionName()}/views`, profile.views.filter(v=>v.isUser).map(v=>v.toJson(this.tableSettings())) );//awaited, or the catch below is dead code
			}
			catch( e ){
				this.snackbar.exception( "Could not save view.", e );
			}
		}

		await this.#reload();
		this.isSettings.set( false );
	}
	async onViewShow(view:View){
		this.data.set( [] );
		this.isSettings.set( false );
		if( view.name?.endsWith("*") && view.isAdhoc )
		 	view.name = view.name.substring( 0, view.name.length-1 );
		view.type = ViewType.Adhoc;
		let profile = new PageProfile( this.resolvedData().profile );
		profile.upsertView( view, this.collectionName(), this.profileStore );
		await this.#refresh( profile );
	}
	async onChangeView(index:number){
		let profile = new PageProfile( this.resolvedData().profile );
		profile.currentViewIndex = index;
		ProfileStore.setViewIndex( this.collectionName(), index );
		await this.#refresh( profile );
	}
	async refresh( profile: PageProfile ){
		this.resolvedData().profile = profile;
		this.resolvedData().schema = new TableSchema( this.resolvedData().schema ); //TODO just copy
		await this.reload();
	}
	//every path that re-queries goes through here so isRefreshing covers the whole load, not just the Refresh button
	async reload(){
		this.isRefreshing.set( true );
		try{
			const reload = await QLListResolver.load( this.ql, this.resolvedData(), this.selector() ? null : this.routeStore );
			this.init( {data:reload} );
		}
		finally{
			this.isRefreshing.set( false );
		}
	}
	async onViewDelete(view:View){
		let profile = this.resolvedData().profile;
		profile.removeView( view.name!, this.collectionName(), this.profileStore, this.tableSettings() ).catch( e=>this.snackbar.exception("Could not save view.", e) );
		profile.currentViewIndex = 0;
		await this.#reload();
		this.isSettings.set( false );
	}

	async onToggleShowDeleted(){
		const showDeleted = !this.showDeleted();
		let view = new View( this.view() );
		view.setDeletedDisplayed( showDeleted );
		ProfileStore.setShowDeleted( this.collectionName(), showDeleted );
		let profile = new PageProfile( this.resolvedData().profile );
		profile.updateView( view );
		profile.showDeleted = showDeleted;
		await this.#refresh( profile );
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
	selector = input<boolean>( false );//pick-rows mode:  always multi-select, no Add/show-deleted, a row click selects instead of navigating

	isLoading = signal<boolean>( true );
	isRefreshing = signal<boolean>( false );
	isSettings = signal<boolean>( false );
	selections = model<SelectionModel<QLRow>>( null as any );//rows, not ids - QLSelector maps them to its callers' id selection

	displayedFields = computed<ViewField[]>( ()=>{
		return this.view().fields.filter( v=>v.displayed );
	});
	//The one column the route declared as a switch, if any.  Its presence is also what hides Show-deleted:  the switch acts
	//ON the deleted rows, so the resolver queries them unconditionally and the checkbox would only offer to hide half the work.
	liveToggleField = computed<ViewField|undefined>( ()=>this.view()?.fields.find(f=>f.liveToggle) );
	pendingLive = signal<unknown[]>( [] );
	@ViewChild('mainTable',{static: false}) _table!:MatTable<any>;
	canPurge = computed<boolean>( ()=>this.tableSettings().canPurge ?? false );
	collectionName = computed<string>( ()=>this.schema().collectionName );
	columns():Record<string,string>{ return this.resolvedData().columns; }
	data = signal<any[]>([]);
	excludedColumns = computed<string[]>( ()=>this.tableSettings().excludedColumns ?? [] );
	get name():string{ return <string>this.routeConfig.title; }
	enums = computed<Map<string, EnumValue[]>>( ()=>this.schema().enums );
	resolvedData = signal<QLListData>(null as any);
	get routeConfig():Route{ return this.route.routeConfig!; }
	routeStore = inject( RouteStore );
	schema = computed<TableSchema>( ()=>this.resolvedData().schema );
	get sort():Sort{ return this.view().sort?.length ? this.view().sort[0] : {active: "", direction: ""}; }
	showDeleted = computed<boolean>( ()=>this.resolvedData().profile.showDeleted );
	canAdd = computed<boolean>( ()=>this.tableSettings().canAdd ?? true );//off the tableSettings, like canPurge - a route that set it anywhere else was silently ignored
	canNavigate = computed<boolean>( ()=>this.tableSettings().canNavigate ?? true );
	tableSettings = computed<TableSettings>( ()=>this.resolvedData().routing.tableSettings );
	type = computed<string>( ()=>MetaObject.toTypeFromCollection(this.collectionName()) );
	view = signal<View>( null as any );
	profileStore = inject(ProfileStore);

	//MVP first-run:  a list with nothing in it, or one the user may not read, showed a bare header.  Three states below the
	//table now - the route's empty-state words, "no access" for a 403 (the server's line plus what to ask for), and a
	//could-not-load with Retry for anything else - each with the page's help topic where one is registered.
	error = signal<unknown>( undefined );//the rows query was refused or failed:  set by the resolver (QLListData.error) or a re-query, cleared by the next good load
	failure = computed<{kind:"forbidden"|"failed", title:string, detail:string}|undefined>( ()=>{
		const e = this.error();
		if( e===undefined )
			return undefined;
		const noun = this.resolvedData().routing.title.toLowerCase();
		const what = errorText( e )?.replace( /^\(\d+\)/, "" ) ?? "";//errorText prefixes the status; the title already says which
		return httpStatus( e )==403
			? { kind: "forbidden", title: `No access to ${noun}.`, detail: `${what}  Ask an administrator for a role that can read ${noun}.`.trim() }
			: { kind: "failed", title: `Could not load ${noun}.`, detail: what };
	});
	emptyState = computed<Required<EmptyState>>( ()=>QLListResolver.emptyState(this.resolvedData().routing) );
	showEmpty = computed<boolean>( ()=>!this.isRefreshing() && this.error()===undefined && !this.data().length );//not while a reload has the rows cleared
	//the help topic for wherever the list is showing - its own page or a detail page's tab - resolved as the navbar's ? does;
	//the fallback topic (the '' route) is navigation help, not this page's, so it is left out
	#helpTopics = helpTopics( inject(HELP_TOPICS, {optional: true}) );
	helpRoute = computed<string[]|undefined>( ()=>{
		const segments = (this.router.url ?? "").split( "?" )[0].split( "/" ).filter( s=>s.length );
		const topic = helpTopicFor( this.#helpTopics, segments );
		return topic && topic.routes?.some( r=>r.length ) ? ['/help', topic.id] : undefined;
	});
}
