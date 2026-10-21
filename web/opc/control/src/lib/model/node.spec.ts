import { EAccess } from './types';
import { variable } from 'jde-opc/testing';

const x = ( json:object={} )=>variable( 1, 'x', json );

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