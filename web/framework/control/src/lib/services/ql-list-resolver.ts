import {ActivatedRoute, ActivatedRouteSnapshot, Resolve, Router, RouterStateSnapshot} from '@angular/router';
import { inject, Injectable } from '@angular/core';
import {SnackbarService} from '../shared/snackbar/snackbar-service';
import { TableSchema} from '../model/ql/schema/table-schema';
import { IGRAPHQL, IGraphQL } from './graphql';
import { PageProfile, PageSettings } from '../pages/graphql/model/page-settings';
import { StringUtils } from '../utils/string-utils';
import { MetaObject } from '../model/ql/schema/meta-object';
import { Field } from '../model/ql/schema/field';
import { RouteItem, ProfileStore, RouteStore } from 'jde-spa';
import { FieldFilter, View, ViewFieldSettings, ViewFilterSettings, ViewSettings } from '../model/ql/view';
import { Sort } from '@angular/material/sort';

//canNavigate: a collection with no ':slug' detail route must not offer the row click-through
//viewName: the toggle label of the default view ("default" when unset);  filters: the default view's, same vocabulary as a
//ViewSettings filter (resources opens on the table rows, criteria null);  views: further system views, see ViewSettings
//empty: what the page says when the query returns no rows - the default names the collection and, where Add is offered, points at it
export type EmptyState = { title?:string, detail?:string, add?:string/*the sentence pointing at Add, appended to detail only where Add is shown*/, icon?:string };
export type TableSettings = {canPurge?:boolean,canAdd?:boolean, canNavigate?:boolean, excludedColumns?:string[], columns?:(string|ViewFieldSettings)[], sort?:Sort[]|string, viewName?:string, filters?:ViewFilterSettings[], views?:ViewSettings[], empty?:EmptyState, noun?:string};//noun:  what the rows are, lower case and plural, where the route title is not - a list in a detail page's tab (reviews/m3-closing.md #6)
export type CollectionItem = string | { path:string, title?:string, data?:{summary:string, collectionName:string, tableSettings:TableSettings} };
export class ListRoute extends RouteItem{
	constructor( collection:string|CollectionItem ){
		super();
		if( typeof collection=='string' )
			collection = {path:collection, title:StringUtils.capitalize(collection)};
		this.path = collection.path;
		this.collectionName = collection.data?.collectionName ?? this.path;
		//per-field defaults: routes pass partial settings (e.g. groups/roles set only excludedColumns) and columns must never be undefined.
		this.tableSettings = { excludedColumns: [], columns: ["name", "created", "updated", "deleted", "slug"], sort: [{active:"name", direction:"asc"}], ...collection.data?.tableSettings };
		this.summary = collection?.data?.summary;
		this.title = collection.title ?? StringUtils.capitalize( this.path );
	}
	static find( path:string, collections:CollectionItem[] ):ListRoute{
		const collection = collections.find( (c:any)=>((typeof c =="string") && c==path) || c["path"]==path );
		return new ListRoute( collection ?? path );//unlisted collection - the defaults (QLSelector embeds collections its host's route never declares)
	}
	tableSettings:TableSettings;
	collectionName: string;
}

export type QLListData = {
	columns: Record<string,string>;
	fixedFilters?: FieldFilter[];//applied on top of whichever view is current and never shown or saved - QLSelector's excludedIds
	pageSettings:PageSettings;
	profile: PageProfile;
	results: any; //{users:ISlugRow[]};
	routing:ListRoute;
	schema: TableSchema;
	error?: unknown;//the rows query was refused (403) or failed:  results is then empty and the page says why instead of showing a bare grid
};

@Injectable()
export class QLListResolver implements Resolve<QLListData> {
	private route:ActivatedRoute = inject( ActivatedRoute );
	private router:Router = inject( Router );
	private cnsl:SnackbarService = inject( SnackbarService );
	private ql:IGraphQL = inject( IGRAPHQL );

	resolve(route: ActivatedRouteSnapshot, state: RouterStateSnapshot):Promise<QLListData>{
		const collectionDisplay = route.paramMap.get( "collectionDisplay" );
		let routing:ListRoute|undefined;
		const siblings:ListRoute[] = [];
		for( let collection of route.data["collections"] ){
			const sibling = new ListRoute( collection );
			siblings.push( sibling );
			if( sibling.path==collectionDisplay )
				routing = sibling;
		}
		if( !routing )
			routing = siblings[0];
		routing.siblings = siblings;
		routing.parent = QLListResolver.parentRoute( route );
		if( routing.parent )
			this.routeStore.setChildren( routing.parent.path, siblings );//the breadcrumb resolves a ':collectionDisplay' segment through RouteStore;  unregistered, it fell back to the raw url segment ('users' instead of 'Users')
		return this.load( routing );
	}

	//the sidenav header link back to the collection's landing page ('/access' for access/users).  ComponentNav renders the
	//siblings as parent.path + '/' + sibling.path, so the path must be absolute or every sibling resolves relative to the
	//current url instead.
	private static parentRoute( route:ActivatedRouteSnapshot ):RouteItem|undefined{
		const segments = route.parent?.url.map( seg=>seg.path ) ?? [];
		if( !segments.length )
			return undefined;
		const title = route.parent!.title ?? StringUtils.capitalize( segments[segments.length-1] );
		return new RouteItem( {path: `/${segments.join('/')}`, title} );
	}

	private async load( routing:ListRoute ):Promise<QLListData>{
		let data:QLListData;
		try{ data = await QLListResolver.data( this.ql, routing, this.profileStore ); }
		catch( e ){ this.cnsl.exception( `Could not open ${routing.title}.`, e ); throw e; }//no schema, so no page to say it on:  the navigation fails, and this is the only word the user gets
		return QLListResolver.loadOrFail( this.ql, data, this.routeStore );
	}
	//load() for a page:  a refused or failed rows query becomes the page's own state (QLListData.error, no rows) rather than a
	//rejected resolve, which the router turns into a NavigationError nobody sees - the user stayed on the previous page with
	//nothing said.  A user without Read on the collection is the common case, and the page has to open to say so.
	static async loadOrFail( ql:IGraphQL, data:QLListData, routeStore:RouteStore|null ):Promise<QLListData>{
		try{ return await QLListResolver.load( ql, data, routeStore ); }
		catch( e ){ return { ...data, results: {[data.schema.collectionName]: []}, error: e }; }
	}
	//what an empty list says:  the route's own words, else the collection's name and - where Add is offered - a pointer at it.
	//Those words are about an empty collection, so they are only said of one (reviews/m3-closing.md #24):  a list the view's
	//filters narrowed may have rows the view hides, one QLSelector's excludedIds narrowed has none but those, and a selector
	//tab has no Add to point at.
	static emptyState( routing:ListRoute, narrowed:{filtered?:boolean, excluded?:boolean, selector?:boolean} = {} ):Required<Omit<EmptyState,"add">>{
		const settings = routing.tableSettings;
		const noun = settings.noun ?? routing.title.toLowerCase();
		const icon = settings.empty?.icon ?? "inbox";
		if( narrowed.filtered )
			return { title: `No ${noun} match this view.`, detail: "", icon: "filter_alt_off" };
		if( narrowed.excluded )
			return { title: `No other ${noun}.`, detail: "", icon };
		const add = narrowed.selector || settings.canAdd===false ? "" : settings.empty?.add ?? (settings.empty?.detail ? "" : "Use Add to create the first one.");//a route's own detail with no `add` is the whole text, as before `add` existed
		return { title: settings.empty?.title ?? `No ${noun} yet.`, detail: [settings.empty?.detail, add].filter( s=>s ).join( "  " ), icon };
	}
	//everything but the rows:  the schema, the default view plus the user's saved ones, and the column display names.
	//QLSelector builds a collection's list the same way, so it lives here rather than in resolve().
	static async data( ql:IGraphQL, routing:ListRoute, profileStore:ProfileStore ):Promise<QLListData>{
		let pageSettings = new PageSettings( routing.tableSettings );
		const collectionName = routing.collectionName;
		const schema = await ql.schemaWithEnums( MetaObject.toTypeFromCollection(collectionName), (m)=>console.log(m) );
		const systemViews = QLListResolver.systemViews( schema, routing.tableSettings );
		var profile = new PageProfile();
		profile.views.push( ...systemViews );
		await profile.loadViews( collectionName, profileStore, schema, systemViews[0].sort );
		profile.currentViewIndex = ProfileStore.viewIndex( collectionName );
		//A live-toggle column IS the show-deleted control for its page:  the rows it switches are precisely the deleted ones,
		//so the page queries them unconditionally rather than opening on a list that looks empty (every `resource` ships
		//deleted, i.e. unenforced) with the rows hidden behind a checkbox called Show deleted.
		profile.showDeleted = QLListResolver.hasLiveToggle( routing.tableSettings ) || ProfileStore.showDeleted( collectionName );
		return {pageSettings, profile, schema, results: null, routing, columns: QLListResolver.columns(schema, routing.tableSettings.columns!, routing.tableSettings.excludedColumns)};
	}
	static hasLiveToggle( settings:TableSettings ):boolean{
		const has = ( columns:(string|ViewFieldSettings)[]|undefined )=>(columns ?? []).some( c=>typeof c!="string" && !!c.liveToggle );
		return has( settings.columns ) || (settings.views ?? []).some( v=>has(v.columns) );
	}
	//the default view first, then the route's declared ones - each falling back to the default's columns and sort for what it leaves unset
	static systemViews( schema:TableSchema, settings:TableSettings ):View[]{
		const columns = settings.columns ?? [];
		const defaultView = new View( {name: settings.viewName, configColumns: columns, sort: View.toSort(settings.sort) ?? [{active: "name", direction: "asc"}], filters: settings.filters}, schema );//settings.sort, not the name default:  a route that declares its own order (resources: schema then name) has to get it
		const views = [defaultView];
		for( const v of settings.views ?? [] )
			views.push( new View({name: v.name, configColumns: v.columns ?? columns, sort: View.toSort(v.sort) ?? [...defaultView.sort], filters: v.filters}, schema) );
		return views;
	}
	static columns( schema:TableSchema, configColumns:(string|ViewFieldSettings)[], excluded: string[] ):Record<string,string>{
		let columns: Record<string,string> = {};
		for( let field of schema.fields.filter(f=>!excluded.includes(f.name)) ){
			let configColumn = configColumns.find( c=>typeof c=="object" && c.name==field.name ) as ViewFieldSettings;
			columns[field.name] = configColumn?.displayName ?? StringUtils.idToDisplay(field.name);
		}
		return columns;
	}
	//routeStore null:  the rows are a pick list (QLSelector - filtered, and never the page's own collection), not the collection's sidenav children
	static async load( ql:IGraphQL, data:QLListData, routeStore:RouteStore|null ):Promise<QLListData>{
		let view = data.profile.view;
		if( data.fixedFilters?.length ){//on a copy:  the profile's view is what the settings panel edits and what a Save persists
			view = new View( view );
			view.fieldFilters = [...view.fieldFilters, ...data.fixedFilters];
		}
		const q = view.query( data.profile.showDeleted, 0 );
		data.results = await ql.query<any>( q.text, q.vars, (m)=>console.log(m) );
		if( routeStore ){
			const children = data.results[data.schema.collectionName].map( (r:any)=>({title:r.name, path:`${r.slug}`}) );//bare targets, like GatewayResolver — DetailResolver renders them under the absolute list url
			routeStore.setChildren( data.routing.path, children );
		}
		return {
			columns: data.columns,
			fixedFilters: data.fixedFilters,
			pageSettings: data.pageSettings,
			results: data.results,
			routing: data.routing,
			profile: data.profile,
			schema: data.schema
		};
	}
	routeStore = inject( RouteStore );
	profileStore = inject( ProfileStore );
}