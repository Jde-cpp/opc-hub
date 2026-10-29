import { Component, effect, inject, input, OnInit, signal, untracked, viewChild } from '@angular/core';
import { MatIconModule } from '@angular/material/icon';
import { LocalPrefs, ProfileStore, RouteStore } from 'jde-spa';
import { CellText, errorMessage, QLList, QLListData, QLListResolver, QLRow } from 'jde-framework';
import { parseHistNodeText } from '../../../model/hist-admin';
import { Gateway } from '../../../services/gateway-service';
import { HistAdminService } from '../../../services/hist-admin-service';
import { OpcStore } from '../../../services/opc-store';
import { HistAdminKind, histAdminListRoute } from './hist-admin-list-route';

//The historian's two lists on a gateway's page (#337), beside Connections:  each a ql-list over the gateway's own table, resolved
//here when its tab opens rather than by the page's resolver, and a row opens `templates/<slug>` or `groups/<slug>` under the page
//(QLList.linkPrefix).  The rows go under `<url>/<kind>` in the RouteStore, where the detail's resolver finds its siblings.  A group's
//root shows by its browse path, as the group's Properties shows it, once its connection's server places it.
@Component({
	selector: 'gateway-hist-list',
	template: `
		@if( listData(); as d ){
			<ql-list [listData]=d [ql]=gateway() [linkPrefix]="[kind()]" [cellText]="kind()=='groups' ? rootText : undefined"></ql-list>
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
	#admin = inject( HistAdminService );
	#opcStore = inject( OpcStore );
	list = viewChild( QLList );
	#rootPaths = signal( new Map<string,string|undefined>() );//by rootKey, undefined where the gateway can't place the root
	constructor(){
		effect( ()=>{//each page of rows, as the list loads it
			const rows = this.list()?.data();
			if( rows?.length && this.kind()=='groups' )
				untracked( ()=>this.#loadRootPaths( rows ) );
		} );
	}
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
	readonly rootText:CellText = ( column, row )=>{
		const key = column=='root' ? rootKey( row ) : undefined;
		return key ? this.#rootPaths().get( key ) : undefined;
	};
	//the roots not placed yet, in one query per connection;  a connection that fails is asked again on the next load
	async #loadRootPaths( rows:QLRow[] ):Promise<void>{
		const gateway = this.gateway(), known = this.#rootPaths();
		const bySlug = new Map<string,Set<string>>();
		for( const row of rows ){
			const key = rootKey( row ), slug = (<any>row['serverConnection'])?.slug;
			if( key && !known.has( key ) )
				bySlug.set( slug, (bySlug.get( slug ) ?? new Set<string>()).add( <string>row['root'] ) );
		}
		await Promise.all( [...bySlug].map( async ( [slug, texts] )=>{
			try{
				const roots = [...texts].map( t=>({text: t, node: parseHistNodeText( t )}) ).filter( r=>!!r.node );
				const paths = await this.#admin.rootPaths( gateway, slug, await this.#opcStore.getConnection( gateway, slug ), roots.map( r=>r.node! ) );
				this.#rootPaths.update( m=>new Map( [...m, ...roots.map( ( r, i )=>[`${slug}/${r.text}`, paths[i]] as const )] ) );
			}
			catch( e ){
				console.warn( `Could not place the roots on '${slug}'.`, e );
			}
		} ) );
	}
}
//a group's root by its connection, which places it:  none for a group with no root, or a row without the connection's slug
function rootKey( row:QLRow ):string|undefined{
	const slug = (<any>row['serverConnection'])?.slug, root = row['root'];
	return slug && typeof root=='string' && root ? `${slug}/${root}` : undefined;
}