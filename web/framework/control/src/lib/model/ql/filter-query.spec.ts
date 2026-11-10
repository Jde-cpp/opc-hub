import { DateUtils } from '../../utils/date-utils';
import { Field } from './schema/field';
import { cellValues, filterArguments, filterPredicate, parseFilter, suggestFilter, takesFilterTerm, toggleFilterTerm, type FilterColumn, type FilterHost } from './filter-query';

const column = ( name:string, type:object, displayName:string, liveToggle?:object ):FilterColumn=>({ qlField: new Field({name, type} as Partial<Field>), displayName, liveToggle });
const scalar = ( type:string )=>({ kind: "SCALAR", name: type });
const required = ( type:string )=>({ kind: "NON_NULL", name: null, ofType: scalar(type) });
//A GraphQL list's columns:  the users', a connection's grafted status and the Resources page's Enforced.
const server = ():FilterHost=>({ server: true, columns: [
	column( "id", required("ID"), "Id" ),
	column( "name", required("String"), "Name" ),
	column( "provider", {kind: "ENUM", name: "Provider"}, "Provider" ),
	column( "description", scalar("String"), "Description" ),
	column( "securityMode", scalar("String"), "Security Mode" ),
	column( "exponent", scalar("UInt"), "Exponent" ),
	column( "isGroup", scalar("Boolean"), "Is Group" ),
	column( "created", required("DateTime"), "Created" ),
	column( "status", {kind: "OBJECT", name: "ConnectionStatus"}, "Status" ),
	column( "members", {kind: "LIST", name: null, ofType: {kind: "UNION", name: "Identity"}}, "Members" ),
	column( "deleted", scalar("DateTime"), "Enforced", {enable: "Enforce", disable: "Stop enforcing"} )
] });
//A list that filters its own rows, as a node's children and the watch list do.
const browser = ():FilterHost=>({ server: false, columns: [
	column( "name", scalar("String"), "Name" ),
	column( "snapshot", scalar("String"), "Snapshot" ),
	column( "status", {kind: "ENUM", name: "Status"}, "Status" ),
	column( "sampling", scalar("Float"), "Sampling" ),
	column( "sourceTime", scalar("DateTime"), "Source Time" )
] });
const today = ()=>new Date( 2026, 10, 10, 15, 30 );//a Tuesday afternoon, nine days after the clocks went back
//the arguments of a text every term of which is read
const compile = ( text:string, host=server() )=>{
	const parsed = parseFilter( text, host );
	expect( parsed.errors, text ).toEqual( [] );
	return filterArguments( parsed.terms, today() );
};
type Case = [text:string, args:string[], vars:Record<string,unknown>];
const expectCases = ( cases:Case[] )=>{
	for( const [text, args, vars] of cases )
		expect( compile(text), text ).toEqual( {args, vars} );
};

describe( "filter-query", ()=>{
	beforeEach( ()=>vi.stubEnv( 'TZ', 'America/New_York' ) );
	afterEach( ()=>vi.unstubAllEnvs() );

	describe( "the grammar, as a GraphQL list's arguments", ()=>{
		it( "reads a value, a list and an exclusion", ()=>expectCases( [
			[ "provider:Google", ["provider:$provider"], {provider: ["Google"]} ],
			[ "provider:Google,Key", ["provider:$provider"], {provider: ["Google", "Key"]} ],
			[ "-provider:Google,Key", ["provider:{nin:$provider_nin}"], {provider_nin: ["Google", "Key"]} ],
			[ "-provider:Google has:provider", ["provider:{nin:$provider_nin,ne:null}"], {provider_nin: ["Google"]} ],
			[ "id:7,9", ["id:$id"], {id: [7, 9]} ],
			[ "isGroup:TRUE", ["isGroup:$isGroup"], {isGroup: [true]} ]
		] ) );
		it( "reads no: and has:", ()=>expectCases( [
			[ "no:description", ["description:$description"], {description: [null]} ],
			[ "has:description", ["description:{ne:null}"], {} ],
			[ "-no:description", ["description:{ne:null}"], {} ],
			[ "-has:description", ["description:$description"], {description: [null]} ],
			[ "no:description,provider", ["description:$description", "provider:$provider"], {description: [null], provider: [null]} ],
			[ "no:\"security mode\"", ["securityMode:$securityMode"], {securityMode: [null]} ]
		] ) );
		it( "reads a comparison and a range on a number", ()=>expectCases( [
			[ "exponent:>3", ["exponent:{gt:$exponent_gt}"], {exponent_gt: 3} ],
			[ "exponent:>=3", ["exponent:{gte:$exponent_gte}"], {exponent_gte: 3} ],
			[ "exponent:<3", ["exponent:{lt:$exponent_lt}"], {exponent_lt: 3} ],
			[ "exponent:<=4", ["exponent:{lte:$exponent_lte}"], {exponent_lte: 4} ],
			[ "exponent:3..9", ["exponent:{gte:$exponent_gte,lte:$exponent_lte}"], {exponent_gte: 3, exponent_lte: 9} ],
			[ "exponent:*..9", ["exponent:{lte:$exponent_lte}"], {exponent_lte: 9} ],
			[ "exponent:3..*", ["exponent:{gte:$exponent_gte}"], {exponent_gte: 3} ]
		] ) );
		it( "names a field as its header reads or by its QL name, in any case", ()=>expectCases( [
			[ "\"security mode\":Sign", ["securityMode:$securityMode"], {securityMode: ["Sign"]} ],
			[ "'Security Mode':Sign", ["securityMode:$securityMode"], {securityMode: ["Sign"]} ],
			[ "securitymode:Sign", ["securityMode:$securityMode"], {securityMode: ["Sign"]} ],
			[ "PROVIDER:Google", ["provider:$provider"], {provider: ["Google"]} ]
		] ) );
		it( "reads a quoted value with a space or a comma, and a quote inside a word as a letter", ()=>expectCases( [
			[ "description:\"a b\",'c,d',e", ["description:$description"], {description: ["a b", "c,d", "e"]} ],
			[ "description:\">3\"", ["description:$description"], {description: [">3"]} ],
			[ "description:o'brien,", ["description:$description"], {description: ["o'brien"]} ],
			[ "o'brien", ["name:{glob:$name_glob}"], {name_glob: "*o'brien*"} ]
		] ) );
		it( "searches the name for a bare word, and for every one of several", ()=>expectCases( [
			[ "pump", ["name:{glob:$name_glob}"], {name_glob: "*pump*"} ],
			[ "  pump   station ", ["name:{glob:$name_glob}"], {name_glob: ["*pump*", "*station*"]} ],
			[ "\"pump station\"", ["name:{glob:$name_glob}"], {name_glob: "*pump station*"} ],
			[ "a*b x? [3] 50%", ["name:{glob:$name_glob}"], {name_glob: ["*a[*]b*", "*x[?]*", "*[[]3]*", "*50%*"]} ],
			[ "provider:Key pump", ["provider:$provider", "name:{glob:$name_glob}"], {provider: ["Key"], name_glob: "*pump*"} ]
		] ) );
	} );

	describe( "a column's terms", ()=>{
		it( "merge into one argument, a variable an operator", ()=>expectCases( [
			[ "provider:Google -provider:Key", ["provider:{in:$provider_in,nin:$provider_nin}"], {provider_in: ["Google"], provider_nin: ["Key"]} ],
			[ "-provider:Google -provider:Key,Google", ["provider:{nin:$provider_nin}"], {provider_nin: ["Google", "Key"]} ],
			[ "provider:Google,Key provider:Key,VK", ["provider:$provider"], {provider: ["Key"]} ],
			[ "provider:Google provider:Key", ["provider:$provider"], {provider: []} ],
			[ "provider:Key,Key", ["provider:$provider"], {provider: ["Key"]} ],
			[ "-provider:Key,Key", ["provider:{nin:$provider_nin}"], {provider_nin: ["Key"]} ],
			[ "provider:Google no:provider", ["provider:$provider"], {provider: []} ],
			[ "has:provider no:provider", ["provider:{in:$provider_in,ne:null}"], {provider_in: [null]} ],
			[ "name:pump station", ["name:{in:$name_in,glob:$name_glob}"], {name_in: ["pump"], name_glob: "*station*"} ],
			[ "exponent:>3 provider:Key exponent:<9", ["exponent:{gt:$exponent_gt,lt:$exponent_lt}", "provider:$provider"], {exponent_gt: 3, exponent_lt: 9, provider: ["Key"]} ]
		] ) );
		it( "keep the tighter of a comparison made twice", ()=>expectCases( [
			[ "exponent:>3 exponent:>5 exponent:>4", ["exponent:{gt:$exponent_gt}"], {exponent_gt: 5} ],
			[ "exponent:<=9 exponent:<=7", ["exponent:{lte:$exponent_lte}"], {exponent_lte: 7} ],
			[ "exponent:2..9 exponent:4..*", ["exponent:{gte:$exponent_gte,lte:$exponent_lte}"], {exponent_gte: 4, exponent_lte: 9} ],
			[ "created:>=2026-01-01 created:>=2026-01-15 created:<2026-02-01", ["created:{gte:$created_gte,lt:$created_lt}"], {created_gte: "2026-01-15T05:00:00.000Z", created_lt: "2026-02-01T05:00:00.000Z"} ]
		] ) );
	} );

	describe( "a date", ()=>{
		it( "is a whole day in the browser's time zone, sent as UTC", ()=>expectCases( [
			[ "created:2026-01-01", ["created:{gte:$created_gte,lt:$created_lt}"], {created_gte: "2026-01-01T05:00:00.000Z", created_lt: "2026-01-02T05:00:00.000Z"} ],
			[ "created:2026-01-01..2026-01-31", ["created:{gte:$created_gte,lt:$created_lt}"], {created_gte: "2026-01-01T05:00:00.000Z", created_lt: "2026-02-01T05:00:00.000Z"} ],
			[ "created:*..2026-01-31", ["created:{lt:$created_lt}"], {created_lt: "2026-02-01T05:00:00.000Z"} ],
			[ "created:2026-01-01..*", ["created:{gte:$created_gte}"], {created_gte: "2026-01-01T05:00:00.000Z"} ],
			[ "created:>=2026-01-01", ["created:{gte:$created_gte}"], {created_gte: "2026-01-01T05:00:00.000Z"} ],
			[ "created:<2026-01-01", ["created:{lt:$created_lt}"], {created_lt: "2026-01-01T05:00:00.000Z"} ],
			[ "created:>2026-01-01", ["created:{gte:$created_gte}"], {created_gte: "2026-01-02T05:00:00.000Z"} ],
			[ "created:<=2026-01-01", ["created:{lt:$created_lt}"], {created_lt: "2026-01-02T05:00:00.000Z"} ]
		] ) );
		it( "ends at the next calendar midnight on a daylight-saving day, not 24 hours on", ()=>expectCases( [
			[ "created:2026-03-08", ["created:{gte:$created_gte,lt:$created_lt}"], {created_gte: "2026-03-08T05:00:00.000Z", created_lt: "2026-03-09T04:00:00.000Z"} ],//23 hours
			[ "created:2026-11-01", ["created:{gte:$created_gte,lt:$created_lt}"], {created_gte: "2026-11-01T04:00:00.000Z", created_lt: "2026-11-02T05:00:00.000Z"} ],//25
			[ "created:>2026-03-07", ["created:{gte:$created_gte}"], {created_gte: "2026-03-08T05:00:00.000Z"} ]
		] ) );
		it( "with a time is its minute, or its second where one is typed", ()=>expectCases( [
			[ "created:2026-01-01T10:00", ["created:{gte:$created_gte,lt:$created_lt}"], {created_gte: "2026-01-01T15:00:00.000Z", created_lt: "2026-01-01T15:01:00.000Z"} ],
			[ "created:2026-01-01T10:00:05", ["created:{gte:$created_gte,lt:$created_lt}"], {created_gte: "2026-01-01T15:00:05.000Z", created_lt: "2026-01-01T15:00:06.000Z"} ],
			[ "created:\"2026-01-01 10:00\"", ["created:{gte:$created_gte,lt:$created_lt}"], {created_gte: "2026-01-01T15:00:00.000Z", created_lt: "2026-01-01T15:01:00.000Z"} ],
			[ "created:>=2026-01-01T10:00", ["created:{gte:$created_gte}"], {created_gte: "2026-01-01T15:00:00.000Z"} ],
			[ "created:<2026-01-01T10:00", ["created:{lt:$created_lt}"], {created_lt: "2026-01-01T15:00:00.000Z"} ],
			[ "created:>2026-01-01T10:00", ["created:{gte:$created_gte}"], {created_gte: "2026-01-01T15:01:00.000Z"} ],
			[ "created:<=2026-01-01T23:59:59", ["created:{lt:$created_lt}"], {created_lt: "2026-01-02T05:00:00.000Z"} ],
			[ "created:2026-01-01T08:00..2026-01-01T17:30", ["created:{gte:$created_gte,lt:$created_lt}"], {created_gte: "2026-01-01T13:00:00.000Z", created_lt: "2026-01-01T22:31:00.000Z"} ],
			[ "created:2026-01-01T08:00..2026-01-02", ["created:{gte:$created_gte,lt:$created_lt}"], {created_gte: "2026-01-01T13:00:00.000Z", created_lt: "2026-01-03T05:00:00.000Z"} ]
		] ) );
		it( "is counted from today for @today", ()=>expectCases( [
			[ "created:@today", ["created:{gte:$created_gte,lt:$created_lt}"], {created_gte: "2026-11-10T05:00:00.000Z", created_lt: "2026-11-11T05:00:00.000Z"} ],
			[ "created:>=@today-7d", ["created:{gte:$created_gte}"], {created_gte: "2026-11-03T05:00:00.000Z"} ],
			[ "created:>=@TODAY-10d", ["created:{gte:$created_gte}"], {created_gte: "2026-10-31T04:00:00.000Z"} ],//before the clocks went back
			[ "created:@today-1d..@today+1d", ["created:{gte:$created_gte,lt:$created_lt}"], {created_gte: "2026-11-09T05:00:00.000Z", created_lt: "2026-11-12T05:00:00.000Z"} ]
		] ) );
		it( "is the day of the browser that runs the query", ()=>{
			const terms = parseFilter( "created:2026-01-01 created:<=@today", server() ).terms;
			vi.stubEnv( 'TZ', 'Europe/Berlin' );
			expect( filterArguments(terms, new Date(2026, 10, 10, 15, 30)).vars ).toEqual( {created_gte: "2025-12-31T23:00:00.000Z", created_lt: "2026-01-01T23:00:00.000Z"} );
		} );
	} );

	describe( "an enum column whose values are known", ()=>{
		const host = ():FilterHost=>({ ...server(), enums: new Map( [["Provider", [{name: "Google"}, {name: "Key"}]]] ) });
		it( "takes a value in any case, and sends the schema's spelling", ()=>{
			expect( compile("provider:google,KEY", host()) ).toEqual( {args: ["provider:$provider"], vars: {provider: ["Google", "Key"]}} );
			expect( compile("-provider:gOOgle", host()) ).toEqual( {args: ["provider:{nin:$provider_nin}"], vars: {provider_nin: ["Google"]}} );
		} );
		it( "refuses one that is none of them, naming them", ()=>{
			expect( parseFilter("provider:Gogle", host()).errors.map(e=>e.message) ).toEqual( ["'Gogle' is not a value of 'Provider' - Google, Key."] );
			expect( parseFilter("-provider:Key,VK", host()).terms ).toEqual( [] );
		} );
	} );

	describe( "a live-toggle column", ()=>{
		it( "reads has: as live, a null, and no: as not", ()=>expectCases( [
			[ "has:enforced", ["deleted:$deleted"], {deleted: [null]} ],
			[ "no:enforced", ["deleted:{ne:null}"], {} ],
			[ "-has:Enforced", ["deleted:{ne:null}"], {} ]
		] ) );
		it( "reads has: and no: by its QL name as what the column holds", ()=>expectCases( [
			[ "has:deleted", ["deleted:{ne:null}"], {} ],
			[ "no:DELETED", ["deleted:$deleted"], {deleted: [null]} ]
		] ) );
		it( "leaves has: and no: as they are on a deleted column that is no toggle", ()=>{
			const host:FilterHost = { server: true, columns: [column("deleted", scalar("DateTime"), "Deleted")] };
			expect( compile("has:deleted", host) ).toEqual( {args: ["deleted:{ne:null}"], vars: {}} );
			expect( compile("no:deleted", host) ).toEqual( {args: ["deleted:$deleted"], vars: {deleted: [null]}} );
		} );
	} );

	describe( "a term the box cannot read", ()=>{
		const refused:[text:string, message:string][] = [
			[ "bogus:1", "No field 'bogus'" ],
			[ "no:bogus", "No field 'bogus'" ],
			[ "status:Error", "'Status' is not a column the server can filter" ],
			[ "has:status", "'Status' is not a column the server can filter" ],
			[ "members:3", "'Members' is not a column the server can filter" ],
			[ "name:>m", "'Name' is not a number or a date" ],
			[ "provider:A..K", "'Provider' is not a number or a date" ],
			[ "isGroup:>false", "'Is Group' is not a number or a date" ],
			[ "\"pump station", "quote is not closed" ],
			[ "description:'a b", "quote is not closed" ],
			[ "description:\"a\"b", "follows a closing quote" ],
			[ "-created:2026-01-01", "cannot be excluded" ],
			[ "created:2026-01-01,2026-01-02", "one day or a range" ],
			[ "created:tomorrow", "'tomorrow' is not a date" ],
			[ "created:2026-02-30", "'2026-02-30' is not a date" ],
			[ "created:2026-01-01T25:00", "is not a date" ],
			[ "created:2026-01-01T10", "is not a date" ],
			[ "created:2026-01-01T10:00:61", "is not a date" ],
			[ "-created:2026-01-01T10:00", "cannot be excluded" ],
			[ "created:@today-7", "is not a date" ],
			[ "enforced:true", "Only has:enforced and no:enforced" ],
			[ "deleted:>2026-01-01", "Only has:enforced and no:enforced" ],
			[ "exponent:abc", "'abc' is not a number" ],
			[ "exponent:<=3.5", "'3.5' is not a whole number" ],
			[ "exponent:-1", "'-1' is below 0" ],
			[ "id:7.5", "'7.5' is not a whole number" ],
			[ "description:\"\"", "An empty value is not read - write no:description" ],
			[ "-'security mode':''", "write no:\"security mode\" for an empty 'Security Mode'" ],
			[ "description:a,\"\"", "An empty value is not read" ],
			[ "exponent:>", "needs a value" ],
			[ "exponent:>3,5", "takes one value" ],
			[ "exponent:1..2,5", "takes one value" ],
			[ "-exponent:>3", "cannot be excluded" ],
			[ "exponent:*..*", "one end at least" ],
			[ "exponent:3..", "both ends" ],
			[ "isGroup:maybe", "'maybe' is not true or false" ],
			[ "provider:", "needs a value" ],
			[ "provider:,", "needs a value" ],
			[ "no:", "needs a field" ],
			[ "-pump", "cannot be excluded" ],
			[ "\"\"", "empty word" ]
		];
		it( "is named, and left out of the terms", ()=>{
			for( const [text, message] of refused ){
				const parsed = parseFilter( text, server() );
				expect( parsed.terms, text ).toEqual( [] );
				expect( parsed.errors.length, text ).toBe( 1 );
				expect( parsed.errors[0].message, text ).toContain( message );
				expect( [parsed.errors[0].start, parsed.errors[0].end], text ).toEqual( [0, text.length] );
			}
		} );
		it( "is marked where it stands, among the terms that are read", ()=>{
			const parsed = parseFilter( "provider:Google bogus:1  pump status:Error", server() );
			expect( parsed.errors.map(e=>[e.start, e.end]) ).toEqual( [[16, 23], [30, 42]] );
			expect( parsed.terms.map(t=>[t.field, t.operator, t.values, t.negated, t.start, t.end]) ).toEqual( [["provider", "in", ["Google"], false, 0, 15], [undefined, "word", ["pump"], false, 25, 29]] );
			expect( filterArguments(parsed.terms, today()) ).toEqual( {args: ["provider:$provider", "name:{glob:$name_glob}"], vars: {provider: ["Google"], name_glob: "*pump*"}} );
		} );
		it( "is a bare word on a GraphQL list with no name", ()=>{
			const host:FilterHost = { server: true, columns: [column("url", scalar("String"), "Url")] };
			expect( parseFilter("pump", host).errors[0].message ).toContain( "no name" );
			expect( parseFilter("pump", {...host, server: false}).errors ).toEqual( [] );
		} );
		it( "is nothing in an empty box", ()=>{
			expect( parseFilter("  ", server()) ).toEqual( {terms: [], errors: []} );
			expect( filterArguments([], today()) ).toEqual( {args: [], vars: {}} );
		} );
	} );

	describe( "as a predicate over rows in the browser", ()=>{
		type Row = Record<string,unknown>;
		const rows = ():Row[]=>[
			{ name: "Pump", snapshot: 7, status: "Good", sampling: 250, sourceTime: "2026-03-08 00:00:00.000" },
			{ name: "pump station", snapshot: "12", status: "BadSensorFailure", sampling: 1000, sourceTime: "2026-03-08 23:59:59.999" },
			{ name: "Valve", snapshot: "open", status: undefined, sampling: undefined, sourceTime: "2026-03-09 00:00:00.000" },
			{ name: "Folder", snapshot: undefined, status: "", sampling: 0, sourceTime: undefined }
		];
		const names = ( text:string )=>{
			const parsed = parseFilter( text, browser() );
			expect( parsed.errors, text ).toEqual( [] );
			const test = filterPredicate<Row>( parsed.terms, browser(), {cell: ( row, name )=>row[name], time: d=>DateUtils.toLocalInput( d ).replace( "T", " " ), today: today()} );
			return rows().filter( test ).map( r=>r["name"] );
		};
		const cases:[text:string, names:string[]][] = [
			[ "", ["Pump", "pump station", "Valve", "Folder"] ],
			[ "name:Pump", ["Pump"] ],//the cell's text exactly
			[ "name:Pump,Valve", ["Pump", "Valve"] ],
			[ "snapshot:12", ["pump station"] ],
			[ "-status:Good", ["pump station", "Valve", "Folder"] ],//an exclusion keeps an empty cell
			[ "-status:Good has:status", ["pump station"] ],
			[ "no:status", ["Valve", "Folder"] ],
			[ "has:snapshot", ["Pump", "pump station", "Valve"] ],
			[ "snapshot:>7", ["pump station", "Valve"] ],//numeric where both are numbers, text otherwise
			[ "snapshot:>=7", ["Pump", "pump station", "Valve"] ],
			[ "snapshot:<10", ["Pump"] ],//an empty cell matches no comparison
			[ "snapshot:<=12", ["Pump", "pump station"] ],
			[ "snapshot:7..12", ["Pump", "pump station"] ],
			[ "sampling:<=250", ["Pump", "Folder"] ],
			[ "sampling:300..*", ["pump station"] ],
			[ "name:>p", ["Pump", "pump station", "Valve"] ],//a comparison on text, which a GraphQL list refuses
			[ "PUMP", ["Pump", "pump station"] ],//a bare word, in any cell, whatever its case
			[ "pump STATION", ["pump station"] ],
			[ "sensor", ["pump station"] ],
			[ "7", ["Pump"] ],//a String cell that holds a number
			[ "250", [] ],//a number's cell is not text
			[ "2026-03-09", [] ],//nor is a time's
			[ "sourceTime:2026-03-08", ["Pump", "pump station"] ],//a whole day, on a cell of local text
			[ "sourceTime:>2026-03-08", ["Valve"] ],
			[ "sourceTime:<=2026-03-08", ["Pump", "pump station"] ],
			[ "sourceTime:<2026-03-08", [] ],
			[ "sourceTime:2026-03-08..2026-03-09", ["Pump", "pump station", "Valve"] ],
			[ "sourceTime:>=2026-03-08T12:00", ["pump station", "Valve"] ],//a date with a time
			[ "sourceTime:2026-03-08T23:59", ["pump station"] ],
			[ "sourceTime:2026-03-08T00:00:00", ["Pump"] ],
			[ "sourceTime:>=@today-300d", ["Pump", "pump station", "Valve"] ],
			[ "status:Good pump", ["Pump"] ]
		];
		it( "matches as the grammar says", ()=>{
			for( const [text, expected] of cases )
				expect( names(text), text ).toEqual( expected );
		} );
		it( "searches no object's cell for a bare word", ()=>{
			const host:FilterHost = { server: false, columns: [column( "name", scalar("String"), "Name" ), column( "parameters", {kind: "OBJECT", name: "Parameters"}, "Parameters" )] };
			const test = filterPredicate<Row>( parseFilter("object", host).terms, host, {cell: ( row, name )=>row[name]} );
			expect( test({name: "Pump", parameters: {queue: 1}}) ).toBe( false );
			expect( test({name: "objective", parameters: {queue: 1}}) ).toBe( true );
		} );
	} );

	//#591:  what the box offers at the cursor, and the text once one is taken
	describe( "suggestFilter", ()=>{
		const enums = new Map( [["Provider", [{name: "Google"}, {name: "Key"}, {name: "Open Id"}]]] );
		const values = ( name:string )=>( {name: ["Pump", "pump station", "a,b", ""], description: [null, "it's", {id: 1}], exponent: [3, 65537], securityMode: ["Sign", ">odd", "a..b"]} as Record<string,unknown[]> )[name] ?? [];
		const host = ()=>({ ...server(), enums });
		const offered = ( text:string, caret=text.length, on:FilterHost=host() )=>suggestFilter( text, caret, on, values );
		const labels = ( text:string, caret?:number, on?:FilterHost )=>offered( text, caret, on ).map( s=>s.label );
		//the text with the suggestion labelled `label` taken, a `|` where the cursor stands
		const take = ( text:string, label:string, caret=text.length )=>{
			const s = offered( text, caret ).find( o=>o.label==label )!;
			return `${s.text.slice( 0, s.caret )}|${s.text.slice( s.caret )}${s.more ? " …" : ""}`;
		};
		it( "offers the headers in a field's place, with no:, has: and - before them, and not a column the box cannot filter", ()=>{
			expect( labels("") ).toEqual( ["no:", "has:", "-", "Id:", "Name:", "Provider:", "Description:", '"Security Mode":', "Exponent:", '"Is Group":', "Created:"] );
			expect( labels("name:a ") ).toEqual( labels("") );
			expect( labels("pro") ).toEqual( ["Provider:"] );
			expect( labels("-") ).toEqual( ["-Id:", "-Name:", "-Provider:", "-Description:", '-"Security Mode":', "-Exponent:", '-"Is Group":', "-Created:"] );
			expect( labels("e") ).toEqual( ["Exponent:", "Name:", "Provider:", "Description:", '"Security Mode":', "Created:"] );//those that start with it first
		} );
		it( "writes a field with its colon, in quotes where it has a space, and has more to say", ()=>{
			expect( take("pro", "Provider:") ).toBe( "Provider:| …" );
			expect( take("name:a sec", '"Security Mode":') ).toBe( 'name:a "Security Mode":| …' );
			expect( take("-ex", "-Exponent:") ).toBe( "-Exponent:| …" );
			expect( take("nam:a", "Name:", 3) ).toBe( "Name:|a …" );//the colon already there
		} );
		it( "offers an enum's values from the host, the rows' for the rest, and none already in the term", ()=>{
			expect( labels("provider:") ).toEqual( ["Google", "Key", "Open Id"] );
			expect( labels("provider:Key,") ).toEqual( ["Google", "Open Id"] );
			expect( labels("provider:g") ).toEqual( ["Google"] );
			expect( labels("provider:google") ).toEqual( [] );//typed whole, in any case
			expect( labels("name:") ).toEqual( ["Pump", "pump station", "a,b"] );
			expect( labels("description:") ).toEqual( ["it's"] );//no null, no object
			expect( labels("isGroup:") ).toEqual( ["true", "false"] );
			expect( labels("status:") ).toEqual( [] );//grafted
			expect( labels("enforced:") ).toEqual( [] );//a live toggle takes has: and no: alone
			expect( labels("nope:") ).toEqual( [] );
		} );
		it( "quotes a value with a space or a comma, or one that would read as a comparison or a range", ()=>{
			expect( take("provider:op", "Open Id") ).toBe( 'provider:"Open Id"|' );
			expect( take("name:", "a,b") ).toBe( 'name:"a,b"|' );
			expect( take("name:Pump,", "pump station") ).toBe( 'name:Pump,"pump station"|' );
			expect( take('name:"pu', "pump station") ).toBe( 'name:"pump station"|' );//the quote it was typed in
			expect( take("securityMode:", ">odd") ).toBe( 'securityMode:">odd"|' );
			expect( take("securityMode:", "a..b") ).toBe( 'securityMode:"a..b"|' );
			expect( take("description:", "it's") ).toBe( "description:it's|" );//an apostrophe within a word is a letter
			expect( take("name: x", "Pump", 5) ).toBe( "name:Pump| x" );
		} );
		it( "offers @today on a date and the comparison signs where the column takes them", ()=>{
			expect( labels("created:") ).toEqual( ["@today", "@today-1d", "@today-7d", "@today-30d", ">=", "<=", ">", "<"] );
			expect( take("created:", ">=") ).toBe( "created:>=| …" );
			expect( take("created:>=", "@today-7d") ).toBe( "created:>=@today-7d|" );
			expect( labels("exponent:") ).toEqual( ["3", "65537", ">=", "<=", ">", "<"] );
			expect( labels("exponent:>6") ).toEqual( ["65537"] );
			expect( labels("name:") ).not.toContain( ">" );//the server compares numbers and dates alone
			expect( labels("name:>") ).toEqual( [] );
			expect( labels("name:", undefined, browser()) ).toContain( ">" );//a list that filters its own rows compares any column
		} );
		it( "offers the columns that can be empty after no: and has:, a live toggle's by its header", ()=>{
			expect( labels("no:") ).toEqual( ["Provider", "Description", "Security Mode", "Exponent", "Is Group", "Enforced"] );
			expect( take("has:sec", "Security Mode") ).toBe( 'has:"Security Mode"|' );
			expect( labels("no:Enforced,") ).not.toContain( "Enforced" );
			expect( labels("has:enforced") ).toEqual( [] );
			expect( labels("has:", undefined, browser()) ).toEqual( ["Name", "Snapshot", "Status", "Sampling", "Source Time"] );
		} );
		it( "offers nothing after a closed quote", ()=>{
			expect( labels('name:"Pump"') ).toEqual( [] );
		} );
	} );

	//a chip clicked, and clicked again
	describe( "toggleFilterTerm", ()=>{
		const host = server(), provider = host.columns[2], mode = host.columns[4];
		it( "adds the term as the box would write it, and takes out a term that is exactly it", ()=>{
			expect( toggleFilterTerm( "", host, provider, "Google" ) ).toBe( "Provider:Google" );
			expect( toggleFilterTerm( "pump ", host, mode, "Sign And Encrypt" ) ).toBe( 'pump "Security Mode":"Sign And Encrypt"' );
			expect( toggleFilterTerm( "pump Provider:Google no:description", host, provider, "Google" ) ).toBe( "pump no:description" );
			expect( toggleFilterTerm( 'provider:"Google"', host, provider, "Google" ) ).toBe( "" );//however it was typed
		} );
		it( "adds beside a term that only holds the value among others, or excludes it", ()=>{
			expect( toggleFilterTerm( "provider:Google,Key", host, provider, "Google" ) ).toBe( "provider:Google,Key Provider:Google" );
			expect( toggleFilterTerm( "-provider:Google", host, provider, "Google" ) ).toBe( "-provider:Google Provider:Google" );
		} );
		it( "says which columns take a term", ()=>{
			const takes = ( name:string, on=host )=>takesFilterTerm( on.columns.find( c=>c.qlField.name==name )!, on );
			expect( ["provider", "status", "members", "deleted"].map( n=>takes(n) ) ).toEqual( [true, false, false, false] );
			expect( takes( "status", browser() ) ).toBe( true );
		} );
	} );

	it( "gathers the rows' distinct values by column, in order", ()=>{
		const rows = [{a: "b", n: 10}, {a: "a", n: 9}, {a: "b", n: null}, {a: "", n: {id: 1}}];
		expect( cellValues( ["a", "n", "none"], rows, ( row, column )=>( row as Record<string,unknown> )[column] ) ).toEqual( {a: ["a", "b"], n: [9, 10], none: []} );
	} );
} );