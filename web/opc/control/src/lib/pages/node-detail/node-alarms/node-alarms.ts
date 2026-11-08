import { ChangeDetectorRef, Component, computed, effect, inject, input, OnDestroy, signal, untracked } from '@angular/core';
import { DatePipe } from '@angular/common';
import { MatButtonModule } from '@angular/material/button';
import { MatDialog } from '@angular/material/dialog';
import { MatIconModule } from '@angular/material/icon';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatTableModule } from '@angular/material/table';
import { MatToolbarModule } from '@angular/material/toolbar';
import { MatTooltipModule } from '@angular/material/tooltip';
import { Subscription } from 'rxjs';
import { SnackbarService } from 'jde-framework';
import { acknowledgeMethod, addCommentMethod, AlarmRow, Alarms, confirmMethod, conditionSelect, sortAlarms } from '../../../model/alarm';
import { UaNode } from '../../../model/node';
import { NodeId } from '../../../model/node-id';
import { OpcError } from '../../../model/opc-error';
import { isBad, scHex } from '../../../model/status-code';
import { StatusCode } from '../../../model/types';
import { EventPush, Gateway, gatewayAnswer } from '../../../services/gateway-service';
import { NodePageData } from '../../../services/resolvers/node-resolver';
import { AlarmCommentDialog } from './alarm-comment-dialog';

//The Alarms tab (clients spec *Alarms and conditions*, #426), on a node whose EventNotifier has the SubscribeToEvents bit:  the Events
//tab's stream, asked with the condition fields, kept by ConditionId (Alarms) - a row per condition with its latest event, the source,
//name, branch, severity, message, the four states, the comment and the transition's time - and gone once the server says Retain is
//false.  When it opens, and on **Refresh**, `refreshConditions` has the server replay every retained condition to this connection's
//subscription, so the retained conditions arrive before the live ones;  on the Server object the replay comes between RefreshStart
//and RefreshEnd, which drop the rows the server no longer retains.  **Acknowledge**, **Confirm** and **Comment** call the type's
//method on the condition node with the row's EventId and a comment, and a refusal - Bad_ConditionBranchAlreadyAcked, say - shows its
//status on the row until the condition's next event.  A refusal of the subscription shows its status;  a connection lost subscribes again.
@Component({
	selector: 'node-alarms',
	templateUrl: './node-alarms.html',
	styleUrl: './node-alarms.scss',
	imports: [DatePipe, MatButtonModule, MatIconModule, MatProgressBarModule, MatTableModule, MatToolbarModule, MatTooltipModule]
})
export class NodeAlarms implements OnDestroy{
	#snackbar = inject( SnackbarService );
	#cdRef = inject( ChangeDetectorRef );
	#dialog = inject( MatDialog );
	constructor(){
		effect( ()=>{ const node = this.node(); untracked( ()=>this.#restart( node ) ); } );//the router reuses the page across nodes
	}
	ngOnDestroy(){ this.#destroyed = true; this.#unsubscribe(); }
	#destroyed = false;

	pageData = input.required<NodePageData>();
	node = input.required<UaNode>();
	readonly columns = ['source', 'name', 'branch', 'severity', 'message', 'enabled', 'active', 'acked', 'confirmed', 'comment', 'time', 'actions'];
	rows = signal<AlarmRow[]>( [] );
	sorted = computed( ()=>sortAlarms( this.rows() ) );
	refreshing = signal( false );//between RefreshStart and RefreshEnd
	busy = signal( false );//a refreshConditions out
	error = signal<string|undefined>( undefined );//a refusal's status, or why the subscription ended
	refreshStatus = signal<string|undefined>( undefined );//what the server answered the refresh with, when not Good
	live = signal( false );
	#alarms = new Alarms();

	#restart( node:UaNode ){
		this.#unsubscribe();
		this.#alarms.clear();
		this.#sync();
		this.error.set( undefined );
		this.refreshStatus.set( undefined );
		this.#subscribe( node );
	}
	#subscribe( node:UaNode ){
		const gateway = this.pageData().gateway;
		this.#subscription = gateway.subscribeEvents( this.cnnctnSlug, node.nodeId, [...conditionSelect], this.#owner ).subscribe( {
			next: e=>this.#onEvent( e ),
			error: ( e:unknown )=>{
				this.#subscription = undefined;
				this.live.set( false );
				const answer = gatewayAnswer( e );
				if( e instanceof OpcError )//refused:  the node, as the server read it - no retry would change it
					this.#showRefused( e.sc, "the subscription" );
				else if( answer && answer.httpStatus>=400 && answer.httpStatus<500 )
					this.error.set( `The gateway refused the subscription:  ${answer.message}` );
				else{//the socket went, or the gateway could not do it yet:  the item is asked for again
					this.error.set( answer ? `The gateway could not subscribe:  ${answer.message}  Trying again.` : "Connection to the gateway lost.  The subscription resumes once it is back." );
					clearTimeout( this.#retry );
					this.#retry = setTimeout( ()=>{ if( !this.#destroyed && node===this.node() && !this.#subscription ) this.#subscribe( node ); }, Gateway.histRetryDelay );
				}
			}
		} );
		this.live.set( true );
		this.error.set( undefined );
	}
	#showRefused( sc:StatusCode, what:string ){
		const text = ()=>`The server refused ${what}:  ${scHex( sc )} ${OpcError.text( sc )}.`;
		this.error.set( text() );
		this.#nameOf( sc, ()=>{ if( this.error() ) this.error.set( text() ); } );
	}
	//the code's name fetched where this browser lacks it, then `shown` again
	#nameOf( sc:StatusCode, shown:()=>void ){
		if( !OpcError.statusCodeText( sc ) )
			this.pageData().gateway.updateErrorCodes().then( ()=>{ if( !this.#destroyed ) shown(); } );
	}
	#subscription:Subscription|undefined;
	#retry:ReturnType<typeof setTimeout>|undefined;
	readonly #owner = `nodeAlarms/${Math.random().toString( 36 ).slice( 2 )}`;
	#unsubscribe(){
		clearTimeout( this.#retry );
		this.#subscription?.unsubscribe();
		this.#subscription = undefined;
		this.live.set( false );
	}
	#onEvent( e:EventPush ){
		if( e.refused )
			return this.#showRefused( e.refused, "the field list" );
		if( e.acked )//the item is up:  the retained conditions, before the live ones
			return void this.refresh();
		if( this.#alarms.apply( e.fields ) )
			this.#sync();
	}
	#sync(){
		this.rows.set( this.#alarms.rows );
		this.refreshing.set( this.#alarms.refreshing );
		this.#cdRef.markForCheck();
	}
	//ConditionRefresh with this connection's subscription:  the server's status where it is not Good
	async refresh(){
		if( this.busy() )
			return;
		const node = this.node(), gateway = this.pageData().gateway;
		this.busy.set( true );
		this.refreshStatus.set( undefined );
		try{
			const sc = await gateway.refreshConditions( this.cnnctnSlug, node.nodeId );
			if( this.#destroyed || node!==this.node() )
				return;
			if( isBad( sc ) ){
				const text = ()=>`${scHex( sc )} ${OpcError.text( sc )}`;
				this.refreshStatus.set( text() );
				this.#nameOf( sc, ()=>{ if( this.refreshStatus() ) this.refreshStatus.set( text() ); } );
			}
		}
		catch( e ){
			if( !this.#destroyed )
				this.#snackbar.exception( "Could not refresh the conditions.", e );
		}
		finally{
			if( !this.#destroyed )
				this.busy.set( false );
		}
	}
	//Acknowledge wants an unacknowledged condition, Confirm an acknowledged and unconfirmed one;  a server that gave no state is asked, and refuses what it will not take
	canAcknowledge( r:AlarmRow ):boolean{ return r.acked!==true; }
	canConfirm( r:AlarmRow ):boolean{ return r.confirmed!==true && r.acked!==false; }
	acknowledge( r:AlarmRow ){ return this.#act( r, "Acknowledge", acknowledgeMethod ); }
	confirm( r:AlarmRow ){ return this.#act( r, "Confirm", confirmMethod ); }
	comment( r:AlarmRow ){ return this.#act( r, "Comment", addCommentMethod ); }
	//the type's method on the condition node with the row's EventId and the comment asked for:  a refusal is the call's status, shown on the row
	async #act( r:AlarmRow, action:string, method:NodeId ){
		const comment = await AlarmCommentDialog.open( this.#dialog, {action, condition: r.name || r.conditionId.uaString(), message: r.message} );
		if( comment===undefined || this.#destroyed )
			return;
		try{
			const result = await this.pageData().gateway.call( this.cnnctnSlug, r.conditionId, method, [r.eventId!, {text: comment}] );
			if( this.#destroyed )
				return;
			const sc = result.statusCode;
			if( isBad( sc ) ){
				const text = ()=>`${action} refused:  ${scHex( sc )} ${OpcError.text( sc )}`;
				this.#alarms.problem( r.key, text() );
				this.#nameOf( sc, ()=>{ this.#alarms.problem( r.key, text() ); this.#sync(); } );
			}
			else
				this.#alarms.problem( r.key, undefined );
			this.#sync();
		}
		catch( e ){
			if( !this.#destroyed )
				this.#snackbar.exception( `Could not ${action.toLowerCase()} the condition.`, e );
		}
	}
	get cnnctnSlug():string{ return this.pageData().server.connection.slug; }
}