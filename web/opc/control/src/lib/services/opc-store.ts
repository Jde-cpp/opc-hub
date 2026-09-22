import { Injectable, InjectionToken, signal } from "@angular/core";
import { CnnctnSlug, ServerCnnctn } from "../model/server-cnnctn";
import { browseEq, ETypes, Ns, toBrowse } from '../model/types';
import { NodeRoute } from "../model/node-route";
import { OpcObject, UaNode, ENodeClass } from "../model/node";
import { NodeId, NodeKey } from "../model/node-id";
import { RouteItem } from "jde-spa";
import { Gateway, GatewaySlug } from "./gateway-service";
import { Server, ServerProps } from "../model/server";
import { NodeView } from "../model/node-view";

class StoreNode{
	constructor( node:UaNode ){
		this.node = node;
	}
	parentId!:NodeId;
	node:UaNode;
	children:UaNode[] = [];
}

@Injectable({providedIn: 'root'})
export class OpcStore{
	constructor()
	{}

	//the node table's active view, kept current by NodeChildren.  setRoute orders the sidenav siblings by its sort, and runs from
	//the resolver - before the component hears of the new route - so the view has to already be here rather than asked for.
	nodeView = signal<NodeView|undefined>( undefined );

	//Memoized per gateway for the node pages, which share it.  That used to be the whole story, so a describe that succeeded once
	//answered for the life of the page:  an edited Name or Default Namespace never reached the node pages, and the Connection tab
	//never went back to "Not connected" (reviews/m3-closing.md #5).  `fresh` - the Connection tab - describes again and keeps
	//nothing when that fails;  forget() is what saving or deleting the connection calls.
	public async getConnection( gatewayService:Gateway, cnnctn:CnnctnSlug, options?:{fresh?:boolean} ):Promise<Server>{
		const gateway = gatewayService.slug;
		let gatewayConnections = this.#connections.get( gateway );
		if( !gatewayConnections )
			this.#connections.set( gateway, gatewayConnections = new Map<CnnctnSlug, Server>() );
		if( options?.fresh )
			gatewayConnections.delete( cnnctn );//before the query:  a failure leaves nothing for the memoized path to answer with
		else if( gatewayConnections.has(cnnctn) )
			return gatewayConnections.get(cnnctn)!;
		let q = `\
			connection: serverConnection( slug: $opc ){ id name slug url certificateUri defaultBrowseNs }
			desc: serverDescription( opc: $opc ){ applicationUri productUri applicationName applicationType gatewayServerUri discoveryProfileUri discoveryUrls }
			policy: securityPolicyUri( opc: $opc )
			mode: securityMode( opc: $opc )
			namespaces( opc: $opc ){ index uri }`;
		let props = await gatewayService.query<ServerProps>(q, {opc:cnnctn}, (m)=>console.log(m));
		let server = new Server( props );
		gatewayConnections.set( cnnctn, server );
		return server;
	}

	//The connection's describe and the nodes browsed under it - a changed url can be another server entirely.
	forget( gateway:GatewaySlug, cnnctn:CnnctnSlug ):void{
		this.#connections.get( gateway )?.delete( cnnctn );
		this.#nodes.get( gateway )?.delete( cnnctn );
	}

	private getNodes( gateway:GatewaySlug, cnnctn:CnnctnSlug ):Map<NodeKey,StoreNode>{
		let gatewayNodes = this.#nodes.get( gateway );
		if( !gatewayNodes )
			this.#nodes.set( gateway, gatewayNodes = new Map<CnnctnSlug, Map<NodeKey,StoreNode>>() );
		let nodes = gatewayNodes.get( cnnctn );
		if( !nodes ){
			gatewayNodes.set( cnnctn, nodes = new Map<NodeKey,StoreNode>() );
			nodes.set( OpcObject.rootNode.key, new StoreNode(new OpcObject({ns: OpcObject.rootNode.nodeId.ns, id: OpcObject.rootNode.nodeId.id, browse: '', name: cnnctn, nodeClass: ENodeClass.Object, typeDefinition: ETypes.Folder})) );
		}
		return nodes;
	}
	private findStore( gateway:GatewaySlug, cnnctn:CnnctnSlug, node:NodeId ):StoreNode|undefined{
		const opcNodes = this.#nodes.get( gateway )?.get( cnnctn );
		let store:StoreNode|undefined;
		if( opcNodes )
			store = opcNodes.get( node.key )!;
		return store;
	}
	private getStore( nodes:Map<NodeKey,StoreNode>, node:UaNode ):StoreNode{
		let store = nodes.get( node.key );
		if( !store ){
			// if( OpcObject.rootNode.equals(node) )
			// 	throw new EvalError( `Root node not set.`, {cause:"Internal Error"} );
			// this.addChildren( nodes, node.parent, [node] );
			nodes.set( node.key, store = new StoreNode(node) );
		}
		return store;
	}
	setServerCnnctns( clients: RouteItem[] ):void{
		this.#serverCnnctnRoutes = [...clients];
		for( let route of this.#serverCnnctnRoutes )
			route.path = route.path.substring( route.path.lastIndexOf("/")+1 );
	}
	getParent( opcNodes:Map<NodeKey,StoreNode>, path:string, defaultNs:Ns ):UaNode|undefined{
		let segments = path.split("/");
		if( segments.length==1 )
			return OpcObject.rootNode;

		let parent = opcNodes.get( OpcObject.rootNode.key );
		for( let segment of segments.slice(0, -1) ){
			if( !parent )
				return undefined;
			let child = parent.children.find( (c)=>browseEq(c.browse!, toBrowse(segment, defaultNs)) );
			if( !child )
				return undefined;
			parent = opcNodes.get( child.key );
		}
		if( !parent )
			throw new EvalError( `Parent not set for '${path}'`, {cause:"Internal Error"} );
		return parent?.node;
	}
	insertNode( route:NodeRoute, parents:any, defaultNs:Ns ):void{
		let opcNodes = this.getNodes( route.gatewaySlug, route.cnnctnSlug );
		for( let parent of parents ){
			parent.browse = toBrowse( parent.path, defaultNs );
			let obj = new OpcObject( parent );
			if( !obj.parent )
				obj.parent = this.getParent( opcNodes, parent.path, defaultNs );
			if( !opcNodes.has(obj.key) )
				this.addChildren( opcNodes, obj.parent!, [obj] );
		}

		if( !route.node.parent )
			route.node.parent = this.getParent( opcNodes, route.path, defaultNs );
		if( !opcNodes.has(route.node.key) )
			this.addChildren( opcNodes, route.node.parent!, [route.node] );
	}

	setNodes( gateway:GatewaySlug, cnnctn:CnnctnSlug, parent:UaNode, children:UaNode[] ){
		let opcNodes = this.getNodes( gateway, cnnctn );
		this.addChildren( opcNodes, parent, children );
	}
	private addChildren( opcNodes:Map<NodeKey,StoreNode>, parent:UaNode, children:UaNode[] ){
		let store = this.getStore( opcNodes, parent );
		store.children = [];
		for( let child of children ){
			store.children.push( child );
			let childStore = this.getStore( opcNodes, child );
			childStore.parentId = parent.nodeId;
			childStore.node = child;
		}
	}

	cnnctnName( gateway:GatewaySlug, cnnctn:CnnctnSlug ):string{
		return this.#connections.get( gateway )?.get( cnnctn )?.connection.name ?? cnnctn;
	}
	//ComponentNav renders each sibling as parent.path + '/' + sibling.path, so the parent must be the absolute url and the
	//siblings bare browse names.  Both used to be full paths, so every link resolved relative to the sidenav route
	//('/gateways/Debug/local/2~DeviceSet/local/2~DeviceSet/2~DeviceFeatures') - no sibling matched the current url and the
	//routerLinkActive highlight never came on.
	//The siblings come in the node table's order - the active view's sort, without its filters - so the sidenav lists them as
	//the parent page did;  in browse order a name-sorted table's last row could land anywhere.
	setRoute(route: NodeRoute, defaultBrowseNs:Ns|undefined ):void{
		const cnnctnName = this.cnnctnName( route.gatewaySlug, route.cnnctnSlug );
		if( route.node.equals(OpcObject.rootNode) ){
			route.parent = new RouteItem( {path: route.gatewayUrl, title: route.gatewaySlug} );//set even here:  ComponentNav only recomputes parentUrl from the parent, so without one the connection page kept whatever the last node page left behind
			route.siblings = [new RouteItem({title: cnnctnName, path: route.cnnctnSlug})]; //TODO add all connections.
			return;
		}
		let findStore = (node:NodeId|undefined):StoreNode|undefined => {
			return node ? this.findStore( route.gatewaySlug, route.cnnctnSlug, node ) : undefined;
		};
		const store = findStore( route.nodeId );
		let parentPaths = [];
		let parent = findStore( store?.parentId );
		for( let current = parent; current && !current.node.equals(OpcObject.rootNode); current=findStore(current.parentId) ){
			parentPaths.push( current.node.browseFQ(defaultBrowseNs) );
		}
		if( !parent )
			throw new EvalError( `Parent not found for ${store?.node.browse}`, {cause:"Internal Error"} );

		//the root store node is named after the slug, so the connection level takes the display name instead
		const parentTitle = parent.node.equals( OpcObject.rootNode ) ? cnnctnName : (parent.node.name ?? cnnctnName);
		route.parent = new RouteItem( {path: [route.cnnctnUrl, ...parentPaths.reverse()].join('/'), title: parentTitle} );
		const siblings:UaNode[] = [];
		for( const sibling of parent.children ){
			const siblingStore = sibling.key == route.nodeId.key ? store : findStore( sibling.nodeId );
			const siblingRef = siblingStore?.node;
			if( siblingRef?.isObject && siblingRef?.displayed )
				siblings.push( siblingRef );
		}
		const view = this.nodeView() ?? NodeView.default();
		route.siblings = view.sortNodes( siblings ).map( n=>new RouteItem({path: n.browseFQ(defaultBrowseNs), title: n.name}) );
	}

	findNodeId( gateway:string, cnnctnSlug:string, browsePath:string ):UaNode|undefined{
		const nodes = this.getNodes( gateway, cnnctnSlug );
		let storeNode = nodes.get( OpcObject.rootNode.key );
		if( !storeNode )
			return undefined;
		const cnnctn = this.#connections.get( gateway )?.get( cnnctnSlug );
		const segments = browsePath.split( "/" );
		let uaNode: UaNode|undefined;
		for( let i=0; i<segments.length; ++i ){
			uaNode = storeNode.children.find( (c)=>browseEq(c.browse!, toBrowse(segments[i], cnnctn?.connection.defaultBrowseNs)) );
			if( !uaNode )
				return undefined;
			if( i+1==segments.length )
				break;//the last segment is the answer;  its own store entry is not needed and may legitimately be absent
			storeNode = this.findStore( gateway, cnnctnSlug, uaNode.nodeId );
			if( !storeNode )
				return undefined;//a known child with no store entry of its own - the rest of the path cannot be walked
		}
		return uaNode;
	}

	#serverCnnctnRoutes!: RouteItem[];
	#nodes = new Map<GatewaySlug,Map<CnnctnSlug, Map<NodeKey,StoreNode>>>();
	#connections = new Map<GatewaySlug,Map<CnnctnSlug, Server>>();
}
//angular-review3 C13: a typed token in place of the string one - a typo now fails the build instead of resolving to nothing at runtime, and inject() can take it.
export const OPC_STORE = new InjectionToken<OpcStore>( 'OpcStore' );
