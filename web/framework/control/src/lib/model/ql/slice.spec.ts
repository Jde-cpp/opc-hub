import { countSlices, inSlice, orderSlices, readSliceCounts, sliceValueOf } from './slice';

//#595:  the slice panel's counts, a GraphQL list's from its `groupBy` query and a browser-side list's from its rows
describe( 'slice', ()=>{
	it( 'reads a cell as a value:  an empty one null, an object its name', ()=>{
		expect( [1, true, "a", "", null, undefined, NaN, {id: 2, name: "Key"}, {id: 3}].map( sliceValueOf ) ).toEqual( [1, true, "a", null, null, null, null, "Key", 3] );
	} );
	it( 'holds a cell by its text, and an empty one for null alone', ()=>{
		expect( [inSlice(1, "1"), inSlice("Good", "Good"), inSlice("good", "Good"), inSlice("", null), inSlice(undefined, null), inSlice("a", null), inSlice(null, "a")] )
			.toEqual( [true, true, false, true, true, false, false] );
	} );
	it( 'counts the rows by what a cell answers', ()=>{
		const rows = [{kind: "a"}, {kind: "b"}, {kind: "a"}, {kind: null}, {}];
		expect( countSlices( rows, ( r:{kind?:string|null} )=>r.kind ) ).toEqual( [{value: "a", count: 2}, {value: "b", count: 1}, {value: null, count: 2}] );
	} );
	it( "reads a groupBy query's rows, and refuses rows the server did not count", ()=>{
		expect( readSliceCounts( [{provider: "Key", count: 2}, {provider: null, count: 1}], "provider" ) ).toEqual( [{value: "Key", count: 2}, {value: null, count: 1}] );
		expect( ()=>readSliceCounts( [{id: 1, provider: "Key"}], "provider" ) ).toThrow( "did not count by 'provider'" );//a custom query that ignored groupBy
		expect( ()=>readSliceCounts( undefined, "provider" ) ).toThrow( "not a list" );
	} );
	it( "orders an enum's values as its schema lists them, the others ascending, the rows with none last and the held value among them", ()=>{
		const counts = [{value: null, count: 1}, {value: "Warning", count: 2}, {value: "Critical", count: 3}, {value: "Debug", count: 4}];
		expect( orderSlices( counts, ["Debug", "Warning", "Error", "Critical"] ).map( c=>c.value ) ).toEqual( ["Debug", "Warning", "Critical", null] );
		expect( orderSlices( [{value: 10, count: 1}, {value: 9, count: 1}, {value: null, count: 1}] ).map( c=>c.value ) ).toEqual( [9, 10, null] );
		expect( orderSlices( counts, ["Debug", "Warning", "Error", "Critical"], "Error" ) ).toContainEqual( {value: "Error", count: 0} );
		expect( orderSlices( counts, undefined, "Debug" ) ).toHaveLength( 4 );
	} );
} );