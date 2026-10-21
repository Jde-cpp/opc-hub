import { Component, OnInit, signal, model, inject } from '@angular/core';
import { ActivatedRoute } from '@angular/router';
import { ComponentPageTitle, persistedTabIndex } from 'jde-spa';
import { AppService, errorMessage, InstanceTab, InstanceTabs, QLList, QLListData } from 'jde-framework';
import { RouteItem } from 'jde-spa';
import { GatewayService } from '../../services/gateway-service';
import { Gateway } from '../../services/gateway-service';
import { GatewayHistList } from './hist-admin/gateway-hist-list';

@Component( {
		templateUrl: './gateway-detail.html',
		host: {class:'main-content mat-drawer-container my-content'},
		imports: [InstanceTab, InstanceTabs, QLList, GatewayHistList]
})
export class GatewayDetail implements OnInit{
	#route = inject( ActivatedRoute );
	#componentPageTitle = inject( ComponentPageTitle );

	ngOnInit(): void {
		this.#route.data.subscribe( async (routeData)=>{
			this.pageData = <QLListData>routeData["data"];
			this.sideNav.set( this.pageData.routing );
			const instanceName = this.pageData.routing.path.split('/').slice(-1)[0];
			this.#componentPageTitle.title = `${instanceName} - Gateway`;
			const url = this.#route.parent!.snapshot.url.map( s=>s.path ).join( '/' );//now:  the snapshot moves on with the next navigation
			this.error.set( undefined );//the page is reused across gateways:  a failed one's message must not outlive the switch
			try{
				const gateway = await this.gatewayService.gateway( instanceName );
				this.url.set( url );//with the gateway, so a tab's list never pairs one gateway's key with the other's rows
				this.gateway.set( gateway );
				this.isLoading.set( false );
				this.instanceId.set( await this.appService.instancePK(instanceName, "OpcGateway") ?? await this.appService.instancePK(instanceName, "OpcHub") );//a hub registers under its own program name
			}
			catch( e ){//as OpcServerDetail:  unhandled, a rejection left the page blank behind isLoading (reviews-issue/web-refactor-review.md #4)
				this.error.set( errorMessage(e, "Could not load the gateway.") );
				this.isLoading.set( false );
			}
		});
	}

	pageData!:QLListData;
	//the page's url, apps/gateways/<instance>, by its route's segments as the resolvers read it - decoded, where router.url is
	//percent-encoded:  the RouteStore key of the Connections tab's rows (GatewayResolver), and with `/templates` or `/groups` of the
	//two historian tabs', where their detail pages find their siblings (HistAdminResolver).  A signal, as the gateway is.
	url = signal( '' );
	tabIndex = persistedTabIndex( 'gateway-detail' );
	sideNav = model.required<RouteItem>();

	gateway = signal<Gateway|undefined>( undefined );//a signal, not a field:  the Logs tab's body sits in the tab group's OnPush portal, which only a signal read refreshes - a field never re-keyed it on a switch (reviews/m3-closing.md #31)
	gatewayService = inject(GatewayService);
	appService = inject(AppService);
	instanceId = signal<number|undefined>( undefined );
	error = signal<string|undefined>( undefined );
	isLoading = signal<boolean>( true );
}