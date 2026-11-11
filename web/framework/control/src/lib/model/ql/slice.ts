import { compareCells } from './filter-query';

//A view's slice (docs/views/spec.md, *Slice by*):  one column's values, each with the count of the rows that hold it, and the one
//value the list is narrowed to.  Pure functions:  a GraphQL list reads the counts with a `groupBy` query, and a list that holds
//its rows counts them.

//A value a row holds in the slice column:  null for the rows with none.
export type SliceValue = string|number|boolean|null;
export type SliceCount = { value:SliceValue, count:number };

//A cell as a slice value:  an empty one is null, and an object - an enum or a foreign key answered `{id name}` - is its name.
export function sliceValueOf( cell:unknown ):SliceValue{
	if( cell!=null && typeof cell=="object" )
		cell = ( cell as {name?:unknown, id?:unknown} ).name ?? ( cell as {id?:unknown} ).id;
	if( cell==null || cell==="" || ( typeof cell=="number" && isNaN(cell) ) )
		return null;
	return typeof cell=="number" || typeof cell=="boolean" ? cell : `${cell}`;
}
//the cell is the slice's:  by its text, as a filter's `field:a` is, an empty cell for null alone
export function inSlice( cell:unknown, value:SliceValue ):boolean{
	const held = sliceValueOf( cell );
	return value===null ? held===null : held!==null && `${held}`==`${value}`;
}
//`rows` counted by what `cell` answers for each
export function countSlices<T>( rows:readonly T[], cell:( row:T )=>unknown ):SliceCount[]{
	const counts = new Map<string,SliceCount>();
	for( const row of rows ){
		const value = sliceValueOf( cell(row) ), key = sliceKey( value );
		const held = counts.get( key );
		if( held )
			++held.count;
		else
			counts.set( key, {value, count: 1} );
	}
	return [...counts.values()];
}
//A `groupBy` query's rows as counts.  THROWS where a row has no count:  a collection a custom query answers may ignore `groupBy`
//and answer its rows, which are not counts.
export function readSliceCounts( rows:unknown, column:string ):SliceCount[]{
	if( !Array.isArray(rows) )
		throw new Error( `The counts by '${column}' are not a list.` );
	return rows.map( row=>{
		const count = ( row as {count?:unknown} )?.count;
		if( typeof count!="number" )
			throw new Error( `The server did not count by '${column}'.` );
		return { value: sliceValueOf( (row as Record<string,unknown>)[column] ), count };
	} );
}
//The panel's order:  an enum's values as its schema lists them (`order`), the others ascending, and the rows with none last.
//`held`, the value the list is narrowed to, is among them though no row holds it now.
export function orderSlices( counts:readonly SliceCount[], order?:readonly string[], held?:SliceValue ):SliceCount[]{
	const y = [...counts];
	if( held!==undefined && !y.some( c=>sliceKey(c.value)==sliceKey(held) ) )
		y.push( {value: held, count: 0} );
	const place = ( value:SliceValue )=>{ const i = order?.indexOf( `${value}` ) ?? -1; return i<0 ? Infinity : i; };
	return y.sort( (a, b)=>{
		if( a.value===null || b.value===null )
			return a.value===null ? ( b.value===null ? 0 : 1 ) : -1;
		const pa = place( a.value ), pb = place( b.value );
		return pa!=pb ? ( pa<pb ? -1 : 1 ) : compareCells( a.value, b.value );
	} );
}
//a value's key among a column's:  1 and "1" apart
export function sliceKey( value:SliceValue ):string{ return `${typeof value}:${value}`; }