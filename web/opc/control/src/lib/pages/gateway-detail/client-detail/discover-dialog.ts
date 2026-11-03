import { Component, computed, inject, signal } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MAT_DIALOG_DATA, MatDialog, MatDialogActions, MatDialogContent, MatDialogRef, MatDialogTitle } from '@angular/material/dialog';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatTableModule } from '@angular/material/table';
import { MatTooltipModule } from '@angular/material/tooltip';
import { errorMessage } from 'jde-framework';
import { DiscoveredEndpoint, DiscoveredServer, discoverEndpointsMutation, discoverServersMutation, EndpointPick, endpointPick, serverUrl } from '../../../model/discovery';
import { modeName, policyName } from '../../../model/gateway-endpoint';
import { Gateway } from '../../../services/gateway-service';

export type DiscoverDialogData = {
	gateway:Gateway;
	url:string;//the form's url, where the discovery starts
};
//The connection page's Discover (docs/clients/spec.md *Discovery*):  the servers found at the url typed - a Local Discovery Server
//lists what registered with it, a server answers with itself - then the endpoints of the one picked, each with its policy, mode,
//the token types it takes and the certificate the server presents on it, and a pick closes with what fills the form.  One server
//found opens its endpoints at once;  a list is picked from, and Back returns to it.  The url is editable here, so another
//discovery url - or the typed one in place of a server's advertised one the gateway cannot reach - is tried without leaving.
@Component({
	templateUrl: './discover-dialog.html',
	styleUrls: ['../hist-admin/hist-admin-shared.scss', './discover-dialog.scss'],
	imports: [MatButtonModule, MatDialogActions, MatDialogContent, MatDialogTitle, MatFormFieldModule, MatIconModule, MatInputModule, MatProgressBarModule, MatTableModule, MatTooltipModule]
})
export class DiscoverDialog{
	constructor(){ this.findServers(); }
	dialogRef = inject<MatDialogRef<DiscoverDialog,EndpointPick|undefined>>( MatDialogRef );
	data = inject<DiscoverDialogData>( MAT_DIALOG_DATA );
	url = signal( this.data.url.trim() );//the discovery url, as typed on the form or edited here
	servers = signal<DiscoveredServer[]>( [] );
	server = signal<DiscoveredServer|undefined>( undefined );//the one picked, whose endpoints are shown
	endpointsAt = signal<string|undefined>( undefined );//the url the endpoints were read at
	endpoints = signal<DiscoveredEndpoint[]>( [] );
	loading = signal( false );
	error = signal<string|undefined>( undefined );
	readonly serverColumns = ['name', 'uri', 'type', 'urls'];
	readonly endpointColumns = ['url', 'policy', 'mode', 'tokens', 'certificate', 'choose'];
	readonly canFind = computed( ()=>!this.loading() && !!this.url().trim() );
	//the endpoints a server advertises under a url the gateway did not reach, or the typed url's own:  the typed one offered in its place
	readonly typedInstead = computed( ()=>{ const at = this.endpointsAt(), typed = this.url().trim(); return at && typed && at!=typed ? typed : undefined; } );

	//FindServers at the url:  the list, or straight to the endpoints of the one server found
	async findServers():Promise<void>{
		const url = this.url().trim();
		this.server.set( undefined );
		this.endpoints.set( [] );
		this.endpointsAt.set( undefined );
		this.servers.set( [] );
		if( !url )
			return;
		const servers = await this.#load( ()=>this.data.gateway.postQL<{discover:DiscoveredServer[]}>( discoverServersMutation, {url} ) );
		if( !servers )
			return;
		this.servers.set( servers.discover ?? [] );
		if( this.servers().length==1 )
			await this.pick( this.servers()[0] );
	}
	//GetEndpoints at the server's own discovery url - the typed one when it advertises none
	async pick( server:DiscoveredServer ):Promise<void>{
		this.server.set( server );
		await this.getEndpoints( serverUrl( server, this.url().trim() ) );
	}
	async getEndpoints( url:string ):Promise<void>{
		this.endpoints.set( [] );
		this.endpointsAt.set( url );
		const endpoints = await this.#load( ()=>this.data.gateway.postQL<{discover:DiscoveredEndpoint[]}>( discoverEndpointsMutation, {url} ) );
		if( endpoints )
			this.endpoints.set( (endpoints.discover ?? []).slice().sort( DiscoverDialog.order ) );
	}
	async tryTyped():Promise<void>{
		const typed = this.typedInstead();
		if( typed )
			await this.getEndpoints( typed );
	}
	back():void{
		this.server.set( undefined );
		this.endpoints.set( [] );
		this.endpointsAt.set( undefined );
		this.error.set( undefined );
	}
	choose( ep:DiscoveredEndpoint ):void{ this.dialogRef.close( endpointPick( ep ) ); }
	//the server's own ranking first, strongest on top, then by url - what a UA client's connection dialog does
	static order( a:DiscoveredEndpoint, b:DiscoveredEndpoint ):number{ return b.securityLevel-a.securityLevel || a.url.localeCompare( b.url ); }
	policyName( ep:DiscoveredEndpoint ):string{ return policyName( ep.policy ); }
	modeName( ep:DiscoveredEndpoint ):string{ return modeName( ep.mode ); }
	tokens( ep:DiscoveredEndpoint ):string{ return [...new Set( ep.tokenPolicies.map( p=>p.type ) )].join( ', ' ); }
	//each token type with the policy that encrypts it - `@None` is a token sent in the clear
	tokensTitle( ep:DiscoveredEndpoint ):string{ return ep.tokenPolicies.map( p=>`${p.type}@${policyName( p.policy )}` ).join( ', ' ); }
	certificateText( ep:DiscoveredEndpoint ):string{ const c = ep.certificate; return !c ? 'none' : c.error ? 'could not be read' : c.uri || c.subject; }
	certificateTitle( ep:DiscoveredEndpoint ):string{
		const c = ep.certificate;
		return !c ? 'The endpoint carries no certificate.' : c.error ? c.error : `Subject ${c.subject}\nIssuer ${c.issuer}\nValid ${c.notBefore} to ${c.notAfter}\nThumbprint ${c.thumbprint}`;
	}
	serverName( s:DiscoveredServer ):string{ return s.applicationName || s.applicationUri; }
	urls( s:DiscoveredServer ):string{ return (s.discoveryUrls ?? []).join( ', ' ); }
	//`work` under the bar, its refusal in the error line
	async #load<T>( work:()=>Promise<T> ):Promise<T|undefined>{
		this.loading.set( true );
		this.error.set( undefined );
		try{
			return await work();
		}
		catch( e ){
			this.error.set( errorMessage( e ) );
			return undefined;
		}
		finally{
			this.loading.set( false );
		}
	}
	//what was picked, or undefined for a cancel
	static open( dialog:MatDialog, data:DiscoverDialogData ):Promise<EndpointPick|undefined>{
		const ref = dialog.open<DiscoverDialog,DiscoverDialogData,EndpointPick|undefined>( DiscoverDialog, {data, width: '960px', maxWidth: '95vw', ariaModal: true} );
		return new Promise( resolve=>ref.afterClosed().subscribe( pick=>resolve( pick ) ) );
	}
}