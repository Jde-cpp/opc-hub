import { DestroyRef, Service, Signal, inject, signal } from '@angular/core';
import { takeUntilDestroyed } from '@angular/core/rxjs-interop';
import { BreakpointObserver } from '@angular/cdk/layout';

//The breakpoints TypeScript reads - `small`, where the shell's sidenav becomes a drawer (ComponentSidenav) and the navbar's
//search takes a row of its own, and `extra-small`, where a list toolbar's view group does.  The widths are constants.scss's,
//published by _general.scss as --jde-breakpoint-* so each number is written once.  A sheet still loading when the service is
//made (a non-blocking styles.css) is read again on the window's load;  a document without the sheet (a spec, a site that never
//@uses general) is never either, and says so once for each.
@Service()
export class ScreenSize{
	#observer = inject( BreakpointObserver );//before the signals:  their initialisers read it
	#destroyRef = inject( DestroyRef );
	readonly isSmall:Signal<boolean> = this.#observe( 'small' );
	readonly isExtraSmall:Signal<boolean> = this.#observe( 'extra-small' );

	#observe( name:string ):Signal<boolean>{
		const matches = signal( false );
		const follow = ()=>{
			const width = getComputedStyle( document.documentElement ).getPropertyValue( `--jde-breakpoint-${name}` ).trim();
			if( width )
				this.#observer.observe( `(max-width: ${width})` ).pipe( takeUntilDestroyed(this.#destroyRef) ).subscribe( state=>matches.set(state.matches) );
			return width.length>0;
		};
		const never = ()=>console.warn( `--jde-breakpoint-${name} is not set (styles/_general.scss is not in the document):  the screen is never ${name}.` );
		if( !follow() ){
			if( document.readyState==='complete' )
				never();
			else{
				const onLoad = ()=>{ if( !follow() ) never(); };
				window.addEventListener( 'load', onLoad, {once: true} );
				this.#destroyRef.onDestroy( ()=>window.removeEventListener('load', onLoad) );
			}
		}
		return matches.asReadonly();
	}
}