import { inject, Injectable } from '@angular/core';
import { Router } from '@angular/router';
import { ISearchProvider, SearchResult } from 'jde-spa';
import { errorText, httpStatus } from 'jde-framework';
import { ENodeClass } from '../model/node';
import { toBrowse } from '../model/types';
import { Gateway, GATEWAY_SERVICE, GatewayService } from './gateway-service';
import { OPC_STORE, OpcStore } from './opc-store';

export type NodeSearchRow = { connection:{ slug:string; name:string }; path:string; name:string; nodeClass:number; depth:number };

//OPC node names through the gateway's `search` query.  Inside a connection's node tree only that connection is searched;
//anywhere else every gateway is asked without an `opc`, which answers from the clients this session already holds - a search
//never opens an OPC session.  Hits render as name over `<connection>/<browse path>`.
@Injectable({ providedIn: 'root' })
export class NodeSearchProvider implements ISearchProvider{
	readonly name = 'nodes';
	readonly prefixes = [ 'node' ];
	#router = inject( Router );
	private gatewayService:GatewayService = inject( GATEWAY_SERVICE );
	private opcStore:OpcStore = inject( OPC_STORE );

	static readonly columns = '{ connection{ slug name } path name nodeClass depth }';
	static readonly currentConnection = /^\/gateways\/([^/?#]+)\/([^/?#]+)/;//app.routes.ts: gateways/:gateway/:connection/**

	async search( text:string, scope:string|undefined, limit:number ):Promise<SearchResult[]>{
		if( !text.length )
			return [];
		const hits:{ gateway:Gateway; row:NodeSearchRow }[] = [];
		const current = NodeSearchProvider.currentConnection.exec( this.#router.url );
		if( current ){
			const gateway = await this.gatewayService.gateway( decodeURIComponent(current[1]) );
			const opc = decodeURIComponent( current[2] );
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
			const gateways = await this.gatewayService.gateways();
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
			rank: row.name.toLowerCase().startsWith( text ) ? 0 : 1,
			source: this.name
		}) );
	}
	unavailable( gateway:Gateway, opc:string, e:unknown ):SearchResult{
		const name = this.opcStore.cnnctnName( gateway.slug, opc );//the node pages' memo, else the slug - never a describe against the gateway that just failed.
		const refused = [401, 403].includes( httpStatus(e) ?? 0 );//gateway/search is enforceable:  the index is fine, the user is not allowed.
		return { title: refused ? `Search is not permitted for ${name}` : `Search is unavailable for ${name}`, summary: errorText(e) ?? 'Unknown error', route: [], icon: refused ? 'block' : 'error_outline', rank: 0, source: this.name, disabled: true };
	}
	//The page a hit opens.  Only an Object has a page:  a Variable (or a Method) is a row on its parent's - its value, status and
	//subscribe box - and routing to it showed a false "Not found." or the variable's own children (reviews/m3-closing.md #9).
	//A Variable the crawl found under another Variable still lands on a Variable here;  NodeResolver walks that on up.
	static pagePath( row:NodeSearchRow ):string[]{
		const segments = row.path.split( '/' );
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
