import { inject, Injectable } from '@angular/core';
import { ActivatedRouteSnapshot, createUrlTreeFromSnapshot, Resolve, Router, RouterStateSnapshot } from '@angular/router';
import { RouteItem } from 'jde-spa';
import { SnackbarService } from 'jde-framework';
import { Gateway, GATEWAY_SERVICE } from '../gateway-service';

//The watch page's data (clients spec *Watch list*, #419), and the OEE dashboard's (#406):  the gateway the url names, and the
//sidenav's routing - the page under the gateway's, as the historian's pages sit (HistAdminResolver), by its route's path and its
//`data.summary`.  The rows are the page's own:  the watch list's the profile store's, the dashboard's the gateway's machines.
export type WatchPageData = { gateway:Gateway; routing:RouteItem };

@Injectable()
export class WatchResolver implements Resolve<WatchPageData>{
	#router = inject( Router );
	#gatewayService = inject( GATEWAY_SERVICE );
	#snackbar = inject( SnackbarService );

	async resolve( route:ActivatedRouteSnapshot, _state:RouterStateSnapshot ):Promise<WatchPageData>{
		const parent = route.parent!;
		const instance = parent.url[parent.url.length-1].path;
		const path = route.routeConfig!.path!, title:string = route.data['summary'];
		try{
			const gateway = await this.#gatewayService.gateway( instance );
			const self = new RouteItem( {path, title} );
			const routing = new RouteItem( {path, title, siblings: [self], parent: new RouteItem( {path: '.', title: instance} )} );
			return { gateway, routing };
		}
		catch( e ){//no gateway of that name - a stale bookmark - says so and lands on the gateway page, as the historian's pages do
			this.#snackbar.exception( `Could not open the ${title[0].toLowerCase()}${title.slice( 1 )} of '${instance}'.`, e );
			this.#router.navigateByUrl( createUrlTreeFromSnapshot( route, ['..'] ) );
			return null as unknown as WatchPageData;
		}
	}
}