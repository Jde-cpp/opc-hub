import { inject, Service } from '@angular/core';
import { ProfileStore } from 'jde-spa';
import { CnnctnSlug } from '../model/server-cnnctn';
import { NodeId } from '../model/node-id';
import { WatchItemJson, WatchListJson, WatchParameters, watchProfileKey, WatchRow } from '../model/watch';

//A gateway's watch list as the profile store keeps it (clients spec *Watch list*, *Open questions* #13):  per user, one list per
//gateway, rows from any of its connections with the parameters each asks.  A node page's row menu adds to it from wherever the
//user is, and another tab may hold the list's page:  so the list is read past the cache, and each change is made to the list as
//the server holds it (ProfileStore.update) rather than written whole from what a page read, which dropped a row added since
//(419-watch #9).  Each change THROWS on a failed read or write.
@Service()
export class WatchStore{
	#profileStore = inject( ProfileStore );
	async load( gateway:string ):Promise<WatchItemJson[]>{
		return itemsOf( await this.#profileStore.load<WatchListJson>( watchProfileKey( gateway ), {items: []}, true ) );
	}
	//rows appended with their parameters, each one the list has not got.  Answers how many were new.
	async append( gateway:string, rows:WatchItemJson[] ):Promise<number>{
		let added = 0;
		await this.#update( gateway, items=>{
			const keys = new Set( items.map( i=>itemKey( i ) ) ), fresh:WatchItemJson[] = [];
			for( const row of rows ){
				const key = itemKey( row );
				if( !keys.has( key ) ){
					keys.add( key );
					fresh.push( row );
				}
			}
			added = fresh.length;
			return [...items, ...fresh];
		} );
		return added;
	}
	//a row added from a node page:  appended with the defaults, or left as it is when the list has it - the page is where its
	//parameters are set.  Answers whether it was new.
	async add( gateway:string, opc:CnnctnSlug, node:NodeId, parameters:WatchParameters={} ):Promise<boolean>{
		return await this.append( gateway, [{opc, node: node.toJson(), parameters}] )>0;
	}
	remove( gateway:string, opc:CnnctnSlug, node:NodeId ):Promise<void>{
		const key = WatchRow.keyOf( opc, node );
		return this.#update( gateway, items=>items.filter( i=>itemKey( i )!=key ) );
	}
	//the row's parameters replaced - or the row appended, where another tab removed it meanwhile:  the page editing it shows it
	setParameters( gateway:string, opc:CnnctnSlug, node:NodeId, parameters:WatchParameters ):Promise<void>{
		const key = WatchRow.keyOf( opc, node );
		return this.#update( gateway, items=>items.some( i=>itemKey( i )==key )
			? items.map( i=>itemKey( i )==key ? {...i, parameters} : i )
			: [...items, {opc, node: node.toJson(), parameters}] );
	}
	#update( gateway:string, change:( items:WatchItemJson[] )=>WatchItemJson[] ):Promise<void>{
		return this.#profileStore.update<WatchListJson>( watchProfileKey( gateway ), {items: []}, json=>({items: change( itemsOf( json ) )}) );
	}
}
function itemsOf( json:WatchListJson|undefined ):WatchItemJson[]{ return Array.isArray( json?.items ) ? json.items.filter( i=>i && typeof i.opc=='string' && i.node ) : []; }
function itemKey( item:WatchItemJson ):string{ return WatchRow.keyOf( item.opc, new NodeId( item.node ) ); }