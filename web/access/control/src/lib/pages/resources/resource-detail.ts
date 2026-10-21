import { Component, computed, inject, model, OnInit, signal } from '@angular/core';
import { MatIcon } from '@angular/material/icon';
import { MatTabsModule } from '@angular/material/tabs';
import { ActivatedRoute, RouterLink } from '@angular/router';
import { ComponentPageTitle, Latest, persistedTabIndex, RouteItem } from 'jde-spa';
import { EnumKeysPipe, Failure, failureState, FailureState } from 'jde-framework';
import { Rights } from '../../model/permission';
import { Resource } from '../../model/resource';
import { HolderLists, ResourceHolders } from '../../model/resource-holder';
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
	imports: [FailureState, MatIcon, MatTabsModule, RouterLink, HolderTable]
})
export class ResourceDetail implements OnInit{
	#route = inject( ActivatedRoute );
	#access = inject( AccessService );
	#componentPageTitle = inject( ComponentPageTitle );

	ngOnInit(){ this.#route.data.subscribe( data=>this.#load(data["pageData"]) ); }

	async #load( data:ResourcePageData ):Promise<void>{
		const load = this.#latest.next();
		this.resource.set( data.resource );
		this.accessUrl.set( data.accessUrl );
		this.sideNav.set( data.routing );
		this.#componentPageTitle.title = data.resource.name;
		this.holders.set( undefined );
		this.error.set( undefined );
		try{
			const holders = await this.#access.resourceRights( data.resource.id, this.#lists );
			this.#lists = holders.lists;
			if( this.#latest.is(load) )
				this.holders.set( holders );
		}
		catch( e ){
			if( this.#latest.is(load) )
				this.error.set( e );
		}
	}

	sideNav = model.required<RouteItem>();
	resource = signal<Resource|undefined>( undefined );
	accessUrl = signal<string>( "/access" );
	listUrl = computed<string>( ()=>`${this.accessUrl()}/resources` );
	holders = signal<ResourceHolders|undefined>( undefined );
	error = signal<unknown>( undefined );
	failure = computed<Failure|undefined>( ()=>failureState(this.error(), "who holds rights here", "Reading the grants takes Read on acl.") );
	//the rights the resource offers - all of them for a row that does not say - and any other a listed grant holds:  the server
	//enforces whatever a grant holds, and a QL mutation can grant what the checkboxes do not offer.
	rights = computed( ()=>{
		const holders = this.holders();
		const held = holders ? [...holders.roles, ...holders.groups, ...holders.users].reduce( (bits,h)=>bits | h.allowed | h.denied, 0 ) : 0;
		const shown = (this.resource()?.availableRights || Rights.All) | held;
		return EnumKeysPipe.prototype.transform( Rights ).filter( kv=>kv.key!=Rights.None && kv.key!=Rights.All && (shown & kv.key) );
	});
	tabIndex = persistedTabIndex( 'resourceDetail' );
	#latest = new Latest();
	#lists:HolderLists|undefined;//kept across the sidenav's resources - reviews-issue/251-resource-page.md #9
}