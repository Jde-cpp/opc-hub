import { inject, Injectable } from '@angular/core';
import { ActivatedRouteSnapshot, createUrlTreeFromSnapshot, Resolve, Router, RouterStateSnapshot } from '@angular/router';
import { RecentVisits, RouteItem, RouteStore } from 'jde-spa';
import { Calendar } from '../../model/calendar';
import { calendarListRoute, calendarsPath } from '../../pages/apps/calendars/calendar-list-route';
import { SnackbarService } from '../../shared/snackbar/snackbar-service';
import { AppService } from '../app/app-service';
import { DetailResolver, DetailResolverData, DetailRoute, SlugNotFoundError } from '../detail-resolver';

//A calendar's row for its page (#396), under an instance page's `calendars/:slug` route, as HistAdminResolver resolves a historian
//group's:  the row is DetailResolver's query of the application server's table, and the siblings are the rows the page's list put
//in the RouteStore under `<page url>/calendars` (CalendarList).  The sidenav's parent is the page itself ('.'), so a sibling link is
//`./calendars/<slug>` from the sidenav's route, which is the page's.  In main with its route, so it imports nothing of the page.
@Injectable()
export class CalendarResolver implements Resolve<DetailResolverData<Calendar>>{
	#router = inject( Router );
	#app = inject( AppService );
	#routeStore = inject( RouteStore );
	#snackbar = inject( SnackbarService );
	#recentVisits = inject( RecentVisits );

	resolve( route:ActivatedRouteSnapshot, state:RouterStateSnapshot ):Promise<DetailResolverData<Calendar>>{ return this.#load( route, state.url ); }

	async #load( route:ActivatedRouteSnapshot, url:string ):Promise<DetailResolverData<Calendar>>{
		const slug = route.paramMap.get( 'slug' )!;
		const parent = route.parent!;
		const instance = parent.url[parent.url.length-1].path;
		const parentUrl = parent.url.map( s=>s.path ).join( '/' );
		try{
			const list = calendarListRoute();
			const siblings = this.#routeStore.getChildren( `${parentUrl}/${calendarsPath}` ).map( s=>new RouteItem( {path: `${calendarsPath}/${s.path}`, title: s.title} ) );
			const routing = new DetailRoute( `${calendarsPath}/${slug}`, siblings.find( s=>s.path==`${calendarsPath}/${slug}` )?.title, siblings, new RouteItem( {path: '.', title: instance} ) );
			routing.tableSettings = list.tableSettings;
			return await DetailResolver.load<Calendar>( this.#app, list.collectionName, slug, routing );
		}
		catch( e ){
			if( e instanceof SlugNotFoundError ){
				this.#snackbar.error( e.message );
				this.#recentVisits.forget( RecentVisits.bare( url ) );//the redirect is a NavigationCancel, so RecentVisits never sees the failure
			}
			else
				this.#snackbar.exception( `Could not load '${slug}'`, e );
			this.#router.navigateByUrl( createUrlTreeFromSnapshot( route, ['..'] ) ).catch( e=>console.warn( "The redirect failed.", e ) );//<page>/calendars, which redirects to the page
			return null as unknown as DetailResolverData<Calendar>;
		}
	}
}