import { RouteItem } from "jde-spa";
import {NodeId} from './node-id';
import { ActivatedRouteSnapshot, Params } from "@angular/router";
import { OpcStore } from "../services/opc-store";
import { OpcObject, UaNode } from "./node";
import { GatewaySlug } from "../services/gateway-service";

export class NodeRoute extends RouteItem{
	constructor( activatedRoute:ActivatedRouteSnapshot, opcStore:OpcStore ){//built with `new` by NodeResolver, never by the injector - the @Inject that used to sit on opcStore did nothing (angular-review3 C5)
		super();
		let paramsRoute = activatedRoute.pathFromRoot.find( (r)=>r.paramMap.get("gateway") );
		this.gatewaySlug = paramsRoute?.paramMap.get("gateway")!;
		this.cnnctnSlug = paramsRoute?.paramMap.get("connection")!;
		this.route = activatedRoute.pathFromRoot[activatedRoute.pathFromRoot.length-1];
		this.node = this.browsePath.length ? opcStore.findNodeId( this.gatewaySlug, this.cnnctnSlug, this.browsePath )! : OpcObject.rootNode;
	}
	route: ActivatedRouteSnapshot;
	//the absolute url of the connection ('/gateways/Debug/local'):  everything ahead of this route's browse-path segments.
	get cnnctnUrl():string{ return '/'+this.route.pathFromRoot.slice( 0, -1 ).flatMap( r=>r.url.map(s=>s.path) ).join('/'); }
	get gatewayUrl():string{ const url = this.cnnctnUrl; return url.substring( 0, url.lastIndexOf('/') ); }
	override get path(): string{
		return this.route.url.map(seg=>seg.path).join("/");
	}
	override get title(): string{ return this.route.title!; }
	override get queryParams():Params{ return this.route.queryParams; }
	node: UaNode;
	get nodeId():NodeId{ return this.node; }
	get browsePath():string{
		return this.route.url.map(seg=>seg.path).join("/");
	}
	get profileKey():string{ return this.nodeId?.toString(); }
	cnnctnSlug:string;
	gatewaySlug:GatewaySlug;
}