import { inject, Injectable } from '@angular/core';
import { CardStatusValue, counted, countRows, httpStatus, ICardStatus, plural, queryRows } from 'jde-framework';
import { GATEWAY_SERVICE } from './gateway-service';

//the Gateways tile and page:  the registered gateways and the OPC server connections they hold between them - the same
//serverConnections query the /gateways/<gateway> page lists.  A gateway that does not answer is counted and flagged
//rather than failing the whole line.  One that answers 401/403 is up but refused the rows:  it is left out of the line,
//and when every gateway refused, the status rejects so the tile keeps its static summary (the CARD_STATUS contract).
@Injectable( {providedIn: 'root'} )
export class GatewayCardStatus implements ICardStatus{
	readonly url = '/gateways';
	async status():Promise<CardStatusValue>{
		const gateways = await this.#gateways.gateways();
		const results = await Promise.allSettled( gateways.map(g=>countRows(g, 'serverConnections', 'slug')) );
		const isRefusal = ( r:PromiseSettledResult<number> )=>r.status=='rejected' && [401, 403].includes( httpStatus(r.reason) ?? 0 );
		const refusals = results.filter( isRefusal );
		if( refusals.length && refusals.length==results.length )
			throw (<PromiseRejectedResult>refusals[0]).reason;
		const connections = results.reduce( (n, r)=>n + (r.status=='fulfilled' ? r.value : 0), 0 );
		const unreachable = results.filter( r=>r.status=='rejected' ).length - refusals.length;
		const counting = gateways.length - refusals.length;
		const notAnswering = `${counted(unreachable, 'gateway')} not answering`;
		const detail = !gateways.length ? 'no gateway registered' : unreachable ? notAnswering : `on ${counted(counting, 'gateway')}`;
		const summary = !gateways.length ? 'No gateway registered'
			: `${counted(connections, 'OPC connection')} on ${counted(counting, 'gateway')}` + (unreachable ? ` · ${notAnswering}` : '');
		return { label: 'OPC connections', figure: connections, detail, summary, warn: !gateways.length || unreachable>0 };
	}
	#gateways = inject( GATEWAY_SERVICE );
}

//each gateway's card on /gateways, and that gateway's own page:  its OPC server connections, by name.
@Injectable( {providedIn: 'root'} )
export class GatewayConnectionsStatus implements ICardStatus{
	readonly url = '/gateways/:gateway';
	async status( url:string ):Promise<CardStatusValue>{
		const slug = decodeURIComponent( url.split('/').filter(s=>s.length)[1] );
		const gateway = (await this.#gateways.gateways()).find( g=>g.slug==slug ) ?? await this.#gateways.gateway( slug );
		const names = (await queryRows<{name:string}>( gateway, 'serverConnections', 'name' )).map( c=>c.name );
		const detail = names.length>2 ? `${names.slice(0, 2).join(', ')} +${names.length-2}` : names.join( ', ' ) || undefined;
		return { label: plural(names.length, 'connection'), figure: names.length, detail, summary: counted(names.length, 'OPC connection') + (detail ? ` · ${detail}` : '') };
	}
	#gateways = inject( GATEWAY_SERVICE );
}
