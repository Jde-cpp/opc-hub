import { ChangeDetectorRef, Component, effect, inject, input, output, signal, untracked } from '@angular/core';
import { MatCheckboxChange, MatCheckboxModule } from '@angular/material/checkbox';
import { MatIconModule } from '@angular/material/icon';
import { MatProgressBarModule } from '@angular/material/progress-bar';
import { MatTableModule } from '@angular/material/table';
import { MatTooltipModule } from '@angular/material/tooltip';
import { errorMessage, SnackbarService } from 'jde-framework';
import { Attribute, AttributeEditor } from '../../../model/attribute';
import { UaNode } from '../../../model/node';
import { NodeView } from '../../../model/node-view';
import { OpcError } from '../../../model/opc-error';
import { scHex, statusIcon } from '../../../model/status-code';
import { numberValue, Value } from '../../../model/value';
import { NodePageData } from '../../../services/resolvers/node-resolver';

//The Attributes tab (clients spec *Attributes*, #417):  every attribute of the node's class in one read, each with its status where the
//server refused it, the Value with its status and both timestamps, and an editor where the node's UserWriteMask has the attribute's bit -
//the server's word under this session's rights - which writes through updateNode and shows what the server reads back.  The Value itself
//is the Children tab's to write:  its gate is the AccessLevel, not the WriteMask.
@Component({
	selector: 'node-attributes',
	templateUrl: './node-attributes.html',
	styleUrl: './node-attributes.scss',
	imports: [MatCheckboxModule, MatIconModule, MatProgressBarModule, MatTableModule, MatTooltipModule]
})
export class NodeAttributes{
	#snackbar = inject( SnackbarService );
	#cdRef = inject( ChangeDetectorRef );
	constructor(){
		effect( ()=>{ this.node(); untracked( ()=>this.load() ); } );//the router reuses the page across nodes, so a node switch reads again
	}
	pageData = input.required<NodePageData>();
	node = input.required<UaNode>();
	renamed = output<string>();//the DisplayName the server read back after a write:  the page's title and sidenav take it
	rows = signal<Attribute[]>( [] );
	userWriteMask = signal<number|undefined>( undefined );//the node's, as read:  the edit gate
	loading = signal<boolean>( true );
	error = signal<string|undefined>( undefined );
	columns = ['attribute', 'value', 'status', 'sourceTime', 'serverTime'];

	//the node's rows, the last node's cleared first;  an answer for a node the page has since left is dropped (as References does)
	async load():Promise<void>{
		const load = ++this.#load;
		this.rows.set( [] );
		this.userWriteMask.set( undefined );
		this.loading.set( true );
		this.error.set( undefined );
		try{
			const rows = await this.pageData().gateway.attributes( this.cnnctnSlug, this.node() );
			if( load==this.#load )
				this.#take( rows );
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
	#take( rows:Attribute[] ){
		this.rows.set( rows );
		const mask = rows.find( a=>a.key=='userWriteMask' )?.value;
		this.userWriteMask.set( typeof mask=='number' ? mask : undefined );
	}
	editor( a:Attribute ):AttributeEditor|undefined{ return a.editor( this.userWriteMask() ); }
	//the quality the server gave - the Value's reading always has one, Good included;  another attribute's shows only where it refused
	status( a:Attribute ):string{ return a.sc || (a.key=='dataValue' && a.value!==undefined) ? OpcError.text( a.sc ) : ""; }
	statusTooltip( a:Attribute ):string{ return a.sc ? `${scHex( a.sc )} - ${this.status( a )}` : ""; }
	qualityIcon( a:Attribute ){ return a.sc ? statusIcon( a.sc ) : undefined; }
	time( d:Date|undefined ):string{ return NodeView.time( d ) ?? ""; }

	toggle( a:Attribute, e:MatCheckboxChange ){
		return this.#write( a, e.checked, ()=>e.source.checked = a.value===true );
	}
	changeNumber( a:Attribute, e:Event ){
		const box = <HTMLInputElement>e.target;
		const show = ()=>box.value = a.text;
		const value = numberValue( box.value, undefined );//no value - a cleared box, text that is no number - and the box shows the row's again
		return value===undefined ? show() : this.#write( a, value, show );
	}
	changeText( a:Attribute, e:Event ){
		const box = <HTMLInputElement>e.target;
		const show = ()=>box.value = a.text;
		return a.def.required && !box.value.trim() ? show() : this.#write( a, box.value, show );//a blank DisplayName writes nothing;  a blank Description removes it
	}
	//The editor ends showing what the server reads back - the echo, with the mask it reads back too, or after a refusal every attribute
	//read again - rather than what was typed, as the Children tab's editors do:  a binding re-applies only when its value changes.
	//Leaving the box for another node fires the write as the page moves, and that node's rows usually land first:  an answer for rows
	//since replaced is dropped, as load() drops one, so the last node's mask never becomes this one's edit gate.  A refusal still says so.
	async #write( a:Attribute, value:Value, show?:()=>void ){
		const load = this.#load, node = this.node();
		try{
			const json = await this.pageData().gateway.updateNode( this.cnnctnSlug, node.nodeId, {[a.key]: value}, [a.key, 'userWriteMask'] );
			if( load!=this.#load )
				return;
			if( json && a.key in json ){
				a.take( json[a.key] );
				if( a.key=='name' )
					this.renamed.emit( a.text );
			}
			const mask = this.rows().find( r=>r.key=='userWriteMask' );
			if( mask && json && 'userWriteMask' in json )
				mask.take( json['userWriteMask'] );
			this.#take( [...this.rows()] );
		}
		catch( e ){
			const left = load!=this.#load;
			this.#snackbar.exception( left ? `Could not write ${a.label} of ${node.name}.` : `Could not write ${a.label}.`, e );
			if( left )
				return;
			await this.load();
		}
		show?.();
		this.#cdRef.markForCheck();
	}
	get cnnctnSlug():string{ return this.pageData().server.connection.slug; }
}