import { DatePipe } from '@angular/common';
import { Component, OnInit, computed, inject, input, signal } from '@angular/core';
import { MatIconButton } from '@angular/material/button';
import { MatIcon } from '@angular/material/icon';
import { MatProgressBar } from '@angular/material/progress-bar';
import { MatTableModule } from '@angular/material/table';
import { MatToolbar } from '@angular/material/toolbar';
import { MatTooltip } from '@angular/material/tooltip';
import { IGraphQL, SnackbarService, errorMessage, isRefusal } from 'jde-framework';
import { EndpointSession, GatewayEndpoint as Endpoint, endpointQuery, endpointSessionsQuery, policyName } from '../../../model/gateway-endpoint';

export const refusedText = "You may not list this endpoint's sessions.";
//what a session's user cell says:  the hub user's name, its id where the name wasn't fetched, or that the token carried none
export const sessionUser = ( s:EndpointSession ):string=>s.user ? ( s.user.name || `user ${s.user.id}` ) : s.type=='Anonymous' ? 'anonymous' : 'no hub user';
//oldest first:  the long-held SCADA session at the top, as a _Connection's Since reads.  Both iso strings, so the text order is the time order.
export const bySince = ( a:EndpointSession, b:EndpointSession ):number=>a.since.localeCompare( b.since );

//The gateway's own OPC UA endpoint (servers spec *Web*, #388):  its url, the policies and modes it offers, where a client certificate
//is trusted from and a refused one written - trust stays a file operation, so the tab says where - and the sessions it holds, each
//with its hub user, token type, channel and start.  Nothing without a /gateway/server block, said so.  The sessions are read apart
//from the rest, as their gate is their own (gateway/sessions):  refused, the url and policies still show.
@Component({
	selector: 'gateway-endpoint',
	templateUrl: './gateway-endpoint.html',
	styleUrl: './gateway-endpoint.scss',
	imports: [DatePipe, MatIcon, MatIconButton, MatProgressBar, MatTableModule, MatToolbar, MatTooltip]
})
export class GatewayEndpoint implements OnInit{
	#snackBar = inject( SnackbarService );

	async ngOnInit(){ await this.load(); }

	async load(){
		this.isLoading.set( true );
		this.error.set( null );
		try{
			const y = await this.gateway().query<{endpoint:Endpoint|null}>( endpointQuery );
			this.endpoint.set( y?.endpoint ?? null );
			if( y?.endpoint )
				await this.loadSessions();
		}
		catch( e ){
			this.endpoint.set( undefined );
			this.error.set( errorMessage(e, "Could not read the endpoint.") );
			this.#snackBar.exception( "Could not read the endpoint.", e );
		}
		this.isLoading.set( false );
	}
	async loadSessions(){
		this.sessionsError.set( null );
		try{
			const y = await this.gateway().query<{endpoint:{sessions:EndpointSession[]}|null}>( endpointSessionsQuery );
			this.sessions.set( [...(y?.endpoint?.sessions ?? [])].sort(bySince) );
		}
		catch( e ){
			this.sessions.set( [] );//not a retry's stale rows under the failure it replaced
			if( isRefusal(e) )//a refusal is a state, not a failure - said in place, no toast.
				this.sessionsError.set( refusedText );
			else{
				this.sessionsError.set( errorMessage(e, "Could not list the sessions.") );
				this.#snackBar.exception( "Could not list the sessions.", e );
			}
		}
	}

	gateway = input.required<IGraphQL>();//the gateway's own service:  endpoint{…} is its custom query
	endpoint = signal<Endpoint|null|undefined>( undefined );//null:  the gateway has none;  undefined:  not read yet, or the read failed
	sessions = signal<EndpointSession[]>( [] );
	isLoading = signal( true );
	error = signal<string|null>( null );
	sessionsError = signal<string|null>( null );
	//the policies by name, each with the modes offered on it:  what a client picks from
	policies = computed( ()=>{
		const byName = new Map<string,string[]>();
		for( const p of this.endpoint()?.policies ?? [] )
			byName.set( policyName( p.uri ), [...(byName.get( policyName( p.uri ) ) ?? []), p.mode] );
		return [...byName].map( ([name, modes])=>({name, modes}) );
	} );
	readonly columns = ["user", "type", "channel", "since"];
	user = sessionUser;
	channel = ( s:EndpointSession ):string=>`${policyName( s.policy )} / ${s.mode}`;
}