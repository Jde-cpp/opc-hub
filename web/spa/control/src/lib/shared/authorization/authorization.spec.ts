import { TestBed } from '@angular/core/testing';
import { provideRouter, Router } from '@angular/router';
import { signal } from '@angular/core';
import { EProvider, IAUTH, IAuth, User } from '../../services/authorization/auth';
import { IENVIRONMENT } from '../../services/environment/environment';
import { Authorization } from './authorization';
import { routerStub, stable } from 'jde-spa/testing';

//review3 L8: `gapi` is a `declare const` over Google's platform.js, which a Google sign-in loads.  A bare `gapi.auth2` is a
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

//#483:  a session the install's guest user stands behind is named Guest, with the sign-in link beside it, whatever sign-in the site
//offers - a password's has no client id (483-demo-guest-sessions review #11);  an anonymous session, no guest, shows the link as before.
//#484:  config.json's `login` false is a site that offers none, and neither has the link.
describe( 'Authorization guest', ()=>{
	const create = async ( guest:boolean, environment?:Record<string,unknown> )=>{
		TestBed.configureTestingModule({ providers: [
			provideRouter( [] ),//the sign-in link is a routerLink
			{ provide: IAUTH, useValue: { user: signal<User|undefined>( undefined ), guest: signal( guest ), logout: vi.fn(), providers: ()=>Promise.resolve([]) } as unknown as IAuth },
			...( environment ? [{ provide: IENVIRONMENT, useValue: {get: ( key:string )=>environment[key]} }] : [] )
		]});
		const fixture = TestBed.createComponent( Authorization );
		await stable( fixture, 3 );
		const element = fixture.nativeElement as HTMLElement;
		return { name: element.querySelector('.guest')?.textContent?.trim(), signIn: !!element.querySelector('a[aria-label="Sign in"]') };
	};

	it( 'names a guest Guest, with the sign-in link', async ()=>{
		const { name, signIn } = await create( true );
		expect( name ).toBe( 'Guest' );
		expect( signIn ).toBe( true );
	} );

	it( 'shows an anonymous session the sign-in link', async ()=>{
		const { name, signIn } = await create( false );
		expect( name ).toBeUndefined();
		expect( signIn ).toBe( true );
	} );

	it( 'keeps the link where the environment says nothing of login, or true', async ()=>{
		expect( (await create( true, {} )).signIn ).toBe( true );
		TestBed.resetTestingModule();//a second build in the one test
		expect( (await create( false, {login: true} )).signIn ).toBe( true );
	} );

	it( 'names a guest with no sign-in link where login is false', async ()=>{
		const { name, signIn } = await create( true, {login: false} );
		expect( name ).toBe( 'Guest' );
		expect( signIn ).toBe( false );
	} );

	it( 'shows an anonymous session nothing where login is false', async ()=>{
		const { name, signIn } = await create( false, {login: false} );
		expect( name ).toBeUndefined();
		expect( signIn ).toBe( false );
	} );
} );