import { inject, Injectable } from '@angular/core';
import { Router } from '@angular/router';
import { ISearchProvider, SearchResult } from 'jde-spa';
import { ENodeClass } from '../model/node';
import { toBrowse } from '../model/types';
import { Gateway, GATEWAY_SERVICE, GatewayService } from './gateway-service';

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

	static readonly columns = '{ connection{ slug name } path name nodeClass depth }';
	static readonly currentConnection = /^\/gateways\/([^/?#]+)\/([^/?#]+)/;//app.routes.ts: gateways/:gateway/:connection/**

	async search( text:string, scope:string|undefined, limit:number ):Promise<SearchResult[]>{
		if( !text.length )
			return [];
		const hits:{ gateway:Gateway; row:NodeSearchRow }[] = [];
		const current = NodeSearchProvider.currentConnection.exec( this.#router.url );
		if( current ){
			const gateway = await this.gatewayService.gateway( decodeURIComponent(current[1]) );
			const rows = await gateway.queryArray<NodeSearchRow>( `search( opc:$opc, text:$text, limit:$limit )${NodeSearchProvider.columns}`, {opc: decodeURIComponent(current[2]), text, limit} );
			hits.push( ...rows.map( row=>({gateway, row}) ) );
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
