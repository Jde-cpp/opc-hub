import { inject, Injectable } from '@angular/core';
import { Router, Routes } from '@angular/router';
import { RouteStore } from '../route-store';
import { matchLiterals } from '../route-utils';
import { capitalize } from '../text';
import { ISearchProvider, searchRank, SearchResult } from './search-provider';

type CollectionItem = string | { path:string; title?:string; data?:{ icon?:string; summary?:string } };

//Router items:  the static route tree (titles, data.summary/icon) plus the children the pages discovered at runtime and parked
//in RouteStore - gateways, connections, app instances, access rows.  No prefix.
@Injectable({ providedIn: 'root' })
export class RouteSearchProvider implements ISearchProvider{
	readonly name = 'routes';
	#router = inject( Router );
	#routeStore = inject( RouteStore );

	async search( text:string, scope:string|undefined, limit:number ):Promise<SearchResult[]>{
		if( scope || !text.length )
			return [];
		const items = this.items();
		return items
			.map( item=>({ ...item, rank: searchRank(text, item.title, item.summary) }) )
			.filter( item=>item.rank<3 )
			.sort( (a,b)=>a.rank-b.rank || a.title.localeCompare(b.title) )
			.slice( 0, limit );
	}

	items():SearchResult[]{
		const out = new Map<string,SearchResult>();
		this.#walk( this.#router.config, '', out );
		this.#dynamic( out );
		return [...out.values()];
	}

	#walk( routes:Routes, parentUrl:string, out:Map<string,SearchResult> ):void{
		for( const route of routes ){
			const path = route.path ?? '';
			if( path=='**' || path=='login' )
				continue;
			const segments = path.split('/').filter( s=>s.length );
			const url = parentUrl+(segments.length ? '/'+segments.join('/') : '') || '/';
			if( segments.some(s=>s.startsWith(':')) ){//not enumerable - except the ':collectionDisplay' list route, whose data.collections says what it stands for.
				const collections = route.data?.['collections'] as CollectionItem[]|undefined;
				if( segments.length==1 && collections ){
					for( const c of collections ){
						const item = typeof c=='string' ? { path: c } : c;
						const title = item.title ?? capitalize( item.path );
						const cUrl = `${parentUrl}/${item.path}`;
						if( !out.has(cUrl) )
							out.set( cUrl, { title, route: cUrl, icon: item.data?.icon, summary: item.data?.summary ?? RouteSearchProvider.parentSummary(parentUrl), source: this.name } );
					}
				}
				continue;
			}
			const title = typeof route.title=='string' ? route.title : undefined;
			if( title && !out.has(url) )
				out.set( url, { title, route: url, icon: route.data?.['icon'], summary: route.data?.['summary'], source: this.name } );
			if( route.children )
				this.#walk( route.children, url, out );
		}
	}

	//A RouteStore key is the parent's absolute url and a child's path is relative to it (RouteStore.key), so the child's url is
	//the two joined.  One the router does not match - an entry cached before its page went - is not offered.
	#dynamic( out:Map<string,SearchResult> ):void{
		for( const [key, children] of this.#routeStore.entries() ){
			for( const child of children ){
				if( !child?.title || !child.path )
					continue;
				const segments = `${key}/${child.path}`.split( '/' ).filter( s=>s.length );
				if( matchLiterals(this.#router.config, segments)<0 )
					continue;
				const url = '/'+segments.map( s=>encodeURIComponent(s) ).join('/');
				if( !out.has(url) )
					out.set( url, { title: child.title, route: ['/'+segments[0], ...segments.slice(1)], icon: child.icon, summary: RouteSearchProvider.parentSummary('/'+segments.slice(0, -1).join('/')), source: this.name } );
			}
		}
	}
	private static parentSummary( parentUrl:string ):string|undefined{
		const segments = parentUrl.split('/').filter( s=>s.length );
		return segments.length ? segments.join(' / ') : undefined;
	}
}