import { Injectable, inject, InjectionToken } from '@angular/core';
import { ActivatedRoute, Routes, UrlSegment } from "@angular/router";
import { RouteItem } from '../pages/component-sidenav/route-item';
import { RouteStore } from './route-store';

//a cards page's tiles
export interface IRouteService{
	docItems( x:UrlSegment[] ):Promise<RouteItem[]>;
}

@Injectable()
export class RouteService implements IRouteService{
	protected route = inject( ActivatedRoute );
	children( urlSegments:UrlSegment[] ):Promise<Routes>{
		let config = this.route.parent?.routeConfig! ?? this.route.routeConfig;
		return Promise.resolve( config.children! );
	}
	async docItems( urlSegments:UrlSegment[] ):Promise<RouteItem[]>{
		const children = await this.children(urlSegments);
		let items = [];
		for( let child of children ){
			const docItem = <RouteItem>{...child.data, path:child.path, title: <string>child.title };
			if( child.path!.length )
				items.push( docItem );
		}
		return items;
	}
}
//Tiles that are no routes of the page's own but a list a service reads - the gateways, a gateway's connections.  RouteStore keeps
//them as the url's children, which the sidenav, the breadcrumb and the search box read.
export abstract class DocItemsRouteService implements IRouteService{
	async docItems( urlSegments:UrlSegment[] ):Promise<RouteItem[]>{
		const y = await this.items( urlSegments );
		this.#routeStore.setChildren( urlSegments, y );
		return y;
	}
	protected abstract items( urlSegments:UrlSegment[] ):Promise<RouteItem[]>;
	#routeStore = inject( RouteStore );
}
//angular-review3 C13: a typed token in place of the string one - a typo now fails the build instead of resolving to nothing at runtime, and inject() can take it.
export const IROUTE_SERVICE = new InjectionToken<IRouteService>( 'IRouteService' );