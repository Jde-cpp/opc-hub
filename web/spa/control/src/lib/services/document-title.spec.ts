import { Component } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { Title } from '@angular/platform-browser';
import { provideRouter, TitleStrategy } from '@angular/router';
import { RouterTestingHarness } from '@angular/router/testing';
import { APP_NAME, AppTitleStrategy, DocumentTitle } from './document-title';

@Component( {template: ''} )
class Blank {}

describe( 'AppTitleStrategy', ()=>{
	const setup = ( app?:string )=>TestBed.configureTestingModule( {providers: [
		provideRouter( [
			{ path: 'gateways', title: 'Gateways', component: Blank },
			{ path: 'gateways/:gateway', title: ':gateway', component: Blank },
			{ path: 'untitled', component: Blank }
		] ),
		{provide: TitleStrategy, useExisting: AppTitleStrategy},
		...(app ? [{provide: APP_NAME, useValue: app}] : [])
	]} );
	const tab = ()=>TestBed.inject( Title ).getTitle();

	it( 'follows the route title with the app name, and keeps the bare page name', async ()=>{
		setup( 'OPC Hub' );
		await (await RouterTestingHarness.create()).navigateByUrl( '/gateways' );
		expect( tab() ).toBe( 'Gateways · OPC Hub' );
		expect( TestBed.inject(DocumentTitle).page() ).toBe( 'Gateways' );
	});

	it( 'names a :param-titled route by the parameter, not the literal', async ()=>{
		setup( 'OPC Hub' );
		await (await RouterTestingHarness.create()).navigateByUrl( '/gateways/gw1' );
		expect( tab() ).toBe( 'gw1 · OPC Hub' );
	});

	it( 'leaves the tab to the page on an untitled route', async ()=>{
		setup( 'OPC Hub' );
		const harness = await RouterTestingHarness.create();
		await harness.navigateByUrl( '/gateways' );
		await harness.navigateByUrl( '/untitled' );
		expect( tab() ).toBe( 'Gateways · OPC Hub' );
	});

	it( 'adds no suffix when the site provides no APP_NAME', async ()=>{
		setup();
		await (await RouterTestingHarness.create()).navigateByUrl( '/gateways' );
		expect( tab() ).toBe( 'Gateways' );
	});
});

describe( 'DocumentTitle', ()=>{
	it( 'shows the app name alone for a page with no name', ()=>{
		TestBed.configureTestingModule( {providers: [{provide: APP_NAME, useValue: 'OPC Hub'}]} );
		TestBed.inject( DocumentTitle ).set( '' );
		expect( TestBed.inject(Title).getTitle() ).toBe( 'OPC Hub' );
	});
});
