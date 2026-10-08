if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { Variable } from './node';
import { EAccess } from './types';

const variable = ( extra:any )=>new Variable( <any>{ns:2, i:1, name: 'x', browse: {ns:2, name: 'x'}, ...extra} );

describe( 'Variable.historyReadable', ()=>{
	it( 'needs historizing, as a boolean or a {v,sc} reading', ()=>{
		expect( variable( {historizing: true} ).historyReadable ).toBe( true );
		expect( variable( {historizing: {v: true, sc: 0x40000000}} ).historyReadable ).toBe( true );
		expect( variable( {historizing: false} ).historyReadable ).toBe( false );
		expect( variable( {historizing: null} ).historyReadable ).toBe( false );//the server did not say
		expect( variable( {historizing: {sc: 0x80000000}} ).historyReadable ).toBe( false );
		expect( variable( {} ).historyReadable ).toBe( false );
	} );
	it( 'needs the HistoryRead bit when the server gave a level, and takes no level as unknown', ()=>{
		expect( variable( {historizing: true, userAccessLevel: EAccess.Read|EAccess.HistoryRead} ).historyReadable ).toBe( true );
		expect( variable( {historizing: true, userAccessLevel: EAccess.Read} ).historyReadable ).toBe( false );
		expect( variable( {historizing: true, userAccessLevel: 0} ).historyReadable ).toBe( false );
		expect( variable( {historizing: true, userAccessLevel: null} ).historyReadable ).toBe( true );
	} );
} );