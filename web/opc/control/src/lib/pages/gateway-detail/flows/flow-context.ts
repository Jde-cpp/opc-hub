import { Component, computed, effect, inject, input, signal, untracked } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatDialog } from '@angular/material/dialog';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatSelectModule } from '@angular/material/select';
import { MatTableModule } from '@angular/material/table';
import { MatTooltipModule } from '@angular/material/tooltip';
import { confirm, errorMessage, SnackbarService } from 'jde-framework';
import { Latest } from 'jde-spa';
import { Flow, FlowContextScope, flowContextScope, FlowContextValue, FlowNode } from '../../../model/flow';
import { FlowService } from '../../../services/flow-service';
import { HistPoster, HistReader } from '../../../services/history-service';

//the value being added or edited:  `text` is its json
export type FlowContextEdit = { scope:FlowContextScope; nodeId:string; name:string; text:string; isNew:boolean };
//What is wrong with a value typed, or nothing:  json, and not null, which takes a key away.
export function contextValueProblem( text:string ):string|undefined{
	if( !text.trim() )
		return 'The value is json:  1, "text", true, [ 1, 2 ] or { "a": 1 }.';
	try{
		return JSON.parse( text )===null ? 'null takes a key away:  Remove does that.' : undefined;
	}
	catch( e ){
		return `Not json:  ${(<Error>e).message}`;
	}
}

//A flow's Context tab (#455, docs/flows/spec.md *Context*):  the values the flow's nodes keep, at the three scopes - a node's own,
//the flow's and the gateway's, which every flow shares - as their rows hold them, each edited through setFlowContext, which sets
//the value in the running flow's store and writes its row before it answers.  A flow that is not running loads it at its start.
@Component({
	selector: 'flow-context',
	templateUrl: './flow-context.html',
	styleUrls: ['../hist-admin/hist-admin-shared.scss', './flow-tabs.scss'],
	imports: [MatButtonModule, MatFormFieldModule, MatIconModule, MatInputModule, MatProgressBarModule, MatSelectModule, MatTableModule, MatTooltipModule]
})
export class FlowContext{
	gateway = input.required<HistReader&HistPoster>();
	flow = input.required<Flow>();
	nodes = input<FlowNode[]>( [] );//the current version's, for a node's name
	readonly columns = ['scope', 'name', 'value', 'actions'];
	rows = signal<FlowContextValue[]>( [] );
	loading = signal( false );
	busy = signal( false );
	error = signal<string|undefined>( undefined );
	editing = signal<FlowContextEdit|undefined>( undefined );
	problem = computed<string|undefined>( ()=>{
		const e = this.editing();
		if( !e )
			return undefined;
		if( !e.name.trim() )
			return 'A value has a name, its key.';
		if( e.scope=='node' && !e.nodeId )
			return 'Choose the node.';
		return contextValueProblem( e.text );
	} );
	#reads = new Latest();
	#flows = inject( FlowService );
	#snackbar = inject( SnackbarService );
	#dialog = inject( MatDialog );

	constructor(){
		effect( ()=>{//the router reuses the page from one flow to the next
			this.flow().id;
			untracked( ()=>{ this.editing.set( undefined ); this.load(); } );
		} );
	}
	async load():Promise<void>{
		const read = this.#reads.next();
		this.loading.set( true );
		this.error.set( undefined );
		try{
			const rows = await this.#flows.contexts( this.gateway(), this.flow().id );
			if( this.#reads.is( read ) )
				this.rows.set( rows );
		}
		catch( e ){
			if( this.#reads.is( read ) ){
				this.rows.set( [] );
				this.error.set( errorMessage( e ) );
			}
		}
		finally{
			if( this.#reads.is( read ) )
				this.loading.set( false );
		}
	}
	scopeText( row:Pick<FlowContextValue,'flowId'|'nodeId'> ):string{
		const scope = flowContextScope( row );
		if( scope!='node' )
			return scope=='flow' ? 'Flow' : 'Gateway';
		const node = this.nodes().find( n=>n.id==row.nodeId );
		return node ? `${node.name || node.type}` : row.nodeId;
	}
	scopeDetail( row:Pick<FlowContextValue,'flowId'|'nodeId'> ):string{
		const scope = flowContextScope( row );
		return scope=='node' ? `Node ${row.nodeId}'s own` : scope=='flow' ? "The flow's, which its nodes share" : "The gateway's, which every flow shares";
	}
	add():void{ this.editing.set( {scope: 'flow', nodeId: '', name: '', text: '', isNew: true} ); }
	edit( row:FlowContextValue ):void{ this.editing.set( {scope: flowContextScope( row ), nodeId: row.nodeId, name: row.name, text: row.value, isNew: false} ); }
	set<K extends keyof FlowContextEdit>( key:K, value:FlowContextEdit[K] ):void{ this.editing.update( e=>e ? {...e, [key]: value} : e ); }
	#key( e:Pick<FlowContextEdit,'scope'|'nodeId'|'name'> ){ return { flowId: e.scope=='global' ? undefined : this.flow().id, nodeId: e.scope=='node' ? e.nodeId : undefined, name: e.name.trim() }; }
	async save():Promise<void>{
		const e = this.editing();
		if( !e || this.problem() )
			return;
		if( await this.#send( ()=>this.#flows.setContext( this.gateway(), this.#key( e ), JSON.parse( e.text ) ), 'Could not set the value.' ) )
			this.editing.set( undefined );
	}
	async remove( row:FlowContextValue ):Promise<void>{
		if( !await confirm( this.#dialog, {title: `Remove ${row.name}?`, message: `The key leaves the ${flowContextScope( row )=='global' ? "gateway's" : flowContextScope( row )=='flow' ? "flow's" : "node's"} context:  a running flow reads none at its next get.`, confirm: 'Remove', destructive: true} ) )
			return;
		await this.#send( ()=>this.#flows.setContext( this.gateway(), this.#key( {scope: flowContextScope( row ), nodeId: row.nodeId, name: row.name} ), undefined ), 'Could not remove the value.' );
	}
	async #send( work:()=>Promise<void>, failed:string ):Promise<boolean>{
		this.busy.set( true );
		try{
			await work();
		}
		catch( e ){
			this.#snackbar.exception( failed, e );
			return false;
		}
		finally{
			this.busy.set( false );
		}
		await this.load();
		return true;
	}
}