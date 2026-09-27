import { MutationType } from 'jde-framework';
import { User } from './user';

//reviews/m3-closing.md #15:  the user form edits Email, but only slug, name and description ever went on the wire - an Email
//edit saved nothing.  (#15's create half is gone:  a user is only ever created by signing in, #32.)
describe( 'User.mutation', ()=>{
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
