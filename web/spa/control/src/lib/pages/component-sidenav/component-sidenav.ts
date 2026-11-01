import {
	Component,
	OnDestroy,
	OnInit,
	ViewEncapsulation,
	model,
	viewChild, inject } from '@angular/core';
import {MatSidenav, MatSidenavModule} from '@angular/material/sidenav';
import {RouterOutlet} from '@angular/router';
import {Subscription} from 'rxjs';

import {
	NavigationFocusService
} from '../../shared/navigation-focus/navigation-focus-service';
import {ScreenSize} from '../../services/screen-size';
import {ComponentPageHeader} from '../component-page-header/component-page-header';
import {ComponentNav} from './component-nav';
import {RouteItem} from './route-item';

// Sidebar + router_outlet
@Component({
	selector: 'app-component-sidenav',
	templateUrl: './component-sidenav.html',
	styleUrls: ['./component-sidenav.scss'],
	encapsulation: ViewEncapsulation.None,
	imports: [ MatSidenavModule, ComponentNav, ComponentPageHeader, RouterOutlet ],
})
export class ComponentSidenav implements OnInit, OnDestroy {
	readonly sidenav = viewChild(MatSidenav);
	//the shell's small breakpoint (constants.scss, through ScreenSize):  the nav is a drawer over the page, closed by a navigation
	isSmall = inject( ScreenSize ).isSmall;
	private subscriptions = new Subscription();
	item = model<RouteItem>(null as any);
	#navigationFocusService = inject( NavigationFocusService );

	ngOnInit() {
		this.subscriptions.add(
			this.#navigationFocusService.navigationEndEvents.subscribe( ()=>{
				if( this.isSmall() )
					this.sidenav()?.close();
			})
		);
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