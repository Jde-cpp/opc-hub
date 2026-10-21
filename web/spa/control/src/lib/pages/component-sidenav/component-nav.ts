import {Component, effect, input, signal, inject } from '@angular/core';
import {MatIconModule} from '@angular/material/icon';
import {MatListModule} from '@angular/material/list';
import {ActivatedRoute, RouterLink, RouterLinkActive} from '@angular/router';

import {RouteItem} from './route-item';

@Component({
  selector: 'app-component-nav',
  templateUrl: './component-nav.html',
	styles: [`
		.parent-nav {
			--mat-list-list-item-one-line-container-height: 32px;
			--mat-list-list-item-label-text-size: .75rem;
			--mat-list-list-item-label-text-weight: 500;
			--mat-list-list-item-label-text-tracking: .08em;
			text-transform: uppercase;
			opacity: .7;
			margin-bottom: 4px;
		}
	`],
  imports: [ MatIconModule, MatListModule, RouterLinkActive, RouterLink ],
})
export class ComponentNav {
  #route = inject( ActivatedRoute );
  constructor(){
		effect(() => {
			let loaded = this.item()!=null;
			if( loaded ){
				if( this.item().parent )
					this.parentUrl = this.item().parent!.path;
				else{
					let segments = this.parentUrl.split( "/" );
					if( segments[segments.length-1].startsWith(":") )
						this.parentUrl = `${segments.slice(0,segments.length-1).join("/")}/${this.item().parent?.path}`;
					//else
					//	this.parentUrl = '';
				}
			}
			this.isLoading.set( !loaded );
		});
	}
  ngOnInit(){
		this.#route.url.subscribe( (urlSegments) => {
			if( urlSegments.length==1 )
				this.parentUrl = '/'+urlSegments[0].path; //access/users
			else
				this.parentUrl = '/'+urlSegments.slice(0, -1).map(segment => segment.path).join('/');
		});
 	};
	item = input.required<RouteItem>();
	parentUrl!: string;
	isLoading = signal( true );
}