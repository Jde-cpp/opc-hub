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

//AuthStore loads the stored user through this constructor on every page refresh.  Its jwt branch kept only the jwt, so the
//session went with the rest and the next request signed in again with the jwt - a new session per refresh.
describe( "User from a stored user", ()=>{
	it( "keeps a jwt user's session", ()=>{
		const user = new User( JSON.parse(JSON.stringify(new User({jwt, sessionId:"92d9ab0b", serverInstances:[{url:"http://localhost:1967", instance:1}]}))) );
		expect( user.sessionId ).toBe( "92d9ab0b" );
		expect( user.authorization ).toBe( "92d9ab0b" );
		expect( user.serverInstances ).toEqual( [{url:"http://localhost:1967", instance:1}] );
		expect( user.email ).toBe( "a@b.c" );
		expect( user.exp ).toEqual( new Date(2000000000*1000) );
	} );
	it( "signs in with the jwt when there is no session", ()=>{
		expect( new User(jwt).authorization ).toBe( `Bearer ${jwt}` );
	} );
} );