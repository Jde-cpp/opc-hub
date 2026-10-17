import { DatePipe } from '@angular/common';
import { Component, OnInit, inject, input, signal } from '@angular/core';
import { MatIconButton } from '@angular/material/button';
import { MatIcon } from '@angular/material/icon';
import { MatTableModule } from '@angular/material/table';
import { MatToolbar } from '@angular/material/toolbar';
import { MatTooltip } from '@angular/material/tooltip';
import { IGraphQL } from '../../../services/graphql';
import { SnackbarService } from '../../../shared/snackbar/snackbar-service';
import { errorMessage, httpStatus } from '../../../utils/errors';

//A row of the instance's answer (libs/web/server/SessionGraphQL.cpp).  The user is resolved for a signed-in session only:  an
//anonymous one - a request with no Authorization, which is every one a stand-alone OPC server sees - carries neither name.
//No id:  the id is the session's own credential, so a release build never lists it, and nothing here can end a session.
export type WebSession = { loginName?:string; domain?:string; endpoint:string; lastUpdate:string; expiration:string };
//sessions( instanceId:… ):  the application server answers for its own instance and asks another over its socket
//(apps/AppServer/src/ql/InstanceSessionsAwait.cpp) - so every page asks the application server, not the instance.
export const webSessionsQuery = ( instanceId:number ):string=>`sessions( instanceId:${instanceId} ){ loginName domain endpoint lastUpdate expiration }`;
export const sessionUser = ( s:WebSession ):string=>s.loginName ? ( s.domain ? `${s.domain}\\${s.loginName}` : s.loginName ) : "anonymous";
//most recently active first:  what an admin looking for a live user wants at the top.  Both iso strings, so the text order is the time order.
export const byLastUpdate = ( a:WebSession, b:WebSession ):number=>b.lastUpdate.localeCompare( a.lastUpdate );
export const refusedText = "You may not list this instance's web sessions.";

@Component({
	selector: 'web-sessions',
	templateUrl: './web-sessions.html',
	styleUrls: ['./web-sessions.scss'],
	imports: [DatePipe, MatIcon, MatIconButton, MatTableModule, MatToolbar, MatTooltip]
})
export class WebSessions implements OnInit{
	private snackBar:SnackbarService = inject( SnackbarService );

	async ngOnInit(){ await this.load(); }

	async load(){
		this.isLoading.set( true );
		this.error.set( null );
		try{
			const y = await this.service().query<{sessions:WebSession[]}>( webSessionsQuery(this.instanceId()), null, (m)=>console.log(m) );
			this.rows.set( [...(y?.sessions ?? [])].sort(byLastUpdate) );
		}
		catch( e ){
			const status = httpStatus( e );
			if( status==401 || status==403 )//a refusal is a state, not a failure - said in place, no toast.
				this.error.set( refusedText );
			else{
				this.error.set( errorMessage(e, "Could not load the web sessions.") );//errorMessage, not `${e}`: an HttpErrorResponse and a {error:IError} rejection both render "[object Object]".
				this.snackBar.exception( "Could not load the web sessions.", e );
			}
		}
		this.isLoading.set( false );
	}

	service = input.required<IGraphQL>();//the application server's - see webSessionsQuery.
	instanceId = input.required<number>();
	rows = signal<WebSession[]>( [] );
	isLoading = signal( true );
	error = signal<string|null>( null );
	readonly columns = ["user", "endpoint", "lastUpdate", "expiration"];
	user = sessionUser;
}