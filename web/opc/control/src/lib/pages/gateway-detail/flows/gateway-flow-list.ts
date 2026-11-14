import { Component, inject, input, signal } from '@angular/core';
import { ActivatedRoute, Router } from '@angular/router';
import { MatButtonModule } from '@angular/material/button';
import { MatDialog } from '@angular/material/dialog';
import { MatIconModule } from '@angular/material/icon';
import { SnackbarService } from 'jde-framework';
import { Gateway } from '../../../services/gateway-service';
import { FlowService } from '../../../services/flow-service';
import { GatewayHistList } from '../hist-admin/gateway-hist-list';
import { FlowCreateDialog } from './flow-create-dialog';
import { FlowDrafts } from './flow-drafts';

//The Flows tab of a gateway's page (#455), beside Groups:  the flows as a list on the historian lists' shape (GatewayHistList), a
//row opening `flows/<slug>` under the page, with Create and Import above it.  Either makes the flow's row and opens its page;  an
//import's graph goes with it as the editor's draft (FlowDrafts), undeployed.
@Component({
	selector: 'gateway-flow-list',
	template: `
		<div class="actions">
			<button matButton="filled" type="button" (click)="create(false)" [disabled]="busy()"><mat-icon>add</mat-icon>Create</button>
			<button matButton="outlined" type="button" (click)="create(true)" [disabled]="busy()"><mat-icon>upload_file</mat-icon>Import</button>
		</div>
		<gateway-hist-list [gateway]="gateway()" kind="flows" [url]="url()"></gateway-hist-list>`,
	styles: ':host{ display: block; } .actions{ display: flex; gap: 8px; padding: 12px 24px 0; }',
	imports: [MatButtonModule, MatIconModule, GatewayHistList]
})
export class GatewayFlowList{
	gateway = input.required<Gateway>();
	url = input.required<string>();//the page's url, apps/gateways/<instance>
	busy = signal( false );
	#flows = inject( FlowService );
	#drafts = inject( FlowDrafts );
	#dialog = inject( MatDialog );
	#router = inject( Router );
	#route = inject( ActivatedRoute );
	#snackbar = inject( SnackbarService );

	async create( imported:boolean ):Promise<void>{
		const made = await FlowCreateDialog.open( this.#dialog, {import: imported} );
		if( !made )
			return;
		this.busy.set( true );
		try{
			const flow = await this.#flows.create( this.gateway(), made );
			if( made.graph && flow.guid )
				this.#drafts.put( flow.guid, made.graph );
			await this.#router.navigate( ['flows', made.slug], {relativeTo: this.#route} );
		}
		catch( e ){
			this.#snackbar.exception( `Could not ${imported ? 'import' : 'create'} '${made.name}'.`, e );
		}
		finally{
			this.busy.set( false );
		}
	}
}