import { inject, Injectable } from '@angular/core';
import { ActivatedRouteSnapshot, Resolve, Router, RouterStateSnapshot } from '@angular/router';
import { RecentVisits, RouteItem, RouteStore } from 'jde-spa';
import { SnackbarService } from 'jde-framework';
import { Resource } from '../model/resource';
import { AccessService } from './access-service';

export type ResourcePageData = { resource:Resource; routing:RouteItem; accessUrl:string/*'/access' - the users/groups/roles pages are under it*/ };

//The resource page's row, under `resources/:schema/:slug`:  a resource's natural key is schema, slug and criteria, and the
//slug alone repeats across schemas (`logs` is app's and every OpcServer's), so the url carries both.  Only the criteria-less
//rows have pages - the Resources list shows no other.  The rights are the page's own query, so a refusal is said on the page
//rather than failing the navigation.  The sidenav lists every table resource, not the RouteStore's rows:  a bookmark opens
//with them, and a name shared across schemas is told apart by its schema.
@Injectable()
export class ResourceResolver implements Resolve<ResourcePageData>{
	#access = inject( AccessService );
	#router = inject( Router );
	#snackbar = inject( SnackbarService );
	#recentVisits = inject( RecentVisits );
	#routeStore = inject( RouteStore );

	async resolve( route:ActivatedRouteSnapshot, state:RouterStateSnapshot ):Promise<ResourcePageData>{
		const schema = route.paramMap.get( "schema" )!, slug = route.paramMap.get( "slug" )!;
		const accessUrl = `/${route.parent!.url.map( s=>s.path ).join( '/' )}`;
		const listUrl = `${accessUrl}/resources`;
		const back = ()=>{//not '..', which takes off one segment - '<list>/<schema>' is no page
			this.#router.navigateByUrl( listUrl ).catch( e=>console.warn("The redirect failed.", e) );
			return null as unknown as ResourcePageData;
		};
		let resources:Resource[];
		try{
			resources = await this.#access.loadResources();
		}
		catch( e ){
			this.#snackbar.exception( `Could not load '${schema}/${slug}'.`, e );
			return back();
		}
		const resource = resources.find( r=>r.schema==schema && r.slug==slug );
		if( !resource ){
			this.#snackbar.error( `There is no resource '${schema}/${slug}'.` );
			this.#recentVisits.forget( RecentVisits.bare(state.url) );//the redirect is a NavigationCancel, so RecentVisits never sees the failure
			return back();
		}
		//the last crumb is named from the children kept under the url before it - without this entry .../access/roles was named
		//"Roles" by the children kept under `access` (the section's lists), since its schema segment is `access` too
		this.#routeStore.setChildren( `${listUrl.slice(1)}/${schema}`, [{title: resource.name, path: slug}] );
		const siblings = ResourceResolver.siblings( resources );
		const routing = new RouteItem( {path: ResourceResolver.path(resource), title: resource.name, siblings, parent: new RouteItem({path: listUrl, title: "Resources"})} );
		return { resource, routing, accessUrl };
	}
	static path( resource:Resource ):string{ return `${resource.schema}/${resource.slug}`; }
	//the list's order - schema, then name - and a name more than one schema has gets its schema
	static siblings( resources:Resource[] ):RouteItem[]{
		const counts = new Map<string,number>();
		resources.forEach( r=>counts.set(r.name, (counts.get(r.name) ?? 0)+1) );
		return [...resources]
			.sort( (a,b)=>(a.schema ?? '').localeCompare(b.schema ?? '') || (a.name ?? '').localeCompare(b.name ?? '') )
			.map( r=>new RouteItem({path: ResourceResolver.path(r), title: counts.get(r.name)!>1 ? `${r.name} (${r.schema})` : r.name}) );
	}
}