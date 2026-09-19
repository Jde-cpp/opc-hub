if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import Long from 'long';
import { toReading } from './value';

//the gateway's three shapes (Value::ToJson):  the bare value for Good, {v,sc} for any other code that is not Bad, {sc} for Bad.
describe( 'toReading', ()=>{
	it( 'reads a bare value as Good', ()=>{
		expect( toReading(42.5) ).toEqual( {value: 42.5, sc: 0} );
		expect( toReading(false) ).toEqual( {value: false, sc: 0} );
		expect( toReading("abc") ).toEqual( {value: "abc", sc: 0} );
	} );

	it( 'keeps the quality beside the value', ()=>{
		expect( toReading({v: 1500, sc: 0x40940600}) ).toEqual( {value: 1500, sc: 0x40940600} );
		expect( toReading({v: 0, sc: 0x00960000}) ).toEqual( {value: 0, sc: 0x00960000} );//a falsy value is still a value
	} );

	it( 'has no value for a Bad reading - not an OpcError standing in for one', ()=>{
		const reading = toReading( {sc: 0x808C0000} );
		expect( reading ).toEqual( {sc: 0x808C0000} );
		expect( "value" in reading ).toBe( false );
	} );

	it( 'unwraps a Long and an array inside the wrapper', ()=>{
		const reading = toReading( {v: {low: 7, high: 0, unsigned: true}, sc: 0x40940000} );
		expect( reading.value ).toBeInstanceOf( Long );
		expect( (reading.value as Long).toNumber() ).toBe( 7 );
		expect( toReading({v: [1, 2], sc: 0x40940000}) ).toEqual( {value: [1, 2], sc: 0x40940000} );
		expect( toReading([1, 2]) ).toEqual( {value: [1, 2], sc: 0} );
	} );

	it( 'takes a null - the server\'s empty value - as it comes', ()=>{
		expect( toReading(null) ).toEqual( {value: null, sc: 0} );
	} );
} );