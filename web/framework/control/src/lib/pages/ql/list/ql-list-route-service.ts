import { inject } from '@angular/core';
import { Injectable } from "@angular/core";
import { Router, Routes, UrlSegment } from "@angular/router";
import { IRouteService, RouteItem, RouteService } from "jde-spa";
import { CollectionItem, ListRoute } from '../../../services/ql-list-resolver';

@Injectable( {providedIn: 'root'} )
export class QLListRouteService extends RouteService implements IRouteService{
	#router = inject( Router );
	//the tiles wear the tint of the section they sit under - the same one its home tile advertised.  The class
	//is set here rather than on the route data, which is shared with the router config, and the section comes
	//from the url so a second section using this service picks up its own color.
	override async docItems( urlSegments:UrlSegment[] ):Promise<RouteItem[]>{
		const cardClass = `card-${urlSegments[0]?.path}`;
		return (await super.docItems( urlSegments )).map( x=>Object.assign(x, {cardClass}) );
	}
	//a ':collectionDisplay' child stands for one card per collection it lists, named as the list page names it
	override children( urlSegments:UrlSegment[] ):Promise<Routes>{
		const y:Routes = [];
		const thisConfig = this.#router.config.find( x=>x.path==urlSegments[urlSegments.length-1].path )!;
		const childrenConfig = this.#router.config.find( x=>x.path==thisConfig.path && x.children?.length )!;
		const children = childrenConfig.children ? childrenConfig.children.filter(x=> !x.path!.endsWith(":slug")) : [];
		for( const child of children ){
			if( child.path!=":collectionDisplay" )
				y.push( child );
			else{
				for( const collection of child.data!["collections"] as CollectionItem[] ){
					const list = new ListRoute( collection );
					y.push( {title: list.title, path: list.path, data: typeof collection=='string' ? undefined : collection.data} );
				}
			}
		}
		return Promise.resolve( y );
	}
}