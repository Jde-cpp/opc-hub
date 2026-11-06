import { inject, Injectable } from '@angular/core';
import { ActivatedRouteSnapshot, createUrlTreeFromSnapshot, Resolve, Router, RouterStateSnapshot } from '@angular/router';
import { RouteItem } from 'jde-spa';
import { SnackbarService } from 'jde-framework';
import { Gateway, GATEWAY_SERVICE } from '../gateway-service';

//The watch page's data (clients spec *Watch list*, #419):  the gateway the url names, and the sidenav's routing - the page under the
//gateway's, as the historian's pages sit (HistAdminResolver).  The rows are the page's own:  the profile store's, read as it opens.
export type WatchPageData = { gateway:Gateway; routing:RouteItem };

@Injectable()
export class WatchResolver implements Resolve<WatchPageData>{
	#router = inject( Router );
	#gatewayService = inject( GATEWAY_SERVICE );
	#snackbar = inject( SnackbarService );

	async resolve( route:ActivatedRouteSnapshot, _state:RouterStateSnapshot ):Promise<WatchPageData>{
		const parent = route.parent!;
		const instance = parent.url[parent.url.length-1].path;
		try{
			const gateway = await this.#gatewayService.gateway( instance );
			const self = new RouteItem( {path: 'watch', title: 'Watch list'} );
			const routing = new RouteItem( {path: 'watch', title: 'Watch list', siblings: [self], parent: new RouteItem( {path: '.', title: instance} )} );
			return { gateway, routing };
		}
		catch( e ){//no gateway of that name - a stale bookmark - says so and lands on the gateway page, as the historian's pages do
			this.#snackbar.exception( `Could not open the watch list of '${instance}'.`, e );
			this.#router.navigateByUrl( createUrlTreeFromSnapshot( route, ['..'] ) );
			return null as unknown as WatchPageData;
		}
	}
}