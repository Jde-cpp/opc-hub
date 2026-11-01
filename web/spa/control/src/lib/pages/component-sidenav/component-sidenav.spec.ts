import { TestBed } from '@angular/core/testing';
import { signal } from '@angular/core';
import { By } from '@angular/platform-browser';
import { provideRouter, Router } from '@angular/router';
import { MatSidenav } from '@angular/material/sidenav';
import { ComponentSidenav } from './component-sidenav';
import { ScreenSize } from '../../services/screen-size';
import { Blank, pageTitleStub, silence } from 'jde-spa/testing';

//The nav is a drawer over the page below the small breakpoint (ScreenSize) and a column beside it above, and the drawer closes on
//a navigation (#330):  the close used to test the Observable itself, always true, which only worked because the drawer exists
//only when small.
describe( 'ComponentSidenav', ()=>{
	afterEach( ()=>vi.restoreAllMocks() );

	const create = ( small:boolean )=>{
		silence( 'warn' );//the outlet's page, Blank, has no sideNav input, which the shell remarks on
		TestBed.configureTestingModule( {providers: [
			provideRouter( [{path: 'x', component: Blank}] ),
			pageTitleStub,
			{provide: ScreenSize, useValue: {isSmall: signal( small )}}
		]} );
		const fixture = TestBed.createComponent( ComponentSidenav );
		fixture.detectChanges();
		return { fixture, shell: fixture.componentInstance, router: TestBed.inject( Router ) };
	};
	const drawer = ( fixture:{debugElement:{query( p:unknown ):unknown}} )=>fixture.debugElement.query( By.directive(MatSidenav) );
	const inlineNav = ( fixture:{nativeElement:HTMLElement} )=>fixture.nativeElement.querySelector( 'main app-component-nav' );

	it( 'puts the nav in a drawer on a small screen, and closes it on a navigation', async ()=>{
		const { fixture, shell, router } = create( true );
		expect( drawer(fixture) ).not.toBeNull();
		expect( inlineNav(fixture) ).toBeNull();
		const close = vi.spyOn( shell.sidenav()!, 'close' );
		await router.navigateByUrl( '/x' );
		expect( close ).toHaveBeenCalledOnce();
	} );

	//the current page's item navigates nowhere, so a navigation never closes the drawer for it;  a tap inside the nav does
	it( 'closes the drawer on a tap inside its nav', ()=>{
		const { fixture, shell } = create( true );
		const close = vi.spyOn( shell.sidenav()!, 'close' );
		fixture.nativeElement.querySelector( 'mat-sidenav app-component-nav' ).click();
		expect( close ).toHaveBeenCalledOnce();
	} );

	it( 'puts the nav beside the page on a wide one', async ()=>{
		const { fixture, shell, router } = create( false );
		expect( drawer(fixture) ).toBeNull();
		expect( inlineNav(fixture) ).not.toBeNull();
		expect( shell.sidenav() ).toBeUndefined();
		await router.navigateByUrl( '/x' );//nothing to close, and nothing thrown
	} );
} );