import { Component, OnInit, inject, model, signal } from '@angular/core';
import { ActivatedRoute } from '@angular/router';
import { ComponentPageTitle, persistedTabIndex, RouteItem, RouteStore } from 'jde-spa';
import { AppService } from '../../../services/app/app-service';
import { errorMessage } from '../../../utils/errors';
import { instanceRouteItem } from '../../../services/resolvers/app-resolver';
import { InstanceTabs } from '../instance-tabs/instance-tabs';

//All the tabs talk to AppService itself.  Unlike the gateway/opc-server pages there is no per-instance service to build:
//the AppServer publishes no /appServers discovery endpoint (only /opcGateways and /opcServers), and the environment's
//`applicationServer` names exactly one - so the connection AppService already holds IS this page's instance.  The
//instancePK lookup is what proves the routed name is that instance rather than some other registration.
@Component( {
		templateUrl: './app-server-detail.html',
		//the trailing class is load-bearing:  Angular hashes a component's *shape* (selectors, host attrs, inputs,
		//prototype members, template decls) into its style-encapsulation id and deliberately leaves the class name out,
		//so this page and its opc-server-detail twin - identical in every one of those - collided with NG0912.
		host: {class:'main-content mat-drawer-container my-content app-server-detail'},
		imports: [InstanceTabs]
})
export class AppServerDetail implements OnInit{
	#route = inject( ActivatedRoute );
	#componentPageTitle = inject( ComponentPageTitle );

	ngOnInit(): void {
		//the ':instance' param sits on the parent route; this component is its path:'' child, which inherits it.
		this.#route.params.subscribe( async (params)=>{
			const instanceName = params["instance"];
			this.error.set( undefined );//the page is reused across instances:  a failed one's banner must not outlive the switch
			this.#componentPageTitle.title = `${instanceName} - AppServer`;
			this.sideNav.set( instanceRouteItem('appServers', instanceName, this.routeStore) );
			try{
				const id = await this.appService.instancePK( instanceName, "AppServer" );
				if( id===undefined )//never fall through to the tabs:  log-detail would answer with the *connected* server's logs under this instance's name
					this.error.set( `No application server instance named '${instanceName}' is registered.` );
				this.instanceId.set( id );
			}
			catch( e ){
				this.error.set( errorMessage(e, "Could not load the application server.") );//errorMessage, not `${e}`: an HttpErrorResponse and a {error:IError} rejection both render "[object Object]".  Without this the page stays behind isLoading and renders blank
			}
			this.isLoading.set( false );
		});
	}


	tabIndex = persistedTabIndex( 'app-server-detail' );
	sideNav = model.required<RouteItem>();

	instanceId = signal<number|undefined>( undefined );
	error = signal<string|undefined>( undefined );
	isLoading = signal<boolean>( true );

	appService = inject( AppService );
	routeStore = inject( RouteStore );
}