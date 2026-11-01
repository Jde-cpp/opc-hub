import { ComponentFixture, TestBed } from '@angular/core/testing';
import { By } from '@angular/platform-browser';
import { provideRouter, Router } from '@angular/router';
import { MatSidenav } from '@angular/material/sidenav';
import { ComponentSidenav } from './component-sidenav';
import { RouteItem } from './route-item';
import { Blank, flush, pageTitleStub, screenSize, silence } from 'jde-spa/testing';

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
			screenSize( small ? 'small' : 'wide' )
		]} );
		const fixture = TestBed.createComponent( ComponentSidenav );
		fixture.detectChanges();
		return { fixture, shell: fixture.componentInstance, router: TestBed.inject( Router ) };
	};
	const drawer = ( fixture:ComponentFixture<ComponentSidenav> )=>fixture.debugElement.query( By.directive(MatSidenav) );
	const inlineNav = ( fixture:ComponentFixture<ComponentSidenav> )=>fixture.nativeElement.querySelector( 'main app-component-nav' );

	it( 'puts the nav in a drawer on a small screen, and closes it on a navigation', async ()=>{
		const { fixture, shell, router } = create( true );
		expect( drawer(fixture) ).not.toBeNull();
		expect( inlineNav(fixture) ).toBeNull();
		const close = vi.spyOn( shell.sidenav()!, 'close' );
		await router.navigateByUrl( '/x' );
		expect( close ).toHaveBeenCalledOnce();
	} );

	//the current page's item navigates nowhere, so a navigation never closes the drawer for it;  a tap on an item does.  A tap
	//between the items, or a Ctrl-click that opens one in a new tab, leaves the drawer open on the page it was on.
	it( 'closes the drawer on a plain click of a nav item, and on nothing else inside it', async ()=>{
		const { fixture, shell } = create( true );
		shell.item.set( new RouteItem({path: 'x', title: 'X', parent: new RouteItem({path: '', title: 'Section'}), siblings: [new RouteItem({path: 'x', title: 'X'})]}) );
		fixture.detectChanges();
		fixture.nativeElement.addEventListener( 'click', ( e:Event )=>e.preventDefault() );//jsdom cannot follow the link routerLink leaves to the browser
		const close = vi.spyOn( shell.sidenav()!, 'close' );
		const link = fixture.nativeElement.querySelector( 'mat-sidenav a[href="/x"]' ) as HTMLElement;
		link.dispatchEvent( new MouseEvent('click', {bubbles: true, cancelable: true, ctrlKey: true}) );
		fixture.nativeElement.querySelector( 'mat-sidenav .docs-component-viewer-nav-content' ).click();
		expect( close ).not.toHaveBeenCalled();
		link.click();
		expect( close ).toHaveBeenCalledOnce();
		await flush( 2 );
		expect( close ).toHaveBeenCalledTimes( 2 );//and again on the NavigationEnd the click brings
	} );

	it( 'puts the nav beside the page on a wide one', async ()=>{
		const { fixture, shell, router } = create( false );
		expect( drawer(fixture) ).toBeNull();
		expect( inlineNav(fixture) ).not.toBeNull();
		expect( shell.sidenav() ).toBeUndefined();
		await router.navigateByUrl( '/x' );//nothing to close, and nothing thrown
	} );
} );