import { Component, inject, input, OnInit, signal } from '@angular/core';
import { MatIconModule } from '@angular/material/icon';
import { ProfileStore, RouteStore } from 'jde-spa';
import { errorText, ListRoute, QLList, QLListData, QLListResolver, Style, TableSettings } from 'jde-framework';
import { Gateway } from '../../../services/gateway-service';

export type HistAdminKind = 'templates'|'groups';

//The historian's two lists on a gateway's page (#337), beside Connections:  each a ql-list over the gateway's own table, resolved
//here when its tab opens rather than by the page's resolver, and a row opens `templates/<slug>` or `groups/<slug>` under the page
//(QLList.linkPrefix).  The rows go under `<url>/<kind>` in the RouteStore, where the detail's resolver finds its siblings.
@Component({
	selector: 'gateway-hist-list',
	template: `
		@if( listData(); as d ){
			<ql-list [listData]=d [linkPrefix]="[kind()]"></ql-list>
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
	#routeStore = inject( RouteStore );
	async ngOnInit(){
		try{
			const data = await QLListResolver.data( this.gateway(), histAdminListRoute( this.kind() ), this.#profileStore );
			const paged = { ...data, childrenKey: `${this.url()}/${this.kind()}` };
			this.listData.set( await QLListResolver.loadOrFail( this.gateway(), {...paged, skip: QLListResolver.savedSkip( paged )}, this.#routeStore ) );
		}
		catch( e ){//no schema:  the gateway is down, or has no hist tables
			this.error.set( errorText( e ) ?? 'Unknown error' );
		}
	}
}
//The lists' settings and routing, shared with the detail pages' resolver (HistAdminResolver), which adopts the collection and its
//settings for the row query and the breadcrumb.
export const histTemplateTableSettings:TableSettings = {
	noun: 'templates',
	empty: { title: 'No templates yet.', detail: 'A template is a node set, as browse paths from a root, that a group applies to one root on one server.', add: 'Use Add to create one.' },
	columns: [ {name: 'name', style: new Style( 300 )}, 'slug', 'description' ],
	sort: 'name'
};
//Read left to right:  the group, where it is - the connection, the template it applies, the root it applies it from - then the description.
export const histGroupTableSettings:TableSettings = {
	noun: 'groups',
	empty: { title: 'No groups yet.', detail: 'A group is the nodes the gateway historizes on one connection:  a template applied to a root, or a node list of its own.', add: 'Use Add to create one.' },
	columns: [ {name: 'name', style: new Style( 240 )}, {name: 'serverConnection', displayName: 'Connection', selection: 'name', style: new Style( 200 )}, {name: 'histTemplate', displayName: 'Template', selection: 'name', style: new Style( 200 )}, {name: 'root', displayName: 'Root'}, 'slug', 'description' ],
	sort: 'name'
};
export function histAdminListRoute( kind:HistAdminKind ):ListRoute{
	return new ListRoute( kind=='templates'
		? { path: 'templates', title: 'Templates', data: {summary: 'Node sets the historian applies to a root', collectionName: 'histTemplates', tableSettings: histTemplateTableSettings} }
		: { path: 'groups', title: 'Groups', data: {summary: 'The nodes the gateway historizes', collectionName: 'histGroups', tableSettings: histGroupTableSettings} } );
}