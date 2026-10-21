import { inject, Injectable } from '@angular/core';
import { ActivatedRouteSnapshot, createUrlTreeFromSnapshot, Resolve, Router, RouterStateSnapshot } from '@angular/router';
import { RecentVisits, RouteItem, RouteStore } from 'jde-spa';
import { DetailResolver, DetailRoute, SlugNotFoundError, SnackbarService } from 'jde-framework';
import { GATEWAY_SERVICE } from '../gateway-service';
import { GatewayDetailData } from './client-resolver';
import { HistAdminKind, histAdminListRoute } from '../../pages/gateway-detail/hist-admin/hist-admin-list-route';

//A template's or a group's row for its page (#337), under the gateway page's `templates/:slug` and `groups/:slug` routes, as
//ClientResolver resolves a connection's:  the gateway is the url's, the row is DetailResolver's query of the gateway's table, and
//the siblings are the rows the page's list put in the RouteStore under `<page url>/<kind>` (GatewayHistList).  The sidenav's
//parent is the page itself ('.'), so a sibling link is `./<kind>/<slug>` from the sidenav's route, which is the page's.
@Injectable()
export class HistAdminResolver implements Resolve<GatewayDetailData<any>>{
	#router = inject( Router );
	#gatewayService = inject( GATEWAY_SERVICE );
	#routeStore = inject( RouteStore );
	#snackbar = inject( SnackbarService );
	#recentVisits = inject( RecentVisits );

	resolve( route:ActivatedRouteSnapshot, state:RouterStateSnapshot ):Promise<GatewayDetailData<any>>{ return this.#load( route, state.url ); }

	async #load( route:ActivatedRouteSnapshot, url:string ):Promise<GatewayDetailData<any>>{
		const kind = <HistAdminKind>route.data['kind'];
		const slug = route.paramMap.get( 'slug' )!;
		const parent = route.parent!;
		const instance = parent.url[parent.url.length-1].path;
		const parentUrl = parent.url.map( s=>s.path ).join( '/' );
		try{
			const gateway = await this.#gatewayService.gateway( instance );
			const list = histAdminListRoute( kind );
			const siblings = this.#routeStore.getChildren( `${parentUrl}/${kind}` ).map( s=>new RouteItem( {path: `${kind}/${s.path}`, title: s.title} ) );
			const routing = new DetailRoute( `${kind}/${slug}`, siblings.find( s=>s.path==`${kind}/${slug}` )?.title, siblings, new RouteItem( {path: '.', title: instance} ) );
			routing.tableSettings = list.tableSettings;
			return { ...await DetailResolver.load( gateway, list.collectionName, slug, routing, null ), gateway };//null vars, as the gateway's other detail sends
		}
		catch( e ){
			if( e instanceof SlugNotFoundError ){
				this.#snackbar.error( e.message );
				this.#recentVisits.forget( RecentVisits.bare( url ) );//the redirect is a NavigationCancel, so RecentVisits never sees the failure
			}
			else
				this.#snackbar.exception( `Could not load '${slug}'`, e );
			this.#router.navigateByUrl( createUrlTreeFromSnapshot( route, ['..'] ) );//<page>/<kind>, which redirects to the page
			return null as unknown as GatewayDetailData<any>;
		}
	}
}