import { Component, computed, input, output } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatIcon } from '@angular/material/icon';
import { MatProgressSpinner } from '@angular/material/progress-spinner';
import { modeName, policyName } from '../../../../model/gateway-endpoint';
import { ClientCertificate, RejectedCertificate, Server } from '../../../../model/server';

/** A row of the description list:  a label and the server's value for it, empty when the server left the field blank;
 * `title` carries the full value when the one shown is an abbreviation of it. */
export type ServerField = { label: string; value: string; title?: string };

//the abbreviated row keeps the full value in its title, so a hover still shows what the server actually sent
const abbreviated = ( label:string, value:string, shown:string ):ServerField=>( shown==value ? {label, value} : {label, value: shown, title: value} );

@Component( {
	selector: 'server-properties',
	templateUrl: './server-properties.html',
	styleUrls: ['./server-properties.scss'],
	imports: [MatButtonModule, MatIcon, MatProgressSpinner]
})
export class ServerProperties{
	//unset when the gateway has no session with the server:  the tab then says so instead of the page hiding it, with the
	//gateway's own reason - a missing tab told the user nothing about why.
	server = input<Server|undefined>();
	error = input<string|undefined>();
	//a new connection never tried:  not a failure, so not the Not connected state
	untested = input( false );
	//the url a Test Connection is trying right now:  shown in place of the last result while the gateway connects, since a
	//connect can take the gateway's full timeout and the stale description or error would otherwise sit there as if it were the answer.
	connecting = input<string|undefined>();
	//the settings the result was for, when the form has changed since:  the result is marked, not cleared, since it may be the saved row's own describe.
	staleFor = input<string|undefined>();
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

	//the server certificate the gateway rejected for this connection (docs/clients/spec.md *Trust*, #415):  shown under the
	//Not connected reason, with Trust for an admin.  Null when there is none.
	rejected = input<RejectedCertificate|null>( null );
	//the gateway's own certificate for the connection, the one it presents - shown with a PEM download, so carrying it to a
	//third-party server's trust list is a button, not a copy-by-path.  Null when its file does not exist.
	client = input<ClientCertificate|null>( null );
	//a trustServer in flight:  the Trust button then reads Trusting… and is disabled, as Test Connection does.
	trusting = input( false );
	//the admin pressed Trust:  the rejected certificate's thumbprint, which the page hands trustServer.  The page gates on the
	//right and shows the refusal - a viewer sees Trust and a toast, as Test Connection is (the server is the gate).
	trust = output<string>();
	readonly rejectedFields = computed<ServerField[]>( ()=>{
		const c = this.rejected();
		return !c ? [] : [
			{ label: "Subject", value: c.subject },
			{ label: "Issuer", value: c.issuer },
			{ label: "Valid From", value: c.notBefore },
			{ label: "Valid To", value: c.notAfter },
			{ label: "Application URI", value: c.uri },
			{ label: "Thumbprint", value: c.thumbprint }
		];
	} );
	readonly clientFields = computed<ServerField[]>( ()=>{
		const c = this.client();
		return !c ? [] : [
			{ label: "Subject", value: c.subject },
			{ label: "Application URI", value: c.uri },
			{ label: "Valid From", value: c.notBefore },
			{ label: "Valid To", value: c.notAfter },
			{ label: "File", value: c.path }
		];
	} );
	onTrust():void{
		const c = this.rejected();
		if( c && !this.trusting() )
			this.trust.emit( c.thumbprint );
	}
	//the gateway's certificate as a .pem file:  a Blob the browser saves, no new route - the PEM is already on the row.
	downloadPem():void{
		const c = this.client();
		if( !c )
			return;
		const blob = new Blob( [c.pem], {type: 'application/x-pem-file'} );
		const url = URL.createObjectURL( blob );
		const a = document.createElement( 'a' );
		a.href = url;
		a.download = `${this.certificateName(c)}.pem`;
		a.click();
		URL.revokeObjectURL( url );
	}
	//the file's stem for the download name, else "gateway-certificate" - the path is the gateway's, "<slug>.pem" as issued.
	private certificateName( c:ClientCertificate ):string{
		const stem = c.path.split( /[/\\]/ ).pop()?.replace( /\.pem$/i, '' );
		return stem || 'gateway-certificate';
	}
}