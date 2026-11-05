import { Component, effect, inject, input, signal, untracked } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatIconModule } from '@angular/material/icon';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatTableModule } from '@angular/material/table';
import { MatTooltipModule } from '@angular/material/tooltip';
import { Router } from '@angular/router';
import { errorMessage, SnackbarService } from 'jde-framework';
import { pageSegments, UaNode } from '../../../model/node';
import { Reference } from '../../../model/reference';
import { placeNode } from '../../../services/gateway-service';
import { NodePageData } from '../../../services/resolvers/node-resolver';

//The References tab (clients spec *Address space*, #416):  every reference of the node in both directions, as direction, reference
//type, target node, class and type definition - the type and the type definition named by the gateway's per-connection cache.  A
//target's name opens its page, by the path the gateway walks up to Root;  a Method's opens its object's, and a target the gateway
//cannot place is a notice.
@Component({
	selector: 'node-references',
	templateUrl: './node-references.html',
	styleUrl: './node-references.scss',
	imports: [MatButtonModule, MatIconModule, MatProgressBarModule, MatTableModule, MatTooltipModule]
})
export class NodeReferences{
	#router = inject( Router );
	#snackbar = inject( SnackbarService );
	constructor(){
		effect( ()=>{ this.node(); untracked( ()=>this.load() ); } );//the router reuses the page across nodes, so a node switch reads again
	}
	pageData = input.required<NodePageData>();
	node = input.required<UaNode>();
	rows = signal<Reference[]>( [] );
	truncated = signal<boolean>( false );//the node has more than the gateway reads (Gateway.references)
	loading = signal<boolean>( true );
	error = signal<string|undefined>( undefined );
	columns = ['direction', 'refType', 'target', 'class', 'typeDef'];

	//the node's rows, the last node's cleared first;  an answer for a node the page has since left is dropped, so a slow one can't
	//overwrite the current node's or end its loading (reviews-issue/416-address-space.md #6)
	async load():Promise<void>{
		const load = ++this.#load;
		this.rows.set( [] );
		this.truncated.set( false );
		this.loading.set( true );
		this.error.set( undefined );
		try{
			const { rows, truncated } = await this.pageData().gateway.references( this.cnnctnSlug, this.node().nodeId );
			if( load==this.#load ){
				this.rows.set( rows );
				this.truncated.set( truncated );
			}
		}
		catch( e ){
			if( load==this.#load )
				this.error.set( errorMessage(e) );
		}
		finally{
			if( load==this.#load )
				this.loading.set( false );
		}
	}
	#load = 0;
	async open( r:Reference ):Promise<void>{
		const gateway = this.pageData().gateway;
		let path:string|null;
		try{
			path = (await placeNode( gateway, this.cnnctnSlug, r.target.id ))?.path ?? null;
		}
		catch( e ){
			this.#snackbar.exception( `Could not place ${r.targetLabel}.`, e );
			return;
		}
		if( path==null )
			return this.#snackbar.info( `${r.targetLabel} has no page:  the gateway cannot place it under Root.` );
		this.#router.navigate( ['/gateways', gateway.slug, this.cnnctnSlug, ...pageSegments( path, r.nodeClass )] );
	}
	get cnnctnSlug():string{ return this.pageData().server.connection.slug; }
}