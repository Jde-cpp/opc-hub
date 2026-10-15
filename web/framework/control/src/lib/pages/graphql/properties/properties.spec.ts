import { TestBed } from '@angular/core/testing';
import { ActivatedRoute, Router } from '@angular/router';
import { ComponentPageTitle } from 'jde-spa';
import { SnackbarService } from '../../../shared/snackbar/snackbar-service';
import { IGRAPHQL } from '../../../services/graphql';
import { TableSchema } from '../../../model/ql/schema/table-schema';
import { Properties } from './properties';

//The fields listened on (keyup), so a right-click paste, a drag-drop or autofill changed the control without ever
//reaching the record - Save stayed disabled and the edit was lost.  (input) fires for all of them; these dispatch only
//that event, never a key, to pin it.
class Widget{
	constructor( o:any ){ Object.assign( this, o ); }
	[key:string]:any;
}
const schema = new TableSchema( { name: "Widget", enums: new Map(), fields: [
	{ name: "name", type: {kind: "NON_NULL", ofType: {kind: "SCALAR", name: "String"}} },
	{ name: "description", type: {kind: "SCALAR", name: "String"} },
	{ name: "url", type: {kind: "SCALAR", name: "String"} }
] } );

const create = ()=>{
	TestBed.resetTestingModule();
	TestBed.configureTestingModule({ providers: [
		{ provide: ActivatedRoute, useValue: {} },
		{ provide: Router, useValue: {} },
		{ provide: ComponentPageTitle, useValue: {} },
		{ provide: SnackbarService, useValue: {} },
		{ provide: IGRAPHQL, useValue: {} }
	]});
	const fixture = TestBed.createComponent( Properties );
	fixture.componentRef.setInput( 'ctor', Widget );
	fixture.componentRef.setInput( 'schema', schema );
	fixture.componentRef.setInput( 'type', 'widget' );
	fixture.componentRef.setInput( 'record', new Widget({name: "w", description: "d", url: "opc.tcp://old"}) );
	fixture.detectChanges();
	return fixture;
};
const paste = ( control:HTMLInputElement|HTMLTextAreaElement, value:string )=>{
	control.value = value;
	control.dispatchEvent( new Event('input', {bubbles: true}) );
};

describe( 'Properties picks up a pasted value', ()=>{
	it( 'on a text input', ()=>{
		const fixture = create();
		paste( fixture.nativeElement.querySelector('input[type=url]'), "opc.tcp://pasted" );
		expect( fixture.componentInstance.record().url ).toBe( "opc.tcp://pasted" );
	} );

	it( 'on the description textarea', ()=>{
		const fixture = create();
		paste( fixture.nativeElement.querySelector('textarea'), "pasted description" );
		expect( fixture.componentInstance.record().description ).toBe( "pasted description" );
	} );

	it( 'as a new record instance, so the dirty-check effect re-runs', ()=>{
		const fixture = create();
		const before = fixture.componentInstance.record();
		paste( fixture.nativeElement.querySelector('input[type=url]'), "opc.tcp://pasted" );
		expect( fixture.componentInstance.record() ).not.toBe( before );
		expect( fixture.componentInstance.record() ).toBeInstanceOf( Widget );
	} );
} );

//`order` input: listed fields first in that order, the rest alphabetical.  The old comparator ranked an unlisted field
//equal to the last listed one, so where the alphabetical tail began was up to the sort.
describe( 'Properties field order', ()=>{
	const wide = new TableSchema( { name: "Widget", enums: new Map(), fields: [
		{ name: "url", type: {kind: "SCALAR", name: "String"} },
		{ name: "name", type: {kind: "NON_NULL", ofType: {kind: "SCALAR", name: "String"}} },
		{ name: "certificateUri", type: {kind: "SCALAR", name: "String"} },
		{ name: "slug", type: {kind: "NON_NULL", ofType: {kind: "SCALAR", name: "String"}} },
		{ name: "description", type: {kind: "SCALAR", name: "String"} },
		{ name: "defaultBrowseNs", type: {kind: "SCALAR", name: "Int"} }
	] } );
	const names = ( order?:string[] )=>{
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({ providers: [
			{ provide: ActivatedRoute, useValue: {} }, { provide: Router, useValue: {} }, { provide: ComponentPageTitle, useValue: {} },
			{ provide: SnackbarService, useValue: {} }, { provide: IGRAPHQL, useValue: {} }
		]});
		const fixture = TestBed.createComponent( Properties );
		fixture.componentRef.setInput( 'ctor', Widget );
		fixture.componentRef.setInput( 'schema', wide );
		fixture.componentRef.setInput( 'type', 'widget' );
		fixture.componentRef.setInput( 'record', new Widget({name: "w"}) );
		if( order )
			fixture.componentRef.setInput( 'order', order );
		fixture.detectChanges();
		return fixture.componentInstance.fields().map( f=>f.name );
	};

	it( 'defaults to Id, Name, then alphabetical', ()=>{
		expect( names() ).toEqual( ["slug", "name", "certificateUri", "defaultBrowseNs", "description", "url"] );
	} );

	it( 'follows the given order and appends the unlisted alphabetically', ()=>{
		expect( names(["slug", "name", "description", "url"]) ).toEqual( ["slug", "name", "description", "url", "certificateUri", "defaultBrowseNs"] );
	} );
} );

//`displayNames` input: the camelCase split cannot know an acronym ("Url", "Certificate Uri"), so a page can name those
//fields itself; everything unlisted keeps the split.
describe( 'Properties field labels', ()=>{
	it( 'use the override where given and the camelCase split elsewhere', ()=>{
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({ providers: [
			{ provide: ActivatedRoute, useValue: {} }, { provide: Router, useValue: {} }, { provide: ComponentPageTitle, useValue: {} },
			{ provide: SnackbarService, useValue: {} }, { provide: IGRAPHQL, useValue: {} }
		]});
		const fixture = TestBed.createComponent( Properties );
		fixture.componentRef.setInput( 'ctor', Widget );
		fixture.componentRef.setInput( 'schema', new TableSchema( { name: "Widget", enums: new Map(), fields: [
			{ name: "slug", type: {kind: "NON_NULL", ofType: {kind: "SCALAR", name: "String"}} },
			{ name: "certificateUri", type: {kind: "SCALAR", name: "String"} },
			{ name: "url", type: {kind: "SCALAR", name: "String"} }
		] } ) );
		fixture.componentRef.setInput( 'type', 'widget' );
		fixture.componentRef.setInput( 'record', new Widget({name: "w"}) );
		fixture.componentRef.setInput( 'displayNames', {url: "URL"} );
		fixture.detectChanges();
		const labels = [...fixture.nativeElement.querySelectorAll('mat-label')].map( (l:Element)=>l.textContent!.trim() );
		expect( labels ).toEqual( ["Slug", "Certificate Uri", "URL"] );
	} );
} );

//A NON_NULL field is what canSave gates on, and the asterisk is the only hint on a new record for why Save is disabled.
describe( 'Properties required markers', ()=>{
	it( 'mark the NON_NULL fields and nothing else', ()=>{
		const fixture = create();//name is NON_NULL, description and url nullable
		const marked = [...fixture.nativeElement.querySelectorAll('mat-form-field')]
			.filter( (f:Element)=>f.querySelector('.mat-mdc-form-field-required-marker') )
			.map( (f:Element)=>f.querySelector('mat-label')!.textContent!.trim() );
		expect( marked ).toEqual( ["Name"] );
		expect( fixture.nativeElement.querySelector('input[type=url]').required ).toBe( false );
	} );
} );

//`readonlyFields` input: a key the server will not update is shown but not editable.
describe( 'Properties readonly fields', ()=>{
	it( 'hold the listed field and leave the rest editable', ()=>{
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({ providers: [
			{ provide: ActivatedRoute, useValue: {} }, { provide: Router, useValue: {} }, { provide: ComponentPageTitle, useValue: {} },
			{ provide: SnackbarService, useValue: {} }, { provide: IGRAPHQL, useValue: {} }
		]});
		const fixture = TestBed.createComponent( Properties );
		fixture.componentRef.setInput( 'ctor', Widget );
		fixture.componentRef.setInput( 'schema', schema );
		fixture.componentRef.setInput( 'type', 'widget' );
		fixture.componentRef.setInput( 'record', new Widget({name: "w", description: "d", url: "opc.tcp://old"}) );
		fixture.componentRef.setInput( 'readonlyFields', ['url'] );
		fixture.detectChanges();
		expect( fixture.nativeElement.querySelector('input[type=url]').readOnly ).toBe( true );
		expect( fixture.nativeElement.querySelector('textarea').readOnly ).toBe( false );
	} );
} );

//#250 review L2:  the fields carried tabindex 1..n, which axe fails and which put them ahead of everything else on the page -
//once a page's buttons lost their 100+, Tab from the last field went to the navbar instead of Save.  Page order instead.
describe( 'Properties tab order', ()=>{
	it( 'sets no positive tabindex', ()=>{
		const root = create().nativeElement as HTMLElement;
		expect( root.querySelectorAll('input, textarea').length ).toBeGreaterThan( 0 );
		const positive = Array.from( root.querySelectorAll('[tabindex]') ).filter( (e)=>+e.getAttribute('tabindex')!>0 );
		expect( positive.map((e)=>e.outerHTML.slice(0, 60)) ).toEqual( [] );
	} );
} );