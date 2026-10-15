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
import { errorText } from 'jde-framework';
import { HistPathSegment } from '../../../model/hist';
import { HistConnection, histNodeText, HistPick, parseHistNodeText } from '../../../model/hist-admin';
import { ExNodeId } from '../../../model/ex-node-id';
import { ENodeClass, OpcObject, UaNode } from '../../../model/node';
import { Server } from '../../../model/server';
import { Gateway } from '../../../services/gateway-service';
import { OpcStore } from '../../../services/opc-store';

export type HistPickerMode = 'paths'|'nodes'|'object';
export type HistNodePickerData = {
	gateway:Gateway;
	title:string;
	//paths:  variables, each with its path from the start - a template's members.  nodes:  variables by node - a group's ad hoc
	//nodes.  object:  one object - a group's root.
	mode:HistPickerMode;
	connections:HistConnection[];//to pick among, when none is fixed
	connection?:string;//the slug a group fixes
	root?:ExNodeId;//where to start, and what the paths are relative to;  the Objects folder when none
	rootEditable?:boolean;//a template's picker:  the root is the user's to set, by text or from the tree, since a template names no server
};
//The browse names a path is made of, as Gateway.browseObjectsFolder answers them
type Browse = { ns?:number; name:String };

//A picker over a connection's address space (#337):  a page per node, as the node pages browse, rather than a tree the web has
//none of.  A trail of buttons leads back up;  an object opens;  a variable is ticked.  What it answers is spelled for the
//historian's tables:  each node by its namespace URI, and its path from the start as browse names by URI (spec *Templates*), which
//the server's namespace array supplies (OpcStore.getConnection) - a namespace the server doesn't list by URI can't be picked.
@Component({
	templateUrl: './hist-node-picker-dialog.html',
	styleUrls: ['./hist-node-picker-dialog.scss'],
	imports: [MatButtonModule, MatCheckboxModule, MatDialogActions, MatDialogContent, MatDialogTitle, MatFormFieldModule, MatIconModule, MatInputModule, MatProgressBarModule, MatSelectModule, MatTableModule, MatTooltipModule]
})
export class HistNodePickerDialog{
	constructor(){ this.start(); }
	dialogRef = inject<MatDialogRef<HistNodePickerDialog,HistPick[]|undefined>>( MatDialogRef );
	data = inject<HistNodePickerData>( MAT_DIALOG_DATA );
	#store = inject( OpcStore );
	connection = signal<string|undefined>( this.data.connection ?? (this.data.connections.length==1 ? this.data.connections[0].slug : undefined) );
	rootText = signal( this.data.root ? histNodeText( this.data.root ) : '' );
	server = signal<Server|undefined>( undefined );
	trail = signal<UaNode[]>( [] );//from the start node to the one shown
	children = signal<UaNode[]>( [] );
	picks = signal<HistPick[]>( [] );//each spelled when ticked:  its path is the trail it was ticked under, which open and up go on to change
	loading = signal( false );
	error = signal<string|undefined>( undefined );
	readonly displayedColumns = this.data.mode=='object' ? ['name', 'browse', 'node', 'choose'] : ['pick', 'name', 'browse', 'node'];
	get mode():HistPickerMode{ return this.data.mode; }
	get current():UaNode|undefined{ const t = this.trail(); return t[t.length-1]; }
	rootError = computed<string|undefined>( ()=>{ const t = this.rootText().trim(); return t && !parseHistNodeText( t ) ? 'A NodeId:  ns=2;i=5001, or {"nsu":"urn:…","i":5001}.' : undefined; } );
	//the start, by the root's text or as the Objects folder
	startName():string{ const t = this.rootText().trim(); return t ? t : 'Objects'; }
	currentName():string{ const c = this.current; return !c ? '' : this.trail().length>1 ? c.name ?? this.browseText( c ) : this.startName(); }

	//the connection's server for its namespaces, then the start node's children.  Again on another connection or root.
	async start():Promise<void>{
		const slug = this.connection();
		this.picks.set( [] );
		this.children.set( [] );
		this.trail.set( [] );
		this.error.set( undefined );
		if( !slug || this.rootError() )
			return;
		this.loading.set( true );
		try{
			const server = await this.#store.getConnection( this.data.gateway, slug );
			this.server.set( server );
			const root = parseHistNodeText( this.rootText() );
			const start = root ? HistNodePickerDialog.toUaNode( root, server ) : OpcObject.rootNode;
			this.trail.set( [start] );
			await this.#browse( start );
		}
		catch( e ){
			this.error.set( errorText( e ) ?? 'Unknown error' );
			this.loading.set( false );
		}
	}
	//a stored node as a node to browse from:  its URI as the server's index, which a server that doesn't list the URI can't give
	static toUaNode( node:ExNodeId, server:Server ):OpcObject{
		let ns = node.ns;
		if( node.nsu ){
			const found = server.namespaces.find( n=>n.uri==node.nsu );
			if( !found )
				throw new Error( `'${server.connection.name}' has no namespace '${node.nsu}'.` );
			ns = found.index;
		}
		return new OpcObject( {...node.toJson(), ns, name: {locale: 'en-US', text: histNodeText( node )}} );
	}
	async #browse( node:UaNode ):Promise<void>{
		this.loading.set( true );
		this.error.set( undefined );
		try{
			const refs = await this.data.gateway.browseObjectsFolder( this.connection()!, node, true, m=>console.log( m ) );
			this.children.set( refs.filter( r=>r.displayed ).sort( HistNodePickerDialog.order ) );
		}
		catch( e ){
			this.children.set( [] );
			this.error.set( errorText( e ) ?? 'Unknown error' );
		}
		finally{
			this.loading.set( false );
		}
	}
	//objects first, each kind by name:  a folder is what one opens, a variable what one picks
	static order( a:UaNode, b:UaNode ):number{ return (a.isObject ? 0 : 1)-(b.isObject ? 0 : 1) || (a.name ?? '').localeCompare( b.name ?? '' ); }
	async open( node:UaNode ):Promise<void>{
		if( !node.isObject || this.loading() )
			return;
		this.trail.update( t=>[...t, node] );
		await this.#browse( node );
	}
	async up( index:number ):Promise<void>{
		const t = this.trail();
		if( index>=t.length-1 || this.loading() )
			return;
		this.trail.set( t.slice( 0, index+1 ) );
		await this.#browse( t[index] );
	}
	selectable( node:UaNode ):boolean{ return node.isVariable && this.mode!='object'; }
	isPicked( node:UaNode ):boolean{ const p = this.pick( node ); return !!p && this.picks().some( x=>this.#same( x, p ) ); }
	toggle( node:UaNode ):void{
		const p = this.pick( node );
		if( p )
			this.picks.update( picks=>picks.some( x=>this.#same( x, p ) ) ? picks.filter( x=>!this.#same( x, p ) ) : [...picks, p] );
	}
	//a template member is its path, so the same node under another folder is another pick;  a group's node is the node, wherever it shows
	#same( a:HistPick, b:HistPick ):boolean{ return this.mode=='paths' ? JSON.stringify( a.path )==JSON.stringify( b.path ) : histNodeText( a.node )==histNodeText( b.node ); }
	//the node's path from the start:  the trail past it, then the node - or the trail alone for the node shown, which ends it - each a
	//browse name by its namespace URI;  none where the server lists no URI for a namespace
	path( node:UaNode ):HistPathSegment[]|undefined{
		const server = this.server();
		const trail = this.trail();
		const nodes = trail.length && trail[trail.length-1].key==node.key ? trail.slice( 1 ) : [...trail.slice( 1 ), node];
		const y:HistPathSegment[] = [];
		for( const n of nodes ){
			const browse = <Browse|undefined>n.browse;
			const nsu = browse ? server?.uri( browse.ns ?? 0 ) : undefined;
			if( nsu==undefined || !browse )
				return undefined;
			y.push( {nsu, name: String( browse.name )} );
		}
		return y;
	}
	pick( node:UaNode ):HistPick|undefined{
		const path = this.path( node );
		const nsu = this.server()?.uri( node.ns );
		if( !path || nsu==undefined )
			return undefined;
		return { node: new ExNodeId( {...node.toJson(), nsu} ), path, name: node.name ?? this.browseText( node ) };
	}
	//a node whose namespace the server lists by no URI:  the tables can't spell it
	unspelled( node:UaNode ):boolean{ return !this.pick( node ); }
	browseText( node:UaNode ):string{ const b = <Browse|undefined>node.browse; return !b ? '' : b.ns ? `${b.ns}~${b.name}` : String( b.name ); }
	icon( node:UaNode ):string{ return node.nodeClass==ENodeClass.Variable ? 'label' : node.nodeClass==ENodeClass.Method ? 'functions' : 'folder'; }
	//the folder shown becomes the root:  its node by URI as the root text, and the trail starts over from it
	async useAsRoot():Promise<void>{
		const c = this.current;
		const p = c ? this.pick( c ) : undefined;
		if( !p )
			return;
		this.rootText.set( histNodeText( p.node ) );
		await this.start();
	}
	choose( node:UaNode ):void{
		const p = this.pick( node );
		if( p )
			this.dialogRef.close( [p] );
	}
	chooseCurrent():void{ if( this.current ) this.choose( this.current ); }
	done():void{ this.dialogRef.close( this.picks() ); }
	//what was picked, or undefined for a cancel
	static open( dialog:MatDialog, data:HistNodePickerData ):Promise<HistPick[]|undefined>{
		const ref = dialog.open<HistNodePickerDialog,HistNodePickerData,HistPick[]|undefined>( HistNodePickerDialog, {data, width: '860px', maxWidth: '95vw', ariaModal: true} );
		return new Promise( resolve=>ref.afterClosed().subscribe( picks=>resolve( picks ) ) );
	}
}