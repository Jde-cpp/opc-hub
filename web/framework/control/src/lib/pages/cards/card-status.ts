import { inject, Injectable, InjectionToken } from '@angular/core';
import { HELP_TOPICS, helpTopics } from 'jde-spa';

//A page's live figure, for the card that opens it and for the page's own header:
//	- a landing tile draws label "OPC connections", figure 5, detail "on 2 gateways";
//	- a section card draws "5 connections · Local, Line 2" - figure, then label and detail as they stand, so a card's
//	  provider spells its label in the case and number it reads in;
//	- the page's header reads `summary`, the whole of it in one line - "5 OPC connections on 2 gateways".
//warn marks something the reader should look at - nothing running, a gateway that did not answer - and paints it in the
//theme's error colour.
export type CardStatusValue = { label:string; figure:number|string; detail?:string; summary?:string; warn?:boolean };

//One per page, keyed by its url - literal ('/gateways') or with ':param' segments ('/gateways/:gateway'), status() then
//given the url it matched.  Multi-provided:  the library that owns the data exports the provider and the site registers
//it, like SEARCH_PROVIDERS.  A status that rejects - no rights to the rows, the service down - leaves the card its static
//summary and the header without a line.
export interface ICardStatus{
	readonly url:string;
	status( url:string ):Promise<CardStatusValue>;
}
export const CARD_STATUS = new InjectionToken<ICardStatus[]>( 'CARD_STATUS' );

//the provider for a url, if any
export function statusFor( providers:readonly ICardStatus[], url:string ):ICardStatus|undefined{
	const segments = url.split( '/' ).filter( s=>s.length );
	return providers.find( p=>{
		const pattern = p.url.split( '/' ).filter( s=>s.length );
		return pattern.length==segments.length && pattern.every( (s, i)=>s.startsWith(':') || s==segments[i] );
	});
}

export function plural( n:number, noun:string ):string{ return n==1 ? noun : noun+'s'; }
export function counted( n:number, noun:string ):string{ return `${n} ${plural(n, noun)}`; }

//The rows a plural query returns, counted.  Not queryArray:  a server-side custom select can answer a list with {} - the
//access server's GroupAwait did for `groups{ id }` until 2026-09 - and queryArray rejects that as "not an array", which
//would cost the card its whole line.  {} counts as none; anything else that is not a list still rejects.
export async function countRows( service:{ query<Y>( ql:string ):Promise<Y> }, collection:string, field = 'id' ):Promise<number>{
	return (await queryRows( service, collection, field )).length;
}
export async function queryRows<T = Record<string,unknown>>( service:{ query<Y>( ql:string ):Promise<Y> }, collection:string, fields = 'id' ):Promise<T[]>{
	const rows = (await service.query<Record<string,unknown>>( `${collection}{ ${fields} }` ))[collection];
	if( Array.isArray(rows) )
		return rows;
	if( rows && typeof rows=='object' && !Object.keys(rows).length )
		return [];
	throw new Error( `'${collection}' is not a list of rows: ${JSON.stringify(rows)}` );
}

//the Help tile and page:  the registered topics, and the first of them - HELP_TOPICS is in display order, the site's
//overview first.
@Injectable( {providedIn: 'root'} )
export class HelpCardStatus implements ICardStatus{
	readonly url = '/help';
	async status():Promise<CardStatusValue>{
		const n = this.#topics.length;
		return { label: 'Topics', figure: n, detail: n ? `start with ${this.#topics[0].title}` : undefined, summary: counted(n, 'topic') };
	}
	#topics = helpTopics( inject(HELP_TOPICS, {optional: true}) );
}
