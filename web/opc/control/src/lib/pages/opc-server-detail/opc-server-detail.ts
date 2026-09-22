import { Component, OnDestroy, OnInit, inject, model, signal } from '@angular/core';
import { MatTabsModule } from '@angular/material/tabs';
import { ActivatedRoute } from '@angular/router';
import { AppService, errorMessage, LogDetail, LogSettingsPanel } from 'jde-framework';
import { ComponentPageTitle, ProfileStore, RouteItem, RouteStore } from 'jde-spa';
import { OpcServer, OpcServerService } from '../../services/opc-server-service';

@Component( {
		templateUrl: './opc-server-detail.html',
		//the trailing class is load-bearing:  Angular hashes a component's *shape* (selectors, host attrs, inputs,
		//prototype members, template decls) into its style-encapsulation id and deliberately leaves the class name out,
		//so this page and its app-server-detail twin - identical in every one of those - collided with NG0912.
		host: {class:'main-content mat-drawer-container my-content opc-server-detail'},
		imports: [MatTabsModule, LogDetail, LogSettingsPanel]
})
export class OpcServerDetail implements OnInit, OnDestroy{
	private route:ActivatedRoute = inject( ActivatedRoute );
	private componentPageTitle:ComponentPageTitle = inject( ComponentPageTitle );

	ngOnInit(): void {
		//the ':instance' param sits on the parent route; this component is its path:'' child, which inherits it.
		this.route.params.subscribe( async (params)=>{
			const instanceName = params["instance"];
			this.error.set( undefined );//the page is reused across instances:  a failed one's banner must not outlive the switch
			this.componentPageTitle.title = `${instanceName} - OpcServer`;//the route carries no title, so nothing else sets the document title for this page
			this.sideNav.set( this.routeItem(instanceName) );
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
	ngOnDestroy(): void {
		ProfileStore.setTabIndex( 'opc-server-detail', this.tabIndex );
	}

	tabIndexChanged( index:number ){ this.tabIndex = index; }

	//AppResolver keys the instance list under the url segment it built for the program ('opcServers'), and the sibling
	//links are rendered as parent.path + '/' + sibling.path - so the parent must be set or they resolve against the
	//route's own url instead.
	private routeItem( instanceName:string ):RouteItem{
		const y = new RouteItem( {path: `opcServers/${instanceName}`, title: `opcServers/${instanceName}`} );
		y.parent = new RouteItem( {path: "/apps", title: "Applications"} );
		y.siblings = this.routeStore.getChildren( "apps/opcServers" );
		return y;
	}

	tabIndex:number = ProfileStore.tabIndex( 'opc-server-detail' );
	sideNav = model.required<RouteItem>();

	server = signal<OpcServer|undefined>( undefined );
	instanceId = signal<number|undefined>( undefined );
	error = signal<string|undefined>( undefined );
	isLoading = signal<boolean>( true );

	appService = inject( AppService );
	opcServerService = inject( OpcServerService );
	routeStore = inject( RouteStore );
}
