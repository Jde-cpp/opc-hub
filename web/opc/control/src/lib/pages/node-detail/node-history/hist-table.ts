import { DatePipe } from '@angular/common';
import { Component, computed, effect, input, signal, untracked } from '@angular/core';
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

//The table (plan Phase 4):  every value loaded, newest first, with both timestamps and the status, paged.  The read opens with
//the newest values and Load earlier adds older ones below them, as a log reads.
@Component({
	selector: 'hist-table',
	templateUrl: './hist-table.html',
	styleUrls: ['./hist-table.scss'],
	imports: [DatePipe, MatChipsModule, MatIconModule, MatPaginatorModule, MatTableModule, MatTooltipModule]
})
export class HistTable{
	constructor(){
		//a new read empties the values:  back to the first page.  A live push is not a reason to move the reader off the page they are on.
		effect( ()=>{ if( !this.values().length ) untracked( ()=>this.pageIndex.set(0) ); } );
	}
	values = input.required<HistValue[]>();//in source-time order
	nodes = input.required<Variable[]>();
	namesVersion = input( 0 );
	pageIndex = signal( 0 );
	pageSize = signal( 100 );
	//latest first, and a value with no source time, which has no place in that order, last
	ordered = computed<HistValue[]>( ()=>{ const v = this.values(); return [...v.filter(x=>x.source).reverse(), ...v.filter(x=>!x.source)]; } );
	rows = computed<HistValue[]>( ()=>{
		const size = this.pageSize(), last = Math.max( Math.ceil(this.ordered().length/size)-1, 0 );
		const start = Math.min( this.pageIndex(), last )*size;//a page that no longer exists shows the last one
		return this.ordered().slice( start, start+size );
	} );
	displayedColumns = computed<string[]>( ()=>[...(this.nodes().length>1 ? ['node'] : []), 'source', 'server', 'status', 'value', 'flags'] );
	nodeName( id:NodeId ):string{ return this.nodes().find( n=>n.key==id.key )?.name ?? id.toString(); }
	status( v:HistValue ):string{ this.namesVersion(); return v.status ? OpcError.text( v.status ) : "Good"; }
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
	onPage( e:PageEvent ){ this.pageIndex.set( e.pageIndex ); this.pageSize.set( e.pageSize ); }
}