import { Component, computed, signal } from '@angular/core';
import { MatIconModule } from '@angular/material/icon';
import { MatTabsModule } from '@angular/material/tabs';
import { DetailActions, DetailPage, errorMessage, PickOption, Properties } from 'jde-framework';
import { oeeCategories, OeeReason, OeeReasonProps } from '../../../model/oee-admin';
import { Gateway } from '../../../services/gateway-service';
import { GatewayDetailData } from '../../../services/resolvers/client-resolver';

const picks:Record<string,PickOption[]> = { category: oeeCategories.map( c=>({value: c, label: c}) ) };
const hints = { category: 'Unplanned counts against availability, Planned is a planned stop, and Excluded is out of planned time.', code: "The value a machine's Reason signal reports for it.  Blank for a reason only operators pick." };

//A reason's page (#404, docs/oee/spec.md *Reasons and codings*):  its row - the name, slug and description, its category and the
//code a Reason signal reports for it - saved as a group's is, a refusal said under the form:  a code another reason has, a
//deleted one too.  The gateway is the row's, from HistAdminResolver.
@Component({
	template: `
		<mat-tab-group class="my-content" mat-stretch-tabs="false" mat-align-tabs="start" [(selectedIndex)]="tabIndex">
			<mat-tab>
				<ng-template mat-tab-label>Properties</ng-template>
				<ng-template matTabContent>
					<properties [ctor]=ctor [(record)]=properties type="oeeReason" [schema]=schema [order]=fieldOrder [readonlyFields]="isNew ? [] : ['slug']" [picks]=picks [hints]=hints></properties>
					@if( saveError(); as e ){
						<section class="form-section"><p class="refused" role="alert"><mat-icon aria-hidden="true" fontIcon="error"></mat-icon>{{ e }}</p></section>
					}
				</ng-template>
			</mat-tab>
		</mat-tab-group>
		<detail-actions [page]="this"/><!--a deleted reason keeps resolving the stops it coded, and leaves the pickers-->`,
	styleUrl: '../hist-admin/hist-admin-shared.scss',
	//the trailing class is load-bearing:  Angular hashes a component's *shape* into its style-encapsulation id and leaves the class
	//name out, so the routed pages that share DetailPage and this host string could collide with NG0912.
	host: {class: 'main-content my-content oee-reason-detail'},
	imports: [DetailActions, MatIconModule, MatTabsModule, Properties]
})
export class OeeReasonDetail extends DetailPage<OeeReason>{
	constructor(){ super( 'oee-reason-detail' ); }

	protected override get ctor(){ return OeeReason; }
	protected override onRow(){ this.gateway = (<GatewayDetailData<OeeReason>>this.pageData).gateway; }
	protected override upsert():OeeReason{ return new OeeReason( <OeeReasonProps>this.properties() ); }
	protected override get title(){ return this.row.name ? `${this.row.name} - Reason` : 'New Reason'; }
	protected override saveFailed( e:unknown ):void{ this.#refusal.set( {of: this.properties(), text: errorMessage(e)} ); }
	protected override get deleteWarning(){ return 'A deleted reason keeps resolving the stops it coded, and leaves the pickers.  Restore brings it back.'; }
	override get ql(){ return this.gateway; }//per-gateway, as ClientDetail's

	readonly fieldOrder = ['slug', 'name', 'description', 'category', 'code'];
	readonly picks = picks;
	readonly hints = hints;
	//the server's refusal of the last Save, said while the form is as it was sent:  an edit answers it
	#refusal = signal<{of:unknown; text:string}|undefined>( undefined );
	saveError = computed( ()=>this.#refusal()?.of===this.properties() ? this.#refusal()!.text : undefined );
	gateway!:Gateway;
}