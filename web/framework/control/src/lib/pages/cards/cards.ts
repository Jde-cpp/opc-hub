import { inject } from '@angular/core';
import { Component, computed, Injectable, OnInit, signal } from "@angular/core";
import { ActivatedRoute, Router, RouterLink, Routes, UrlSegment } from "@angular/router";
import { NgTemplateOutlet } from "@angular/common";
import { MatButtonModule } from "@angular/material/button";
import { MatIconModule } from "@angular/material/icon";
import { RouteItem, IROUTE_SERVICE, IRouteService, RouteService } from "jde-spa";
import { CARD_STATUS, CardStatusValue, statusFor } from './card-status';
import { RecentRow } from './recent-row/recent-row';
import { SectionHeader } from './section-header/section-header';
import { errorText, httpStatus } from '../../utils/errors';


@Injectable( {providedIn: 'root'} )
export class HomeRouteService extends RouteService{
	private router:Router = inject( Router );
	override children():Promise<Routes>{
		let y:Routes = [];
		for( let config of this.router.config.filter(x=> x.title && x.path!.length && x.path!="login" && !x.path!.includes('/')) ){
			const data = config.data ?? {};
			const pageSettings = data["pageSettings"];//the app routes set summary/icon directly; pageSettings is the older shape
			//cards.scss colors the tile per section; the route path is the section name (gateways/access/apps)
			y.push( {title: config.title, path: config.path, data:{ id: config.path, icon: data["icon"] ?? pageSettings?.icon, summary: data["summary"] ?? pageSettings?.summary, cardClass: data["cardClass"] ?? `card-${config.path}` }} );
		}
		return Promise.resolve( y );
	}
}

//the route summary names the page ("Available Gateways"); the title is the fallback, unless it is an unsubstituted parameter like ":gateway"
export function pageHeading( route:ActivatedRoute ):string{
	const summary = <string|undefined>route.snapshot.data["summary"];
	const title = <string|undefined>route.snapshot.title;
	return summary ?? (title?.startsWith(":") ? "" : title ?? "");
}

//Route data `hero` makes the page a landing page:  a banner in place of the plain heading - `lead` + the accented `name` make
//the title, `tagline` the line under it - and big tiles in place of the cards, under `tilesHeading`, each drawing its
//CARD_STATUS figure.
interface CardsHero{ lead:string; name:string; tagline?:string; tilesHeading?:string; }

@Component( {
	templateUrl: './cards.html',
	styleUrls: ['./cards.scss'],
	imports: [MatButtonModule, MatIconModule, NgTemplateOutlet, RecentRow, RouterLink, SectionHeader]
})
export class Cards implements OnInit {
	private route:ActivatedRoute = inject( ActivatedRoute );
	private router:Router = inject( Router );
	private routerService:IRouteService = inject( IROUTE_SERVICE );
	ngOnInit(){
		this.route.url.subscribe( urlSegments=>this.#load(urlSegments) );
	}
	//A rejected docItems() - a refused serverConnections query, a gateway no longer registered, one that is down - used to be
	//an unhandled rejection under "Nothing to show here.", the words for a gateway with no connections;  that showed while
	//loading too, and a failed move to another gateway (the component is reused across ':gateway') left the last one's cards
	//up, linked under the new url (reviews/m3-closing.md #7).  Now every url starts from nothing, a failure is the page's own
	//state, and the generation drops whatever a url the page has left answers late - cards, a failure or a status.
	async #load( urlSegments:UrlSegment[] ){
		const generation = ++this.#generation;
		this.heading.set( pageHeading(this.route) );
		this.hero.set( this.route.snapshot.data["hero"] );
		this.url.set( '/'+this.route.snapshot.pathFromRoot.flatMap( r=>r.url.map(s=>s.path) ).join('/') );
		this.items.set( [] );
		this.statuses.set( {} );
		this.error.set( undefined );
		this.loaded.set( false );
		try{
			const items = await this.routerService.docItems( urlSegments );
			if( generation!=this.#generation )
				return;
			this.items.set( items.filter((x)=>x.path.length && x.path!="login") );
			this.#loadStatuses( this.items(), generation );
		}
		catch( e ){
			if( generation==this.#generation )
				this.error.set( e );
		}
		finally{
			if( generation==this.#generation )
				this.loaded.set( true );
		}
	}
	onRetry(){ this.#load( this.route.snapshot.url ); }
	//Each tile's status settles on its own, so one slow gateway does not hold the other lines back.
	#loadStatuses( items:RouteItem[], generation:number ){
		for( const item of items ){
			const url = this.url().replace( /\/$/, '' )+'/'+item.path;
			statusFor( this.#cardStatus, url )?.status( url ).then(
				value=>{ if( generation==this.#generation ) this.statuses.update( s=>({...s, [item.path]: value}) ); },
				()=>{} );//no rights to the rows, a service down:  the tile keeps its static summary
		}
	}
	heading = signal<string>( "" );
	hero = signal<CardsHero|undefined>( undefined );
	url = signal<string>( '/' );//this page's, '/gateways/gw1'
	section = computed( ()=>this.url().split('/').find( s=>s.length ) ?? '' );//the first segment - the home tile it sits under
	sectionIcon = computed( ()=>this.router.config.find( r=>r.path==this.section() && r.data?.['icon'] )?.data!['icon'] as string|undefined );//for a card with no icon of its own
	items = signal<RouteItem[]>( [] );
	statuses = signal<Record<string,CardStatusValue>>( {} );//by item path
	loaded = signal<boolean>( false );//docItems() has answered for this url - until then an empty list is not "nothing here"
	error = signal<unknown>( undefined );//docItems() was refused or failed
	failure = computed<{kind:"forbidden"|"failed", title:string, detail:string}|undefined>( ()=>{//QLList's two states, for a page of cards
		const e = this.error();
		if( e===undefined )
			return undefined;
		const what = errorText( e )?.replace( /^\(\d+\)/, "" ) ?? "";//errorText prefixes the status; the title already says which
		return httpStatus( e )==403
			? { kind: "forbidden", title: "No access.", detail: `${what}  Ask an administrator for a role that grants it.`.trim() }
			: { kind: "failed", title: "Could not load.", detail: what };
	});
	#cardStatus = inject( CARD_STATUS, {optional: true} ) ?? [];
	#generation = 0;
}
