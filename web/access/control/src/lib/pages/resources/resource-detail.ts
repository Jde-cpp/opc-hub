import { Component, computed, inject, model, OnDestroy, OnInit, signal } from '@angular/core';
import { MatIcon } from '@angular/material/icon';
import { MatTabsModule } from '@angular/material/tabs';
import { ActivatedRoute, RouterLink } from '@angular/router';
import { ComponentPageTitle, ProfileStore, RouteItem } from 'jde-spa';
import { EnumKeysPipe, errorText, httpStatus } from 'jde-framework';
import { Rights } from '../../model/permission';
import { Resource } from '../../model/resource';
import { ResourceHolders } from '../../model/resource-holder';
import { AccessService } from '../../services/access-service';
import { ResourcePageData } from '../../services/resource-resolver';
import { HolderTable } from './holder-table/holder-table';

//Who reaches a resource:  the roles holding a permission on it, and the groups and users those roles - or a direct grant -
//reach, each with its rights and, on hover, the grants behind them.  The server's answer (resourceRights, Authorize::Holders),
//so the Users tab is what enforcement answers, not a client-side guess.  Read-only:  a resource row has no editable field,
//its enforcement is the Resources list's switch, and grants are made on roles.
@Component({
	templateUrl: './resource-detail.html',
	styleUrls: ['./resource-detail.scss'],
	host: {class:'main-content mat-drawer-container my-content resource-detail'},
	imports: [MatIcon, MatTabsModule, RouterLink, HolderTable]
})
export class ResourceDetail implements OnInit, OnDestroy{
	#route = inject( ActivatedRoute );
	#access = inject( AccessService );
	#componentPageTitle = inject( ComponentPageTitle );

	ngOnInit(){ this.#route.data.subscribe( data=>this.#load(data["pageData"]) ); }
	ngOnDestroy(){ ProfileStore.setTabIndex( 'resourceDetail', this.tabIndex() ); }
	onTabIndexChanged( index:number ){ this.tabIndex.set( index ); }

	async #load( data:ResourcePageData ):Promise<void>{
		const load = ++this.#loadId;
		this.resource.set( data.resource );
		this.accessUrl.set( data.accessUrl );
		this.sideNav.set( data.routing );
		this.#componentPageTitle.title = data.resource.name;
		this.holders.set( undefined );
		this.error.set( undefined );
		try{
			const holders = await this.#access.resourceRights( data.resource.id );
			if( load==this.#loadId )//not overtaken by the next resource
				this.holders.set( holders );
		}
		catch( e ){
			if( load==this.#loadId )
				this.error.set( e );
		}
	}

	sideNav = model.required<RouteItem>();
	resource = signal<Resource|undefined>( undefined );
	accessUrl = signal<string>( "/access" );
	listUrl = computed<string>( ()=>`${this.accessUrl()}/resources` );
	holders = signal<ResourceHolders|undefined>( undefined );
	error = signal<unknown>( undefined );
	failure = computed<{forbidden:boolean, title:string, detail:string}|undefined>( ()=>{
		const e = this.error();
		if( e===undefined )
			return undefined;
		const what = errorText( e )?.replace( /^\(\d+\)/, "" ) ?? "";
		return httpStatus( e )==403
			? { forbidden: true, title: "No access to who holds rights here.", detail: `${what}  Reading the grants takes Read on acl.`.trim() }
			: { forbidden: false, title: "Could not load who holds rights here.", detail: what };
	});
	//the rights the resource offers - a grant's others mean nothing here.  All of them for a row that does not say.
	rights = computed( ()=>{
		const available = this.resource()?.availableRights || Rights.All;
		return EnumKeysPipe.prototype.transform( Rights ).filter( kv=>kv.key!=Rights.None && kv.key!=Rights.All && (available & kv.key) );
	});
	tabIndex = signal<number>( ProfileStore.tabIndex('resourceDetail') );
	#loadId = 0;
}