import { inject, Injectable, InjectionToken, signal } from '@angular/core';
import { Title } from '@angular/platform-browser';
import { RouterStateSnapshot, TitleStrategy } from '@angular/router';

//the product name the browser tab carries after the page's own ("Gateways · OPC Hub").  Optional:  a site that does not
//provide it gets the bare page name.
export const APP_NAME = new InjectionToken<string>( 'APP_NAME' );
//the navbar brand's mark, beside APP_NAME.  Painted through a CSS mask, so only the image's shape counts and it takes the
//navbar's colour in every theme.
export const APP_LOGO = new InjectionToken<string>( 'APP_LOGO' );

//The browser tab's text.  One writer for both the routes (AppTitleStrategy) and the pages that name themselves
//(ComponentPageTitle), so the suffix is applied in one place and the bare page name stays readable - the favorites
//dialog offers it as a new favorite's name.
@Injectable( {providedIn: 'root'} )
export class DocumentTitle {
	set( page:string ){
		this.page.set( page );
		this.#title.setTitle( page ? (this.#app ? `${page} · ${this.#app}` : page) : this.#app ?? 'Jde' );
	}
	readonly page = signal<string>( '' );
	#title = inject( Title );
	#app = inject( APP_NAME, {optional: true} );
}

//Router titles through DocumentTitle.  A ':param' title (`gateways/:gateway` is titled ":gateway") is the
//substitute-the-segment convention the breadcrumbs use, so the tab gets the parameter's value rather than the literal.
@Injectable( {providedIn: 'root'} )
export class AppTitleStrategy extends TitleStrategy {
	override updateTitle( snapshot:RouterStateSnapshot ):void{
		const title = this.buildTitle( snapshot );
		if( title===undefined )//an untitled route leaves the tab to its page, which names itself through ComponentPageTitle
			return;
		this.#documentTitle.set( title.startsWith(':') ? AppTitleStrategy.param(snapshot, title.substring(1)) ?? title : title );
	}
	static param( snapshot:RouterStateSnapshot, name:string ):string|undefined{
		let value:string|undefined;
		for( let route = snapshot.root.firstChild; route; route = route.firstChild )
			value = route.params[name] ?? value;
		return value;
	}
	#documentTitle = inject( DocumentTitle );
}
