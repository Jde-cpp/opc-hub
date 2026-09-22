import { MutationType } from 'jde-framework';
import { User } from './user';

//reviews/m3-closing.md #15:  the user form edits Provider, Email and Login name, but only slug, name and description ever went
//on the wire.  A user added ahead of their first sign-in was stored with no provider and no login name, so the sign-in missed
//it and made a second identity - the roles granted to the first on a row nobody could sign in as.  An Email edit saved nothing.
describe( 'User.mutation', ()=>{
	const created = ()=>new User( {slug: "jane", name: "Jane", provider: 1, loginName: "jane@plant.com", email: "jane@plant.com", roles: [], groups: [], permissions: []} );
	it( 'creates the identity a sign-in binds to:  provider id, login name and email', ()=>{
		const [m] = created().mutation( new User({}) );
		expect( m.type ).toBe( MutationType.Create );
		const text = m.toString();
		expect( text ).toContain( 'providerId:1' );//providerId, not provider:  the server looks the arg up by the column's member name
		expect( text ).toContain( 'loginName:"jane@plant.com"' );
		expect( text ).toContain( 'email:"jane@plant.com"' );
	} );

	it( 'saves an email edit on an existing user', ()=>{
		const original = new User( {id: 5, slug: "jane", name: "Jane", provider: "Google", loginName: "jane@plant.com", email: "jane@plant.com", roles: [], groups: [], permissions: []} );
		const edited = new User( {...original, email: "jane.doe@plant.com"} );
		expect( edited.equals(original) ).toBe( false );//or Save never enables - key-properties compares through equals
		const [m] = edited.mutation( original );
		expect( m.type ).toBe( MutationType.Update );
		expect( m.toString() ).toContain( 'email:"jane.doe@plant.com"' );
		expect( m.toString() ).not.toContain( 'loginName' );//provider and login name are fixed once the user exists
	} );

	it( 'clears an email', ()=>{
		const original = new User( {id: 5, slug: "jane", name: "Jane", email: "jane@plant.com", roles: [], groups: [], permissions: []} );
		const [m] = new User( {...original, email: ""} ).mutation( original );
		expect( m.toString() ).toContain( 'email:null' );
	} );

	it( 'sends nothing for an untouched existing user', ()=>{
		const original = new User( {id: 5, slug: "jane", name: "Jane", email: "jane@plant.com", roles: [], groups: [], permissions: []} );
		expect( new User({...original}).mutation(original) ).toEqual( [] );
	} );
} );
