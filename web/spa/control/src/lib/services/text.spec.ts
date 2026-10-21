import { capitalize, plural, singular, uncapitalize } from './text';

//reviews/angular-refactor.md A7:  capitalize was written out by hand eight times, and two `plural`s answered different questions.
describe( 'text', ()=>{
	it( 'capitalizes and uncapitalizes the first letter alone', ()=>{
		expect( capitalize('serverConnections') ).toBe( "ServerConnections" );
		expect( uncapitalize('OpcServer') ).toBe( "opcServer" );
		expect( capitalize('') ).toBe( "" );//toJson's value[0] threw on an empty string
		expect( uncapitalize('') ).toBe( "" );
	} );
	it( 'pluralizes a name, or a noun by its count', ()=>{
		expect( plural('OpcServer') ).toBe( "OpcServers" );
		expect( plural('users') ).toBe( "users" );
		expect( [0, 1, 2].map( n=>plural('role', n) ) ).toEqual( ["roles", "role", "roles"] );
		expect( singular('users') ).toBe( "user" );
	} );
} );