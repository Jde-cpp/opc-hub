import { OpcObject } from './node';
import { NodeView } from './node-view';
import { OpcError } from './opc-error';
import { EAccess } from './types';
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
	it( 'shows the timestamps as ISO text, so they sort in time order, and blank until a push brings them', ()=>{
		const v = x( {value: 7} );
		expect( NodeView.cellValue(v, "sourceTime") ).toBeUndefined();
		v.setReading( {value: 8, sc: 0, source: new Date(Date.UTC(2026, 9, 9, 13, 4, 5, 6)), server: new Date(Date.UTC(2026, 9, 9, 13, 4, 5, 7))} );
		expect( NodeView.cellValue(v, "sourceTime") ).toBe( "2026-10-09T13:04:05.006Z" );
		expect( NodeView.cellValue(v, "serverTime") ).toBe( "2026-10-09T13:04:05.007Z" );
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