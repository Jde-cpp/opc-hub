import { ActivatedRouteSnapshot, createUrlTreeFromSnapshot, Resolve, Router, RouterStateSnapshot } from '@angular/router';
import { inject, Injectable } from '@angular/core';
import { RecentVisits, RouteItem, RouteStore } from 'jde-spa';
import { DetailResolver, DetailResolverData, DetailRoute, errorMessage, SnackbarService, SlugNotFoundError } from 'jde-framework';
import { Gateway, GATEWAY_SERVICE } from '../gateway-service';
import { ServerCnnctn } from '../../model/server-cnnctn';
import { OpcStore } from '../opc-store';

//A gateway's detail row with the gateway it was read from.  The page takes its gateway from here in onRow rather than once from the url:
//the router reuses the page when another gateway's row opens, and a row id means nothing on another gateway.
export type GatewayDetailData<T> = DetailResolverData<T> & { gateway:Gateway };

@Injectable()
export class ClientResolver implements Resolve<GatewayDetailData<ServerCnnctn>> {
	#router = inject( Router );
	#gatewayService = inject( GATEWAY_SERVICE );

	resolve(route: ActivatedRouteSnapshot, state: RouterStateSnapshot):Promise<GatewayDetailData<ServerCnnctn>>{
		return this.loadProfile( route, state.url, route.paramMap.get("connection")! );
	}

	//The route is the only input:  everything user-facing calls one of these rows a connection ("Connections" tab,
	//"<name> - Connection" title, the serverConnections collection), and the sibling titles come from the RouteStore, so
	//there was nothing for the collection-display name this used to carry to feed.  `url` is only for Recently visited.
	private async loadProfile( route: ActivatedRouteSnapshot, url:string, slug:string ):Promise<GatewayDetailData<ServerCnnctn>>{
		const parent = route.parent!;
		let gatewaySlug = parent.url[parent.url.length-1].path;
		const ql = await this.#gatewayService.gateway( gatewaySlug );
		let siblings = this.routeStore.getChildren( parent.url ).map( s=>new RouteItem({path:`${s.path}`, title:s.title}) );
		const routing = new DetailRoute(
			slug,
			siblings.find(s=>s.path==slug)?.title,
			siblings,
			new RouteItem({path:'.', title:parent.params["instance"]})
		);
		try{
			return { ...await ClientResolver.load( ql, this.opcStore, slug, routing, this.snackbar ), gateway: ql };//await inside try — without it, async failures skip the catch entirely
		}
		catch( e:unknown ){
			//As DetailResolver:  a missing row and a failed query used to arrive here as the same throw - the server answers
			//{"data":{"serverConnection":null}} for a slug it does not have, and the null then TypeError'd on obj["id"] -
			//so a malformed query or a 500 was reported as "Slug not found." and the real error never left the log.
			if( e instanceof SlugNotFoundError ){
				this.snackbar.error( e.message );
				this.recentVisits.forget( RecentVisits.bare(url) );//as DetailResolver:  the redirect is a NavigationCancel, so RecentVisits never sees the failure (reviews/m3-closing.md #25)
			}
			else
				this.snackbar.exception( `Could not load '${slug}'`, e );
			this.#router.navigateByUrl( createUrlTreeFromSnapshot(route, ['..']) );//an injected ActivatedRoute is the ROOT route inside a resolver, so relativeTo sent this to '/';  the snapshot is this route.
			return null as unknown as GatewayDetailData<ServerCnnctn>;
		}
	}

	//Delegates to DetailResolver.load instead of copying it (review3 C1).  The copy had already drifted - L14's null guard
	//had to be written here a second time - and the opcStore fetch below is the only part that was ever Client-specific.
	//`null` vars, not the default {}: that is what this query has always sent to the gateway.
	static async load( ql:Gateway, opcStore:OpcStore, slug:string, routing:DetailRoute, snackbar:SnackbarService ):Promise<DetailResolverData<ServerCnnctn>>{//snackbar is passed in:  inject() needs an injection context, which a static method never has.
		const y = await DetailResolver.load<ServerCnnctn>( ql, "serverConnections", slug, routing, null );
		if( slug && slug!="$new" ){
			try{
				y.row["server"] = await opcStore.getConnection( ql, slug, {fresh: true} );//the tab is where a user checks the connection:  never the page's memo of an earlier describe
			}
			catch( e ){ //can't connect, maybe bad settings.  The toast goes by; the row keeps the reason for the Connection tab's not-connected state.
				y.row["serverError"] = errorMessage( e );
				snackbar.exception( "Could not connect to server.", e );
			}
		}
		return y;
	}
	opcStore = inject( OpcStore );
	recentVisits = inject( RecentVisits );
	routeStore = inject( RouteStore );
	snackbar = inject( SnackbarService );
}