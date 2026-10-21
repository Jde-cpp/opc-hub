import { inject, Injectable } from '@angular/core';
import { UrlSegment } from "@angular/router";
import { DocItemsRouteService, RouteItem } from "jde-spa";
import { GATEWAY_SERVICE } from '../gateway-service';

//the /gateways tiles
@Injectable( {providedIn: 'root'} )
export class GatewayRouteService extends DocItemsRouteService{
	#gatewayService = inject( GATEWAY_SERVICE );
	protected override async items():Promise<RouteItem[]>{
		const gateways = await this.#gatewayService.gateways();
		return gateways.map( gateway=>new RouteItem({path: gateway.slug, title: gateway.name, icon: "router"}) );
	}
}

//a gateway's connections, the /gateways/:gateway tiles
@Injectable( {providedIn: 'root'} )
export class GatewayCnnctnRouteService extends DocItemsRouteService{
	#gatewayService = inject( GATEWAY_SERVICE );
	protected override async items( urlSegments:UrlSegment[] ):Promise<RouteItem[]>{
		const gateway = await this.#gatewayService.gateway( urlSegments[urlSegments.length-1].path );
		const connections = await gateway.queryArray<{name:string, slug:string}>( `serverConnections(deleted:null){ name slug }` );
		return connections.map( c=>new RouteItem({path: c.slug, title: c.name, icon: "lan"}) );
	}
}