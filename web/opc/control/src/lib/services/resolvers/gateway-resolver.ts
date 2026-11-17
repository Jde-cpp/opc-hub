import { inject, Injectable } from '@angular/core';
import {ActivatedRouteSnapshot, Resolve, Router, RouterStateSnapshot, UrlSegment} from '@angular/router';
import { AppInstanceRoute, isRefusal, SnackbarService, QLListData, QLListResolver, TableSchema, TableSettings, View, AppResolver } from 'jde-framework';
import { Gateway, GATEWAY_SERVICE } from '../gateway-service';
import { withoutSessionColumns } from '../../pages/gateway-detail/gateway-table-settings';
import { LocalPrefs, matchConfig, RouteItem, ProfileStore, RouteStore } from 'jde-spa';

export type GatewayData = Omit<QLListData, "routing"> & { routing:AppInstanceRoute };//the Connections tab is a QLList

@Injectable()
export class GatewayResolver implements Resolve<GatewayData> {
	#router = inject( Router );
	#cnsl = inject( SnackbarService );
	#gatewayService = inject( GATEWAY_SERVICE );

	resolve(route: ActivatedRouteSnapshot, state: RouterStateSnapshot):Promise<GatewayData>{
		const routing = new AppInstanceRoute( "gateways", route.params["instance"], route.data["tableSettings"] );
		routing.siblings = AppResolver.siblings( "gateways", this.routeStore );
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
			this.#cnsl.exception( `Could not open ${instanceName}.`, e );
			this.#router.navigateByUrl( GatewayResolver.fallback(this.#router) );
			return null as unknown as GatewayData;
		}
	}
	//Where the site offers no Applications, the Gateways list, which opens no instance:  the home may be a page under this one
	//(config.json's `home`, the demo's floor), and going there would come straight back (reviews-issue/485-demo-box.md #1).
	static fallback( router:Router ):string{
		for( const path of ['apps', 'gateways'] ){
			if( matchConfig(router.config, [path]) )
				return `/${path}`;
		}
		return "/";
	}
	async #load( instanceName:string, routing:AppInstanceRoute, url:UrlSegment[] ):Promise<GatewayData>{
		const gateway = await this.#gatewayService.gateway( instanceName );
		const schema = await gateway.schemaWithEnums( "ServerConnection" );
		return GatewayResolver.orWithoutSessions( routing.tableSettings, settings=>{
			const listed = new AppInstanceRoute( "gateways", instanceName, settings );
			listed.siblings = routing.siblings;
			listed.parent = routing.parent;
			return this.#rows( gateway, schema, listed, url );
		} );
	}
	//The list's two counts take a right beyond the connections' own, gateway/sessions Read, and the gateway refuses a query that
	//names one whole (gateway-table-settings.ts).  So a refused list is asked again without them:  a caller who reads the
	//connections and no session, the demo's visitor, has the list, each row's status with it.  Refused again, the right missing
	//is the table's, and the tab says so.
	static async orWithoutSessions( settings:TableSettings, rows:( settings:TableSettings )=>Promise<GatewayData> ):Promise<GatewayData>{
		const y = await rows( settings );
		const open = isRefusal( y.error ) ? withoutSessionColumns( settings ) : undefined;
		return open ? rows( open ) : y;
	}
	async #rows( gateway:Gateway, schema:TableSchema, routing:AppInstanceRoute, url:UrlSegment[] ):Promise<GatewayData>{
		const defaultView = View.fromTable( routing.tableSettings, schema );
		const deleted = QLListResolver.deletedView( defaultView );
		const profile = QLListResolver.viewSet( schema.collectionName, this.profileStore, this.#prefs, routing.tableSettings );
		await profile.load( deleted ? [defaultView, deleted] : [defaultView], schema );

		return GatewayResolver.load( gateway, {columns: QLListResolver.columns(schema, [], []), profile, schema, results: undefined, routing}, this.routeStore, url );
	}
	//The tab's first page, loaded as a list page's is:  the page it was left on, and a refused or failed rows query as the
	//tab's own state - no access, or could not load with Retry.  A user holding no role yet is the common case once
	//gateway/serverConnections is enforced, and the tab has to open to tell them (reviews/m3-closing.md #6).  The children
	//go under the url (apps/gateways/<instance>):  routing.path, gateways/<instance>, is the browse tree's key
	//(GatewayCnnctnRouteService).
	static async load( gateway:Gateway, data:GatewayData, routeStore:RouteStore, url:string|UrlSegment[] ):Promise<GatewayData>{
		const paged = { ...data, childrenKey: RouteStore.key( url ) };
		return QLListResolver.loadOrFail( gateway, {...paged, skip: QLListResolver.savedSkip(paged)}, routeStore );
	}
	routeStore = inject( RouteStore );
	profileStore = inject( ProfileStore );
	#prefs = inject( LocalPrefs );
}