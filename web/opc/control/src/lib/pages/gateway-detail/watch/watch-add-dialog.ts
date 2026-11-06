import { Component, computed, inject, signal } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatCheckboxModule } from '@angular/material/checkbox';
import { MAT_DIALOG_DATA, MatDialog, MatDialogActions, MatDialogContent, MatDialogRef, MatDialogTitle } from '@angular/material/dialog';
import { MatFormFieldModule } from '@angular/material/form-field';
import { MatIconModule } from '@angular/material/icon';
import { MatInputModule } from '@angular/material/input';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatSelectModule } from '@angular/material/select';
import { MatTableModule } from '@angular/material/table';
import { MatTooltipModule } from '@angular/material/tooltip';
import { errorMessage } from 'jde-framework';
import { ENodeClass } from '../../../model/node';
import { NodeId } from '../../../model/node-id';
import { CnnctnSlug } from '../../../model/server-cnnctn';
import { Gateway } from '../../../services/gateway-service';
import { NodeSearchProvider } from '../../../services/node-search-provider';

export type WatchPick = { opc:CnnctnSlug; node:NodeId };
export type WatchConnection = { slug:CnnctnSlug; name:string };
export type WatchAddDialogData = { gateway:Gateway; connections:WatchConnection[]; opc?:CnnctnSlug };
//a hit of the gateway's search, with the node's id on it (SearchQLAwait writes the id's keys onto the row)
type Hit = { node:NodeId; name:string; path:string; nodeClass:ENodeClass };

//Rows for the watch list (spec *Watch list*):  on a connection of the gateway, a node by its id - `ns=2;i=1001` - or hits of the
//connection's search by name, of which the variables are picked.  The id form is added as typed:  the list describes it, and says
//so where the connection has no such node.
@Component({
	templateUrl: './watch-add-dialog.html',
	styleUrls: ['../hist-admin/hist-admin-shared.scss', './watch-add-dialog.scss'],
	imports: [MatButtonModule, MatCheckboxModule, MatDialogActions, MatDialogContent, MatDialogTitle, MatFormFieldModule, MatIconModule, MatInputModule, MatProgressBarModule, MatSelectModule, MatTableModule, MatTooltipModule]
})
export class WatchAddDialog{
	dialogRef = inject<MatDialogRef<WatchAddDialog,WatchPick[]|undefined>>( MatDialogRef );
	data = inject<WatchAddDialogData>( MAT_DIALOG_DATA );
	connection = signal<CnnctnSlug|undefined>( this.data.opc ?? (this.data.connections.length==1 ? this.data.connections[0].slug : undefined) );
	text = signal( '' );
	hits = signal<Hit[]>( [] );
	picks = signal<Hit[]>( [] );
	searched = signal<string|undefined>( undefined );//the text the hits answer, once a search has run
	loading = signal( false );
	error = signal<string|undefined>( undefined );
	readonly displayedColumns = ['pick', 'name', 'path', 'node'];
	readonly limit = 50;
	//the text as a node id, or nothing:  a name is never that shape
	nodeId = computed<NodeId|undefined>( ()=>NodeSearchProvider.nodeId( this.text().trim() ) );
	canSearch = computed( ()=>!!this.connection() && this.text().trim().length>0 && !this.nodeId() && !this.loading() );
	canAdd = computed( ()=>!!this.connection() && (!!this.nodeId() || this.picks().length>0) );
	static readonly columns = '{ id path name nodeClass }';

	onConnection( slug:CnnctnSlug ){
		this.connection.set( slug );
		this.hits.set( [] );
		this.picks.set( [] );
		this.searched.set( undefined );
	}
	//Enter:  an id adds, a name searches
	onEnter(){
		if( this.nodeId() )
			this.add();
		else
			this.search();
	}
	async search():Promise<void>{
		const opc = this.connection(), text = this.text().trim();
		if( !opc || !text || this.nodeId() )
			return;
		this.loading.set( true );
		this.error.set( undefined );
		try{
			const rows = await this.data.gateway.queryArray<any>( `search( opc:$opc, text:$text, limit:$limit )${WatchAddDialog.columns}`, {opc, text, limit: this.limit} );
			this.hits.set( rows.map( r=>({node: new NodeId( r ), name: r.name, path: r.path ?? '', nodeClass: r.nodeClass}) ) );
			this.picks.set( [] );
			this.searched.set( text );
		}
		catch( e ){
			this.hits.set( [] );
			this.error.set( errorMessage( e ) );
		}
		finally{
			this.loading.set( false );
		}
	}
	selectable( hit:Hit ):boolean{ return hit.nodeClass==ENodeClass.Variable; }
	isPicked( hit:Hit ):boolean{ return this.picks().some( p=>p.node.key==hit.node.key ); }
	toggle( hit:Hit ){
		if( this.selectable( hit ) )
			this.picks.update( picks=>this.isPicked( hit ) ? picks.filter( p=>p.node.key!=hit.node.key ) : [...picks, hit] );
	}
	className( hit:Hit ):string{ return ENodeClass[hit.nodeClass] ?? `${hit.nodeClass}`; }
	displayPath( hit:Hit ):string{ return hit.path ? NodeSearchProvider.displayPath( hit.path ) : ''; }
	icon( hit:Hit ):string{ return NodeSearchProvider.icon( hit.nodeClass ); }
	//the id typed, or the hits picked
	add(){
		const opc = this.connection();
		if( !opc )
			return;
		const id = this.nodeId();
		this.dialogRef.close( id ? [{opc, node: id}] : this.picks().map( p=>({opc, node: p.node}) ) );
	}
	//what was picked, or undefined for a cancel
	static open( dialog:MatDialog, data:WatchAddDialogData ):Promise<WatchPick[]|undefined>{
		const ref = dialog.open<WatchAddDialog,WatchAddDialogData,WatchPick[]|undefined>( WatchAddDialog, {data, width: '760px', maxWidth: '95vw', ariaModal: true} );
		return new Promise( resolve=>ref.afterClosed().subscribe( picks=>resolve( picks ) ) );
	}
}