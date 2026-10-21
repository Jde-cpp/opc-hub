import { inject } from '@angular/core';
import { Component, computed, OnInit, signal } from "@angular/core";
import { ActivatedRoute, Router, RouterLink, UrlSegment } from "@angular/router";
import { NgTemplateOutlet } from "@angular/common";
import { MatIconModule } from "@angular/material/icon";
import { RouteItem, IROUTE_SERVICE, Latest } from "jde-spa";
import { CARD_STATUS, CardStatusValue, statusFor } from './card-status';
import { RecentRow } from './recent-row/recent-row';
import { SectionHeader } from './section-header/section-header';
import { Failure, failureState } from '../../utils/errors';
import { FailureState } from '../../shared/failure-state/failure-state';
import { pageHeading } from '../page-heading';

//Route data `hero` makes the page a landing page:  a banner in place of the plain heading - `lead` + the accented `name` make
//the title, `tagline` the line under it - and big tiles in place of the cards, under `tilesHeading`, each drawing its
//CARD_STATUS figure.
interface CardsHero{ lead:string; name:string; tagline?:string; tilesHeading?:string; }

@Component( {
	templateUrl: './cards.html',
	styleUrls: ['./cards.scss'],
	imports: [FailureState, MatIconModule, NgTemplateOutlet, RecentRow, RouterLink, SectionHeader]
})
export class Cards implements OnInit {
	#route = inject( ActivatedRoute );
	#router = inject( Router );
	#routerService = inject( IROUTE_SERVICE );
	ngOnInit(){
		this.#route.url.subscribe( urlSegments=>this.#load(urlSegments) );
	}
	//A rejected docItems() - a refused serverConnections query, a gateway no longer registered, one that is down - used to be
	//an unhandled rejection under "Nothing to show here.", the words for a gateway with no connections;  that showed while
	//loading too, and a failed move to another gateway (the component is reused across ':gateway') left the last one's cards
	//up, linked under the new url (reviews/m3-closing.md #7).  Now every url starts from nothing, a failure is the page's own
	//state, and the generation drops whatever a url the page has left answers late - cards, a failure or a status.
	async #load( urlSegments:UrlSegment[] ){
		const generation = this.#latest.next();
		this.heading.set( pageHeading(this.#route) );
		this.hero.set( this.#route.snapshot.data["hero"] );
		this.url.set( '/'+this.#route.snapshot.pathFromRoot.flatMap( r=>r.url.map(s=>s.path) ).join('/') );
		this.items.set( [] );
		this.statuses.set( {} );
		this.error.set( undefined );
		this.loaded.set( false );
		try{
			const items = await this.#routerService.docItems( urlSegments );
			if( !this.#latest.is(generation) )
				return;
			this.items.set( items.filter((x)=>x.path.length && x.path!="login") );
			this.#loadStatuses( this.items(), generation );
		}
		catch( e ){
			if( this.#latest.is(generation) )
				this.error.set( e );
		}
		finally{
			if( this.#latest.is(generation) )
				this.loaded.set( true );
		}
	}
	onRetry(){ this.#load( this.#route.snapshot.url ); }
	//Each tile's status settles on its own, so one slow gateway does not hold the other lines back.
	#loadStatuses( items:RouteItem[], generation:number ){
		for( const item of items ){
			const url = this.url().replace( /\/$/, '' )+'/'+item.path;
			statusFor( this.#cardStatus, url )?.status( url ).then(
				value=>{ if( this.#latest.is(generation) ) this.statuses.update( s=>({...s, [item.path]: value}) ); },
				()=>{} );//no rights to the rows, a service down:  the tile keeps its static summary
		}
	}
	heading = signal<string>( "" );
	hero = signal<CardsHero|undefined>( undefined );
	url = signal<string>( '/' );//this page's, '/gateways/gw1'
	section = computed( ()=>this.url().split('/').find( s=>s.length ) ?? '' );//the first segment - the home tile it sits under
	sectionIcon = computed( ()=>this.#router.config.find( r=>r.path==this.section() && r.data?.['icon'] )?.data!['icon'] as string|undefined );//for a card with no icon of its own
	items = signal<RouteItem[]>( [] );
	statuses = signal<Record<string,CardStatusValue>>( {} );//by item path
	loaded = signal<boolean>( false );//docItems() has answered for this url - until then an empty list is not "nothing here"
	error = signal<unknown>( undefined );//docItems() was refused or failed
	failure = computed<Failure|undefined>( ()=>failureState(this.error()) );//QLList's two states, for a page of cards
	#cardStatus = inject( CARD_STATUS, {optional: true} ) ?? [];
	#latest = new Latest();
}