import { TestBed } from '@angular/core/testing';
import { BreakpointObserver, BreakpointState } from '@angular/cdk/layout';
import { BehaviorSubject } from 'rxjs';
import { ScreenSize } from './screen-size';
import { silence } from 'jde-spa/testing';

//The breakpoints are written in constants.scss alone:  _general.scss publishes them as --jde-breakpoint-*, and the service builds
//its media queries from those rather than from numbers of its own (#330).
describe( 'ScreenSize', ()=>{
	afterEach( ()=>{
		document.documentElement.style.removeProperty( '--jde-breakpoint-small' );
		document.documentElement.style.removeProperty( '--jde-breakpoint-extra-small' );
		vi.restoreAllMocks();
	} );

	const publish = ()=>{
		document.documentElement.style.setProperty( '--jde-breakpoint-small', '959px' );
		document.documentElement.style.setProperty( '--jde-breakpoint-extra-small', '720px' );
	};
	//the observer is a fake, one state per query, which the test moves
	const create = ()=>{
		const states = new Map<string,BehaviorSubject<BreakpointState>>();
		const observe = vi.fn( ( query:string )=>{
			const state = new BehaviorSubject<BreakpointState>( {matches: false, breakpoints: {}} );
			states.set( query, state );
			return state;
		} );
		TestBed.configureTestingModule( {providers: [{provide: BreakpointObserver, useValue: {observe}}]} );
		return { screen: TestBed.inject( ScreenSize ), states, observe };
	};

	it( 'asks the observer for the widths the sheet publishes, and follows them', ()=>{
		publish();
		const { screen, states, observe } = create();
		expect( observe.mock.calls ).toEqual( [['(max-width: 959px)'], ['(max-width: 720px)']] );
		expect( screen.isSmall() ).toBe( false );
		states.get( '(max-width: 959px)' )!.next( {matches: true, breakpoints: {}} );
		expect( screen.isSmall() ).toBe( true );
		expect( screen.isExtraSmall() ).toBe( false );
		states.get( '(max-width: 720px)' )!.next( {matches: true, breakpoints: {}} );
		expect( screen.isExtraSmall() ).toBe( true );
	} );

	it( 'is never small without the sheet, and says so once for each breakpoint', ()=>{
		const warn = silence( 'warn' );
		const { screen, observe } = create();
		expect( screen.isSmall() ).toBe( false );
		expect( screen.isExtraSmall() ).toBe( false );
		expect( observe ).not.toHaveBeenCalled();
		expect( warn.mock.calls.map( call=>(call[0] as string).split(' ')[0] ) ).toEqual( ['--jde-breakpoint-small', '--jde-breakpoint-extra-small'] );
	} );

	//production loads styles.css without blocking the render, so the first page can be made before the sheet applies
	it( 'reads the sheet again on the load of a document still loading', ()=>{
		const warn = silence( 'warn' );
		vi.spyOn( document, 'readyState', 'get' ).mockReturnValue( 'loading' );
		const { screen, states, observe } = create();
		expect( observe ).not.toHaveBeenCalled();
		publish();
		window.dispatchEvent( new Event('load') );
		expect( observe ).toHaveBeenCalledTimes( 2 );
		states.get( '(max-width: 959px)' )!.next( {matches: true, breakpoints: {}} );
		expect( screen.isSmall() ).toBe( true );
		expect( warn ).not.toHaveBeenCalled();
	} );

	it( 'says so on the load when the sheet never came', ()=>{
		const warn = silence( 'warn' );
		vi.spyOn( document, 'readyState', 'get' ).mockReturnValue( 'loading' );
		const { screen } = create();
		expect( warn ).not.toHaveBeenCalled();
		window.dispatchEvent( new Event('load') );
		expect( screen.isSmall() ).toBe( false );
		expect( warn ).toHaveBeenCalledTimes( 2 );
	} );
} );