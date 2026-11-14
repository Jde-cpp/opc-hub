import { DatePipe } from '@angular/common';
import { Component, computed, inject, input, LOCALE_ID, output } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatIcon } from '@angular/material/icon';
import { MatProgressSpinner } from '@angular/material/progress-spinner';
import Long from 'long';
import { ProtoUtils, Timestamp } from 'jde-framework';
import { modeName, policyName } from '../../../../model/gateway-endpoint';
import { identityFields, OpDiagnostics, OpenProtocolIdentity, OpName, OpReading, scBadNoCommunication, scBadNotSupported, scBadWaitingForInitialData } from '../../../../model/open-protocol';
import { ClientCertificate, RejectedCertificate, Server } from '../../../../model/server';
import { statusText } from '../../../../model/status-code';

/** A row of the description list:  a label and the server's value for it, empty when the server left the field blank;
 * `title` carries the full value when the one shown is an abbreviation of it;  `status` is what a variable reads in place of a value. */
export type ServerField = { label: string; value: string; title?: string; status?: string };

//the abbreviated row keeps the full value in its title, so a hover still shows what the server actually sent
const abbreviated = ( label:string, value:string, shown:string ):ServerField=>( shown==value ? {label, value} : {label, value: shown, title: value} );
//a `_Op` variable's status in place of a value (docs/open-protocol/spec.md *Address space*):  the two it reads before a value comes, the
//link's loss, else the app's status text
export const opStatusText = ( sc:number|undefined ):string=>sc==scBadWaitingForInitialData ? "waiting for the first read" : sc==scBadNotSupported ? "not supported by the controller" : sc==scBadNoCommunication ? "no communication" : statusText( sc );
//a UInt64 as the gateway writes it, a number or the Long form {low, high, unsigned}
export const countText = ( v:unknown ):string=>{
	if( typeof v=='number' )
		return String( v );
	if( v && typeof v=='object' && 'low' in v && 'high' in v )
		return new Long( Number((v as any).low), Number((v as any).high), !!(v as any).unsigned ).toString();
	return v==null ? "" : String( v );
};
//the controller's clock less the gateway's, in seconds, signed
export const skewText = ( v:unknown ):string=>typeof v=='number' ? `${v>0 ? "+" : ""}${v.toFixed( 1 )} s` : v==null ? "" : String( v );

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
	//An Open Protocol row (docs/open-protocol/spec.md *Web*):  the form's kind, so the untested state says what the test asks;  the
	//controller's identity a Test Connection answered in place of a server's description;  and a saved row's `_Op`, its driver's
	//diagnostics read by id, shown in place of the driver server's description, which says nothing of the controller - the
	//diagnostics, or why they could not be read, or that they are being read.
	openProtocol = input( false );
	identity = input<OpenProtocolIdentity|undefined>();
	op = input<OpDiagnostics|undefined>();
	opError = input<string|undefined>();
	opReading = input( false );
	readonly showOp = computed( ()=>!!this.op() || !!this.opError() || this.opReading() );
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
	//MID 0002's fields as the test answered them, a row per field whether or not the controller's revision carried it
	readonly identityRows = computed<ServerField[]>( ()=>{ const id = this.identity(); return id ? identityFields( id ) : []; } );
	//`_Op` in the spec's order, but for the identity, whose json text is spelled out as the test's answer is, where the link stands:
	//what is read before a value comes is its status, in words
	readonly opFields = computed<ServerField[]>( ()=>{
		const op = this.op();
		if( !op )
			return [];
		const row = ( label:string, name:OpName, text:( v:unknown )=>string ):ServerField=>{
			const r:OpReading|undefined = op[name];
			if( !r )
				return { label, value: "", status: "not answered" };
			return r.value===undefined ? { label, value: "", status: opStatusText( r.sc ) } : { label, value: text( r.value ) };
		};
		const yesNo = ( v:unknown )=>v===true ? "yes" : v===false ? "no" : v==null ? "" : String( v );
		const str = ( v:unknown )=>v==null ? "" : String( v );
		const identity = op.Identity;
		let identityRows:ServerField[];
		if( identity?.value===undefined )
			identityRows = [{ label: "Controller", value: "", status: identity ? opStatusText( identity.sc ) : "not answered" }];
		else{
			let parsed:OpenProtocolIdentity|undefined;
			try{ parsed = typeof identity.value=='string' && identity.value.trim() ? JSON.parse( identity.value ) : undefined; }
			catch{ parsed = undefined; }
			identityRows = parsed ? identityFields( parsed ) : [{ label: "Controller", value: typeof identity.value=='string' ? identity.value : "" }];
		}
		return [
			row( "Connected", "Connected", yesNo ),
			row( "Since", "Since", v=>this.#time( v ) ),
			row( "Last Error", "LastError", str ),
			row( "Reconnects", "Reconnects", countText ),
			row( "URL", "Url", str ),
			...identityRows,
			row( "Link Level", "LinkLevel", yesNo ),
			row( "Revisions", "Revisions", str ),
			row( "Clock Skew", "Skew", skewText ),
			row( "Results", "Results", countText ),
			row( "Lost Results", "LostResults", countText )
		];
	} );
	//a DateTime as the gateway writes it, {seconds, nanos}, in the viewer's locale - the Server tab's form;  blank for none, the text
	//as it came for one that is no date
	#time( v:unknown ):string{
		if( v && typeof v=='object' && typeof (v as {seconds?:unknown}).seconds=='number' ){
			const d = ProtoUtils.toDate( { nanos: 0, ...v } as Timestamp );
			return d ? this.#date.transform( d, 'medium' ) ?? "" : "";
		}
		if( typeof v!='string' || !v )
			return "";
		const d = new Date( v );
		return isNaN( d.getTime() ) ? v : this.#date.transform( d, 'medium' ) ?? v;
	}
	#date = new DatePipe( inject(LOCALE_ID) );

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
	//the gateway's certificate as a .pem file:  a Blob the browser saves, no new route - the PEM is already on the row.  The anchor is
	//attached for its click, and the url outlives it:  Chrome starts the fetch inside click(), Firefox and Safari after it returns,
	//and a url revoked by then saves nothing.  Revoked long after, as FileSaver.js does - the blob is a few kB.
	downloadPem():void{
		const c = this.client();
		if( !c )
			return;
		const blob = new Blob( [c.pem], {type: 'application/x-pem-file'} );
		const url = URL.createObjectURL( blob );
		const a = document.createElement( 'a' );
		a.href = url;
		a.download = `${this.certificateName(c)}.pem`;
		a.hidden = true;
		document.body.append( a );
		a.click();
		a.remove();
		setTimeout( ()=>URL.revokeObjectURL(url), 40_000 );
	}
	//the file's stem for the download name, else "gateway-certificate" - the path is the gateway's, "<slug>.pem" as issued.
	private certificateName( c:ClientCertificate ):string{
		const stem = c.path.split( /[/\\]/ ).pop()?.replace( /\.pem$/i, '' );
		return stem || 'gateway-certificate';
	}
}