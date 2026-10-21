import {
	Component,
	OnDestroy,
	OnInit,
	ViewEncapsulation,
	model,
	viewChild, inject } from '@angular/core';
import {BreakpointObserver} from '@angular/cdk/layout';
import {AsyncPipe} from '@angular/common';
import {MatSidenav, MatSidenavModule} from '@angular/material/sidenav';
import {RouterOutlet} from '@angular/router';
import {Observable, Subscription} from 'rxjs';
import {map} from 'rxjs/operators';

import {
	NavigationFocusService
} from '../../shared/navigation-focus/navigation-focus-service';
import {ComponentPageHeader} from '../component-page-header/component-page-header';
import {ComponentNav} from './component-nav';
import {RouteItem} from './route-item';

// Used by the ComponentSidenav for orchestrating the MatSidenav in a responsive way: hiding the
// sidenav, defaulting it to open, and changing the mode from over to side.
// The value was determined through the combination of Material Design breakpoints and careful
// testing of the application across a range of common device widths (360px+).
// It needs to stay in sync with $small-breakpoint-width in styles/_constants.scss.
const SMALL_WIDTH_BREAKPOINT = 959;
// Sidebar + router_outlet
@Component({
	selector: 'app-component-sidenav',
	templateUrl: './component-sidenav.html',
	styleUrls: ['./component-sidenav.scss'],
	encapsulation: ViewEncapsulation.None,
	imports: [ MatSidenavModule, ComponentNav, ComponentPageHeader, RouterOutlet, AsyncPipe ],
})
export class ComponentSidenav implements OnInit, OnDestroy {
	readonly sidenav = viewChild(MatSidenav);
	isScreenSmall: Observable<boolean>;
	private subscriptions = new Subscription();
	item = model<RouteItem>(null as any);
	#navigationFocusService = inject( NavigationFocusService );
	constructor(){
		const breakpoints:BreakpointObserver = inject( BreakpointObserver );//local, not a field: nothing else reads it
		this.isScreenSmall = breakpoints.observe(`(max-width: ${SMALL_WIDTH_BREAKPOINT}px)`).pipe(map(breakpoint => breakpoint.matches));
	}

	ngOnInit() {
		this.subscriptions.add(
			this.#navigationFocusService.navigationEndEvents.pipe(map(() => this.isScreenSmall))
			.subscribe((shouldCloseSideNav) => {
					const sidenav = this.sidenav();
					if (shouldCloseSideNav && sidenav) {
						sidenav.close();
					}
				}
		));
	}
	onRouterOutletActivate( event:object ){//the activated component; `in` narrows it to one carrying a sideNav slot
		if( 'sideNav' in event ){
			event.sideNav = this.item;
		}
		else
			console.warn( `onRouterOutletActivate: activated component has no 'sideNav' input` );
	}
	ngOnDestroy() {
		this.subscriptions.unsubscribe();
	}

	toggleSidenav(): void {
		this.sidenav()?.toggle();
	}
}