import { Component, computed, input } from '@angular/core';
import { MatIcon } from '@angular/material/icon';
import { MatProgressSpinner } from '@angular/material/progress-spinner';
import { Server } from '../../../../model/server';

/** A row of the description list:  a label and the server's value for it, empty when the server left the field blank;
 * `title` carries the full value when the one shown is an abbreviation of it. */
export type ServerField = { label: string; value: string; title?: string };

/** `http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256` → `Basic256Sha256`:  every policy shares the prefix, so the
 * fragment is the part that says anything;  a uri without one (or an empty one) is shown as it came. */
export const policyName = ( uri:string ):string=>uri.substring( uri.lastIndexOf('#')+1 );
/** The wire name of the message security mode as words;  anything not listed is shown as it came. */
export const modeName = ( mode:string ):string=>({ SignAndEncrypt: "Sign & Encrypt" } as Record<string,string>)[mode] ?? mode;
//the abbreviated row keeps the full value in its title, so a hover still shows what the server actually sent
const abbreviated = ( label:string, value:string, shown:string ):ServerField=>( shown==value ? {label, value} : {label, value: shown, title: value} );

@Component( {
	selector: 'server-properties',
	templateUrl: './server-properties.html',
	styleUrls: ['./server-properties.scss'],
	imports: [MatIcon, MatProgressSpinner]
})
export class ServerProperties{
	//unset when the gateway has no session with the server:  the tab then says so instead of the page hiding it, with the
	//gateway's own reason - a missing tab told the user nothing about why.
	server = input<Server|undefined>();
	error = input<string|undefined>();
	//the url a Test Connection is trying right now:  shown in place of the last result while the gateway connects, since a
	//connect can take the gateway's full timeout and the stale description or error would otherwise sit there as if it were the answer.
	connecting = input<string|undefined>();
	//Not a form:  nothing here is editable - the gateway reports what the server told it - and a disabled input dims the
	//text and will not let it be selected, when the URIs and the policy are exactly what gets copied out of this page.
	//Read order:  what the server is, then how this gateway talks to it, then how it was found - the discovery URLs list
	//follows straight after so the two discovery fields sit next to it.
	readonly fields = computed<ServerField[]>( ()=>{
		const s = this.server();
		return !s ? [] : [
			{ label: "Application Name", value: s.applicationName },
			{ label: "Application URI", value: s.applicationUri },
			{ label: "Application Type", value: s.applicationType },
			{ label: "Product URI", value: s.productUri },
			abbreviated( "Policy", s.policy, policyName(s.policy) ),
			abbreviated( "Mode", s.mode, modeName(s.mode) ),
			{ label: "Gateway Server URI", value: s.gatewayServerUri },
			{ label: "Discovery Profile URI", value: s.discoveryProfileUri }
		];
	} );
}