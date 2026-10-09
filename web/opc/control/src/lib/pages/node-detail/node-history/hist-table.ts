import { DatePipe } from '@angular/common';
import { Component, computed, effect, input, output, signal, untracked } from '@angular/core';
import { MatButtonModule } from '@angular/material/button';
import { MatChipsModule } from '@angular/material/chips';
import { MatIconModule } from '@angular/material/icon';
import { MatPaginatorModule, PageEvent } from '@angular/material/paginator';
import { MatTableModule } from '@angular/material/table';
import { MatTooltipModule } from '@angular/material/tooltip';
import { HistValue } from '../../../model/hist';
import { Variable } from '../../../model/node';
import { NodeId } from '../../../model/node-id';
import { OpcError } from '../../../model/opc-error';
import { scHex, statusIcon } from '../../../model/status-code';
import { valueString } from '../../../model/value';

//The table:  every value loaded, newest first, with both timestamps and the status, paged.  The read opens with
//the newest values and Load earlier adds older ones below them, as a log reads.  In modified mode the rows are the edits
//made - the value an Insert put in, or the one a Replace, Update or Delete took out - each with its ModificationInfo (spec
//*Reads*).  A value of a node this user may edit has a Replace and a Delete of its own.
@Component({
	selector: 'hist-table',
	templateUrl: './hist-table.html',
	styleUrls: ['./hist-table.scss'],
	imports: [DatePipe, MatButtonModule, MatChipsModule, MatIconModule, MatPaginatorModule, MatTableModule, MatTooltipModule]
})
export class HistTable{
	constructor(){
		//a new read empties the values:  back to the first page.  A live push is not a reason to move the reader off the page they are on.
		effect( ()=>{ if( !this.values().length ) untracked( ()=>this.pageIndex.set(0) ); } );
	}
	values = input.required<HistValue[]>();//in source-time order
	nodes = input.required<Variable[]>();
	modified = input( false );//the rows are modifications, with their ModificationInfo in place of the flags
	editable = input<Variable[]>( [] );//the nodes whose values offer a Replace and a Delete
	replace = output<HistValue>();
	remove = output<HistValue>();
	pageIndex = signal( 0 );
	pageSize = signal( 100 );
	//latest first, and a value with no source time, which has no place in that order, last
	ordered = computed<HistValue[]>( ()=>{ const v = this.values(); return [...v.filter(x=>x.source).reverse(), ...v.filter(x=>!x.source)]; } );
	rows = computed<HistValue[]>( ()=>{
		const size = this.pageSize(), last = Math.max( Math.ceil(this.ordered().length/size)-1, 0 );
		const start = Math.min( this.pageIndex(), last )*size;//a page that no longer exists shows the last one
		return this.ordered().slice( start, start+size );
	} );
	editableKeys = computed( ()=>new Set( this.editable().map( n=>n.key ) ) );
	displayedColumns = computed<string[]>( ()=>[
		...(this.nodes().length>1 ? ['node'] : []), 'source', 'server', 'status', 'value',
		...(this.modified() ? ['modType', 'modTime', 'modUser'] : ['flags']),
		...(!this.modified() && this.editable().length ? ['actions'] : [])
	] );
	nodeName( id:NodeId ):string{ return this.nodes().find( n=>n.key==id.key )?.name ?? id.toString(); }
	status( v:HistValue ):string{ return v.status ? OpcError.text( v.status ) : "Good"; }
	statusTooltip( v:HistValue ):string{ return v.status ? `${scHex( v.status )} - ${this.status( v )}` : ""; }
	qualityIcon( v:HistValue ){ return statusIcon( v.status ); }
	valueText( v:HistValue ):string{ return valueString( v.value ); }
	//what a value is besides a reading:  a bound returnBounds asked for, a group's heartbeat, or an edit's record in modified mode
	flags( v:HistValue ):string[]{
		const y:string[] = [];
		if( v.bound ) y.push( "bound" );
		if( v.heartbeat ) y.push( "heartbeat" );
		if( v.modification ) y.push( `${v.modification.type} by ${v.modification.user}` );
		return y;
	}
	//a stored value of a node this user may edit:  not a bound, which stands for a record outside the range, and not one
	//without its source time to the tick, which no edit could name - a push's, or one with no source time
	canEdit( v:HistValue ):boolean{ return !this.modified() && !v.bound && !!v.sourceTime && this.editableKeys().has( v.node.key ); }
	onPage( e:PageEvent ){ this.pageIndex.set( e.pageIndex ); this.pageSize.set( e.pageSize ); }
}