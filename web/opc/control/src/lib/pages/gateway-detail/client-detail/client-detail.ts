import { Component, computed, inject, signal } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatIcon } from '@angular/material/icon';
import { MatTabsModule } from '@angular/material/tabs';

import { DetailPage, errorText, Properties} from 'jde-framework';

import { ServerProperties } from './server-properties/server-properties';
import { ServerCnnctn, ServerCnnctnProps } from '../../../model/server-cnnctn';
import { Gateway, GatewayService } from '../../../services/gateway-service';
import { Server, TestConnectionResult } from '../../../model/server';
import { OpcStore } from '../../../services/opc-store';

@Component( {
	templateUrl: './client-detail.html',
	styleUrls: ['./client-detail.scss'],
	//the trailing class is load-bearing:  Angular hashes a component's *shape* into its style-encapsulation id and leaves
		//the class name out, so four routed pages that now share DetailPage and this host string could collide with NG0912.
		host: {class:'main-content mat-drawer-container my-content client-detail'},
	imports: [MatButtonModule, MatIcon, MatTabsModule, Properties, ServerProperties],
})
export class ClientDetail extends DetailPage<ServerCnnctn>{
	constructor(){ super( 'client-detail' ); }

	override async ngOnInit(){
		super.ngOnInit();
		const segments = this.router.url.split( "/" );
		this.gateway = await this.gatewayService.gateway( segments[segments.length-2] );
	}

	protected override get ctor(){ return ServerCnnctn; }
	protected override onRow(){//no child collections - the connection is its properties;  the Connection tab opens on the resolver's describe, until a test replaces it
		this.server.set( this.row.server );
		this.serverError.set( this.row.serverError );
	}
	protected override upsert():ServerCnnctn{
		return new ServerCnnctn( {
			...this.properties(),
		} as ServerCnnctnProps);
	}
	protected override afterMutate(){//the node pages answer from OpcStore's describe:  an edit has to reach them, a delete end them (reviews/m3-closing.md #5)
		if( this.row.slug )
			this.opcStore.forget( this.gateway.slug, this.row.slug );
	}
	protected override get title(){ return this.row.name ? `${this.row.name} - Connection` : "New Connection"; }
	override get ql(){ return this.gateway; }//per-gateway, not a single injected service - resolved in ngOnInit

	//what a user reads first, then the connection, then the two technical fields - the alphabetical default buried Description between them and put Url last
	readonly fieldOrder = ["slug", "name", "description", "url", "certificateUri", "defaultBrowseNs"];
	readonly fieldLabels = { url: "URL", certificateUri: "Certificate URI", defaultBrowseNs: "Default Namespace" };//the camelCase split gives "Url", "Certificate Uri", "Default Browse Ns"
	get serverCnnctn(){ return this.row; }//the template's name for it

	//Test Connection (#250):  the form's values, saved or not, connected by the gateway as the saved row would be (testConnection) and
	//shown in the Connection tab's states - a wrong url or an untrusted certificate says why there, and a saved connection whose
	//page loaded Not connected is retried without a reload.  The tab is always there now;  it was gated on `isNew`, and a new
	//connection's settings could only be checked by saving them.  The base's `!id` clamp (review3 L2) still lands a new row on Properties.
	//Enabled on what the gateway needs - a url, and a slug that passes the insert's rule, since the slug names the certificate the
	//gateway presents and the credential this session holds - and never twice at once.
	readonly canTest = computed( ()=>{
		const p = this.properties() as ServerCnnctn|null;
		return !this.testing() && !!p?.url?.trim() && !!p?.slug?.trim() && !p.fieldError( "slug" );
	} );
	async onTestClick(){
		const p = this.properties() as ServerCnnctn;
		const slug = p.slug.trim(), url = p.url.trim();
		this.testing.set( url );
		this.tabIndex.set( 1 );//the result lands on the Connection tab, and it shows the connect in flight
		try{
			const result = await this.gateway.querySingle<TestConnectionResult>( ClientDetail.testQuery, {opc: slug, url, certificateUri: p.certificateUri?.trim() ?? ""}, (m)=>console.log(m) );
			this.server.set( new Server({ connection: {...p, slug, url} as ServerCnnctnProps, desc: result, policy: result.securityPolicyUri, mode: result.securityMode, namespaces: result.namespaces }) );
			this.serverError.set( undefined );
		}
		catch( e ){//the tab's not-connected state carries the reason - no toast over it
			this.server.set( undefined );
			this.serverError.set( errorText(e) ?? "Unknown error" );
		}
		finally{
			this.testing.set( undefined );
		}
	}
	static readonly testQuery = "testConnection( opc: $opc, url: $url, certificateUri: $certificateUri ){ applicationUri productUri applicationName applicationType gatewayServerUri discoveryProfileUri discoveryUrls securityPolicyUri securityMode namespaces{ index uri } }";
	//what the Connection tab shows:  the resolver's describe or its failure (onRow), then the last test's.  A new connection has
	//neither until it is tested, and the tab says what the button does rather than why a session that was never tried is missing.
	server = signal<Server|undefined>( undefined );
	serverError = signal<string|undefined>( undefined );
	get connectionDetail():string|undefined{ return this.serverError() ?? (this.isNew && !this.server() ? "Test Connection checks the URL and Certificate URI with the gateway before the first save." : undefined); }//a getter, not a computed:  `row` is no signal, and the router reuses this page across connections
	testing = signal<string|undefined>( undefined );//the url under test while a Test Connection is in flight
	gatewayService:GatewayService = inject( GatewayService );
	private opcStore = inject( OpcStore );
	gateway!:Gateway;
}