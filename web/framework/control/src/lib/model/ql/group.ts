import { SliceCount, sliceKey, SliceValue, sliceValueOf } from './slice';
import type { ChipTone, ChipTones } from './view';

//A view's groups (docs/views/spec.md, *Group by*):  the rows of a page, or of a list that holds its rows, gathered by the group
//column's value, each group under a header with its count and the sums of the columns chosen.  Pure functions, the same for
//ql-table and the hand-built tables.  The rows come sorted by the group column first (View.ordered), so the groups are in its
//order, and a page that starts inside one starts with its header again.  Gathered, not cut where the value changes:  a host's
//rows need not be in that order - a node's, a push since their sort, or its own row first (reviews-issue/596-views-group-by.md #1).

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
	//*12 of 40*, where the list holds more of the value than the rows here:  not where it holds fewer, counts read before rows were
	//added (QLListResolver.loadGroupCounts keeps them while their query is the same)
	get of():number|undefined{ return this.total!==undefined && this.total>this.count ? this.total : undefined; }
}
export function isGroupHeader( row:unknown ):row is GroupHeader{ return row instanceof GroupHeader; }
//What a table that draws groups binds:  the header's one column, its row definition's `when`, and its `trackBy` - a header by its
//key, a row by itself - so that a header made anew keeps its row, and the focus on its chevron (reviews-issue/596-views-group-by.md #2).
export const groupColumns:readonly string[] = ["jde-group"];
export const isGroupRow = ( _:number, row:unknown ):boolean=>isGroupHeader( row );
export const trackGroupRow = ( _:number, row:unknown ):unknown=>isGroupHeader( row ) ? row.key : row;

const none:ReadonlySet<string> = new Set();
//`rows` under a header for each value of the group column, in the order the values first come, each group's rows in the order
//they come, and a `collapsed` group's (by its key) left out.  `cell` answers a row's value in a column, as the list shows it.
export function groupRows<T>( rows:readonly T[], grouping:Grouping, cell:( row:T, column:string )=>unknown, collapsed:ReadonlySet<string> = none ):(T|GroupHeader)[]{
	const totals = new Map( grouping.totals?.map( c=>[sliceKey( c.value ), c.count] ) );
	const groups = new Map<string,{header:GroupHeader, rows:T[]}>();
	for( const row of rows ){
		const value = sliceValueOf( cell(row, grouping.by) ), key = sliceKey( value );
		let group = groups.get( key );
		if( !group ){
			const tone = grouping.chip ? grouping.chip[`${value}`] ?? "" : undefined;
			group = { header: new GroupHeader( key, value, value===null ? `No ${grouping.label}` : `${value}`, collapsed.has( key ), totals.get( key ), grouping.sums, tone ), rows: [] };
			groups.set( key, group );
		}
		const header = group.header;
		++header.count;
		for( const sum of header.sums ){
			const n = cell( row, sum.name );
			if( typeof n=="number" && !isNaN( n ) )
				sum.sum = Math.round( (sum.sum+n)*1e6 )/1e6;//a fraction's sum without its binary tail
		}
		group.rows.push( row );
	}
	return [...groups.values()].flatMap<T|GroupHeader>( g=>g.header.collapsed ? [g.header] : [g.header, ...g.rows] );
}