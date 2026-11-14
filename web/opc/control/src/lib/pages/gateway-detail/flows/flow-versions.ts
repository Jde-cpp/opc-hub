import { DatePipe } from '@angular/common';
import { Component, computed, effect, inject, input, output, signal, untracked } from '@angular/core';
import { Clipboard } from '@angular/cdk/clipboard';
import { MatButtonModule } from '@angular/material/button';
import { MatDialog } from '@angular/material/dialog';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatSelectModule } from '@angular/material/select';
import { MatTableModule } from '@angular/material/table';
import { MatTooltipModule } from '@angular/material/tooltip';
import { errorMessage, SnackbarService } from 'jde-framework';
import { Latest } from 'jde-spa';
import { diffFlowGraphs, exportFlow, Flow, FlowNodeChange, FlowRun, FlowVersion } from '../../../model/flow';
import { FlowService } from '../../../services/flow-service';
import { HistPoster, HistReader } from '../../../services/history-service';
import { FlowReplayDialog } from './flow-replay-dialog';

//A flow's Versions tab (#455, docs/flows/spec.md *Web*):  its deploys, newest first, each with who deployed it, when and why;  two
//of them compared by node, what the later added, removed and changed;  a version put back on the canvas, to deploy again;  and the
//replays of the current version (#453), started here, listed with how each ended, and cancelled.  A run is opened in History, which
//is phase 10's.
@Component({
	selector: 'flow-versions',
	templateUrl: './flow-versions.html',
	styleUrls: ['../hist-admin/hist-admin-shared.scss', './flow-tabs.scss'],
	imports: [DatePipe, MatButtonModule, MatFormFieldModule, MatIconModule, MatProgressBarModule, MatSelectModule, MatTableModule, MatTooltipModule]
})
export class FlowVersions{
	gateway = input.required<HistReader&HistPoster>();
	flow = input.required<Flow>();
	versions = input.required<FlowVersion[]>();//newest first
	loading = input( false );
	open = output<FlowVersion>();//onto the canvas
	refresh = output<void>();
	readonly columns = ['version', 'deployed', 'userName', 'comment', 'nodes', 'actions'];
	readonly runColumns = ['run', 'version', 'range', 'mode', 'started', 'userName', 'status', 'actions'];
	//the two versions compared, by number:  the current one against the one before it, until another is chosen
	from = signal<number|undefined>( undefined );
	to = signal<number|undefined>( undefined );
	runs = signal<FlowRun[]>( [] );
	runsError = signal<string|undefined>( undefined );
	busy = signal( false );
	#reads = new Latest();
	#flows = inject( FlowService );
	#dialog = inject( MatDialog );
	#snackbar = inject( SnackbarService );
	#clipboard = inject( Clipboard );

	constructor(){
		effect( ()=>{//another flow, or a deploy:  the newest pair
			const versions = this.versions();
			untracked( ()=>{
				this.to.set( versions[0]?.version );
				this.from.set( versions[1]?.version );
			} );
		} );
		effect( ()=>{
			this.flow().id;
			untracked( ()=>this.loadRuns() );
		} );
	}
	version( n:number|undefined ):FlowVersion|undefined{ return this.versions().find( v=>v.version==n ); }
	diff = computed<FlowNodeChange[]>( ()=>{
		const from = this.version( this.from() ), to = this.version( this.to() );
		return from && to ? diffFlowGraphs( from.graph, to.graph ) : [];
	} );
	changes = computed( ()=>this.diff().filter( c=>c.kind!='moved' ) );
	moved = computed( ()=>this.diff().filter( c=>c.kind=='moved' ).length );
	changeIcon( c:FlowNodeChange ):string{ return c.kind=='added' ? 'add_circle' : c.kind=='removed' ? 'remove_circle' : 'change_circle'; }
	copy( v:FlowVersion ):void{
		if( this.#clipboard.copy( exportFlow( this.flow(), v.graph ) ) )
			this.#snackbar.info( `Version ${v.version}'s json copied.` );
		else
			this.#snackbar.error( 'Could not copy the json.' );
	}

	async loadRuns():Promise<void>{
		const read = this.#reads.next();
		this.runsError.set( undefined );
		try{
			const runs = await this.#flows.runs( this.gateway(), this.flow().id );
			if( this.#reads.is( read ) )
				this.runs.set( runs );
		}
		catch( e ){
			if( this.#reads.is( read ) ){
				this.runs.set( [] );
				this.runsError.set( errorMessage( e ) );
			}
		}
	}
	runState( run:FlowRun ):'ok'|'bad'|'pending'{ return !run.status ? 'pending' : run.status.startsWith( 'failed' ) ? 'bad' : 'ok'; }
	async replay():Promise<void>{
		const flow = this.flow();
		const asked = await FlowReplayDialog.open( this.#dialog, {name: flow.name, version: flow.version!} );
		if( !asked )
			return;
		await this.#run( async ()=>{
			const run = await this.#flows.replay( this.gateway(), flow.id, asked.from, asked.to, asked.mode );
			this.#snackbar.info( `Replay ${run.id} started.` );
		}, 'Could not start the replay.' );
	}
	async cancel( run:FlowRun ):Promise<void>{
		await this.#run( ()=>this.#flows.cancelRun( this.gateway(), run.id ), 'Could not cancel the replay.' );
	}
	async #run( work:()=>Promise<void>, failed:string ):Promise<void>{
		this.busy.set( true );
		try{
			await work();
		}
		catch( e ){
			this.#snackbar.exception( failed, e );
		}
		finally{
			this.busy.set( false );
		}
		await this.loadRuns();
	}
	onRefresh():void{
		this.refresh.emit();
		this.loadRuns();
	}
}