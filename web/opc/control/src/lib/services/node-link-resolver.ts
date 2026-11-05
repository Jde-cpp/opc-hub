import { inject, Injectable } from '@angular/core';
import { NodeLink, NodeLinkResolver } from 'jde-access';
import { pageSegments } from '../model/node';
import { NodeId } from '../model/node-id';
import { Gateway, GATEWAY_SERVICE, placeNode } from './gateway-service';
import { OPC_STORE } from './opc-store';

//jde-access's NODE_LINK_RESOLVER:  "opc.<x>" + "ns=5;i=5005" -> ['/gateways', gateway, connection, ...browse path], the node
//page's url (app.routes.ts: gateways/:gateway/:connection/**, the array form the search provider builds too).  <x> is the
//server's accessResource - the [bracketed] part of its applicationName, what node-access grants under - so every gateway's
//connections are described until one matches, once per schema and only remembered when found.  The path is the gateway's
//own answer (placeNode):  a null path is a node it cannot place, hence no link;  a method's opens its object (pageSegments).
@Injectable({providedIn: 'root'})
export class OpcNodeLinkResolver implements NodeLinkResolver{
	#gateways = inject( GATEWAY_SERVICE );
	#store = inject( OPC_STORE );
	#connections = new Map<string, Promise<Placement|undefined>>();

	async resolve( schema:string, criteria:string ):Promise<NodeLink|undefined>{
		if( !schema.startsWith("opc.") )
			return undefined;
		const placement = await this.#connection( schema.substring("opc.".length) );
		if( !placement )
			return undefined;
		const node = await placeNode( placement.gateway, placement.cnnctn, NodeId.fromUaString(criteria) );
		if( node?.path==null )
			return undefined;
		return { route: [ '/gateways', placement.gateway.slug, placement.cnnctn, ...pageSegments(node.path, node.nodeClass) ], name: node.name || undefined, path: node.path };
	}

	#connection( accessResource:string ):Promise<Placement|undefined>{
		let y = this.#connections.get( accessResource );
		if( !y ){
			y = this.#find( accessResource ).then(
				found=>{ if( !found ) this.#connections.delete( accessResource ); return found; },//a miss is not remembered - the server may simply be down right now
				e=>{ this.#connections.delete( accessResource ); throw e; } );//nor is a failure:  kept, every later call re-awaited it and no request went out for the session (reviews/m3-closing.md #29)
			this.#connections.set( accessResource, y );
		}
		return y;
	}
	async #find( accessResource:string ):Promise<Placement|undefined>{
		for( const gateway of await this.#gateways.gateways() ){
			let connections:{slug:string}[];
			try{
				connections = await gateway.queryArray<{slug:string}>( `serverConnections(deleted:null){ slug }` );
			}
			catch( e ){//down, or refusing (serverConnections enforced, no Read):  a miss on this gateway, not the end of the search
				console.warn( `node link: '${gateway.slug}' did not list its connections.`, e );
				continue;
			}
			for( const c of connections ){
				try{
					if( (await this.#store.getConnection(gateway, c.slug)).accessResource==accessResource )
						return { gateway, cnnctn: c.slug };
				}
				catch( e ){
					console.warn( `node link: '${c.slug}' on '${gateway.slug}' could not be described.`, e );//one unreachable server must not sink the others
				}
			}
		}
		return undefined;
	}
}
type Placement = { gateway:Gateway; cnnctn:string };