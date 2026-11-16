import { inject, Injectable } from '@angular/core';
import { ActivatedRouteSnapshot, createUrlTreeFromSnapshot, Resolve, Router, RouterStateSnapshot } from '@angular/router';
import { RecentVisits, RouteItem, RouteStore } from 'jde-spa';
import { AppService, CalendarRef, CalendarService, DetailRoute, SlugNotFoundError, SnackbarService } from 'jde-framework';
import { OeeFloor, OeeFloorShape } from '../../model/oee-floor-rows';
import { OeeLineRef, OeePlant } from '../../model/oee-lines';
import { Gateway, GATEWAY_SERVICE } from '../gateway-service';
import { OeeFloorService } from '../oee-floor-service';

//The floor page's data (#481, docs/demo/spec.md *Drawing*), `lines/:slug/floor` under the gateway's page, of HistAdminResolver's
//shape:  the gateway the url names, the line by its slug, its floor, the floor's shapes, the plant - the machines, lines and
//memberships the map draws the line's reach from - and the line's calendar for the plant's zone, UTC for a line with none or
//one the application server doesn't list.  A line with no floor lands on its OEE page, saying so;  a slug that isn't a line's
//says so and lands on the gateway's page, as the historian's pages do.  The sidenav's siblings are the floors of the other lines
//the page's Lines tab listed.
export type OeeFloorPageData = {
	gateway:Gateway; routing:RouteItem; line:OeeLineRef & {calendarId?:number}; floor:OeeFloor; shapes:OeeFloorShape[]; plant:OeePlant;
	zone:string; calendar?:CalendarRef;
};

@Injectable()
export class OeeFloorResolver implements Resolve<OeeFloorPageData>{
	#router = inject( Router );
	#gatewayService = inject( GATEWAY_SERVICE );
	#routeStore = inject( RouteStore );
	#snackbar = inject( SnackbarService );
	#recentVisits = inject( RecentVisits );
	#service = inject( OeeFloorService );
	#app = inject( AppService );
	#calendars = inject( CalendarService );

	async resolve( route:ActivatedRouteSnapshot, state:RouterStateSnapshot ):Promise<OeeFloorPageData>{
		const slug = route.paramMap.get( 'slug' )!, parent = route.parent!;
		const instance = parent.url[parent.url.length-1].path, parentUrl = parent.url.map( s=>s.path ).join( '/' );
		try{
			const gateway = await this.#gatewayService.gateway( instance );
			const [line, floors] = await Promise.all( [this.#service.line( gateway, slug ), this.#service.floors( gateway )] );
			if( !line )
				throw new SlugNotFoundError( slug );
			const floor = floors.find( f=>f.lineId==line.id );
			if( !floor ){
				this.#snackbar.info( `'${line.name}' has no floor.` );
				this.#router.navigateByUrl( createUrlTreeFromSnapshot( route, ['../oee'] ) );
				return null as unknown as OeeFloorPageData;
			}
			const [shapes, plant, calendar] = await Promise.all( [this.#service.floorShapes( gateway, floor.id ), this.#service.plant( gateway ), this.#calendar( line.calendarId )] );
			const floored = new Set( plant.lines.filter( l=>floors.some( f=>f.lineId==l.id ) ).map( l=>l.slug ) );
			const siblings = this.#routeStore.getChildren( `${parentUrl}/lines` ).filter( s=>floored.has( s.path ) ).map( s=>new RouteItem( {path: `lines/${s.path}/floor`, title: s.title} ) );
			const path = `lines/${slug}/floor`;
			const routing = new DetailRoute( path, siblings.find( s=>s.path==path )?.title ?? line.name, siblings, new RouteItem( {path: '.', title: instance} ) );
			return { gateway, routing, line, floor, shapes, plant, zone: calendar?.timeZone || 'UTC', calendar };
		}
		catch( e ){
			if( e instanceof SlugNotFoundError ){
				this.#snackbar.error( e.message );
				this.#recentVisits.forget( RecentVisits.bare( state.url ) );
			}
			else
				this.#snackbar.exception( `Could not load the floor of '${slug}'`, e );
			this.#router.navigateByUrl( createUrlTreeFromSnapshot( route, ['../..'] ) );
			return null as unknown as OeeFloorPageData;
		}
	}
	//the line's calendar, from the application server's list;  none for a line with none, or a list the server doesn't give
	async #calendar( id:number|undefined ):Promise<CalendarRef|undefined>{
		if( id==undefined )
			return undefined;
		try{
			return ( await this.#calendars.calendars( this.#app ) ).find( c=>c.id==id );
		}
		catch( e ){
			console.warn( "Could not list the calendars:  the floor's clock is UTC.", e );
			return undefined;
		}
	}
}