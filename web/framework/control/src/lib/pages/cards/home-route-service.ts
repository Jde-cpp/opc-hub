import { inject, Injectable } from '@angular/core';
import { Router, Routes } from '@angular/router';
import { RouteService, isNotFound } from 'jde-spa';

//The home page's tiles, one per top-level section.  Its own file, not cards.ts (#351):  app.routes.ts provides it by name, which
//puts the file it lives in into the initial bundle - with the Cards page, had they shared one.
@Injectable( {providedIn: 'root'} )
export class HomeRouteService extends RouteService{
	private router:Router = inject( Router );
	override children():Promise<Routes>{
		let y:Routes = [];
		for( let config of this.router.config.filter(x=> x.title && x.path!.length && x.path!="login" && !x.path!.includes('/') && !isNotFound(x)) ){
			const data = config.data ?? {};
			const pageSettings = data["pageSettings"];//the app routes set summary/icon directly; pageSettings is the older shape
			//cards.scss colors the tile per section; the route path is the section name (gateways/access/apps)
			y.push( {title: config.title, path: config.path, data:{ id: config.path, icon: data["icon"] ?? pageSettings?.icon, summary: data["summary"] ?? pageSettings?.summary, cardClass: data["cardClass"] ?? `card-${config.path}` }} );
		}
		return Promise.resolve( y );
	}
}