import { ActivatedRouteSnapshot, createUrlTreeFromSnapshot, Resolve, Router, RouterStateSnapshot } from '@angular/router';
import { inject, Injectable } from '@angular/core';
import { SnackbarService } from '../shared/snackbar/snackbar-service';
import { TableSchema } from '../model/ql/schema/table-schema';
import { IGRAPHQL, IGraphQL } from './graphql';
import { ListRoute, TableSettings } from './ql-list-resolver';
import { MetaObject } from '../model/ql/schema/meta-object';
import { RecentVisits, RouteItem, RouteStore } from 'jde-spa';
import { ProfileStore } from 'jde-spa';

export type DetailPageSettings = {
	excludedColumns:string[];
};

export class DetailRoute extends RouteItem{
	constructor( slug:string, title:string|undefined, siblings:RouteItem[], parent:RouteItem ){
		super( {path:slug, title:title, siblings:siblings, parent:parent} );
		if( parent instanceof ListRoute )//adopt the collection's settings; was never assigned — `routing.tableSettings.excludedColumns` threw and no detail page could resolve
			this.tableSettings = parent.tableSettings;
	}
	tableSettings: TableSettings = { excludedColumns: [] };
}

export type DetailResolverData<T>={
	row:any;
	schema: TableSchema;
	routing:DetailRoute;
};

//A slug the server does not have, told apart from a query that failed.  Both used to arrive here as the same throw - the
//server answers {"data":{"role":null}} for a missing row and the null then TypeError'd on obj["id"] in the subQueries loop -
//so a malformed query (an unknown column, a 500) was reported as "Slug not found" and the real error never left the log.
export class SlugNotFoundError extends Error{
	constructor( readonly slug:string ){
		super( `Slug not found:  '${slug}'` );
		this.name = "SlugNotFoundError";
	}
}

@Injectable()
export class DetailResolver<T> implements Resolve<DetailResolverData<T>> {
	private router = inject( Router );
	private snackbar = inject( SnackbarService );
	private ql:IGraphQL = inject( IGRAPHQL );
	private recentVisits = inject( RecentVisits );

	resolve(route: ActivatedRouteSnapshot, state: RouterStateSnapshot):Promise<DetailResolverData<T>>{
		let collectionDisplay = route.url.length>1 ? route.url[route.url.length-2].path : route.data["collectionName"]; //users
		let slug = route.paramMap.get( "slug" )!;
		return this.loadProfile( route, state.url, collectionDisplay, slug );
	}
	//The absolute list url the breadcrumb needs is rebuilt from the route below;  `url` is only for Recently visited.
	private async loadProfile( route: ActivatedRouteSnapshot, url:string, collectionDisplay:string, slug:string ):Promise<DetailResolverData<T>>{
		//ComponentNav renders each sibling as parent.path + '/' + sibling.path, so the parent must be the absolute list url
		//('/access/users') and the siblings bare targets — the relative ListRoute path resolved against the sidenav route
		//('/access/users/users/<slug>'), breaking sibling navigation and the routerLinkActive highlight.
		let siblings = this.routeStore.getChildren( collectionDisplay ).map( s=>new RouteItem(
			{path: s.path.startsWith(collectionDisplay+'/') ? s.path.substring(collectionDisplay.length+1) : s.path, title: s.title}) );//pre-fix localStorage entries are collection-prefixed
		const parent = ListRoute.find( collectionDisplay, route.parent!.routeConfig!.children!.find(x=>x.path==":collectionDisplay")!.data!["collections"] );
		parent.path = `/${[...route.parent!.url.map(s=>s.path), collectionDisplay].join('/')}`;
		const routing = new DetailRoute( slug, siblings.find(s=>s.path==slug)?.title, siblings, parent );
		try{
			return await DetailResolver.load<T>( this.ql, this.ql.toCollectionName(collectionDisplay), slug, routing );//await inside try — without it, async failures skip the catch entirely
		}
		catch( e ){
			if( e instanceof SlugNotFoundError ){
				this.snackbar.error( e.message );
				this.recentVisits.forget( RecentVisits.bare(url) );//the redirect below is a NavigationCancel, not the NavigationError RecentVisits drops a page on (reviews/m3-closing.md #25);  only a missing row - a failed query is transient
			}
			else
				this.snackbar.exception( `Could not load '${slug}'`, e );//whatever actually failed - a 500 from a malformed query used to be indistinguishable from a missing row
			this.router.navigateByUrl( createUrlTreeFromSnapshot(route, ['..']) );//an injected ActivatedRoute is the ROOT route inside a resolver, so relativeTo sent this to '/';  the snapshot is this route.
			return null as unknown as DetailResolverData<T>;
		}
	}

	//`vars` is a parameter only so ClientResolver can share this body without changing what goes on the wire (review3 C1):
	//ql() appends `&variables=` for any TRUTHY vars, so the {} the access pages have always sent is not the same request as
	//the null the gateway has always had, and the two talk to different servers.  Neither behaviour is proven on the other.
	static async load<T>( ql:IGraphQL, collectionName:string, slug:string, routing:DetailRoute, vars:any={} ):Promise<DetailResolverData<T>>{
		const schema = await ql.schemaWithEnums( MetaObject.toTypeFromCollection(collectionName), (m)=>console.log(m) );
		let obj:any = {};
		if( slug && slug!="$new" ){//`slug &&`: a missing route param must not query for the row named 'undefined'
			obj = await ql.querySingle( ql.slugQuery(schema, slug, ProfileStore.showDeleted(collectionName), routing.tableSettings.excludedColumns), vars, (m)=>console.log(m) );
			if( obj==null )//{"data":{"<singular>":null}} - the row is not there.  Checked before the subQueries loop, whose obj["id"] would otherwise TypeError and hide every other failure behind the same message.
				throw new SlugNotFoundError( slug );
			for( let query of ql.subQueries(schema.type, obj["id"]) ){
				const subRows = await ql.query<any>( query, vars, (m)=>console.log(m) );
				//"acl":[{"role":{"id":33,"name":"Opc Gateway Permissions","deleted":null},"identity":{"id":1}}]}
				let [property, propValue] = Object.entries(subRows)[0];
				if( !obj[property] )
					obj[property] = [...<[]>propValue];
				else
					obj[property] = obj[property].concat( propValue );
			}
		}
		return {
			row: obj,
			schema: schema,
			routing: routing
		};
	}
	routeStore = inject( RouteStore );
}