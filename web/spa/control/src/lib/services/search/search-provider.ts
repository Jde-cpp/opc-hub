import { InjectionToken } from '@angular/core';
import { Params } from '@angular/router';

//One hit in the navbar search.  `route` is what the router navigates to - a url string, or a commands array when the segments
//need encoding (opc browse paths).  `prefix` renders as `user:alice` and is also the scope the provider answers to.
export type SearchResult = {
	title:string;
	route:string|any[];
	queryParams?:Params;
	icon?:string;
	summary?:string;
	prefix?:string;
	rank?:number;//0 best - providers rank starts-with ahead of contains;  ties fall back to registration order, then title.
	source:string;//the provider's name.
	disabled?:boolean;//a notice, not a hit - shown, never navigated to.
};

//jde-spa owns the navbar but sits below the libraries that own the data (routes here, users/roles in jde-access, opc nodes in
//jde-opc), so the providers reach it through this multi token - the 'IProfileService' pattern.  Registration order is result
//precedence.  `prefixes` are the `<prefix>:` scopes a provider answers; a typed `user:al` only asks providers listing 'user'.
export interface ISearchProvider{
	readonly name:string;
	readonly prefixes?:string[];
	search( text:string, scope:string|undefined, limit:number ):Promise<SearchResult[]>;
}
export const SEARCH_PROVIDERS = new InjectionToken<ISearchProvider[]>( 'ISearchProvider' );

export function searchRank( text:string, title:string, summary?:string ):number{//0 title starts with, 1 title contains, 2 summary contains, 3 miss.
	const t = title.toLowerCase();
	return t.startsWith( text ) ? 0 : t.includes( text ) ? 1 : summary?.toLowerCase().includes( text ) ? 2 : 3;
}
