import { inject, Injectable } from '@angular/core';
import { ProfileStore } from 'jde-spa';
import { CnnctnSlug } from '../model/server-cnnctn';
import { NodeId } from '../model/node-id';
import { WatchItemJson, WatchListJson, WatchParameters, watchProfileKey, WatchRow } from '../model/watch';

//A gateway's watch list as the profile store keeps it (clients spec *Watch list*, *Open questions* #13):  per user, one list per
//gateway, rows from any of its connections with the parameters each asks.  The page reads and writes it whole;  a node page's row
//menu adds to it from wherever the user is, so the page finds the row when it opens.
@Injectable( {providedIn: 'root'} )
export class WatchStore{
	#profileStore = inject( ProfileStore );
	async load( gateway:string ):Promise<WatchItemJson[]>{
		const json = await this.#profileStore.load<WatchListJson>( watchProfileKey( gateway ), {items: []} );
		return Array.isArray( json?.items ) ? json.items.filter( i=>i && typeof i.opc=='string' && i.node ) : [];
	}
	//THROWS on a failed write, as ProfileStore.save does
	save( gateway:string, items:WatchItemJson[] ):Promise<void>{
		return this.#profileStore.save<WatchListJson>( watchProfileKey( gateway ), {items} );
	}
	//a row added from a node page:  appended with the defaults, or left as it is when the list has it - the page is where its
	//parameters are set.  Answers whether it was new.
	async add( gateway:string, opc:CnnctnSlug, node:NodeId, parameters:WatchParameters={} ):Promise<boolean>{
		const items = await this.load( gateway );
		const key = WatchRow.keyOf( opc, node );
		if( items.some( i=>WatchRow.keyOf( i.opc, new NodeId( i.node ) )==key ) )
			return false;
		items.push( {opc, node: node.toJson(), parameters} );
		await this.save( gateway, items );
		return true;
	}
}