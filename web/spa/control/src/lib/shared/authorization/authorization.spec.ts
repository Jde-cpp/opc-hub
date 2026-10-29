import { TestBed } from '@angular/core/testing';
import { Router } from '@angular/router';
import { signal } from '@angular/core';
import { EProvider, IAUTH, IAuth, User } from '../../services/authorization/auth';
import { Authorization } from './authorization';
import { routerStub, stable } from 'jde-spa/testing';

//review3 L8: `gapi` is a `declare const` over index.html's `async defer` platform.js.  A bare `gapi.auth2` is a
//ReferenceError wherever that script has not loaded, and it sat between logout() and the navigate to /login - so the user
//was logged out and left standing on the page they had just lost access to.
describe( 'Authorization.onLogout', ()=>{
	const create = ( provider:EProvider )=>{
		const router = routerStub();
		const logout = vi.fn().mockResolvedValue( undefined );
		TestBed.configureTestingModule({ providers: [
			{ provide: Router, useValue: router },
			{ provide: IAUTH, useValue: {
				user: signal<User|undefined>( {provider} as User ),
				logout,
				providers: ()=>Promise.resolve([])
			} as unknown as IAuth }
		]});
		return { authorization: TestBed.createComponent( Authorization ).componentInstance, navigate: router.navigate, logout };
	};

	afterEach( ()=>{ delete (globalThis as any).gapi; } );

	it( 'still reaches /login when gapi never loaded', async ()=>{
		expect( 'gapi' in globalThis ).toBe( false );//the whole point: the identifier is not merely undefined, it is absent
		const { authorization, navigate, logout } = create( EProvider.Google );
		await authorization.onLogout();
		expect( logout ).toHaveBeenCalled();
		expect( navigate ).toHaveBeenCalledWith( ['/login'] );
	} );

	it( 'still reaches /login when signOut rejects', async ()=>{
		(globalThis as any).gapi = { auth2: {getAuthInstance: ()=>({signOut: ()=>Promise.reject(new Error("popup closed"))})} };
		const { authorization, navigate } = create( EProvider.Google );
		await authorization.onLogout();
		expect( navigate ).toHaveBeenCalledWith( ['/login'] );
	} );

	it( 'signs out of Google when the library is there', async ()=>{
		const signOut = vi.fn().mockResolvedValue( undefined );
		(globalThis as any).gapi = { auth2: {getAuthInstance: ()=>({signOut})} };
		const { authorization, navigate } = create( EProvider.Google );
		await authorization.onLogout();
		expect( signOut ).toHaveBeenCalled();
		expect( navigate ).toHaveBeenCalledWith( ['/login'] );
	} );

	it( 'does not touch Google for a non-Google login', async ()=>{
		const signOut = vi.fn();
		(globalThis as any).gapi = { auth2: {getAuthInstance: ()=>({signOut})} };
		const { authorization, navigate } = create( EProvider.OpcServer );
		await authorization.onLogout();
		expect( signOut ).not.toHaveBeenCalled();
		expect( navigate ).toHaveBeenCalledWith( ['/login'] );
	} );
} );

//Google's avatar host rate-limits (429) a page reloaded often;  the button keeps its label and shows the icon instead of a blank.
describe( 'Authorization picture', ()=>{
	const create = async ( picture:string )=>{
		const user = signal<User|undefined>( {provider:EProvider.Google, name:'Test User', picture} as User );
		TestBed.configureTestingModule({ providers: [
			{ provide: Router, useValue: routerStub() },
			{ provide: IAUTH, useValue: { user, logout: vi.fn(), providers: ()=>Promise.resolve([]) } as unknown as IAuth }
		]});
		const fixture = TestBed.createComponent( Authorization );
		await stable( fixture );
		return { fixture, user, element: fixture.nativeElement as HTMLElement };
	};

	it( 'sends no referrer', async ()=>{
		const { element } = await create( 'https://lh3.example/a' );
		expect( element.querySelector('img')?.getAttribute('referrerpolicy') ).toBe( 'no-referrer' );
	} );

	it( 'falls back to the icon when the picture fails, until it changes', async ()=>{
		const { fixture, user, element } = await create( 'https://lh3.example/a' );
		element.querySelector( 'img' )!.dispatchEvent( new Event('error') );
		await stable( fixture );
		expect( element.querySelector('img') ).toBeNull();
		expect( element.querySelector('button.account mat-icon')?.textContent ).toBe( 'account_circle' );

		user.set( {provider:EProvider.Google, name:'Test User', picture:'https://lh3.example/b'} as User );
		await stable( fixture );
		expect( element.querySelector('img')?.getAttribute('src') ).toBe( 'https://lh3.example/b' );
	} );
} );