import { TestBed } from '@angular/core/testing';
import { MatSnackBar } from '@angular/material/snack-bar';
import { HttpErrorResponse } from '@angular/common/http';
import { SnackbarService } from './snackbar-service';
import { Snackbar } from './snackbar';
import { silence } from 'jde-spa/testing';

describe( 'SnackbarService', ()=>{
	const create = ()=>{
		const snackbar = { openFromComponent: vi.fn() };
		//MatSnackBar is inject()ed since C5, so the stub goes in through the injector rather than the constructor
		TestBed.configureTestingModule({ providers: [ {provide: MatSnackBar, useValue: snackbar} ] });
		return { service: TestBed.inject( SnackbarService ), snackbar, lastConfig: ()=>snackbar.openFromComponent.mock.lastCall?.[1] };
	};

	it( 'error shows the error tone for 10s and logs', ()=>{
		const { service, snackbar } = create();
		const log = silence( 'error' );
		service.error( 'boom' );
		expect( snackbar.openFromComponent ).toHaveBeenCalledWith( Snackbar, {panelClass: ['error-snackbar'], data: {message: 'boom', duration: 10000}} );
		expect( log ).toHaveBeenCalledWith( 'boom' );
		log.mockRestore();
	});

	it( 'warn shows the warn tone for 5s and logs', ()=>{
		const { service, lastConfig } = create();
		const log = silence();
		service.warn( 'careful' );
		expect( lastConfig() ).toEqual( {panelClass: ['warn-snackbar'], data: {message: 'careful', duration: 5000}} );
		expect( log ).toHaveBeenCalledWith( 'careful' );
		log.mockRestore();
	});

	it( 'info shows for 1s', ()=>{
		const { service, lastConfig } = create();
		service.info( 'fyi' );
		expect( lastConfig() ).toEqual( {panelClass: ['info-snackbar'], data: {message: 'fyi', duration: 1000}} );
	});

	it( 'a throw with nothing quotable leaves the context as the whole message', ()=>{
		const { service, lastConfig } = create();
		service.exception( 'string throw', ()=>{} );
		expect( lastConfig() ).toEqual( {panelClass: ['error-snackbar'], data: {message: 'string throw', duration: 10000}} );
	});

	it( 'a ProtoService rejection shows status and message, not JSON', ()=>{
		const { service, lastConfig } = create();
		service.exception( 'ctx', {error: {requestId: 5, message: 'denied', sc: 3, httpStatus: 401}} );
		expect( lastConfig()?.data.message ).toBe( 'ctx  (401)denied' );
		service.exception( 'ctx', {error: {message: 'boom'}} );//no status - message alone, no "(undefined)".
		expect( lastConfig()?.data.message ).toBe( 'ctx  boom' );
	});

	//the caller's context used to be dropped for exactly the shapes that reach production, so a 500 surfaced as the bare
	//server text with no clue which page produced it.
	it( 'an HttpErrorResponse keeps the caller context and the server text', ()=>{
		const { service, lastConfig } = create();
		service.exception( "Could not load 'readers'", new HttpErrorResponse({status: 500, error: 'Query failed.'}) );
		expect( lastConfig()?.data.message ).toBe( "Could not load 'readers'  (500)Query failed." );
	});

	it( 'a context equal to the error text is not doubled', ()=>{
		const { service, lastConfig } = create();
		service.exception( 'boom', 'boom' );
		expect( lastConfig()?.data.message ).toBe( 'boom' );
	});

	it( 'assert(false) shows and throws', ()=>{
		const { service, lastConfig } = create();
		expect( ()=>service.assert(false) ).toThrow();
		expect( lastConfig()?.panelClass ).toEqual( ['error-snackbar'] );
	});
});