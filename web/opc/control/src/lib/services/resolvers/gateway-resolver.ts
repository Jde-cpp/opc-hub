import { inject, Injectable } from '@angular/core';
import {ActivatedRoute, ActivatedRouteSnapshot, Resolve, Router, RouterStateSnapshot, UrlSegment} from '@angular/router';
import { AppInstanceRoute, SnackbarService, PageProfile, PageSettings, QLListResolver, TableSchema, View } from 'jde-framework';
import { Gateway, GATEWAY_SERVICE, GatewayService } from '../gateway-service';
import { RouteItem, ProfileStore, RouteStore } from 'jde-spa';

export type GatewayData = {
	columns: Record<string,string>;
	pageSettings:PageSettings;
	profile: PageProfile;
	results:{ serverConnections: any }|undefined; //{users:ISlugRow[]};
	routing:AppInstanceRoute;
	schema: TableSchema;
	error?: unknown;//the rows query was refused or failed - QLList.init reads it, as it reads QLListData.error
};

@Injectable()
export class GatewayResolver implements Resolve<GatewayData> {
	private route:ActivatedRoute = inject( ActivatedRoute );
	private router:Router = inject( Router );
	private cnsl:SnackbarService = inject( SnackbarService );
	private gatewayService:GatewayService = inject( GATEWAY_SERVICE );

	resolve(route: ActivatedRouteSnapshot, state: RouterStateSnapshot):Promise<GatewayData>{
		const routing = new AppInstanceRoute( "gateways", route.params["instance"], route.data["tableSettings"] );
		routing.siblings = this.routeStore.getChildren( route.parent!.url.slice(0, -1) );
		routing.parent = new RouteItem( { path: "/apps", title:"Applications" } );
		return this.load( route.params["instance"], routing, route.parent!.url );
	}

	//What fails here leaves no page to say it on - no gateway of that name (a stale bookmark or Recently-visited tile), or no
	//schema - so it says it in a snackbar and lands on Applications, as DetailResolver goes to its list.  A rejected resolve was
	//a NavigationError only the console saw, and the click did nothing (reviews/m3-closing.md #6).  The rows query is the
	//static load's, which gives the tab a state of its own instead.
	private async load( instanceName:string, routing:AppInstanceRoute, url:UrlSegment[] ):Promise<GatewayData>{
		try{
			return await this.#load( instanceName, routing, url );
		}
		catch( e ){
			this.cnsl.exception( `Could not open ${instanceName}.`, e );
			this.router.navigateByUrl( "/apps" );
			return null as unknown as GatewayData;
		}
	}
	async #load( instanceName:string, routing:AppInstanceRoute, url:UrlSegment[] ):Promise<GatewayData>{
		const gateway = await this.gatewayService.gateway( instanceName );
		const pageSettings = new PageSettings( routing.tableSettings );
		const schema = await gateway.schemaWithEnums( "ServerConnection", (m)=>console.log(m) );
		var profile = new PageProfile();
		const defaultView = new View( routing.tableSettings, schema );
		profile.views.push( defaultView );
		await profile.loadViews( schema.collectionName, this.profileStore, schema, defaultView.sort );
		profile.currentViewIndex = ProfileStore.viewIndex( schema.collectionName );
		profile.showDeleted = ProfileStore.showDeleted( schema.collectionName );

		return GatewayResolver.load( gateway, {columns: QLListResolver.columns(schema, [], []), pageSettings, profile, schema, results: undefined, routing}, this.routeStore, url );
	}
	//A refused or failed rows query is the Connections tab's own state - no access, or could not load with Retry - as
	//QLListResolver.loadOrFail makes it for a list page:  a user holding no role yet is the common case once
	//gateway/serverConnections is enforced, and the tab has to open to tell them (reviews/m3-closing.md #6).
	static async load( gateway:Gateway, data:GatewayData, routeStore:RouteStore, childrenKey:string|UrlSegment[] ):Promise<GatewayData>{
		const query = data.profile.view.query( data.profile.showDeleted, 0 );//the toggle persists under the collection name (serverConnections), not "gateways"
		let results:any;
		try{
			results = await gateway.query<any>( query.text, query.vars, (m)=>console.log(m) );
		}
		catch( e ){
			return { ...data, results: {serverConnections: []}, error: e };
		}
		routeStore.setChildren( childrenKey, results[data.schema.collectionName].map( (r:any)=>{return {title:r.name, path: r.slug};}) );
		return {
			columns: data.columns,
			pageSettings: data.pageSettings,
			profile: data.profile,
			results: results,
			routing: data.routing,
			schema: data.schema
		};
	}
	routeStore = inject( RouteStore );
	profileStore = inject( ProfileStore );
}