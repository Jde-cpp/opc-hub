import {ActivatedRouteSnapshot, createUrlTreeFromSnapshot, Resolve, Router, RouterStateSnapshot} from '@angular/router';
import { inject, Injectable } from '@angular/core';
import {SnackbarService} from '../shared/snackbar/snackbar-service';
import { TableSchema} from '../model/ql/schema/table-schema';
import { IGRAPHQL, IGraphQL, Query } from './graphql';
import { ViewSet } from '../model/ql/view-set';
import { StringUtils } from '../utils/string-utils';
import { MetaObject } from '../model/ql/schema/meta-object';
import { capitalize, LocalPrefs, ProfileStore, RouteItem, RouteStore } from 'jde-spa';
import { FieldFilter, Operator, View, ViewField, ViewFieldSettings, ViewFilterSettings, ViewSettings } from '../model/ql/view';
import { Sort } from '@angular/material/sort';
import { QLRow } from '../model/ql/slug-row';

//canNavigate: a collection with no ':slug' detail route must not offer the row click-through;  rowLink: the segments below the
//list to a row's page where they are not its slug alone - a resource's slug repeats across schemas, so its page is schema/slug.
//Its `fields` are the row fields `segments` reads:  every view queries them, displayed or not, as it does id and slug.  No
//segments:  the row has no page - a click does nothing, and the row is no sidenav child.
//viewName: the toggle label of the default view ("Default" when unset);  filters: the default view's, same vocabulary as a
//ViewSettings filter (resources opens on the table rows, criteria null);  views: further system views, see ViewSettings
//empty: what the page says when the query returns no rows - the default names the collection and, where Add is offered, points at it
export type EmptyState = { title?:string, detail?:string, add?:string/*the sentence pointing at Add, appended to detail only where Add is shown*/, icon?:string };
export type RowLink = { fields:string[], segments:(row:QLRow)=>string[]|undefined };
export type TableSettings = {canPurge?:boolean,canAdd?:boolean, canNavigate?:boolean, rowLink?:RowLink, excludedColumns?:string[], columns?:(string|ViewFieldSettings)[], sort?:Sort[]|string, viewName?:string, filters?:ViewFilterSettings[], views?:ViewSettings[], empty?:EmptyState, noun?:string};//noun:  what the rows are, lower case and plural, where the route title is not - a list in a detail page's tab (reviews/m3-closing.md #6)
export type CollectionItem = string | { path:string, title?:string, data?:{summary:string, collectionName:string, tableSettings:TableSettings} };
export class ListRoute extends RouteItem{
	constructor( collection:string|CollectionItem ){
		super();
		if( typeof collection=='string' )
			collection = {path:collection, title:capitalize(collection)};
		this.path = collection.path;
		this.collectionName = collection.data?.collectionName ?? this.path;
		//per-field defaults: routes pass partial settings (e.g. groups/roles set only excludedColumns) and columns must never be undefined.
		this.tableSettings = { excludedColumns: [], columns: ["name", "created", "updated", "slug"], sort: [{active:"name", direction:"asc"}], ...collection.data?.tableSettings };
		this.summary = collection?.data?.summary;
		this.title = collection.title ?? capitalize( this.path );
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
	profile: ViewSet<View>;
	results: any; //{users:ISlugRow[]};
	routing:ListRoute;
	schema: TableSchema;
	error?: unknown;//the rows query was refused (403) or failed:  results is then empty and the page says why instead of showing a bare grid
	skip?: number;//the offset of the page in results - the first page when unset
	more?: boolean;//a row past the page exists, so the pager offers a next page (QLListResolver.page)
	childrenKey?: string;//where the sidenav children go when it is not the list's own url:  a gateway's tabs, under the gateway's page
};
//what load() reads and hands back - a gateway's Connections tab (GatewayData) pages the same way, under an AppInstanceRoute
export type PageData = Omit<QLListData, "routing"> & { routing:{path:string, parent?:{path:string}} };

@Injectable()
export class QLListResolver implements Resolve<QLListData> {
	#router = inject( Router );
	#cnsl = inject( SnackbarService );
	#ql = inject( IGRAPHQL );

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
		if( !routing && collectionDisplay ){//a typed or bookmarked name the route does not list opened the first list under that name (reviews/m3-closing.md #38)
			this.#cnsl.error( `There is no ${collectionDisplay} list.` );
			QLListResolver.redirectUp( this.#router, route );
			return Promise.resolve( null as unknown as QLListData );
		}
		if( !routing )
			routing = siblings[0];
		routing.siblings = siblings;
		routing.parent = QLListResolver.parentRoute( route );
		if( routing.parent )
			this.routeStore.setChildren( routing.parent.path, siblings );//the breadcrumb resolves a ':collectionDisplay' segment through RouteStore;  unregistered, it fell back to the raw url segment ('users' instead of 'Users')
		return this.load( routing );
	}

	//a refusal's way back to the page above.  Fired and forgotten, so a failure there is logged here:  unhandled, the rejection
	//was an uncaught error on top of whatever the page above's own resolver had already said about it.
	static redirectUp( router:Router, route:ActivatedRouteSnapshot ):void{
		router.navigateByUrl( createUrlTreeFromSnapshot(route, ['..']) ).catch( e=>console.warn("The redirect failed.", e) );
	}

	//the sidenav header link back to the collection's landing page ('/access' for access/users).  ComponentNav renders the
	//siblings as parent.path + '/' + sibling.path, so the path must be absolute or every sibling resolves relative to the
	//current url instead.
	private static parentRoute( route:ActivatedRouteSnapshot ):RouteItem|undefined{
		const segments = route.parent?.url.map( seg=>seg.path ) ?? [];
		if( !segments.length )
			return undefined;
		const title = route.parent!.title ?? capitalize( segments[segments.length-1] );
		return new RouteItem( {path: `/${segments.join('/')}`, title} );
	}

	private async load( routing:ListRoute ):Promise<QLListData>{
		let data:QLListData;
		try{ data = await QLListResolver.data( this.#ql, routing, this.profileStore, this.prefs ); }
		catch( e ){ this.#cnsl.exception( `Could not open ${routing.title}.`, e ); throw e; }//no schema, so no page to say it on:  the navigation fails, and this is the only word the user gets
		data.skip = QLListResolver.savedSkip( data );
		return QLListResolver.loadOrFail( this.#ql, data, this.routeStore );
	}
	//load() for a page:  a refused or failed rows query becomes the page's own state (QLListData.error, no rows) rather than a
	//rejected resolve, which the router turns into a NavigationError nobody sees - the user stayed on the previous page with
	//nothing said.  A user without Read on the collection is the common case, and the page has to open to say so.
	static async loadOrFail<T extends PageData>( ql:Pick<IGraphQL,"query">, data:T, routeStore:RouteStore|null ):Promise<T>{
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
	static async data( ql:IGraphQL, routing:ListRoute, profileStore:ProfileStore, prefs:LocalPrefs ):Promise<QLListData>{
		const collectionName = routing.collectionName;
		const schema = await ql.schemaWithEnums( MetaObject.toTypeFromCollection(collectionName) );
		const systemViews = QLListResolver.systemViews( schema, routing.tableSettings );
		const profile = QLListResolver.viewSet( collectionName, profileStore, prefs, routing.tableSettings );
		await profile.load( systemViews, schema );
		return {profile, schema, results: null, routing, columns: QLListResolver.columns(schema, routing.tableSettings.columns!, routing.tableSettings.excludedColumns)};
	}
	//a collection's views - GatewayResolver's Connections tab keeps its own the same way
	static viewSet( collectionName:string, profileStore:ProfileStore, prefs:LocalPrefs, settings:TableSettings ):ViewSet<View>{
		return new ViewSet( {type: View, key: collectionName, viewsKey: `qlList/${collectionName}/views`, profileStore, prefs, settings} );
	}
	//the default view first, then the route's declared ones - each falling back to the default's columns and sort for what it leaves unset - then Deleted
	static systemViews( schema:TableSchema, settings:TableSettings ):View[]{
		const columns = settings.columns ?? [];
		const queried = settings.rowLink?.fields;
		const defaultView = View.fromConfig( {name: settings.viewName, configColumns: columns, sort: View.toSort(settings.sort) ?? [{active: "name", direction: "asc"}], filters: settings.filters, queried}, schema );//settings.sort, not the name default:  a route that declares its own order (resources: schema then name) has to get it
		const views = [defaultView];
		for( const v of settings.views ?? [] )
			views.push( View.fromConfig({name: v.name, configColumns: v.columns ?? columns, sort: View.toSort(v.sort) ?? [...defaultView.sort], filters: v.filters, queried}, schema) );
		const deleted = QLListResolver.deletedView( defaultView );
		if( deleted )
			views.push( deleted );
		return views;
	}
	static readonly deletedViewName = "Deleted";
	static isDeletedView( view:View ):boolean{ return view.isSystem && view.name==QLListResolver.deletedViewName; }
	//The default view's columns and filters, plus the deleted rows View.query otherwise leaves out - and their deleted date.
	//None where `deleted` is a live toggle (the Resources page's Enforced):  that list shows every row already.
	static deletedView( base:View ):View|undefined{
		const deleted = base.fields.find( f=>f.name=="deleted" );
		if( !deleted || deleted.liveToggle )
			return undefined;
		let view = View.copy( base );
		view.name = QLListResolver.deletedViewName;
		const shown = new ViewField( deleted );
		shown.displayed = true;
		view.fields = view.fields.map( f=>f===deleted ? shown : f );
		view.fieldFilters = [...view.fieldFilters.filter( ff=>ff.field.name!="deleted" ), {field: deleted.qlField, filter: {operator: Operator.Greater, value: ["<not null>"]}}];//Greater: what the settings panel gives a DateTime filter
		return view;
	}
	static columns( schema:TableSchema, configColumns:(string|ViewFieldSettings)[], excluded: string[] ):Record<string,string>{
		let columns: Record<string,string> = {};
		for( let field of schema.fields.filter(f=>!excluded.includes(f.name)) ){
			let configColumn = configColumns.find( c=>typeof c=="object" && c.name==field.name ) as ViewFieldSettings;
			columns[field.name] = configColumn?.displayName ?? StringUtils.idToDisplay(field.name);
		}
		return columns;
	}
	//The page's query:  one row PAST the page, so the pager can say whether there is a next page without a count query
	//the server does not offer.  page() takes the extra row off again.  `id` goes last in the sort:  without a total order, rows
	//that tie on the view's sort can land on two pages or on none, and MSSQL refuses offset/fetch with no order by at all.
	static pageQuery( view:View, skip:number ):Query{
		if( view.limit ){
			view = View.copy( view );
			view.limit += 1;
			if( view.fields.some(f=>f.name=="id") && !view.sort?.some(s=>s.active=="id") )
				view.sort = [...view.sort ?? [], {active: "id", direction: "asc"}];
		}
		return view.query( skip );
	}
	static page( rows:any[], view:View ):{rows:any[], more:boolean}{
		const more = !!view.limit && rows.length>view.limit;
		return { rows: more ? rows.slice(0, view.limit) : rows, more };
	}
	//routeStore null:  the rows are a pick list (QLSelector - filtered, and never the page's own collection), not the collection's sidenav children
	static async load<T extends PageData>( ql:Pick<IGraphQL,"query">, data:T, routeStore:RouteStore|null ):Promise<T>{
		let view = data.profile.current();
		if( data.fixedFilters?.length ){//on a copy:  the profile's view is what the settings panel edits and what a Save persists
			view = View.copy( view );
			view.fieldFilters = [...view.fieldFilters, ...data.fixedFilters];
		}
		const collectionName = data.schema.collectionName;
		const query = ( skip:number )=>{
			const q = QLListResolver.pageQuery( view, skip );
			return ql.query<any>( q.text, q.vars );
		};
		let skip = data.skip ?? 0;
		let results = await query( skip );
		if( skip && !results[collectionName].length ){//a page past the end - rows deleted since it was read:  the first page, not an empty "No … yet"
			skip = 0;
			results = await query( skip );
		}
		const page = QLListResolver.page( results[collectionName], view );
		const y = { ...data, results: {...results, [collectionName]: page.rows}, skip, more: page.more, error: undefined };
		if( routeStore )
			QLListResolver.setChildren( routeStore, y );
		return y;
	}
	//the page's rows as the collection's sidenav children
	static setChildren( routeStore:RouteStore, data:PageData ){
		const rowLink = (data.routing as Partial<ListRoute>).tableSettings?.rowLink?.segments ?? ( (r:QLRow)=>[`${r.slug}`] );
		const children = data.results[data.schema.collectionName].flatMap( (r:QLRow)=>{//bare targets - DetailResolver renders them under the absolute list url
			const link = rowLink( r );
			return link ? [{title: r.name, path: link.join('/')}] : [];
		} );
		routeStore.setChildren( QLListResolver.childrenKey(data), children );
	}
	//the list's absolute url - its section's and its own segment, 'access/users' - or childrenKey
	static childrenKey( data:PageData ):string{ return RouteStore.key( data.childrenKey ?? [data.routing.parent?.path ?? '', data.routing.path] ); }
	//The page a list was left on, so coming back from one of its records - the breadcrumb, Back - lands on it again.  Kept beside
	//its sidenav children, which are that page's rows, and for this tab only:  a new visit starts on the first page.
	static savedSkip( data:PageData ):number{
		try{ return +( sessionStorage.getItem(`${QLListResolver.childrenKey(data)}/skip`) ?? 0 ) || 0; }
		catch{ return 0; }
	}
	static saveSkip( data:PageData, skip:number ):void{
		const key = `${QLListResolver.childrenKey(data)}/skip`;
		try{
			if( skip )
				sessionStorage.setItem( key, `${skip}` );
			else
				sessionStorage.removeItem( key );
		}
		catch{}
	}
	routeStore = inject( RouteStore );
	profileStore = inject( ProfileStore );
	prefs = inject( LocalPrefs );
}