import { inject, Injectable } from '@angular/core';
import { catchError, from, map, mergeMap, Observable, of, scan, startWith } from 'rxjs';
import { ISearchProvider, SEARCH_PROVIDERS, SearchResult } from './search-provider';

//Fans a query out to every registered ISearchProvider and streams the merged, ranked list - each provider's hits land as they
//arrive, so route and access hits show while a first-time opc node crawl is still running.
@Injectable({ providedIn: 'root' })
export class SearchService{
	#providers:ISearchProvider[] = inject( SEARCH_PROVIDERS, {optional: true} ) ?? [];

	get prefixes():string[]{ return this.#providers.flatMap( p=>p.prefixes ?? [] ); }

	//`user:al` -> {scope:'user', text:'al'} when some provider lists the prefix;  otherwise the colon is literal text.
	parse( raw:string ):{ scope?:string; text:string }{
		const trimmed = raw.trim();
		const m = /^([a-z]+):(.*)$/i.exec( trimmed );
		if( m && this.prefixes.includes(m[1].toLowerCase()) )
			return { scope: m[1].toLowerCase(), text: m[2].trim().toLowerCase() };
		return { text: trimmed.toLowerCase() };
	}

	search( raw:string, limit=20 ):Observable<SearchResult[]>{
		const { scope, text } = this.parse( raw );
		if( !text.length && !scope )
			return of( [] );
		const providers = this.#providers.filter( p=>!scope || p.prefixes?.includes(scope) );
		const order = new Map( this.#providers.map( (p,i)=>[p.name, i] as [string,number] ) );
		return from( providers ).pipe(
			mergeMap( p=>from( Promise.resolve().then(()=>p.search(text, scope, limit)) ).pipe(//the then() also turns a synchronous throw into a rejection.
				catchError( e=>{ console.warn( `search provider '${p.name}' failed.`, e ); return of( [] as SearchResult[] ); } )
			) ),
			scan( (acc, results)=>SearchService.merge( acc, results, order ), [] as SearchResult[] ),
			map( all=>all.slice(0, limit) ),
			startWith( [] as SearchResult[] )
		);
	}

	//A hit, not just the page it lands on:  every resource hit opens the one resources list, and keyed by route alone the merge
	//kept the first and dropped the rest (reviews/m3-closing.md #26).  Not the prefix:  RouteSearchProvider repeats the users,
	//groups and roles a list page parked in RouteStore - same page, same name, no prefix - and those are still one row.
	static key( r:SearchResult ):string{
		return (Array.isArray(r.route) ? r.route.join('/') : r.route) + (r.queryParams ? JSON.stringify(r.queryParams) : '') + '\n' + r.title;
	}
	static merge( acc:SearchResult[], results:SearchResult[], order:Map<string,number> ):SearchResult[]{
		const seen = new Set( acc.map(SearchService.key) );
		const merged = [...acc];
		for( const r of results ){
			const key = SearchService.key( r );
			if( seen.has(key) )
				continue;
			seen.add( key );
			merged.push( r );
		}
		return merged.sort( (a,b)=>(a.rank ?? 0)-(b.rank ?? 0) || (order.get(a.source) ?? 0)-(order.get(b.source) ?? 0) || a.title.localeCompare(b.title) );
	}
}
