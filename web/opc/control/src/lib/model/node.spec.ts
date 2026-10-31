import { OpcObject } from './node';
import { NodeId } from './node-id';
import { EAccess } from './types';
import { variable } from 'jde-opc/testing';

const x = ( json:object={} )=>variable( 1, 'x', json );

//clients spec, Gaps #6:  the model read referenceType and typeDefinition where the browse writes refType and typeDef
//(Browse::Response::SetJson), so neither was ever filled.
describe( 'UaNode browse keys', ()=>{
	it( 'reads the reference type and the type definition under the keys the gateway writes', ()=>{
		const v = x( {refType: {ns: 0, i: 47}, typeDef: {ns: 2, i: 1002}} );
		expect( v.refType?.uaString() ).toBe( "i=47" );
		expect( v.typeDef?.uaString() ).toBe( "ns=2;i=1002" );
		expect( v.typeDef?.constructor ).toBe( NodeId );//an id, not an ObjectType node with no name
		const o = new OpcObject( <any>{ns: 2, i: 5, name: "o", browse: {ns: 2, name: "o"}, refType: {ns: 0, i: 35}, typeDef: {ns: 0, i: 61}} );
		expect( o.refType?.uaString() ).toBe( "i=35" );
		expect( o.typeDef?.uaString() ).toBe( "i=61" );
	} );
	it( 'has neither where the browse sent neither', ()=>{
		expect( x().refType ).toBeUndefined();
		expect( x().typeDef ).toBeUndefined();
	} );
} );

//clients spec, Gaps #8:  a push carries both timestamps (Opc.FromServer.proto NodeValues.source/server) and the row kept
//value and status alone.
describe( 'Variable timestamps', ()=>{
	const source = new Date( 1_700_000_000_000 ), server = new Date( 1_700_000_000_500 );
	it( 'keeps both timestamps a push carries', ()=>{
		const v = x( {value: 7} );
		expect( v.sourceTime ).toBeUndefined();//the browse brings none
		v.setReading( {value: 8, sc: 0, source, server} );
		expect( v ).toMatchObject( {value: 8, sourceTime: source, serverTime: server} );
	} );
	it( 'times a Bad push that carries no value, which the row keeps its last value through', ()=>{
		const v = x( {value: 7} );
		v.setReading( {sc: 0x808C0000, source, server} );
		expect( v ).toMatchObject( {value: 7, sourceTime: source, serverTime: server} );
	} );
	it( 'drops them for a new reading that came without - a write\'s echo or a re-read over REST', ()=>{
		const v = x( {value: 7} );
		v.setReading( {value: 8, sc: 0, source, server} );
		v.setReading( {value: 9, sc: 0} );
		expect( v.value ).toBe( 9 );
		expect( v.sourceTime ).toBeUndefined();
		expect( v.serverTime ).toBeUndefined();
		v.setReading( {value: 9, sc: 0, source, server} );
		v.setReading( {value: 9, sc: 0x40000000} );//the value again, but another quality:  another reading
		expect( v.sourceTime ).toBeUndefined();
	} );
	//reviews-issue/412-clients-web-fixes.md #8:  a Refresh, or a write's echo after the push it caused, blanked the times of a
	//value the push had timed.
	it( 'keeps them for a REST reading of the value and quality the push brought', ()=>{
		const v = x( {value: 7} );
		v.setReading( {value: 8, sc: 0, source, server} );
		v.setReading( {value: 8, sc: 0} );
		expect( v ).toMatchObject( {value: 8, sourceTime: source, serverTime: server} );
	} );
	it( 'hands them to a Refresh\'s row that reads the same, and the value too through a fault', ()=>{
		const before = x( {value: 8} );
		before.setReading( {value: 8, sc: 0, source, server} );
		const same = x( {value: 8} ), changed = x( {value: 9} );
		same.takeOver( before );
		changed.takeOver( before );
		expect( same ).toMatchObject( {sourceTime: source, serverTime: server} );
		expect( changed.sourceTime ).toBeUndefined();
		const faulted = x( {value: {sc: 0x808C0000}} );
		before.setReading( {sc: 0x808C0000, source, server} );
		faulted.takeOver( before );
		expect( faulted ).toMatchObject( {value: 8, sc: 0x808C0000, sourceTime: source} );
	} );
} );

describe( 'Variable.historyReadable', ()=>{
	it( 'needs historizing, as a boolean or a {v,sc} reading', ()=>{
		expect( x( {historizing: true} ).historyReadable ).toBe( true );
		expect( x( {historizing: {v: true, sc: 0x40000000}} ).historyReadable ).toBe( true );
		expect( x( {historizing: false} ).historyReadable ).toBe( false );
		expect( x( {historizing: null} ).historyReadable ).toBe( false );//the server did not say
		expect( x( {historizing: {sc: 0x80000000}} ).historyReadable ).toBe( false );
		expect( x( {} ).historyReadable ).toBe( false );
	} );
	it( 'needs the HistoryRead bit when the server gave a level, and takes no level as unknown', ()=>{
		expect( x( {historizing: true, userAccessLevel: EAccess.Read|EAccess.HistoryRead} ).historyReadable ).toBe( true );
		expect( x( {historizing: true, userAccessLevel: EAccess.Read} ).historyReadable ).toBe( false );
		expect( x( {historizing: true, userAccessLevel: 0} ).historyReadable ).toBe( false );
		expect( x( {historizing: true, userAccessLevel: null} ).historyReadable ).toBe( true );
	} );
} );

describe( 'Variable.historyWritable', ()=>{
	it( 'needs historizing and the HistoryWrite bit, which OpcServer withholds on an unprotected node, and takes no level as unknown', ()=>{
		expect( x( {historizing: true, userAccessLevel: EAccess.Read|EAccess.HistoryRead|EAccess.HistoryWrite} ).historyWritable ).toBe( true );
		expect( x( {historizing: true, userAccessLevel: EAccess.Read|EAccess.HistoryRead} ).historyWritable ).toBe( false );
		expect( x( {historizing: true, userAccessLevel: EAccess.HistoryWrite} ).historyWritable ).toBe( true );
		expect( x( {historizing: false, userAccessLevel: EAccess.All} ).historyWritable ).toBe( false );
		expect( x( {historizing: true} ).historyWritable ).toBe( true );
	} );
} );