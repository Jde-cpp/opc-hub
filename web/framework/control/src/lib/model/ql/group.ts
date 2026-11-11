import { SliceCount, sliceKey, SliceValue, sliceValueOf } from './slice';
import type { ChipTone, ChipTones } from './view';

//A view's groups (docs/views/spec.md, *Group by*):  the rows of a page, or of a list that holds its rows, cut where the group
//column's value changes, each run under a header with its count and the sums of the columns chosen.  Pure functions, the same for
//ql-table and the hand-built tables.  The rows come sorted by the group column first (View.ordered), so a value is one run, and a
//page that starts inside one starts with its header again.

//What a list is grouped by, as its view and its host say it (View.grouping):  the column and its header's text, its chip's tones,
//the columns summed, and - a list read a page at a time - the count of each value over the whole list.
export type Grouping = { by:string, label:string, chip?:ChipTones, sums:{name:string, label:string}[], totals?:readonly SliceCount[] };

//A group's header among the rows, a class so that no row is taken for one.  `count` and `sums` are of the rows it heads, shown or
//collapsed, and `total` the value's count over the whole list, where the host has it.
export class GroupHeader{
	constructor( readonly key:string, readonly value:SliceValue, readonly label:string, readonly collapsed:boolean, readonly total:number|undefined, sums:{name:string, label:string}[], readonly tone?:ChipTone|"" ){
		this.sums = sums.map( s=>({...s, sum: 0}) );
	}
	count = 0;
	readonly sums:{name:string, label:string, sum:number}[];
	get chip():boolean{ return this.tone!==undefined && this.value!==null; }//the value reads as its column's chip
	//*12 of 40*, where the list holds more of the value than the rows here
	get of():number|undefined{ return this.total!==undefined && this.total!=this.count ? this.total : undefined; }
}
export function isGroupHeader( row:unknown ):row is GroupHeader{ return row instanceof GroupHeader; }

const none:ReadonlySet<string> = new Set();
//`rows` with a header ahead of each run of one value of the group column, the rows of a `collapsed` group (by its key) left out.
//`cell` answers a row's value in a column, as the list shows it.
export function groupRows<T>( rows:readonly T[], grouping:Grouping, cell:( row:T, column:string )=>unknown, collapsed:ReadonlySet<string> = none ):(T|GroupHeader)[]{
	const totals = new Map( grouping.totals?.map( c=>[sliceKey( c.value ), c.count] ) );
	const y:(T|GroupHeader)[] = [];
	let header:GroupHeader|undefined;
	for( const row of rows ){
		const value = sliceValueOf( cell(row, grouping.by) ), key = sliceKey( value );
		if( header?.key!=key ){
			const tone = grouping.chip ? grouping.chip[`${value}`] ?? "" : undefined;
			header = new GroupHeader( key, value, value===null ? `No ${grouping.label}` : `${value}`, collapsed.has( key ), totals.get( key ), grouping.sums, tone );
			y.push( header );
		}
		++header.count;
		for( const sum of header.sums ){
			const n = cell( row, sum.name );
			if( typeof n=="number" && !isNaN( n ) )
				sum.sum = Math.round( (sum.sum+n)*1e6 )/1e6;//a fraction's sum without its binary tail
		}
		if( !header.collapsed )
			y.push( row );
	}
	return y;
}