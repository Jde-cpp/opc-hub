import { Component, OnInit, signal, model, inject } from '@angular/core';
import { ActivatedRoute, RouterModule } from '@angular/router';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { ComponentPageTitle, Latest, persistedTabIndex } from 'jde-spa';
import { AppService, CalendarList, errorMessage, InstanceTab, InstanceTabs, QLList, QLListData } from 'jde-framework';
import { RouteItem } from 'jde-spa';
import { GatewayService } from '../../services/gateway-service';
import { Gateway } from '../../services/gateway-service';
import { GatewayEndpoint } from './gateway-endpoint/gateway-endpoint';
import { GatewayHistList } from './hist-admin/gateway-hist-list';

@Component( {
		templateUrl: './gateway-detail.html',
		host: {class:'main-content my-content'},
		imports: [RouterModule, MatButtonModule, MatIconModule, CalendarList, InstanceTab, InstanceTabs, QLList, GatewayHistList, GatewayEndpoint]
})
export class GatewayDetail implements OnInit{
	#route = inject( ActivatedRoute );
	#componentPageTitle = inject( ComponentPageTitle );
	#loads = new Latest();//the page is reused across gateways:  a slower earlier one must not land on the one now shown

	ngOnInit(): void {
		this.#route.data.subscribe( async (routeData)=>{
			const load = this.#loads.next();
			this.pageData = <QLListData>routeData["data"];
			this.sideNav.set( this.pageData.routing );
			const instanceName = this.pageData.routing.path.split('/').slice(-1)[0];
			this.#componentPageTitle.title = `${instanceName} - Gateway`;
			const url = this.#route.parent!.snapshot.url.map( s=>s.path ).join( '/' );//now:  the snapshot moves on with the next navigation
			this.error.set( undefined );//the page is reused across gateways:  a failed one's message must not outlive the switch
			try{
				const gateway = await this.gatewayService.gateway( instanceName );
				//ahead of the tabs, which a hub has one more of, its last:  shown without it, the group clamps the index kept for it
				const [gatewayId, hubId] = await this.appService.instancePKs( instanceName, ["OpcGateway", "OpcHub"] );//a hub registers under its own program name
				if( !this.#loads.is( load ) )
					return;
				this.url.set( url );//with the gateway, so a tab's list never pairs one gateway's key with the other's rows
				this.gateway.set( gateway );
				this.isHub.set( gatewayId===undefined && hubId!==undefined );
				this.instanceId.set( gatewayId ?? hubId );
				this.isLoading.set( false );
			}
			catch( e ){//as OpcServerDetail:  unhandled, a rejection left the page blank behind isLoading (reviews-issue/web-refactor-review.md #4)
				if( !this.#loads.is( load ) )
					return;
				this.error.set( errorMessage(e, "Could not load the gateway.") );
				this.isLoading.set( false );
			}
		});
	}

	pageData!:QLListData;
	//the page's url, apps/gateways/<instance>, by its route's segments as the resolvers read it - decoded, where router.url is
	//percent-encoded:  the RouteStore key of the Connections tab's rows (GatewayResolver), and with `/templates`, `/groups`, `/machines`
	//or `/reasons` of the historian's and OEE's tabs', where their detail pages find their siblings (HistAdminResolver).  A signal, as the gateway is.
	url = signal( '' );
	tabIndex = persistedTabIndex( 'gateway-detail' );
	sideNav = model.required<RouteItem>();

	gateway = signal<Gateway|undefined>( undefined );//a signal, not a field:  the Logs tab's body sits in the tab group's OnPush portal, which only a signal read refreshes - a field never re-keyed it on a switch (reviews/m3-closing.md #31)
	gatewayService = inject(GatewayService);
	appService = inject(AppService);
	instanceId = signal<number|undefined>( undefined );
	isHub = signal( false );//the application server and the gateway in one process, whose page this then is for both:  it has the application server's Calendars tab
	error = signal<string|undefined>( undefined );
	isLoading = signal<boolean>( true );
}