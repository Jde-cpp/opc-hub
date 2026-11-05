import { inject, Injectable } from '@angular/core';
import { ActivatedRouteSnapshot, createUrlTreeFromSnapshot, Resolve, Router, RouterStateSnapshot } from '@angular/router';
import { SnackbarService } from 'jde-framework';
import { LocalPrefs, ProfileStore, RecentVisits } from 'jde-spa';
import { Gateway, GATEWAY_SERVICE } from '../gateway-service';
import { ENodeClass, nodeColumns, OpcObject, UaNode, Variable, variableColumns } from '../../model/node';
import { NodeRoute } from '../../model/node-route';
import { OPC_STORE } from '../opc-store';
import { Server } from '../../model/server';
import { NodeView } from '../../model/node-view';
import { HistGroup } from '../../model/hist';
import { HistoryService } from '../history-service';

//The server answered and the path is not there:  node( path ) put the TranslateBrowsePaths result's status in `sc`.  Told apart
//from a gateway or an OPC server that did not answer - those reject, and the page may well be there once they are back.
class NodeNotFoundError extends EvalError{}

export type NodePageData = {
	route:NodeRoute;
	nodes:UaNode[];
	gateway: Gateway;
	server:Server;
	groups:HistGroup[];//the gateway's history groups that hold any of the nodes:  the History tab's other sources
};
@Injectable()
export class NodeResolver implements Resolve<NodePageData> {
	#router = inject( Router );
	#snackbar = inject( SnackbarService );
	#gatewayService = inject( GATEWAY_SERVICE );
	#opcStore = inject( OPC_STORE );
	#profileStore = inject( ProfileStore );
	#prefs = inject( LocalPrefs );
	#recentVisits = inject( RecentVisits );
	#history = inject( HistoryService );

	async load( route:NodeRoute, url:string ):Promise<NodePageData>{
		try{
			let gateway = await this.#gatewayService.gateway( route.gatewaySlug );
			const server = await this.#opcStore.getConnection( gateway, route.cnnctnSlug );
			const defaultBrowseNs = server.connection.defaultBrowseNs;
			if( route.node?.isMethod )//browsed already, so the store knows what it is:  a Call on its object's row, no page
				return this.#showParent( route );
			if( !route.node ){
				//the url's path from Root (clients spec *Address space*):  the node, and its parents, each with the browse name the server
				//spells, since a bare segment may be namespace 0's.  Each parent with its class and a Variable's own columns - read for
				//every parent, a non-Variable answering each with a status - so the store keeps a Variable parent as a Variable, with its
				//value and history gate, not an object (reviews-issue/416-address-space.md #3).  The node's own the same way:  the path form
				//reads no `... on Variable{}` fragment, as it learns the class with the columns, and a read by id after cost a round trip (#13).
				const vars = { opc: route.cnnctnSlug, path: route.browsePath };
				const node = (await gateway.query<any>(`node( opc: $opc, path:$path, root:"Root" ){${nodeColumns} ${variableColumns} parents{${nodeColumns} path ${variableColumns}}}`, vars) )["node"];
				if( node.sc )
					throw new NodeNotFoundError( (await gateway.errorCodeText(node.sc)), {cause:"Opc Interface"} );
				if( node.nodeClass==ENodeClass.Method )
					return this.#showParent( route );
				route.node = UaNode.fromJson( node ) ?? new OpcObject( node );//a class the model does not spell is browsed as an object
				this.#opcStore.insertNode( route, node.parents, defaultBrowseNs );
			}
			const [references] = await Promise.all( [//apart:  the one reads the children, the other the node's own DataType
				gateway.browseObjectsFolder( route.cnnctnSlug, route.node, true ),
				route.node instanceof Variable ? gateway.resolveEnum( route.cnnctnSlug, route.node ) : undefined//its own row's select:  a deep link's node, or a parent the path query stored (#3), was never resolved
			] );
			const nodes = route.node.isVariable ? [route.node, ...references] : references;//a Variable's page shows its own value, status and tick first, then its children
			if( !this.#opcStore.nodeView() ){//a load straight onto a node page:  NodeChildren has not read the views yet, and the sidenav order is fixed here.  ProfileStore caches the row, so the component's own read is a hit.
				const views = NodeView.viewSet( this.#profileStore, this.#prefs );
				await NodeView.loadViews( views );
				this.#opcStore.nodeView.set( views.current() );
			}
			this.#opcStore.setRoute( route, defaultBrowseNs );
			const groups = await this.#groups( gateway, route.cnnctnSlug, nodes );
			return { route: route, nodes, gateway: gateway, server: server, groups };
		}catch( e ){
			this.#snackbar.exception( "Not found.", e );
			if( e instanceof NodeNotFoundError )//a removed or renamed node:  the redirect is a NavigationCancel, so RecentVisits never sees it (reviews/m3-closing.md #25).  Not on an outage, or every node tile would go.
				this.#recentVisits.forget( RecentVisits.bare(url) );
			this.#router.navigateByUrl( createUrlTreeFromSnapshot(route.route, ['..']) );//an injected ActivatedRoute is the ROOT route inside a resolver, so relativeTo sent this to '/';  NodeRoute keeps this route's snapshot.
			return undefined as unknown as NodePageData;
		}
	}
	//The gateway's groups that hold any of the page's variables (HistoryService.groups), with the page rather than after it, so the
	//History tab is there when the page shows.  None when the gateway can't say - one older than the field, or one that refuses -
	//since the server's own history still shows.
	async #groups( gateway:Gateway, cnnctn:string, nodes:UaNode[] ):Promise<HistGroup[]>{
		try{
			return await this.#history.groups( gateway, cnnctn, nodes.filter( n=>n.isVariable ).map( n=>n.nodeId ) );
		}
		catch( e ){
			console.warn( `Could not list the gateway's history groups on ${cnnctn}.`, e );
			return [];
		}
	}
	//A node with no page of its own - a Method is a Call on its object's row (reviews-issue/318-opc-calls.md #7) - reached by a
	//search hit, a bookmark or a typed url.  Not an error:  the page to show is the parent's.  A Variable had none either
	//(reviews/m3-closing.md #9) until Phase 4 gave it one (#416).
	#showParent( route:NodeRoute ):NodePageData{
		this.#router.navigateByUrl( createUrlTreeFromSnapshot(route.route, ['..']) );
		return undefined as unknown as NodePageData;
	}
	resolve(route: ActivatedRouteSnapshot, state: RouterStateSnapshot):Promise<NodePageData>{
		return this.load( new NodeRoute(route, this.#opcStore), state.url );
	}
}