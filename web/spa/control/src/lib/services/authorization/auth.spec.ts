import { User } from './auth';
import { fakeJwt } from 'jde-spa/testing';

const jwt = fakeJwt( {email:"a@b.c", name:"A B", exp:2000000000, iss:"https://accounts.google.com"} );

describe( "User.decodeJwt", ()=>{
	it( "decodes a well-formed token", ()=>{
		expect( User.decodeJwt(jwt).email ).toBe( "a@b.c" );
	} );
	//`split('.')[1]` was undefined and `.replace` threw a TypeError from inside the AuthStore constructor, which is a
	//providedIn:'root' service at the head of the DI chain - the app bootstrapped to a blank page on every load.
	it( "names the problem when there is no '.'-separated payload", ()=>{
		expect( ()=>User.decodeJwt("legacy-session-id") ).toThrowError( /is not a jwt/ );
	} );
	it( "names the problem for a token with only a header", ()=>{
		expect( ()=>User.decodeJwt("header.") ).toThrowError( /is not a jwt/ );
	} );
	it( "reports through the User constructor too", ()=>{
		expect( ()=>new User("legacy-session-id") ).toThrowError( /is not a jwt/ );
	} );
} );