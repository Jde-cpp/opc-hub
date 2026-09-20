if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { OpcObject, Variable } from './node';
import { NodeView } from './node-view';
import { OpcError } from './opc-error';
import { EAccess } from './types';

const bad = 0x808C0000, uncertainHigh = 0x40940600;
const variable = ( json:object={} )=>new Variable( <any>{ns:2, i:1, name:"x", browse:{ns:2, name:"x"}, ...json} );

//OPC 10000-4 7.38:  a reading is a value AND its quality, and every path hands a row both through setReading.
describe( 'Variable.setReading', ()=>{
	it( 'takes the browse\'s reading, quality and all', ()=>{
		expect( variable({value: 7}) ).toMatchObject( {value: 7, sc: 0, stale: false} );
		expect( variable({value: {v: 1500, sc: uncertainHigh}}) ).toMatchObject( {value: 1500, sc: uncertainHigh, stale: false} );
	} );

	//the OpcError a Bad reading used to become was bound straight into <input type=number> and <mat-checkbox>.
	it( 'leaves a browse that arrives Bad with a status and no value', ()=>{
		const v = variable( {value: {sc: bad}} );
		expect( v.value ).toBeUndefined();
		expect( v.sc ).toBe( bad );
		expect( v.stale ).toBe( true );
	} );

	it( 'has neither a value nor a status where the browse brought no reading', ()=>{
		const v = variable();
		expect( v.value ).toBeUndefined();
		expect( v.sc ).toBeUndefined();
	} );

	it( 'keeps the last value through a Bad reading that carries none', ()=>{
		const v = variable( {value: 7} );
		v.setReading( {sc: bad} );
		expect( v ).toMatchObject( {value: 7, sc: bad, stale: true} );
	} );

	it( 'takes the value a Bad push does carry - the one the server holds', ()=>{
		const v = variable( {value: 7} );
		v.setReading( {value: 6, sc: bad} );
		expect( v ).toMatchObject( {value: 6, sc: bad, stale: true} );
	} );

	it( 'never takes a failure for a value', ()=>{
		const v = variable( {value: 7} );
		v.setReading( {value: new OpcError(0x80340000, "Subscribe", "", undefined), sc: 0x80340000} );//a refused subscribe, as the gateway reports it
		expect( v ).toMatchObject( {value: 7, sc: 0x80340000} );
	} );

	it( 'recovers on the next good reading', ()=>{
		const v = variable( {value: 7} );
		v.setReading( {sc: bad} );
		v.setReading( {value: 8} );
		expect( v ).toMatchObject( {value: 8, sc: 0, stale: false} );
	} );

	it( 'blanks the value on a reading that is not Bad and has none', ()=>{
		const v = variable( {value: 7} );
		v.setReading( {sc: 0} );
		expect( v.value ).toBeUndefined();
	} );
} );

describe( 'NodeView status column', ()=>{
	it( 'words the quality as the cell shows it, so the filter and the sort agree with the screen', ()=>{
		expect( NodeView.cellValue(variable({value: 7}), "status") ).toBe( "Good" );
		OpcError.setMessages( [{sc: 0x40940000, message: "UncertainEngineeringUnitsExceeded"}, {sc: bad, message: "BadSensorFailure"}] );
		expect( NodeView.cellValue(variable({value: {v: 1500, sc: uncertainHigh}}), "status") ).toBe( "UncertainEngineeringUnitsExceeded+High" );
		expect( NodeView.cellValue(variable({value: {sc: bad}}), "status") ).toBe( "BadSensorFailure" );
	} );

	it( 'is blank where there is no reading to qualify', ()=>{
		expect( NodeView.cellValue(new OpcObject(<any>{ns:2, i:9, name:"o", browse:{ns:2, name:"o"}}), "status") ).toBeUndefined();
		expect( NodeView.cellValue(variable(), "status") ).toBeUndefined();
		expect( NodeView.cellValue(variable({value: null}), "status") ).toBeUndefined();
		expect( NodeView.cellValue(variable({value: 7, userAccessLevel: EAccess.None}), "status") ).toBeUndefined();//"no read access" says it
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
		const old = new NodeView( {name: "mine", configColumns: ["id", "name", "snapshot"], sort: [{active: "name", direction: "asc"}]}, NodeView.schema );
		const json = JSON.parse( JSON.stringify(old.toJson(undefined)) );
		expect( JSON.stringify(json) ).not.toContain( "status" );
		const loaded = new NodeView( json, NodeView.schema );
		expect( loaded.displayedColumns ).toEqual( ["id", "name", "snapshot"] );
	} );
} );
