import { Location } from '@angular/common';
import { inject } from '@angular/core';
import { NavigationError } from '@angular/router';

//A routed page's chunk that didn't load:  the hub restarting, or a tab from before an upgrade asking for a chunk the new build
//doesn't have.  Transient, not the page gone.  Chrome, Firefox and Safari each word it their own way.
export function isChunkLoadError( error:unknown ):boolean{
	return error instanceof TypeError && /dynamically imported module|importing a module script failed/i.test( error.message );
}

//For withNavigationErrorHandler:  a page whose chunk didn't load is opened again as a full page load, so a tab from before an
//upgrade picks up the new build.  Once a minute per url - when the new build lacks the chunk too, the second failure stands
//rather than reloading in a loop - and not at all when sessionStorage can't record that.
export function reloadOnChunkLoadError( reload = ( url:string )=>location.assign( url ) ):( e:NavigationError )=>void{
	return e=>{
		if( !isChunkLoadError(e.error) )
			return;
		const url = inject( Location ).prepareExternalUrl( e.url );
		try{
			const last = JSON.parse( sessionStorage.getItem(chunkReloadKey) ?? 'null' ) as { url:string, at:number }|null;
			if( last?.url==url && Date.now()-last.at<chunkReloadWindow )
				return;
			sessionStorage.setItem( chunkReloadKey, JSON.stringify({url, at: Date.now()}) );
		}
		catch{
			return;
		}
		reload( url );
	};
}
export const chunkReloadKey = 'chunkReload';
const chunkReloadWindow = 60_000;//ms