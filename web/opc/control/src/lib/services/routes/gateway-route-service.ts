import { inject, Injectable } from '@angular/core';
import { ActivatedRoute, Routes, UrlSegment } from "@angular/router";
import { RouteItem, IRouteService, RouteStore } from "jde-spa";
import { subscribe } from "jde-framework";
import { GATEWAY_SERVICE, GatewayService } from '../gateway-service';

@Injectable( {providedIn: 'root'} )
export class GatewayRouteService implements IRouteService{
	private _gatewayService:GatewayService = inject( GATEWAY_SERVICE );
	async children():Promise<Routes>{
		throw new Error("Not implemented");
	}

	async docItems( urlSegments:UrlSegment[] ):Promise<RouteItem[]>{
		let y = [];
		let gateways = await this._gatewayService.gateways();
		for( const gateway of gateways )
			y.push( new RouteItem({path: gateway.slug, title: gateway.name, icon: "router"}) );

		this.routeStore.setChildren( urlSegments, y );
		return y;
	}

	routeStore = inject( RouteStore );
}

@Injectable( {providedIn: 'root'} )
export class GatewayCnnctnRouteService implements IRouteService{
	private _route:ActivatedRoute = inject( ActivatedRoute );
	private _gatewayService:GatewayService = inject( GATEWAY_SERVICE );
	async children():Promise<Routes>{
		throw new Error("Not implemented");
	}

	async docItems( urlSegments:UrlSegment[] ):Promise<RouteItem[]>{
		let y = [];
		let route = this._route.snapshot.children[0];
		let gatewaySlug = route.paramMap.get("gateway")!;
		let gateway = await this._gatewayService.gateway( gatewaySlug );
		let connections = await gateway.queryArray<any>( `serverConnections(deleted:null){ name slug }`,  );
		for( const c of connections )
			y.push( new RouteItem({path: c.slug, title: c.name, icon: "lan"}) );

		this.routeStore.setChildren( route.url, y );
		return y;
	}

	routeStore = inject( RouteStore );
}