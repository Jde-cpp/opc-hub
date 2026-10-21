import {Injectable, OnDestroy, inject } from '@angular/core';
import {Event, NavigationEnd, Router} from '@angular/router';
import {filter, skip} from 'rxjs/operators';
import {Subscription} from 'rxjs';

@Injectable({
	providedIn: 'root',
})
export class NavigationFocusService implements OnDestroy {
	private subscriptions = new Subscription();
	private navigationFocusRequests: HTMLElement[] = [];

	readonly navigationEndEvents;
	readonly softNavigations;

	#router = inject( Router );
	constructor() {
		this.navigationEndEvents = this.#router.events.pipe(filter((event: Event): event is NavigationEnd => event instanceof NavigationEnd));
		this.softNavigations = this.navigationEndEvents.pipe(skip(1));
		this.subscriptions.add(this.softNavigations.subscribe(() => {
			// focus if url does not have fragment
			if (!this.#router.url.split('#')[1]) {
				setTimeout(() => {
					if (this.navigationFocusRequests.length) {
						this.navigationFocusRequests[this.navigationFocusRequests.length - 1]
							.focus({preventScroll: true});
					}
				}, 100);
			}
		}));
	}

	ngOnDestroy() {
		this.subscriptions.unsubscribe();
	}

	requestFocusOnNavigation(el: HTMLElement) {
		this.navigationFocusRequests.push(el);
	}

	relinquishFocusOnNavigation(el: HTMLElement) {
		this.navigationFocusRequests.splice(this.navigationFocusRequests.indexOf(el), 1);
	}
}