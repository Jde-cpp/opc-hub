import { Injectable } from "@angular/core";
import { UrlSegment } from "@angular/router";
import { RouteItem } from "../pages/component-sidenav/route-item";

export type RouteKey = string | readonly (string|UrlSegment)[];

//A url's children - a list's rows, a cards page's tiles - which the sidenav, the breadcrumb and the search box read back.
@Injectable({ providedIn: 'root' })
export class RouteStore{
	getChildren( url:RouteKey ):RouteItem[]{
		const key = RouteStore.key( url );
		return this.#children.get( key ) ?? this.loadChildren( key );
	}

	setChildren( url:RouteKey, children:readonly Partial<RouteItem>[] ){//copied into RouteItems:  a list's rows are passed as plain {title, path}
		const key = RouteStore.key( url );
		this.#children.set( key, children.map(s=>{return new RouteItem({title:s.title, path:s.path, icon:s.icon})}) );
		localStorage.setItem( key, JSON.stringify(this.#children.get(key)) );
		this.#rememberKey( key );
	}
	//The one spelling of a key:  the parent's absolute url with no leading or trailing slash - 'access/users',
	//'apps/gateways/opchub'.  A writer passes whatever it holds, a url or a route's segments, and its children's paths are relative
	//to it, so a reader joins the two.  A bare 'users' was the list's own segment, which the readers had to guess the section of.
	static key( url:RouteKey ):string{
		const path = typeof url=="string" ? url : url.map( s=>typeof s=="string" ? s : s.path ).join( '/' );
		return path.split( '/' ).filter( s=>s.length ).join( '/' );
	}

	//Every parent url with cached children - the navbar search enumerates them.  localStorage has no prefix to list by, so the
	//keys are kept in their own entry and hydrated through loadChildren on first use.
	entries():ReadonlyMap<string,RouteItem[]>{
		for( const key of this.#keys() ){
			if( !this.#children.has(key) )
				this.loadChildren( key );
		}
		return this.#children;
	}
	#keys():string[]{
		try{
			const keys = JSON.parse( localStorage.getItem(RouteStore.keysKey) ?? '[]' );
			return Array.isArray( keys ) ? keys.filter( (k:unknown)=>typeof k=='string' ) : [];
		}
		catch{
			return [];
		}
	}
	static readonly keysKey = 'routeStore.keys';

	private loadChildren( key:string ):RouteItem[]{
		let storage = localStorage.getItem( key );
		let children:RouteItem[] = [];
		if( storage ){
			children = JSON.parse(storage).map( (s:Partial<RouteItem>)=>new RouteItem(s) );
			this.#children.set( key, children );
			this.#rememberKey( key );//written before the key list existed - entries() sees it from now on
		}
		return children;
	}
	//Logout has to drop this:  the cached children are the NAMES of the rows the last user browsed - users, roles, groups,
	//gateway connections - and the navbar search reads them straight back out of localStorage for whoever logs in next.
	//Best effort by design:  a key written before the key list existed is only reachable once loadChildren has hydrated it,
	//so those go with the in-memory map here and with the rest of the list the next time they are read.
	clear():void{
		for( const key of this.#keys() ){
			try{ localStorage.removeItem( key ); }
			catch( e ){ console.warn( `Could not clear localStorage['${key}']:`, e ); }
		}
		try{ localStorage.removeItem( RouteStore.keysKey ); }
		catch( e ){ console.warn( `Could not clear localStorage['${RouteStore.keysKey}']:`, e ); }
		this.#children.clear();
	}

	#rememberKey( key:string ):void{
		const keys = this.#keys();
		if( !keys.includes(key) )
			localStorage.setItem( RouteStore.keysKey, JSON.stringify([...keys, key]) );
	}

	#children:Map<string,RouteItem[]> = new Map<string,RouteItem[]>();//RouteStore.key, children relative to it
}