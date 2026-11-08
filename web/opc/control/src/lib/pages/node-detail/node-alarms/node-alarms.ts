import { ChangeDetectorRef, Component, computed, effect, inject, input, OnDestroy, signal, untracked } from '@angular/core';
import { DatePipe } from '@angular/common';
import { MatButtonModule } from '@angular/material/button';
import { MatDialog } from '@angular/material/dialog';
import { MatIconModule } from '@angular/material/icon';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatTableModule } from '@angular/material/table';
import { MatToolbarModule } from '@angular/material/toolbar';
import { MatTooltipModule } from '@angular/material/tooltip';
import { SnackbarService } from 'jde-framework';
import { acknowledgeMethod, addCommentMethod, AlarmRow, Alarms, badEventIdUnknown, confirmMethod, conditionSelect, isQueueOverflow, isRefreshRequired, sortAlarms } from '../../../model/alarm';
import { EventFields } from '../../../model/event';
import { UaNode } from '../../../model/node';
import { NodeId } from '../../../model/node-id';
import { OpcError } from '../../../model/opc-error';
import { isBad, scHex } from '../../../model/status-code';
import { NodePageData } from '../../../services/resolvers/node-resolver';
import { EventStream } from '../event-stream';
import { AlarmCommentDialog } from './alarm-comment-dialog';

//The Alarms tab (clients spec *Alarms and conditions*, #426), on a node whose EventNotifier has the SubscribeToEvents bit:  the Events
//tab's stream, asked with the condition fields, kept by ConditionId (Alarms) - a row per condition with its latest event, the source,
//name, branch, severity, message, the four states, the comment and the transition's time - and gone once the server says Retain is
//false.  When it opens, and on **Refresh**, `refreshConditions` has the server replay every retained condition to this connection's
//subscription, so the retained conditions arrive before the live ones;  on the Server object the replay comes between RefreshStart
//and RefreshEnd, which drop the rows the server no longer retains.  **Acknowledge**, **Confirm** and **Comment** call the type's
//method on the condition node with the row's EventId and a comment, and a refusal - Bad_ConditionBranchAlreadyAcked, say - shows its
//status on the row until the condition's next event;  when the row moved on while its dialog was open, the dialog comes back with the
//comment, against the new event.  A refusal of the subscription shows its status;  a connection lost subscribes again, and each time
//the item comes back up the table starts over from the refresh.  A server's RefreshRequired runs the refresh;  its queue overflowing
//says the list may be incomplete, until the next refresh.
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
	ngOnDestroy(){ this.#destroyed = true; this.#stream.destroy(); }
	#destroyed = false;

	pageData = input.required<NodePageData>();
	node = input.required<UaNode>();
	readonly columns = ['source', 'name', 'branch', 'severity', 'message', 'enabled', 'active', 'acked', 'confirmed', 'comment', 'time', 'actions'];
	#stream = new EventStream( {
		gateway: ()=>this.pageData().gateway,
		connection: ()=>this.cnnctnSlug,
		node: ()=>this.node(),
		select: ()=>[...conditionSelect],
		owner: `nodeAlarms/${Math.random().toString( 36 ).slice( 2 )}`,
		onEvent: fields=>this.#onEvent( fields ),
		onUp: ()=>this.#up()
	} );
	readonly live = this.#stream.live;
	readonly error = this.#stream.error;
	rows = signal<AlarmRow[]>( [] );
	sorted = computed( ()=>sortAlarms( this.rows() ) );
	#bracket = signal( false );
	refreshing = computed( ()=>this.#bracket() && this.live() );//between RefreshStart and RefreshEnd, while the stream is there to finish it
	busy = signal( false );//a refreshConditions out
	refreshStatus = signal<string|undefined>( undefined );//what the server answered the refresh with, when not Good
	incomplete = signal( false );//the server discarded events, or asked for refreshes too fast to follow:  the list may be short until a refresh
	#alarms = new Alarms();
	static readonly refreshRequiredGap = 10_000;//a replay is more work for a server that is already behind, so its asks don't each run one
	#lastRequired = 0;
	#refreshAgain = false;//the item came up while a refreshConditions was out, which may have been before it

	#restart( node:UaNode ){
		this.#stream.close();
		this.#alarms.clear();
		this.#sync();
		this.refreshStatus.set( undefined );
		this.incomplete.set( false );
		this.#lastRequired = 0;
		this.#refreshAgain = false;
		this.#stream.open( node );
	}
	#onEvent( fields:EventFields ){
		if( isQueueOverflow( fields ) )
			return this.incomplete.set( true );
		if( isRefreshRequired( fields ) )
			return this.#refreshRequired();
		if( this.#alarms.apply( fields ) )
			this.#sync();
	}
	#refreshRequired(){
		const now = Date.now();
		if( now-this.#lastRequired<NodeAlarms.refreshRequiredGap )
			return this.incomplete.set( true );
		this.#lastRequired = now;
		void this.refresh();
	}
	//The item is up, first or again:  the table starts over, since a condition an outage ended sends no event, and the retained ones come back before the live ones.
	#up(){
		this.#alarms.clear();
		this.#sync();
		void this.refresh();
	}
	#sync(){
		this.rows.set( this.#alarms.rows );
		this.#bracket.set( this.#alarms.refreshing );
		this.#cdRef.markForCheck();
	}
	//ConditionRefresh with this connection's subscription:  the server's status where it is not Good
	async refresh(){
		if( this.busy() ){
			this.#refreshAgain = true;
			return;
		}
		const node = this.node(), gateway = this.pageData().gateway;
		this.busy.set( true );
		this.refreshStatus.set( undefined );
		this.incomplete.set( false );
		try{
			const sc = await gateway.refreshConditions( this.cnnctnSlug, node.nodeId );
			if( this.#destroyed || node!==this.node() )
				return;
			if( isBad( sc ) ){
				const text = ()=>`${scHex( sc )} ${OpcError.text( sc )}`;
				this.refreshStatus.set( text() );
				this.#stream.nameOf( sc, ()=>{ if( this.refreshStatus() ) this.refreshStatus.set( text() ); } );
			}
		}
		catch( e ){
			if( !this.#destroyed && node===this.node() )
				this.#snackbar.exception( "Could not refresh the conditions.", e );
		}
		finally{
			if( !this.#destroyed ){
				this.busy.set( false );
				const again = this.#refreshAgain && this.live();
				this.#refreshAgain = false;
				if( again )
					void this.refresh();
			}
		}
	}
	//Acknowledge wants an unacknowledged condition, Confirm an acknowledged and unconfirmed one;  a server that gave no state is asked, and refuses what it will not take
	canAcknowledge( r:AlarmRow ):boolean{ return r.acked!==true; }
	canConfirm( r:AlarmRow ):boolean{ return r.confirmed!==true && r.acked!==false; }
	acknowledge( r:AlarmRow ){ return this.#act( r, "Acknowledge", acknowledgeMethod, x=>this.canAcknowledge( x ) ); }
	confirm( r:AlarmRow ){ return this.#act( r, "Confirm", confirmMethod, x=>this.canConfirm( x ) ); }
	comment( r:AlarmRow ){ return this.#act( r, "Comment", addCommentMethod, ()=>true ); }
	//the type's method on the condition node with the row's EventId and the comment asked for:  a refusal is the call's status, shown on the row
	async #act( r:AlarmRow, action:string, method:NodeId, allowed:( r:AlarmRow )=>boolean, typed?:string ){
		const comment = await AlarmCommentDialog.open( this.#dialog, {action, condition: r.name || r.conditionId.uaString(), message: r.message, comment: typed} );
		if( comment===undefined || this.#destroyed )
			return;
		try{
			const result = await this.pageData().gateway.call( this.cnnctnSlug, r.conditionId, method, [r.eventId!, {text: comment}] );
			if( this.#destroyed )
				return;
			const sc = result.statusCode;
			if( isBad( sc ) ){
				//never the newest EventId sent unasked:  an operator acknowledges the transition they saw, so they see this one first
				const moved = sc==badEventIdUnknown ? this.#alarms.rows.find( x=>x.key==r.key && x.eventKey!=r.eventKey ) : undefined;
				if( moved && allowed( moved ) )
					return await this.#act( moved, action, method, allowed, comment );
				const text = ()=>`${action} refused:  ${scHex( sc )} ${OpcError.text( sc )}`, shown = text();
				this.#alarms.problem( r.key, shown );
				this.#stream.nameOf( sc, ()=>{
					if( this.#alarms.rows.find( x=>x.key==r.key )?.problem==shown ){//the condition's next event clears it
						this.#alarms.problem( r.key, text() );
						this.#sync();
					}
				} );
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