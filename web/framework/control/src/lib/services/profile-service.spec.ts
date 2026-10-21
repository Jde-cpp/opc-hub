//ProfileService: the 'IProfileService' implementation ProfileStore persists through - exact QL
//strings against a mocked AppService, and the logged-in predicate behind userKey.

import { TestBed } from '@angular/core/testing';
import { signal } from '@angular/core';
import { AppService } from './app/app-service';
import { ProfileService } from './profile-service';
import { User } from 'jde-spa';

describe( 'ProfileService', ()=>{
	const create = ()=>{
		const app = { user: signal<User|undefined>( undefined ), querySingle: vi.fn(), mutate: vi.fn() };
		TestBed.configureTestingModule( { providers: [{provide: AppService, useValue: app}, ProfileService] } );
		return { app, service: TestBed.inject( ProfileService ) };
	};

	it( 'userKey is undefined until a jwt or id marks the user logged in', ()=>{
		const { app, service } = create();
		expect( service.userKey() ).toBeUndefined();
		app.user.set( {sessionId:'anon'} as User );//anonymous REST session: not logged in
		expect( service.userKey() ).toBeUndefined();
		app.user.set( {id:'bob'} as User );
		expect( service.userKey() ).toBe( 'bob' );
		app.user.set( {jwt:'j', email:'g@x.com'} as User );//Google login without an id
		expect( service.userKey() ).toBe( 'g@x.com' );
	});

	it( 'load queries profile by url and unwraps value', async ()=>{
		const { app, service } = create();
		app.querySingle.mockResolvedValue( {value:'{"a":1}'} );
		await expect( service.load('qlList/users/views') ).resolves.toBe( '{"a":1}' );
		expect( app.querySingle ).toHaveBeenCalledWith( 'profile( url:"qlList/users/views" ){ value }' );
	});

	it( 'load returns null when the server has no row', async ()=>{
		const { app, service } = create();
		app.querySingle.mockResolvedValue( null );
		await expect( service.load('k') ).resolves.toBeNull();
	});

	it( 'save mutates updateProfile with the escaped value', async ()=>{
		const { app, service } = create();
		app.mutate.mockResolvedValue( {} );
		await service.save( 'favorites', '[{"name":"a b"}]' );
		expect( app.mutate ).toHaveBeenCalledTimes( 1 );
		expect( app.mutate.mock.calls[0][0].toString() ).toBe( 'updateProfile( url:"favorites", value:"[{\\"name\\":\\"a b\\"}]" )' );
	});

	it( 'save with null deletes the row', async ()=>{
		const { app, service } = create();
		app.mutate.mockResolvedValue( {} );
		await service.save( 'k', null );
		expect( app.mutate.mock.calls[0][0].toString() ).toBe( 'updateProfile( url:"k", value:null )' );
	});
});