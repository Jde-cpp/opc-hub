import { Component, computed, inject, input } from '@angular/core';
import { Router, RouterLink } from '@angular/router';
import { RouteItem } from '../../../pages/component-sidenav/route-item';

@Component({
	selector: 'breadcrumbs',
	imports: [RouterLink],
	template: `
		<nav aria-label="Breadcrumb">
			<ol>
				@for( crumb of links(); track crumb.path ?? crumb.title; let last = $last ){
					<li>
						@if( !last && crumb.link ){
							<a [routerLink]=crumb.link>{{crumb.title}}</a>
						} @else {
							<span [attr.aria-current]="last ? 'page' : null">{{crumb.title}}</span>
						}
					</li>
				}
			</ol>
		</nav>`,
	styles: `
		nav{ height: 36px; box-sizing: border-box; display: flex; align-items: center; padding: 0 16px; overflow-x: auto; white-space: nowrap;
			background: var(--mat-sys-surface-container);
			border-bottom: 1px solid var(--mat-sys-outline-variant); }
		ol{ display: flex; align-items: center; list-style: none; margin: 0; padding: 0; }
		li{ display: flex; }
		li + li{ margin-left: -9px; }/*nest the notch over the previous arrow tip, leaving a 3px sliver of row background as the separator*/
		a, span{ display: flex; align-items: center; height: 26px; padding: 0 10px 0 20px; font-size: 13px; text-decoration: none;
			background: var(--mat-sys-secondary-container); color: var(--mat-sys-on-secondary-container);
			clip-path: polygon(0 0, calc(100% - 12px) 0, 100% 50%, calc(100% - 12px) 100%, 0 100%, 12px 50%); }
		li:first-child a, li:first-child span{ padding-left: 12px;
			clip-path: polygon(0 0, calc(100% - 12px) 0, 100% 50%, calc(100% - 12px) 100%, 0 100%); }
		a:hover{ background: color-mix(in srgb, var(--mat-sys-primary) 18%, var(--mat-sys-secondary-container)); }
		a:focus-visible{ outline: none; background: var(--mat-sys-primary); color: var(--mat-sys-on-primary); }/*outline would be clipped by the arrow clip-path, so focus flips the fill instead*/
		li:last-child span{ background: var(--mat-sys-primary); color: var(--mat-sys-on-primary); font-weight: 500; }`
})
export class Breadcrumbs{
	crumbs = input.required<RouteItem[]>();
	//A crumb's path is cut from the serialized - percent-encoded - url, and a string routerLink encodes its '%' again:  the
	//crumb back up to `Simulation Examples` linked to `Simulation%2520Examples` (reviews/m3-closing.md #8).  Parsed into a
	//tree instead, in a computed so it is not a fresh input every pass.
	links = computed( ()=>this.crumbs().map( crumb=>({path: crumb.path, title: crumb.title, link: crumb.path ? this.#router.parseUrl(crumb.path) : undefined}) ) );
	#router = inject( Router );
}
