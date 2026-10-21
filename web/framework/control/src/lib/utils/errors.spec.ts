import { HttpErrorResponse } from '@angular/common/http';
import { errorMessage, errorText, failureState, httpStatus, isRefusal } from './errors';

//The server's AccessException reaches the browser as a 403 with a PLAIN-TEXT body ("[bob]User does not have 'Read' access
//to 'users'.").  HttpClient asked for json, fails to parse it, and hands the caller {error: SyntaxError, text: body} - which
//errorText printed as "(403)[object Object]".  The body is the text.
describe( 'errorText', ()=>{
	it( 'reads the text out of HttpClient\'s non-json error wrapper', ()=>{
		const e = new HttpErrorResponse( {status: 403, error: {error: new SyntaxError("Unexpected token"), text: "[bob]User does not have 'Read' access to 'users'."}} );
		expect( errorText(e) ).toBe( "(403)[bob]User does not have 'Read' access to 'users'." );
		expect( httpStatus(e) ).toBe( 403 );
	} );
	it( 'still prefers a json body\'s message', ()=>{
		expect( errorText(new HttpErrorResponse({status: 500, error: {message: "boom"}})) ).toBe( "boom" );
	} );
	it( 'prints a string body with its status', ()=>{
		expect( errorText(new HttpErrorResponse({status: 404, error: "gone"})) ).toBe( "(404)gone" );
	} );
} );

//reviews/angular-refactor.md A6:  the refusal test was spelled four ways, the failure state three.
describe( 'isRefusal', ()=>{
	it( 'is a 401 or a 403, and nothing else', ()=>{
		expect( isRefusal(new HttpErrorResponse({status: 401})) ).toBe( true );
		expect( isRefusal({status: 403}) ).toBe( true );
		expect( isRefusal(new HttpErrorResponse({status: 500})) ).toBe( false );
		expect( isRefusal(new Error("down")) ).toBe( false );
	} );
} );

describe( 'failureState', ()=>{
	const refused = new HttpErrorResponse( {status: 403, error: {message: "No 'Read' on users."}} );
	it( 'is nothing without an error', ()=>{
		expect( failureState(undefined, "users") ).toBeUndefined();
	} );
	it( 'says what to ask for on a 403', ()=>{
		expect( failureState(refused, "users") ).toEqual( {kind: "forbidden", title: "No access to users.", detail: "No 'Read' on users.  Ask an administrator for a role that can read users."} );
		expect( failureState(refused) ).toEqual( {kind: "forbidden", title: "No access.", detail: "No 'Read' on users.  Ask an administrator for a role that grants it."} );
		expect( failureState(refused, "grants", "It takes Read on acl.")?.detail ).toBe( "No 'Read' on users.  It takes Read on acl." );
	} );
	it( 'drops the status errorText prefixes, and calls anything else a failure', ()=>{
		expect( failureState(new HttpErrorResponse({status: 500, error: "boom"}), "users") ).toEqual( {kind: "failed", title: "Could not load users.", detail: "boom"} );
		expect( failureState(new HttpErrorResponse({status: 401, error: "who?"})) ).toEqual( {kind: "failed", title: "Could not load.", detail: "who?"} );//a 401 signs the user out:  no role to ask for
	} );
} );

//review3 L13: the inline error banners built their text with `${e}`, which renders "[object Object]" for the two shapes
//that actually reach production - an HttpErrorResponse and a {error:IError} ProtoService rejection.  errorText was the
//snackbar's private static; it is module scope now so a banner and a snackbar cannot disagree about the same throw.
describe( 'errorMessage', ()=>{
	const rejection = { error: {requestId: 1, message: "no such instance", sc: 0, httpStatus: 404} };
	const http = new HttpErrorResponse( {status: 500, error: {message: "server exploded"}} );

	it( 'never renders [object Object]', ()=>{
		for( const e of [rejection, http, new Error("boom"), "plain string", {}, undefined] )
			expect( errorMessage(e, "Could not load.") ).not.toContain( "[object Object]" );
	} );

	it( 'quotes a ProtoService rejection with its http status', ()=>{
		expect( errorMessage(rejection, "Could not load.") ).toBe( "Could not load.  (404)no such instance" );
	} );

	it( 'quotes an HttpErrorResponse', ()=>{
		expect( errorMessage(http, "Could not load.") ).toBe( "Could not load.  server exploded" );
	} );

	it( "falls back to the caller's context when the throw says nothing quotable", ()=>{
		expect( errorMessage({}, "Could not load.") ).toBe( "Could not load." );
		expect( errorText({}) ).toBeUndefined();
	} );

	it( 'says something even with no context at all', ()=>{
		expect( errorMessage(undefined) ).toBe( "Unknown error" );
	} );
} );

//review3 C4: handle401 used to read `e["status"]` off an `any`, which compiled whatever it was spelled.  This narrows once.
describe( 'httpStatus', ()=>{
	it( 'reads an HttpErrorResponse', ()=>{
		expect( httpStatus(new HttpErrorResponse({status: 401})) ).toBe( 401 );
	} );
	it( 'reads a plain rejection carrying a numeric status', ()=>{
		expect( httpStatus({status: 0, error: 'net'}) ).toBe( 0 );//0 is a real status here - the opaque network failure - so it must not read as absent
	} );
	it( 'is undefined for anything else', ()=>{
		for( const e of [new Error("boom"), "plain", {status: "401"}, undefined, null] )
			expect( httpStatus(e) ).toBeUndefined();
	} );
} );