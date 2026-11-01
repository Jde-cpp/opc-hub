import { TestBed } from '@angular/core/testing';
import { BreakpointObserver, BreakpointState } from '@angular/cdk/layout';
import { BehaviorSubject } from 'rxjs';
import { ScreenSize } from './screen-size';
import { silence } from 'jde-spa/testing';

//The small breakpoint is written in constants.scss alone:  _general.scss publishes it as --jde-breakpoint-small, and the service
//builds its media query from that rather than from a number of its own (#330).
describe( 'ScreenSize', ()=>{
	afterEach( ()=>{ document.documentElement.style.removeProperty( '--jde-breakpoint-small' ); vi.restoreAllMocks(); } );

	//the document with the sheet's property, or without;  the observer is a fake whose state the test moves
	const create = ( width?:string )=>{
		if( width )
			document.documentElement.style.setProperty( '--jde-breakpoint-small', width );
		const state = new BehaviorSubject<BreakpointState>( {matches: false, breakpoints: {}} );
		const observe = vi.fn( ()=>state );
		TestBed.configureTestingModule( {providers: [{provide: BreakpointObserver, useValue: {observe}}]} );
		return { screen: TestBed.inject( ScreenSize ), state, observe };
	};

	it( 'asks the observer for the width the sheet publishes, and follows it', ()=>{
		const { screen, state, observe } = create( '959px' );
		expect( observe ).toHaveBeenCalledExactlyOnceWith( '(max-width: 959px)' );
		expect( screen.isSmall() ).toBe( false );
		state.next( {matches: true, breakpoints: {}} );
		expect( screen.isSmall() ).toBe( true );
	} );

	it( 'is never small without the sheet, and says so once', ()=>{
		const warn = silence( 'warn' );
		const { screen, observe } = create();
		expect( screen.isSmall() ).toBe( false );
		expect( observe ).not.toHaveBeenCalled();
		expect( warn ).toHaveBeenCalledOnce();
	} );
} );