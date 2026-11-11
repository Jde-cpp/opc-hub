import { parseFilter } from 'jde-framework';
import { OpcObject } from './node';
import { NodeView } from './node-view';
import { OpcError } from './opc-error';
import { EAccess, ETypes } from './types';
import { WatchView } from './watch-view';
import { variable } from 'jde-opc/testing';

const bad = 0x808C0000, uncertainHigh = 0x40940600;
const x = ( json:object={} )=>variable( 1, 'x', json );

//OPC 10000-4 7.38:  a reading is a value AND its quality, and every path hands a row both through setReading.
describe( 'Variable.setReading', ()=>{
	it( 'takes the browse\'s reading, quality and all', ()=>{
		expect( x({value: 7}) ).toMatchObject( {value: 7, sc: 0, stale: false} );
		expect( x({value: {v: 1500, sc: uncertainHigh}}) ).toMatchObject( {value: 1500, sc: uncertainHigh, stale: false} );
	} );

	//the OpcError a Bad reading used to become was bound straight into <input type=number> and <mat-checkbox>.
	it( 'leaves a browse that arrives Bad with a status and no value', ()=>{
		const v = x( {value: {sc: bad}} );
		expect( v.value ).toBeUndefined();
		expect( v.sc ).toBe( bad );
		expect( v.stale ).toBe( true );
	} );

	it( 'has neither a value nor a status where the browse brought no reading', ()=>{
		const v = x();
		expect( v.value ).toBeUndefined();
		expect( v.sc ).toBeUndefined();
	} );

	it( 'keeps the last value through a Bad reading that carries none', ()=>{
		const v = x( {value: 7} );
		v.setReading( {sc: bad} );
		expect( v ).toMatchObject( {value: 7, sc: bad, stale: true} );
	} );

	it( 'takes the value a Bad push does carry - the one the server holds', ()=>{
		const v = x( {value: 7} );
		v.setReading( {value: 6, sc: bad} );
		expect( v ).toMatchObject( {value: 6, sc: bad, stale: true} );
	} );

	it( 'never takes a failure for a value', ()=>{
		const v = x( {value: 7} );
		v.setReading( {value: new OpcError(0x80340000, "Subscribe", "", undefined), sc: 0x80340000} );//a refused subscribe, as the gateway reports it
		expect( v ).toMatchObject( {value: 7, sc: 0x80340000} );
	} );

	it( 'recovers on the next good reading', ()=>{
		const v = x( {value: 7} );
		v.setReading( {sc: bad} );
		v.setReading( {value: 8} );
		expect( v ).toMatchObject( {value: 8, sc: 0, stale: false} );
	} );

	it( 'blanks the value on a reading that is not Bad and has none', ()=>{
		const v = x( {value: 7} );
		v.setReading( {sc: 0} );
		expect( v.value ).toBeUndefined();
	} );
} );

describe( 'NodeView status column', ()=>{
	it( 'words the quality as the cell shows it, so the filter and the sort agree with the screen', ()=>{
		expect( NodeView.cellValue(x({value: 7}), "status") ).toBe( "Good" );
		OpcError.setMessages( [{sc: 0x40940000, message: "UncertainEngineeringUnitsExceeded"}, {sc: bad, message: "BadSensorFailure"}] );
		expect( NodeView.cellValue(x({value: {v: 1500, sc: uncertainHigh}}), "status") ).toBe( "UncertainEngineeringUnitsExceeded+High" );
		expect( NodeView.cellValue(x({value: {sc: bad}}), "status") ).toBe( "BadSensorFailure" );
	} );

	it( 'is blank where there is no reading to qualify', ()=>{
		expect( NodeView.cellValue(new OpcObject(<any>{ns:2, i:9, name:"o", browse:{ns:2, name:"o"}}), "status") ).toBeUndefined();
		expect( NodeView.cellValue(x(), "status") ).toBeUndefined();
		expect( NodeView.cellValue(x({value: null}), "status") ).toBeUndefined();
		expect( NodeView.cellValue(x({value: 7, userAccessLevel: EAccess.None}), "status") ).toBeUndefined();//"no read access" says it
	} );

	//the default view leaves the quality to the icon beside the value;  the column is there for a view that wants to sort or filter on it.
	it( 'offers Status in the default view, hidden, right after Snapshot', ()=>{
		const view = NodeView.default();
		expect( view.displayedColumns ).not.toContain( "status" );
		const names = view.fields.map( f=>f.name );
		expect( names.indexOf("status") ).toBe( names.indexOf("snapshot")+1 );
	} );

	//a user's saved view persists only the columns it displayed, so one from before the column loads without it - hidden, not
	//broken - and the icon in the Snapshot cell is what still shows the quality there.
	it( 'loads a view saved before the column existed, with Status hidden', ()=>{
		const old = NodeView.fromConfig( {name: "mine", configColumns: ["id", "name", "snapshot"], sort: [{active: "name", direction: "asc"}]}, NodeView.schema );
		const json = JSON.parse( JSON.stringify(old.toJson(undefined)) );
		expect( JSON.stringify(json) ).not.toContain( "status" );
		const loaded = NodeView.fromJson( json, NodeView.schema );
		expect( loaded.displayedColumns ).toEqual( ["id", "name", "snapshot"] );
	} );
} );

//the columns hidden in the default view beside Status:  the reading's timestamps, and what the browse answered the node by
describe( 'NodeView timestamp and browse columns', ()=>{
	//reviews-issue/412-clients-web-fixes.md #5:  the text was UTC ISO under a cell that shows local time, so the filter's
	//suggestions and its Less/Greater compared against text the screen never showed.
	it( 'shows the timestamps in local time, as the filter and sort read them, and blank until a push brings them', ()=>{
		const v = x( {value: 7} );
		expect( NodeView.cellValue(v, "sourceTime") ).toBeUndefined();
		v.setReading( {value: 8, sc: 0, source: new Date(2026, 9, 9, 9, 4, 5, 6), server: new Date(2026, 9, 9, 9, 4, 5, 7)} );
		expect( NodeView.cellValue(v, "sourceTime") ).toBe( "2026-10-09 09:04:05.006" );
		expect( NodeView.cellValue(v, "serverTime") ).toBe( "2026-10-09 09:04:05.007" );
	} );
	//clients spec *Values and writes* (#423):  an enumeration of the server's own by the name of its value, once resolved, so the cell, the
	//filter and the sort agree;  the number until then, and for a value the type doesn't name
	it( 'shows an enumeration by name', ()=>{
		const v = x( {dataType: {ns: 2, i: 3002}, value: 2} );
		expect( NodeView.cellValue(v, "snapshot") ).toBe( 2 );
		v.customDataType = <any>{ id: v.customDataType, name: 'Health', enumValues: [{id: 1, name: 'OK'}, {id: 2, name: 'Failed'}] };
		expect( NodeView.cellValue(v, "snapshot") ).toBe( 'Failed' );
		v.setReading( {value: 7, sc: 0} );
		expect( NodeView.cellValue(v, "snapshot") ).toBe( 7 );
	} );
	//reviews-issue/423-clients-writes.md W3:  a StatusCode value read as a decimal number;  it is spelled as the Status column spells a code,
	//its severity and hex until the name arrives
	it( 'shows a StatusCode value by name', ()=>{
		const v = x( {dataType: {ns: 0, i: ETypes.StatusCode}, value: 0x40920000} );
		expect( NodeView.cellValue(v, "snapshot") ).toBe( 'Uncertain 0x40920000' );
		expect( OpcError.emptyMessages() ).toContain( 0x40920000 );//asked for by the reading, so the fetch after a browse brings it
		OpcError.setMessages( [{sc: 0x40920000, message: "UncertainSubstituteValue"}] );
		expect( NodeView.cellValue(v, "snapshot") ).toBe( 'UncertainSubstituteValue' );
		v.setReading( {value: 0, sc: 0} );
		expect( NodeView.cellValue(v, "snapshot") ).toBe( 'Good' );
		expect( NodeView.cellValue(x( {dataType: {ns: 0, i: ETypes.UInt32}, value: 0x40920000} ), "snapshot") ).toBe( 0x40920000 );//a number of another type stays one
	} );
	it( 'shows the reference type and the type definition as node ids', ()=>{
		const v = x( {refType: {ns: 0, i: 47}, typeDef: {ns: 2, i: 1002}} );
		expect( NodeView.cellValue(v, "refType") ).toBe( "i=47" );
		expect( NodeView.cellValue(v, "typeDef") ).toBe( "ns=2;i=1002" );
		expect( NodeView.cellValue(x(), "refType") ).toBeUndefined();
	} );
	it( 'offers them in the default view, hidden', ()=>{
		const view = NodeView.default();
		for( const name of ["sourceTime", "serverTime", "refType", "typeDef"] ){
			expect( view.fields.map(f=>f.name) ).toContain( name );
			expect( view.displayedColumns ).not.toContain( name );
		}
	} );
} );

//reviews-issue/591-views-filter-box.md #1:  the fields' names were made from their QL names, so the filter box refused a column
//named as its header reads - Reference Type was Ref Type, and the watch list's Revised was Revised Sampling
describe( 'NodeView and WatchView column names', ()=>{
	const unread = ( view:NodeView|WatchView, filter:string )=>parseFilter( filter, view.filterHost() ).errors.map( e=>e.message );
	it( "are the page's headers, which the filter box reads", ()=>{
		const node = NodeView.default();
		expect( node.fields.map( f=>f.displayName ) ).toEqual( Object.values( NodeView.columns ) );
		expect( unread( node, '"Reference Type":i=46 "Type Definition":i=58 refType:i=47' ) ).toEqual( [] );
		expect( unread( node, '"Ref Type":i=46' ) ).toEqual( ["No field 'Ref Type'."] );
		const watch = WatchView.default();
		expect( watch.fields.map( f=>f.displayName ) ).toEqual( Object.values( WatchView.columns ) );
		expect( unread( watch, "sampling:>100 revised:>100" ) ).toEqual( [] );
		expect( watch.displayedColumns ).toEqual( ["connection", "node", "value", "status", "sampling", "revisedSampling", "filter"] );
	} );
} );

//MVP first-run:  an identity with no role gets userAccessLevel 0 on every node, and the Snapshot cell showed a blank that
//read as a null value.  A row the browse never gave a level is not a denial.
describe( 'NodeView.readDenied', ()=>{
	it( 'flags a variable the server reports no Read right on, and nothing else', ()=>{
		const denied = x( {userAccessLevel: EAccess.None} );
		const readable = x( {userAccessLevel: EAccess.Read} );
		expect( NodeView.readDenied(denied) ).toBe( true );
		expect( NodeView.readDenied(readable) ).toBe( false );
		expect( NodeView.readDenied(x()) ).toBe( false );//no level in the row
	} );
} );

//#591:  the filter box's terms run over the cells (views spec *The filter box*)
describe( 'NodeView filter', ()=>{
	const kept = ( filter:string, ...nodes:ReturnType<typeof x>[] )=>{
		const view = NodeView.default();
		view.filter = filter;
		expect( view.parsedFilter().errors, filter ).toEqual( [] );
		return view.apply( nodes ).map( n=>n.name );
	};
	const named = ( name:string, json:object={} )=>variable( 1, name, json );
	it( 'declares what its columns hold, and filters in the browser', ()=>{
		const field = ( name:string )=>NodeView.schema.find( name );
		expect( ["class", "dataType", "status"].map( n=>field(n).isEnum ) ).toEqual( [true, true, true] );
		expect( ["sourceTime", "serverTime"].map( n=>field(n).isDateTime ) ).toEqual( [true, true] );
		expect( ["id", "name", "snapshot", "access", "description"].map( n=>field(n).isString ) ).toEqual( [true, true, true, true, true] );
		expect( NodeView.default().filterHost() ).toMatchObject( {server: false, enums: undefined} );
	} );
	it( 'compares a String column as numbers where both sides are, and as text otherwise', ()=>{
		const nodes = [named( "nine", {value: 9} ), named( "ten", {value: 10} ), named( "text", {value: "b"} )];
		expect( kept( "snapshot:>9", ...nodes ) ).toEqual( ["ten", "text"] );//10 is past 9 as a number, where the text "10" is before "9"
		expect( kept( "snapshot:<=9", ...nodes ) ).toEqual( ["nine"] );
		expect( kept( "snapshot:>a", ...nodes ) ).toEqual( ["text"] );
	} );
	it( 'matches no comparison on an empty cell, and keeps one under an exclusion', ()=>{
		const nodes = [named( "empty" ), named( "five", {value: 5} )];
		expect( kept( "snapshot:<9", ...nodes ) ).toEqual( ["five"] );
		expect( kept( "snapshot:<=9", ...nodes ) ).toEqual( ["five"] );
		expect( kept( "-snapshot:5", ...nodes ) ).toEqual( ["empty"] );
		expect( kept( "no:snapshot", ...nodes ) ).toEqual( ["empty"] );
	} );
	it( 'reads a day on a time column as the whole of it, in local time', ()=>{
		const at = ( name:string, source:Date )=>{ const v = named( name, {value: 1} ); v.setReading( {value: 1, sc: 0, source} ); return v; };
		const nodes = [at( "eve", new Date(2026, 9, 8, 23, 59, 59, 999) ), at( "morning", new Date(2026, 9, 9, 0, 0) ), at( "night", new Date(2026, 9, 9, 23, 59, 59, 999) ), named( "never", {value: 1} )];
		expect( kept( "sourceTime:2026-10-09", ...nodes ) ).toEqual( ["morning", "night"] );
		expect( kept( '"source time":>2026-10-08', ...nodes ) ).toEqual( ["morning", "night"] );
		expect( kept( "sourceTime:<=2026-10-08", ...nodes ) ).toEqual( ["eve"] );
		expect( kept( "sourceTime:2026-10-08..2026-10-09", ...nodes ) ).toEqual( ["eve", "morning", "night"] );
	} );
	it( 'matches an enum cell exactly and a bare word in any text cell, whatever its case', ()=>{
		const nodes = [named( "Pump", {value: 1} ), named( "valve", {value: "pumped"} ), named( "Tank" )];
		expect( kept( "status:Good", ...nodes ) ).toEqual( ["Pump", "valve"] );
		expect( kept( "status:good", ...nodes ) ).toEqual( [] );
		expect( kept( "PUMP", ...nodes ) ).toEqual( ["Pump", "valve"] );
	} );
} );
//#595:  the node and watch lists count their own rows for the slice panel, and narrow them to its value
describe( 'NodeView and WatchView slice', ()=>{
	const named = ( name:string, json:object={} )=>variable( 1, name, json );
	it( 'slices by the small sets and the numbers, never by text or a time', ()=>{
		expect( NodeView.default().sliceChoices( NodeView.columns ).map( c=>c.name ) ).toEqual( ["class", "dataType", "status"] );
		expect( WatchView.default().sliceChoices( WatchView.columns ).map( c=>c.name ) ).toEqual( ["connection", "status", "sampling", "revisedSampling", "queue", "revisedQueue", "discard"] );
	} );
	it( 'counts the filtered rows by the cell, and narrows them to a value or to the rows with none', ()=>{
		const failing = named( "b", {value: 2} );
		failing.sc = bad;
		const nodes = [named( "a", {value: 1} ), failing, named( "c", {value: 3} ), new OpcObject( <any>{ns: 2, i: 9, name: "folder", browse: {ns: 2, name: "folder"}} )];
		const view = NodeView.default();
		view.sliceBy = "status";
		const good = OpcError.text( 0 ), failed = OpcError.text( bad );
		const counts = ()=>view.sliceCounts( view.filtered(nodes) );
		const narrowed = ( slice:string|null|undefined )=>{ view.slice = slice; return view.apply( nodes ).map( n=>n.name ); };
		expect( counts() ).toEqual( [{value: good, count: 2}, {value: failed, count: 1}, {value: null, count: 1}] );
		expect( [narrowed( good ), narrowed( null ), narrowed( undefined ).length] ).toEqual( [["a", "c"], ["folder"], 4] );
		view.filter = "name:a,b";
		expect( counts() ).toEqual( [{value: good, count: 1}, {value: failed, count: 1}] );
		view.sliceBy = undefined;
		expect( [counts(), narrowed( good ).length] ).toEqual( [[], 2] );
	} );
} );