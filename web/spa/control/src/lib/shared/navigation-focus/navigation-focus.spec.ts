import { Component } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { NavigationFocusService } from './navigation-focus-service';
import { NavigationFocus } from './navigation-focus';

//review3 C9: the two @HostBinding decorators moved into the decorator's `host` object.  A render test, because that move
//is not type-checked - a wrong key there sets a literally-named attribute instead of the property it meant to bind.
@Component( {template: '<div focusOnNavigation id="already-set"></div><div focusOnNavigation></div>', imports: [NavigationFocus]} )
class Host{}

describe( 'NavigationFocus host bindings', ()=>{
	const create = ()=>{
		const service = { requestFocusOnNavigation: vi.fn(), relinquishFocusOnNavigation: vi.fn() };
		TestBed.configureTestingModule({ providers: [ {provide: NavigationFocusService, useValue: service} ] });
		const fixture = TestBed.createComponent( Host );
		fixture.detectChanges();
		return { fixture, service };
	};

	it( 'makes the host focusable and drops its focus ring', ()=>{
		const el:HTMLElement = create().fixture.nativeElement.querySelector( '#already-set' );
		expect( el.tabIndex ).toBe( -1 );//the PROPERTY, which is what @HostBinding('tabindex') set
		expect( el.style.outline ).toBe( 'none' );
	} );

	it( 'registers and unregisters with the focus service', ()=>{
		const { fixture, service } = create();
		expect( service.requestFocusOnNavigation ).toHaveBeenCalledTimes( 2 );
		fixture.destroy();
		expect( service.relinquishFocusOnNavigation ).toHaveBeenCalledTimes( 2 );
	} );
} );