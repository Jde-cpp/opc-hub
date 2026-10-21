import { Component, OnInit, inject, model, signal } from '@angular/core';
import { ActivatedRoute } from '@angular/router';
import { AppService, errorMessage, InstanceTabs, instanceRouteItem } from 'jde-framework';
import { ComponentPageTitle, persistedTabIndex, RouteItem, RouteStore } from 'jde-spa';
import { OpcServer, OpcServerService } from '../../services/opc-server-service';

@Component( {
		templateUrl: './opc-server-detail.html',
		//the trailing class is load-bearing (NG0912):  see app-server-detail.ts
		host: {class:'main-content mat-drawer-container my-content opc-server-detail'},
		imports: [InstanceTabs]
})
export class OpcServerDetail implements OnInit{
	#route = inject( ActivatedRoute );
	#componentPageTitle = inject( ComponentPageTitle );

	ngOnInit(): void {
		//the ':instance' param sits on the parent route; this component is its path:'' child, which inherits it.
		this.#route.params.subscribe( async (params)=>{
			const instanceName = params["instance"];
			this.error.set( undefined );//the page is reused across instances:  a failed one's banner must not outlive the switch
			this.#componentPageTitle.title = `${instanceName} - OpcServer`;//the route carries no title, so nothing else sets the document title for this page
			this.sideNav.set( instanceRouteItem('opcServers', instanceName, this.routeStore) );
			try{
				this.server.set( await this.opcServerService.server(instanceName) );
				this.instanceId.set( await this.appService.instancePK(instanceName, "OpcServer") );
			}
			catch( e ){
				this.error.set( errorMessage(e, "Could not load the OPC server.") );//errorMessage, not `${e}`: an HttpErrorResponse and a {error:IError} rejection both render "[object Object]".  Without this the page stays behind isLoading and renders blank
			}
			this.isLoading.set( false );
		});
	}


	tabIndex = persistedTabIndex( 'opc-server-detail' );
	sideNav = model.required<RouteItem>();

	server = signal<OpcServer|undefined>( undefined );
	instanceId = signal<number|undefined>( undefined );
	error = signal<string|undefined>( undefined );
	isLoading = signal<boolean>( true );

	appService = inject( AppService );
	opcServerService = inject( OpcServerService );
	routeStore = inject( RouteStore );
}