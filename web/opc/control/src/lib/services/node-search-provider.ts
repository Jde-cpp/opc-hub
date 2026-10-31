import { inject, Injectable } from '@angular/core';
import { Router } from '@angular/router';
import { ISearchProvider, SearchResult } from 'jde-spa';
import { errorMessage, isRefusal } from 'jde-framework';
import { ENodeClass } from '../model/node';
import { NodeId } from '../model/node-id';
import { toBrowse } from '../model/types';
import { Gateway, GATEWAY_SERVICE } from './gateway-service';
import { OPC_STORE } from './opc-store';

export type NodeSearchRow = { connection:{ slug:string; name:string }; path:string; name:string; nodeClass:number; depth:number };

//OPC node names through the gateway's `search` query.  Inside a connection's node tree only that connection is searched;
//anywhere else every gateway is asked without an `opc`, which answers from the clients this session already holds - a search
//never opens an OPC session.  Hits render as name over `<connection>/<browse path>`.  A node id, `ns=2;i=1001`, is looked
//up instead:  inside a connection the gateway walks up to the Objects folder for its page's path (NodeQLAwait's id form, the
//node-scoped resources' link);  elsewhere there is no connection to look it up on, and a notice says so.
@Injectable({ providedIn: 'root' })
export class NodeSearchProvider implements ISearchProvider{
	readonly name = 'nodes';
	readonly prefixes = [ 'node' ];
	#router = inject( Router );
	#gatewayService = inject( GATEWAY_SERVICE );
	#opcStore = inject( OPC_STORE );

	static readonly columns = '{ connection{ slug name } path name nodeClass depth }';
	static readonly currentConnection = /^\/gateways\/([^/?#]+)\/([^/?#]+)/;//app.routes.ts: gateways/:gateway/:connection/**

	async search( text:string, scope:string|undefined, limit:number ):Promise<SearchResult[]>{
		if( !text.length )
			return [];
		const hits:{ gateway:Gateway; row:NodeSearchRow }[] = [];
		const current = NodeSearchProvider.currentConnection.exec( this.#router.url );
		const id = NodeSearchProvider.nodeId( text );
		if( id && !current )
			return [{ title: `Open a connection to look up ${id.uaString()}`, summary: "A node id is looked up on the connection whose pages you are on.", route: [], icon: 'info_outline', rank: 0, source: this.name, disabled: true }];
		if( current ){
			const gateway = await this.#gatewayService.gateway( decodeURIComponent(current[1]) );
			const opc = decodeURIComponent( current[2] );
			if( id )
				return this.byId( gateway, opc, id );
			try{
				const rows = await gateway.queryArray<NodeSearchRow>( `search( opc:$opc, text:$text, limit:$limit )${NodeSearchProvider.columns}`, {opc, text, limit} );
				hits.push( ...rows.map( row=>({gateway, row}) ) );
			}
			catch( e ){//the connection's index failed:  say so, rather than read as "no such node" (reviews/m3-closing.md #33).
				console.warn( `search: '${opc}' on gateway '${gateway.slug}' failed.`, e );
				return [ this.unavailable( gateway, opc, e ) ];
			}
		}
		else{
			const gateways = await this.#gatewayService.gateways();
			const settled = await Promise.allSettled( gateways.map( g=>g.queryArray<NodeSearchRow>(`search( text:$text, limit:$limit )${NodeSearchProvider.columns}`, {text, limit}) ) );
			settled.forEach( (s,i)=>{
				if( s.status=='fulfilled' )
					hits.push( ...s.value.map( row=>({gateway: gateways[i], row}) ) );
				else
					console.warn( `search: gateway '${gateways[i].slug}' failed.`, s.reason );//an unreachable gateway must not sink the others.
			} );
		}
		return hits.slice( 0, limit ).map( ({gateway, row})=>({
			title: row.name,
			route: [ '/gateways', gateway.slug, row.connection.slug, ...NodeSearchProvider.pagePath(row) ],//array form: the router encodes each browse segment, NodeRoute decodes them back.
			summary: `${row.connection.name}/${NodeSearchProvider.displayPath(row.path)}`,
			icon: NodeSearchProvider.icon( row.nodeClass ),
			rank: row.name.toLowerCase().startsWith( text.toLowerCase() ) ? 0 : 1,
			source: this.name
		}) );
	}
	//the one hit a node id names:  its page, by the path the gateway walks up - none for a node it cannot place (unknown, or
	//outside the Objects folder, which has no page) - as a notice, so the id is not mistaken for a name no node has.
	async byId( gateway:Gateway, opc:string, id:NodeId ):Promise<SearchResult[]>{
		let node:{ name?:string; nodeClass?:number; path:string|null };
		try{
			node = await gateway.querySingle( `node( opc:$opc, id:$id ){ name nodeClass path }`, {opc, id: id.toJson()} );
		}
		catch( e ){
			console.warn( `search: '${id.uaString()}' on '${opc}' failed.`, e );
			return [ this.unavailable( gateway, opc, e ) ];
		}
		const name = this.#opcStore.cnnctnName( gateway.slug, opc );
		if( node?.path==null )
			return [{ title: `No page for ${id.uaString()}`, summary: `${name}: no such node, or one outside the Objects folder.`, route: [], icon: 'error_outline', rank: 0, source: this.name, disabled: true }];
		const row:NodeSearchRow = { connection: {slug: opc, name}, path: node.path, name: typeof node.name=="string" && node.name ? node.name : id.uaString(), nodeClass: node.nodeClass ?? ENodeClass.Object, depth: 0 };
		return [{ title: row.name, route: [ '/gateways', gateway.slug, opc, ...NodeSearchProvider.pagePath(row) ], summary: `${name}${row.path ? '/'+NodeSearchProvider.displayPath(row.path) : ''}`, icon: NodeSearchProvider.icon( row.nodeClass ), rank: 0, source: this.name }];
	}
	//the text as a node id in the UA spelling, `[ns=<n>;]<i|s|g|b>=<identifier>`, or nothing:  a name is never that shape
	static nodeId( text:string ):NodeId|undefined{
		try{ return NodeId.fromUaString( text ); }
		catch{ return undefined; }
	}
	unavailable( gateway:Gateway, opc:string, e:unknown ):SearchResult{
		const name = this.#opcStore.cnnctnName( gateway.slug, opc );//the node pages' memo, else the slug - never a describe against the gateway that just failed.
		const refused = isRefusal( e );//gateway/search is enforceable:  the index is fine, the user is not allowed.
		return { title: refused ? `Search is not permitted for ${name}` : `Search is unavailable for ${name}`, summary: errorMessage(e), route: [], icon: refused ? 'block' : 'error_outline', rank: 0, source: this.name, disabled: true };
	}
	//The page a hit opens.  Only an Object has a page:  a Variable (or a Method) is a row on its parent's - its value, status and
	//subscribe box - and routing to it showed a false "Not found." or the variable's own children (reviews/m3-closing.md #9).
	//A Variable the crawl found under another Variable still lands on a Variable here;  NodeResolver walks that on up.
	static pagePath( row:NodeSearchRow ):string[]{
		const segments = row.path ? row.path.split( '/' ) : [];//the Objects folder's own path is empty:  the connection's page
		return row.nodeClass==ENodeClass.Object ? segments : segments.slice( 0, -1 );
	}
	//the browse path as the breadcrumbs name it (nodeSegmentName):  `5~pumps/5~pump1` reads pumps/pump1.
	static displayPath( path:string ):string{
		return path.split( '/' ).map( segment=>String(toBrowse(segment, undefined).name) ).join( '/' );
	}
	static icon( nodeClass:number ):string{
		switch( nodeClass ){
			case ENodeClass.Variable: return 'label';
			case ENodeClass.Method: return 'functions';
			default: return 'folder';
		}
	}
}