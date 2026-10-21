import {Directive, ElementRef, OnDestroy, inject } from '@angular/core';
import {NavigationFocusService} from './navigation-focus-service';

@Directive({
	selector: '[focusOnNavigation]',
	//bindings, not static attributes:  @HostBinding bound the tabindex PROPERTY and the outline style, and a static
	//`tabindex` in here could be overridden by one on the host element instead of winning (review3 C9).
	host: {
		'[tabindex]': '"-1"',
		'[style.outline]': '"none"'
	}
})
export class NavigationFocus implements OnDestroy {
	#el = inject( ElementRef );
	#navigationFocusService = inject( NavigationFocusService );
	constructor() {
		this.#navigationFocusService.requestFocusOnNavigation(this.#el.nativeElement);
	}

	ngOnDestroy() {
		this.#navigationFocusService.relinquishFocusOnNavigation(this.#el.nativeElement);
	}
}