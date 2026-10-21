import { Component, inject, input, OnInit, signal } from '@angular/core';
import { MatIconModule } from '@angular/material/icon';
import { LocalPrefs, ProfileStore, RouteStore } from 'jde-spa';
import { errorMessage, QLList, QLListData, QLListResolver } from 'jde-framework';
import { Gateway } from '../../../services/gateway-service';
import { HistAdminKind, histAdminListRoute } from './hist-admin-list-route';

//The historian's two lists on a gateway's page (#337), beside Connections:  each a ql-list over the gateway's own table, resolved
//here when its tab opens rather than by the page's resolver, and a row opens `templates/<slug>` or `groups/<slug>` under the page
//(QLList.linkPrefix).  The rows go under `<url>/<kind>` in the RouteStore, where the detail's resolver finds its siblings.
@Component({
	selector: 'gateway-hist-list',
	template: `
		@if( listData(); as d ){
			<ql-list [listData]=d [ql]=gateway() [linkPrefix]="[kind()]"></ql-list>
		}
		@else if( error(); as e ){
			<div class="jde-empty" role="alert"><mat-icon aria-hidden="true">error_outline</mat-icon><p class="title">Could not open {{ kind() }}.</p><p class="detail">{{ e }}</p></div>
		}`,
	styles: ':host{ display: block; }',
	imports: [MatIconModule, QLList]
})
export class GatewayHistList implements OnInit{
	gateway = input.required<Gateway>();
	kind = input.required<HistAdminKind>();
	url = input.required<string>();//the page's url, apps/gateways/<instance>:  the key the rows go under, with the kind
	listData = signal<QLListData|undefined>( undefined );
	error = signal<string|undefined>( undefined );
	#profileStore = inject( ProfileStore );
	#prefs = inject( LocalPrefs );
	#routeStore = inject( RouteStore );
	async ngOnInit(){
		try{
			const data = await QLListResolver.data( this.gateway(), histAdminListRoute( this.kind() ), this.#profileStore, this.#prefs );
			const paged = { ...data, childrenKey: `${this.url()}/${this.kind()}` };
			this.listData.set( await QLListResolver.loadOrFail( this.gateway(), {...paged, skip: QLListResolver.savedSkip( paged )}, this.#routeStore ) );
		}
		catch( e ){//no schema:  the gateway is down, or has no hist tables
			this.error.set( errorMessage(e) );
		}
	}
}