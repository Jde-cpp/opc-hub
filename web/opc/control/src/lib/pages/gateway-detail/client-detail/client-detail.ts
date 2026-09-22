import { Component, inject } from '@angular/core';
import { CommonModule } from '@angular/common';
import { MatButtonModule } from '@angular/material/button';
import { MatIcon } from '@angular/material/icon';
import { MatTabsModule } from '@angular/material/tabs';

import { DetailPage, Properties} from 'jde-framework';

import { ServerProperties } from './server-properties/server-properties';
import { ServerCnnctn, ServerCnnctnProps } from '../../../model/server-cnnctn';
import { Gateway, GatewayService } from '../../../services/gateway-service';
import { Server } from '../../../model/server';
import { OpcStore } from '../../../services/opc-store';

@Component( {
	templateUrl: './client-detail.html',
	styleUrls: ['./client-detail.scss'],
	//the trailing class is load-bearing:  Angular hashes a component's *shape* into its style-encapsulation id and leaves
		//the class name out, so four routed pages that now share DetailPage and this host string could collide with NG0912.
		host: {class:'main-content mat-drawer-container my-content client-detail'},
	imports: [CommonModule, MatButtonModule, MatIcon, MatTabsModule, Properties, ServerProperties],
})
export class ClientDetail extends DetailPage<ServerCnnctn>{
	constructor(){ super( 'client-detail' ); }

	override async ngOnInit(){
		super.ngOnInit();
		const segments = this.router.url.split( "/" );
		this.gateway = await this.gatewayService.gateway( segments[segments.length-2] );
	}

	protected override get ctor(){ return ServerCnnctn; }
	protected override onRow(){}//no child collections - the connection is its properties
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
	//`isNew` (the base's) gates the Connection tab as well as the Id field.  The tab used to be gated on `server`, so an
	//unreachable server had no tab at all;  it now shows its not-connected state, and the base's `!id` clamp (review3 L2)
	//covers the one case left.  The Slug field is hidden on a new row because slug is the connection's identity and the
	//gateway meta refuses to update it, so the form does not offer it.
	get server(): Server|undefined{ return this.row?.server; }
	get serverError(): string|undefined{ return this.row?.serverError; }
	gatewayService:GatewayService = inject( GatewayService );
	private opcStore = inject( OpcStore );
	gateway!:Gateway;
}
