import { Component, computed, model, OnInit, signal } from '@angular/core';
import { MatAutocompleteModule } from '@angular/material/autocomplete';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatInputModule } from '@angular/material/input';
import { Properties } from 'jde-framework';
import { openProtocolDefaultPort, timeZones } from '../../../../model/open-protocol';
import { OpenProtocolFieldName, ServerCnnctn } from '../../../../model/server-cnnctn';

//An Open Protocol connection's own fields (docs/open-protocol/spec.md *The row*, *Web*), in the Properties form after the Kind pick, in
//place of the url and the certificate URI a UA row shows:  the controller's host and port, which compose the row's url, and its time
//zone and, behind a disclosure, the revision ceilings, which compose its `driver`.  Each writes the input's string onto the record as
//the form's own fields do (Properties.onChange), and the record says what is wrong under the field (ServerCnnctn.fieldError).
@Component({
	selector: 'open-protocol-fields',
	templateUrl: './open-protocol-fields.html',
	styleUrl: './open-protocol-fields.scss',
	imports: [MatAutocompleteModule, MatFormFieldModule, MatInputModule]
})
export class OpenProtocolFields implements OnInit{
	record = model.required<any>();//the page's edited connection, as Properties' record
	ngOnInit(){ this.ceilingsOpen.set( !!this.record()?.revisions?.trim() ); }//open where the row has ceilings, so a saved one is seen;  the user's toggle keeps it after
	onChange( field:OpenProtocolFieldName, value:string ){ this.record.set( Properties.edited(ServerCnnctn, this.record(), {[field]: value}) ); }
	fieldError( field:OpenProtocolFieldName ):string|undefined{ return this.record()?.fieldError?.( field ); }
	readonly defaultPortText = String( openProtocolDefaultPort );//the port field's placeholder
	readonly hints = {
		host: "The controller, or its virtual station, by name or address.",
		port: `Empty:  ${openProtocolDefaultPort}, Open Protocol's port.  A virtual station of a Power Focus 6000 listens on a port of its own.`,
		timeZone: "Empty:  the gateway's own zone.  The zone the controller's clock keeps, Europe/Stockholm:  each result is stamped in it.",
		revisions: "Empty:  the highest revision the driver speaks of each MID.  MID:revision pairs, 0061:3, cap what it asks a controller that answers a revision badly."
	};
	//the zones the browser names, narrowed to what is typed - fifty at most, so the panel stays a list and not the database
	readonly #zones = timeZones();
	readonly zoneOptions = computed<string[]>( ()=>{
		const typed = String( this.record()?.timeZone ?? "" ).trim().toLowerCase();
		return typed ? this.#zones.filter( z=>z.toLowerCase().includes(typed) ).slice( 0, 50 ) : [];
	} );
	ceilingsOpen = signal( false );
	onToggle( open:boolean ){ this.ceilingsOpen.set( open ); }
}