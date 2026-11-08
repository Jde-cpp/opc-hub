import { signal } from '@angular/core';
import { Subscription } from 'rxjs';
import { EventFields } from '../../model/event';
import { UaNode } from '../../model/node';
import { OpcError } from '../../model/opc-error';
import { scHex } from '../../model/status-code';
import { StatusCode } from '../../model/types';
import { EventPush, Gateway, gatewayAnswer } from '../../services/gateway-service';

//What a tab of a node's events gives its EventStream.  `node` is the page's node now, `select` the fields asked at each subscribe.  `onUp`
//is told each time the item is up:  the ack, the ack of a subscribe after a lost socket, or a rebuild of the server's session.
export type EventStreamHost = {
	gateway:()=>Gateway;
	connection:()=>string;
	node:()=>UaNode;
	select:()=>string[];
	owner:string;
	onEvent:( fields:EventFields )=>void;
	onUp?:()=>void;
};
//The events subscription the Events and Alarms tabs share (clients spec *Events*):  a refusal shows its status, and a connection lost
//subscribes again once the gateway is back.
export class EventStream{
	constructor( host:EventStreamHost ){ this.#host = host; }
	readonly #host:EventStreamHost;
	readonly live = signal( false );//the subscription is up:  its ack came and nothing ended it
	readonly error = signal<string|undefined>( undefined );//a refusal's status, or why the subscription ended
	open( node:UaNode ){
		const host = this.#host;
		this.error.set( undefined );
		this.#subscription = host.gateway().subscribeEvents( host.connection(), node.nodeId, host.select(), host.owner ).subscribe( {
			next: e=>this.#push( e ),
			error: ( e:unknown )=>this.#ended( node, e )
		} );
	}
	close(){
		clearTimeout( this.#retry );
		this.#subscription?.unsubscribe();
		this.#subscription = undefined;
		this.live.set( false );
	}
	destroy(){ this.#destroyed = true; this.close(); }
	showRefused( sc:StatusCode, what:string ){
		const text = ()=>`The server refused ${what}:  ${scHex( sc )} ${OpcError.text( sc )}.`;
		this.error.set( text() );
		this.nameOf( sc, ()=>{ if( this.error() ) this.error.set( text() ); } );
	}
	//the code's name fetched where this browser lacks it, then `shown` again
	nameOf( sc:StatusCode, shown:()=>void ){
		if( !OpcError.statusCodeText( sc ) )
			this.#host.gateway().updateErrorCodes().then( ()=>{ if( !this.#destroyed ) shown(); } );
	}
	#push( e:EventPush ){
		if( e.refused )
			return this.showRefused( e.refused, "the field list" );
		if( e.acked )
			this.live.set( true );
		if( e.acked || e.restored )
			return this.#host.onUp?.();
		this.#host.onEvent( e.fields );
	}
	#ended( node:UaNode, e:unknown ){
		this.#subscription = undefined;
		this.live.set( false );
		const answer = gatewayAnswer( e );
		if( e instanceof OpcError )//refused:  the node, or a field, as the server read it - no retry would change it
			this.showRefused( e.sc, "the subscription" );
		else if( answer && answer.httpStatus>=400 && answer.httpStatus<500 )//a field the gateway cannot read, likewise
			this.error.set( `The gateway refused the subscription:  ${answer.message}` );
		else{//the socket went, or the gateway could not do it yet - a session not back, a server down:  the item is asked for again
			this.error.set( answer ? `The gateway could not subscribe:  ${answer.message}  Trying again.` : "Connection to the gateway lost.  The subscription resumes once it is back." );
			clearTimeout( this.#retry );
			this.#retry = setTimeout( ()=>{ if( !this.#destroyed && node===this.#host.node() && !this.#subscription ) this.open( node ); }, Gateway.histRetryDelay );
		}
	}
	#subscription:Subscription|undefined;
	#retry:ReturnType<typeof setTimeout>|undefined;
	#destroyed = false;
}