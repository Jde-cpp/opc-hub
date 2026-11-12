import { GroupHeader, Grouping, groupRows, isGroupHeader } from './group';

//#596:  a grouped list's rows under a header per value, the same cut for a GraphQL list's page and a list that holds its rows
describe( 'groupRows', ()=>{
	type Row = { name:string, kind?:string|null, size?:number|null };
	const grouping = ( settings:Partial<Grouping>={} ):Grouping=>({by: "kind", label: "Kind", sums: [], ...settings});
	const cell = ( row:Row, column:string )=>( row as Record<string,unknown> )[column];
	const rows:Row[] = [{name: "a", kind: "x", size: 1}, {name: "b", kind: "x", size: 2}, {name: "c", kind: "y", size: 4}, {name: "d", kind: null}, {name: "e"}];
	//the headers as `label count`, the rows by name
	const read = ( y:(Row|GroupHeader)[] )=>y.map( r=>isGroupHeader( r ) ? `${r.label} ${r.count}${r.of===undefined ? "" : ` of ${r.of}`}` : r.name );

	it( "puts a header ahead of each value's rows, with its count, and the rows with none under No <field>", ()=>{
		expect( read( groupRows(rows, grouping(), cell) ) ).toEqual( ["x 2", "a", "b", "y 1", "c", "No Kind 2", "d", "e"] );
		expect( groupRows( [], grouping(), cell ) ).toEqual( [] );
	} );
	//reviews-issue/596-views-group-by.md #1:  the rows were cut where the value changed, so rows out of the group column's order - a
	//node's after a push, a Variable page's own row first - had a header for each run, two of one key
	it( "gathers a value's rows under one header whatever their order, the groups as their values first come", ()=>{
		const mixed = [rows[2], rows[0], rows[3], rows[1], rows[4]];
		expect( read( groupRows(mixed, grouping(), cell) ) ).toEqual( ["y 1", "c", "x 2", "a", "b", "No Kind 2", "d", "e"] );
	} );
	//reviews-issue/596-views-group-by.md #6:  counts read before rows were added said *5 of 3*
	it( "says the list's count only where it holds more than the rows here", ()=>{
		expect( read( groupRows(rows, grouping({totals: [{value: "x", count: 1}, {value: "y", count: 1}]}), cell) ).slice( 0, 4 ) ).toEqual( ["x 2", "a", "b", "y 1"] );
	} );
	it( "repeats a group's header on the page a page break left the rest of it on, each with its page's count of the list's", ()=>{
		const totals = [{value: "x", count: 2}, {value: "y", count: 1}, {value: null, count: 2}];
		expect( read( groupRows(rows.slice(0, 1), grouping({totals}), cell) ) ).toEqual( ["x 1 of 2", "a"] );
		expect( read( groupRows(rows.slice(1, 4), grouping({totals}), cell) ) ).toEqual( ["x 1 of 2", "b", "y 1", "c", "No Kind 1 of 2", "d"] );
	} );
	it( 'leaves out the rows of a collapsed group, its header still counting and summing them', ()=>{
		const y = groupRows( rows, grouping({sums: [{name: "size", label: "Size"}]}), cell, new Set(["string:x"]) );
		expect( read( y ) ).toEqual( ["x 2", "y 1", "c", "No Kind 2", "d", "e"] );
		const headers = y.filter( isGroupHeader );
		expect( headers.map( h=>[h.key, h.collapsed, h.sums] ) ).toEqual( [
			["string:x", true, [{name: "size", label: "Size", sum: 3}]], ["string:y", false, [{name: "size", label: "Size", sum: 4}]], ["object:null", false, [{name: "size", label: "Size", sum: 0}]]] );
	} );
	it( 'sums the numbers alone, a fraction without its binary tail', ()=>{
		const [header] = groupRows( [{name: "a", kind: "x", size: 0.1}, {name: "b", kind: "x", size: 0.2}, {name: "c", kind: "x", size: null}, {name: "d", kind: "x", size: "7" as unknown as number}], grouping({sums: [{name: "size", label: "Size"}]}), cell );
		expect( ( header as GroupHeader ).sums[0].sum ).toBe( 0.3 );
	} );
	it( "reads a value as its column's chip, in its tone, and the rows with none as text", ()=>{
		const chip = {x: "ok"} as const;
		const headers = groupRows( rows, grouping({chip}), cell ).filter( isGroupHeader );
		expect( headers.map( h=>[h.chip, h.tone] ) ).toEqual( [[true, "ok"], [true, ""], [false, ""]] );
		expect( ( groupRows(rows, grouping(), cell)[0] as GroupHeader ).chip ).toBe( false );
	} );
	it( 'keeps 1 and "1" apart, and reads an object by its name', ()=>{
		const y = groupRows( [{kind: 1}, {kind: "1"}, {kind: {id: 2, name: "Key"}}] as unknown as Row[], grouping(), cell );
		expect( y.filter( isGroupHeader ).map( h=>h.key ) ).toEqual( ["number:1", "string:1", "string:Key"] );
	} );
	it( 'takes no row for a header', ()=>{
		expect( [isGroupHeader( {key: "a", count: 1} ), isGroupHeader( null )] ).toEqual( [false, false] );
	} );
} );