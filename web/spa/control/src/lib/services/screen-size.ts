import { Service, Signal, inject, signal } from '@angular/core';
import { toSignal } from '@angular/core/rxjs-interop';
import { BreakpointObserver } from '@angular/cdk/layout';
import { map } from 'rxjs';

//The breakpoint TypeScript reads - `small`, where the shell's sidenav becomes a drawer (ComponentSidenav).  The width is
//constants.scss's, published by _general.scss as --jde-breakpoint-small so the number is written once;  a document without that
//sheet (a spec, a site that never @uses general) is never small, and says so once.
@Service()
export class ScreenSize{
	#observer = inject( BreakpointObserver );//before isSmall:  its initialiser reads it
	readonly isSmall:Signal<boolean> = this.#observe( 'small' );

	#observe( name:string ):Signal<boolean>{
		const width = getComputedStyle( document.documentElement ).getPropertyValue( `--jde-breakpoint-${name}` ).trim();
		if( !width ){
			console.warn( `--jde-breakpoint-${name} is not set (styles/_general.scss is not in the document):  the screen is never ${name}.` );
			return signal( false ).asReadonly();
		}
		return toSignal( this.#observer.observe( `(max-width: ${width})` ).pipe( map( state=>state.matches ) ), {requireSync: true} );
	}
}