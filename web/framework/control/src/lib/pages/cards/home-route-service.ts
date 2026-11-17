import { inject, Injectable } from '@angular/core';
import { Route, Router, Routes } from '@angular/router';
import { RouteService, isNotFound } from 'jde-spa';

//A top-level section of the site:  a tile on the home page, and what config.json's `closed` may name (the site's app.routes.ts).
export const isSection = ( route:Route ):boolean=>!!route.title && !!route.path?.length && route.path!="login" && !route.path.includes( '/' ) && !isNotFound( route );

//The home page's tiles, one per top-level section.
@Injectable( {providedIn: 'root'} )
export class HomeRouteService extends RouteService{
	#router = inject( Router );
	override children():Promise<Routes>{
		let y:Routes = [];
		for( let config of this.#router.config.filter(isSection) ){
			const data = config.data ?? {};
			//cards.scss colors the tile per section; the route path is the section name (gateways/access/apps)
			y.push( {title: config.title, path: config.path, data:{ id: config.path, icon: data["icon"], summary: data["summary"], cardClass: data["cardClass"] ?? `card-${config.path}` }} );
		}
		return Promise.resolve( y );
	}
}