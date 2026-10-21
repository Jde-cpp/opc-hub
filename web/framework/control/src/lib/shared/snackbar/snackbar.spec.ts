import { TestBed } from '@angular/core/testing';
import { MAT_SNACK_BAR_DATA, MatSnackBarRef } from '@angular/material/snack-bar';
import { Snackbar } from './snackbar';

describe( 'Snackbar', ()=>{
	const create = ()=>{
		const ref = { dismiss: vi.fn() };
		TestBed.configureTestingModule({ providers: [
			{provide: MatSnackBarRef, useValue: ref},
			{provide: MAT_SNACK_BAR_DATA, useValue: {message: 'boom', duration: 1000}}
		]});
		return { fixture: TestBed.createComponent( Snackbar ), ref };
	};
	beforeEach( ()=>vi.useFakeTimers() );//the component sets its own dismiss timer
	afterEach( ()=>vi.useRealTimers() );

	it( 'auto-dismisses after duration', ()=>{
		const { ref } = create();
		vi.advanceTimersByTime( 1001 );
		expect( ref.dismiss ).toHaveBeenCalled();
	});

	it( 'pin cancels auto-dismiss', ()=>{
		const { fixture, ref } = create();
		fixture.componentInstance.togglePin();
		vi.advanceTimersByTime( 5000 );
		expect( ref.dismiss ).not.toHaveBeenCalled();
	});

	it( 'unpin restarts the full countdown', ()=>{
		const { fixture, ref } = create();
		fixture.componentInstance.togglePin();
		vi.advanceTimersByTime( 5000 );
		fixture.componentInstance.togglePin();
		vi.advanceTimersByTime( 999 );
		expect( ref.dismiss ).not.toHaveBeenCalled();
		vi.advanceTimersByTime( 2 );
		expect( ref.dismiss ).toHaveBeenCalled();
	});

	it( 'close dismisses immediately', ()=>{
		const { fixture, ref } = create();
		fixture.componentInstance.ref.dismiss();
		expect( ref.dismiss ).toHaveBeenCalled();
	});

	it( 'destroy clears the pending timer', ()=>{
		const { fixture, ref } = create();
		fixture.destroy();
		vi.advanceTimersByTime( 5000 );
		expect( ref.dismiss ).not.toHaveBeenCalled();
	});
});