import { Latest } from './latest';

describe( 'Latest', ()=>{
	it( 'holds only for the last ticket taken', ()=>{
		const latest = new Latest();
		const first = latest.next(), second = latest.next();
		expect( latest.is(first) ).toBe( false );
		expect( latest.is(second) ).toBe( true );
		expect( latest.current ).toBe( second );
	} );
	it( 'drops every ticket out when next() is taken with nothing to follow', ()=>{
		const latest = new Latest();
		const out = latest.next();
		latest.next();
		expect( latest.is(out) ).toBe( false );
	} );
} );