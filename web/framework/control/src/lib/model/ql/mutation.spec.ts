import { Mutation, MutationType } from './mutation';

describe( 'Mutation.toString', ()=>{
	describe( 'each MutationType', ()=>{
		it( 'creates', ()=>{
			expect( new Mutation('user', 0, {name: "Ann", target: "ann"}, MutationType.Create).toString() ).toBe( `createUser( name:"Ann", target:"ann" )` );
		} );
		it( 'updates', ()=>{
			expect( new Mutation('user', 3, {name: "Ann"}, MutationType.Update).toString() ).toBe( `updateUser( id:3, name:"Ann" )` );
		} );
		it( 'deletes', ()=>{
			expect( new Mutation('user', 3, null, MutationType.Delete).toString() ).toBe( `deleteUser( id:3 )` );
			expect( new Mutation('user', 3, {}, MutationType.Delete).toString() ).toBe( `deleteUser( id:3 )` );
		} );
		it( 'purges', ()=>{
			expect( new Mutation('user', 3, null, MutationType.Purge).toString() ).toBe( `purgeUser( id:3 )` );
			expect( new Mutation('acl', undefined, {identity: {id: 1}, role: {id: 2}}, MutationType.Purge).toString() ).toBe( `purgeAcl( identity:{id:1}, role:{id:2} )` );//Role.mutations' shape
		} );
		it( 'adds', ()=>{
			expect( new Mutation('role', 5, {permissionRight: {allowed: 1, denied: 0, resource: {id: 9}}}, MutationType.Add).toString() )
				.toBe( `addRole( id:5, permissionRight:{allowed:1, denied:0, resource:{id:9}} )` );
		} );
		it( 'removes', ()=>{
			expect( new Mutation('role', 5, {permissionRight: {id: 7}}, MutationType.Remove).toString() ).toBe( `removeRole( id:5, permissionRight:{id:7} )` );
			expect( new Mutation('role', 5, null, MutationType.Remove).toString() ).toBe( `removeRole( id:5 )` );
		} );
		it( 'infers update or create from the id when no type is given', ()=>{
			expect( new Mutation('user', 3, {name: "Ann"}, undefined as unknown as MutationType).toString() ).toBe( `updateUser( id:3, name:"Ann" )` );
			expect( new Mutation('user', 0, {name: "Ann"}, undefined as unknown as MutationType).toString() ).toBe( `createUser( name:"Ann" )` );
		} );
		it( 'appends the result selection', ()=>{
			expect( new Mutation('user', 0, {name: "Ann"}, MutationType.Create, ["id", "name"]).toString() ).toBe( `createUser( name:"Ann" ){id,name}` );
			expect( new Mutation('user', 3, null, MutationType.Delete, ["id", "deleted"]).toString() ).toBe( `deleteUser( id:3 ){id,deleted}` );
			expect( new Mutation('role', 5, null, MutationType.Remove, ["id"]).toString() ).toBe( `removeRole( id:5 ){id}` );
		} );
		it( 'is null with nothing to send', ()=>{
			expect( new Mutation('user', 3, null, MutationType.Update).toString() ).toBeNull();
			expect( new Mutation('user', 0, null, MutationType.Delete).toString() ).toBeNull();
		} );
	} );

	describe( 'children', ()=>{
		const permission = ( allowed:number )=>new Mutation( 'permissionRight', 0, {allowed, resource: {id: 9}}, MutationType.Create );

		it( 'nests a create\'s children in its args, one list per child type', ()=>{
			const role = new Mutation( 'role', 0, {name: "ops"}, MutationType.Create );
			role.add( permission(1) );
			role.add( permission(3) );
			role.add( new Mutation('identity', 0, {id: 4}, MutationType.Create) );
			const expected = `createRole( name:"ops", permissionRight:[{allowed:1, resource:{id:9}},{allowed:3, resource:{id:9}}], identity:[{id:4}] )`;
			expect( role.toString() ).toBe( expected );
			expect( role.args ).toEqual( {name: "ops"} );//nested into a copy, so a second call does not nest them twice
			expect( role.toString() ).toBe( expected );
		} );

		it( 'appends add and remove children after the parent\'s own mutation', ()=>{
			const role = new Mutation( 'role', 5, {name: "ops"}, MutationType.Update );
			role.add( new Mutation('role', 5, {permissionRight: {id: 7}}, MutationType.Remove) );
			role.add( new Mutation('role', 5, {permissionRight: {allowed: 1, denied: 0, resource: {id: 9}}}, MutationType.Add) );
			expect( role.toString() ).toBe( `updateRole( id:5, name:"ops" )removeRole( id:5, permissionRight:{id:7} )addRole( id:5, permissionRight:{allowed:1, denied:0, resource:{id:9}} )` );
		} );

		it( 'is only its children when it has no args of its own', ()=>{
			const role = new Mutation( 'role', 5, null, MutationType.Update );
			role.add( new Mutation('role', 5, {identity: {id: [1, 2]}}, MutationType.Add) );//SlugRow.childMutations' shape
			role.add( new Mutation('role', 5, {identity: {id: [3]}}, MutationType.Remove) );
			expect( role.toString() ).toBe( `addRole( id:5, identity:{id:[1,2]} )removeRole( id:5, identity:{id:[3]} )` );
		} );

		it( 'skips a child with nothing to send', ()=>{
			const role = new Mutation( 'role', 5, {name: "ops"}, MutationType.Update );
			role.add( new Mutation('role', 5, null, MutationType.Update) );
			expect( role.toString() ).toBe( `updateRole( id:5, name:"ops" )` );
		} );

		it( 'is null when no child has anything to send', ()=>{
			const role = new Mutation( 'role', 5, null, MutationType.Update );
			role.add( new Mutation('role', 5, null, MutationType.Update) );
			role.add( new Mutation('role', 0, null, MutationType.Delete) );
			expect( role.toString() ).toBeNull();
		} );

		it( 'keeps an id-only remove ahead of its children', ()=>{
			const role = new Mutation( 'role', 5, null, MutationType.Remove );
			role.add( new Mutation('role', 6, null, MutationType.Remove) );
			expect( role.toString() ).toBe( `removeRole( id:5 )removeRole( id:6 )` );
		} );
	} );

	describe( 'argument quoting', ()=>{
		const args = ( value:unknown )=>new Mutation( 'user', 0, {value}, MutationType.Create ).toString();

		it( 'escapes a string as a GraphQL string literal', ()=>{
			expect( args("plain") ).toBe( `createUser( value:"plain" )` );
			expect( args(`say "hi"\\ok\n`) ).toBe( `createUser( value:"say \\"hi\\"\\\\ok\\n" )` );
			expect( args("") ).toBe( `createUser( value:"" )` );
		} );

		it( 'writes null, numbers and booleans bare', ()=>{
			expect( args(null) ).toBe( `createUser( value:null )` );
			expect( args(0) ).toBe( `createUser( value:0 )` );
			expect( args(-2.5) ).toBe( `createUser( value:-2.5 )` );
			expect( args(true) ).toBe( `createUser( value:true )` );
			expect( args(false) ).toBe( `createUser( value:false )` );
		} );

		it( 'quotes each array item by its own type', ()=>{
			expect( args([]) ).toBe( `createUser( value:[] )` );
			expect( args([1, 2]) ).toBe( `createUser( value:[1,2] )` );
			expect( args(["a", `b"c`]) ).toBe( `createUser( value:["a","b\\"c"] )` );
			expect( args([null, undefined, true]) ).toBe( `createUser( value:[null,null,true] )` );
			expect( args([{id: 1, name: "x"}, {id: 2}]) ).toBe( `createUser( value:[{id:1, name:"x"},{id:2}] )` );
			expect( args([[1, "a"], []]) ).toBe( `createUser( value:[[1,"a"],[]] )` );
		} );

		it( 'nests objects', ()=>{
			expect( args({a: {b: "c", d: null}, e: [1]}) ).toBe( `createUser( value:{a:{b:"c", d:null}, e:[1]} )` );
		} );

		it( 'writes undefined as null, as an argument, a nested field or an array item', ()=>{
			expect( args(undefined) ).toBe( `createUser( value:null )` );
			expect( args({a: {b: undefined}}) ).toBe( `createUser( value:{a:{b:null}} )` );
			expect( args([undefined]) ).toBe( `createUser( value:[null] )` );
			expect( new Mutation('user', 3, {name: "Ann", description: undefined}, MutationType.Update).toString() ).toBe( `updateUser( id:3, name:"Ann", description:null )` );//SlugRow.mutationArgs' shape
		} );

		it( 'refuses a value with no GraphQL spelling', ()=>{
			expect( ()=>args(()=>0) ).toThrow( "Assertion failed" );
			expect( ()=>args(1n) ).toThrow( "Assertion failed" );
		} );
	} );
} );