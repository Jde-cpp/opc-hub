import { inject, Injectable } from '@angular/core';
import { ActivatedRouteSnapshot, createUrlTreeFromSnapshot, Params, Resolve, Router, RouterStateSnapshot } from '@angular/router';
import { SnackbarService } from 'jde-framework';
import { ProfileStore, RecentVisits } from 'jde-spa';
import { Gateway, GATEWAY_SERVICE, GatewayService } from '../gateway-service';
import { ENodeClass, OpcObject, UaNode } from '../../model/node';
import { NodeRoute } from '../../model/node-route';
import { OPC_STORE, OpcStore } from '../opc-store';
import { Server } from '../../model/server';
import { NodeView } from '../../model/node-view';

//The server answered and the path is not there:  node( path ) put the TranslateBrowsePaths result's status in `sc`.  Told apart
//from a gateway or an OPC server that did not answer - those reject, and the page may well be there once they are back.
class NodeNotFoundError extends EvalError{}

export type NodePageData = {
	route:NodeRoute;
	nodes:UaNode[];
	gateway: Gateway;
	server:Server;
};
@Injectable()
export class NodeResolver implements Resolve<NodePageData> {
	private router = inject( Router );
	private snackbar = inject( SnackbarService );
	private gatewayService:GatewayService = inject( GATEWAY_SERVICE );
	private opcStore:OpcStore = inject( OPC_STORE );
	private profileStore = inject( ProfileStore );
	private recentVisits = inject( RecentVisits );

	async load( route:NodeRoute, url:string ):Promise<NodePageData>{
		try{
			let gateway = await this.gatewayService.gateway( route.gatewaySlug );
			const server = await this.opcStore.getConnection( gateway, route.cnnctnSlug );
			const defaultBrowseNs = server.connection.defaultBrowseNs;
			if( route.node?.isVariable )//browsed already, so the store knows what it is
				return this.#showParent( route );
			if( !route.node ){
				const vars = { opc: route.cnnctnSlug, path: route.browsePath };
				const node = (await gateway.query<any>(`node( opc: $opc, path:$path ){id name nodeClass parents{id name path}}`, vars, (m)=>console.log(m)) )["node"];
				if( node.sc )
					throw new NodeNotFoundError( (await gateway.errorCodeText(node.sc)), {cause:"Opc Interface"} );
				if( node.nodeClass!=undefined && node.nodeClass!=ENodeClass.Object )//a Variable or a Method - wrapped as an object, its own children were listed under its name
					return this.#showParent( route );
				route.node = new OpcObject( {...node, browse: route.browse(defaultBrowseNs)} );
				this.opcStore.insertNode( route, node.parents, defaultBrowseNs );
			}
			let references = await gateway.browseObjectsFolder( route.cnnctnSlug, route.node, true, (m)=>console.log(m) );
			let displayed = references.filter( (r)=>r.displayed );
			if( !this.opcStore.nodeView() ){//a load straight onto a node page:  NodeChildren has not read the views yet, and the sidenav order is fixed here.  ProfileStore caches the row, so the component's own read is a hit.
				const {views, index} = await NodeView.loadActive( this.profileStore );
				this.opcStore.nodeView.set( views[index] );
			}
			this.opcStore.setRoute( route, defaultBrowseNs );
			return { route: route, nodes: displayed, gateway: gateway, server: server };
		}catch( e ){
			this.snackbar.exception( "Not found.", e );
			if( e instanceof NodeNotFoundError )//a removed or renamed node:  the redirect is a NavigationCancel, so RecentVisits never sees it (reviews/m3-closing.md #25).  Not on an outage, or every node tile would go.
				this.recentVisits.forget( RecentVisits.bare(url) );
			this.router.navigateByUrl( createUrlTreeFromSnapshot(route.route, ['..']) );//an injected ActivatedRoute is the ROOT route inside a resolver, so relativeTo sent this to '/';  NodeRoute keeps this route's snapshot.
			return undefined as unknown as NodePageData;
		}
	}
	//A node with no page of its own - a Variable is a row on its parent's page, with its value, status and subscribe box - reached
	//by a search hit, a bookmark or a typed url.  Not an error:  the page to show is the parent's, and a Variable under a Variable
	//walks up one level per resolve (reviews/m3-closing.md #9).  It used to throw from browseObjectsFolder and say "Not found."
	//about a node that exists.
	#showParent( route:NodeRoute ):NodePageData{
		this.router.navigateByUrl( createUrlTreeFromSnapshot(route.route, ['..']) );
		return undefined as unknown as NodePageData;
	}
	resolve(route: ActivatedRouteSnapshot, state: RouterStateSnapshot):Promise<NodePageData>{
		return this.load( new NodeRoute(route, this.opcStore), state.url );
	}
}