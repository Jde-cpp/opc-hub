import { inject, Injectable, InjectionToken, signal } from '@angular/core';
import { NavigationError, Router } from '@angular/router';
import { filter } from 'rxjs';
import { RouteItem } from '../pages/component-sidenav/route-item';
import { IPROFILE_SERVICE } from './profile/profile-service';
import { ProfileStore } from './profile/profile-store';

//A site's name for a url segment the router cannot name - neither a route title nor a RouteStore child - ahead of the
//breadcrumbs' title-cased fallback.  jde-opc's names a node segment (`5~pump1` -> pump1).  undefined:  not one of its own.
export type SegmentName = ( segments:string[], index:number )=>string|undefined;
export const SEGMENT_NAME = new InjectionToken<SegmentName>( 'SegmentName' );

//a page the user opened, named as the breadcrumb bar named it then.  section is the url's first segment (the home tile it
//sits under);  path is the two crumbs above the page.
export type RecentVisit = { url:string; title:string; path:string; section:string; icon?:string; at:number };

//The pages the user opened below the top-level sections - a node, a connection, a role, a service, a help topic - newest
//first, no repeats, for the home page's Recently visited row.  Kept in the user's profile like the favorites, so it follows
//them to another browser;  a page that fails to open (deleted since) drops off.
@Injectable( {providedIn: 'root'} )
export class RecentVisits{
	static readonly max = 6;
	static readonly key = 'recentVisits';
	static readonly saveDelay = 2000;//ms - a burst of navigations (walking down a node tree) is one profile write, not one each
	readonly visits = signal<RecentVisit[]>( [] );

	constructor(){
		this.#router.events.pipe( filter((e):e is NavigationError=>e instanceof NavigationError) ).subscribe( e=>this.forget(RecentVisits.bare(e.url)) );
	}
	//the saved history for whoever is signed in now;  the home page awaits it before drawing the row
	load():Promise<void>{ return this.#ready(); }

	//crumbs:  the breadcrumb bar's items for url, Home first.  Only pages below a section count - the sections are the tiles.
	async visit( url:string, crumbs:RouteItem[] ):Promise<void>{
		url = RecentVisits.bare( url );
		const segments = url.split( '/' ).filter( s=>s.length );
		if( segments.length<2 || segments[0]=='login' )
			return;
		const titles = crumbs.slice( 1 ).map( c=>c.title );
		const section = segments[0];
		const sectionIcon = this.#router.config.find( r=>r.path==section && r.data?.['icon'] )?.data!['icon'];
		const visit:RecentVisit = { url, title: titles.at(-1) ?? url, path: titles.slice(-3, -1).join(' › '), section, icon: crumbs.at(-1)?.icon ?? sectionIcon, at: Date.now() };
		await this.#ready();//so a visit made while the history loads lands on top of it rather than being overwritten by it
		this.#set( [visit, ...this.visits().filter(v=>v.url!=url)].slice(0, RecentVisits.max) );
	}
	//A page that no longer opens:  its resolver says so (DetailResolver and the rest catch and redirect, which the router reports
	//as a NavigationCancel), or the navigation errors.  After the load - a deep link to a deleted page fails before anything
	//has read the history, and forgetting from the empty list let the load bring it straight back.
	async forget( url:string ):Promise<void>{
		await this.#ready();
		if( this.visits().some(v=>v.url==url) )
			this.#set( this.visits().filter(v=>v.url!=url) );
	}
	clear():void{ this.#set( [] ); }

	static bare( url:string ):string{ return url.split( /[?#]/ )[0]; }
	//the history saved before this session, behind anything already opened in it
	static merge( session:RecentVisit[], saved:RecentVisit[] ):RecentVisit[]{
		return [...session, ...saved.filter( s=>!session.some(v=>v.url==s.url) )].slice( 0, RecentVisits.max );
	}

	//(Re)loads when the signed-in user changes.  Another user's history is dropped rather than merged;  the pages opened while
	//signed out (login) never count, so a sign-in just picks up the saved list.
	#ready():Promise<void>{
		const user = this.#profile?.userKey() ?? null;
		if( user!==this.#user || !this.#loading ){
			const previous = this.#user;
			this.#user = user;
			this.#loading = this.#store.load<RecentVisit[]>( RecentVisits.key, [] )
				.catch( e=>{ console.warn( 'Could not load the recently visited pages.', e ); return [] as RecentVisit[]; } )
				.then( saved=>this.visits.update( mine=>RecentVisits.merge(previous ? [] : mine, Array.isArray(saved) ? saved.map(v=>this.#rename(v)) : []) ) );
		}
		return this.#loading;
	}
	//A saved entry keeps the names its crumbs had when it was visited;  re-derive the ones SEGMENT_NAME names now, so a page
	//saved as "5~pump1" before the node names existed reads "pump1".  The path's parts are the crumbs of the segments just
	//above the page's own, one each.
	#rename( visit:RecentVisit ):RecentVisit{
		if( !this.#segmentName )
			return visit;
		const segments = visit.url.split( '/' ).filter( s=>s.length );
		const name = ( i:number )=>i>=0 ? this.#segmentName!( segments, i ) : undefined;
		const parts = visit.path ? visit.path.split( ' › ' ) : [];
		const first = segments.length-1-parts.length;
		return { ...visit, title: name(segments.length-1) ?? visit.title, path: parts.map( (p, k)=>name(first+k) ?? p ).join(' › ') };
	}
	#set( visits:RecentVisit[] ):void{
		this.visits.set( visits );
		clearTimeout( this.#saveTimer );
		this.#saveTimer = setTimeout( ()=>this.#store.save( RecentVisits.key, this.visits() )
			.catch( e=>console.warn('Could not save the recently visited pages.', e) ), RecentVisits.saveDelay );
	}
	#loading?:Promise<void>;
	#user:string|null = null;
	#saveTimer?:ReturnType<typeof setTimeout>;
	#profile = inject( IPROFILE_SERVICE, {optional: true} );
	#segmentName = inject( SEGMENT_NAME, {optional: true} );
	#router = inject( Router );
	#store = inject( ProfileStore );
}

//"12 min ago" - the time a recent visit shows, relative to now
export function timeAgo( at:number, now:number ):string{
	const minutes = Math.floor( Math.max(0, now-at)/60_000 );
	if( minutes<1 ) return 'just now';
	if( minutes<60 ) return `${minutes} min ago`;
	const hours = Math.floor( minutes/60 );
	if( hours<24 ) return `${hours} h ago`;
	const days = Math.floor( hours/24 );
	return days==1 ? 'yesterday' : `${days} days ago`;
}
