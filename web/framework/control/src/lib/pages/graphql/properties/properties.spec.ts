import { TestBed } from '@angular/core/testing';
import { Router } from '@angular/router';
import { SnackbarService } from '../../../shared/snackbar/snackbar-service';
import { IGRAPHQL } from '../../../services/graphql';
import { TableSchema } from '../../../model/ql/schema/table-schema';
import { Properties } from './properties';
import { pageTitleStub, provideRoute, routerStub, stable } from 'jde-spa/testing';
import { required, scalar, schemaOf, snackbarStub, Widget } from 'jde-framework/testing';

//The fields listened on (keyup), so a right-click paste, a drag-drop or autofill changed the control without ever
//reaching the record - Save stayed disabled and the edit was lost.  (input) fires for all of them; these dispatch only
//that event, never a key, to pin it.
const schema = schemaOf( "Widget", required("name"), scalar("description"), scalar("url") );

//The form over `on`, editing `record`, with the other `inputs` set.
const render = ( on:TableSchema, record:Widget, inputs:Record<string,unknown>={} )=>{
	TestBed.configureTestingModule({ providers: [
		provideRoute(),
		{ provide: Router, useValue: routerStub() },
		pageTitleStub,
		{ provide: SnackbarService, useValue: snackbarStub() },
		{ provide: IGRAPHQL, useValue: {} }
	]});
	const fixture = TestBed.createComponent( Properties );
	for( const [name, value] of Object.entries({ctor: Widget, schema: on, type: 'widget', record, ...inputs}) )
		fixture.componentRef.setInput( name, value );
	fixture.detectChanges();
	return fixture;
};
const create = ()=>render( schema, new Widget({name: "w", description: "d", url: "opc.tcp://old"}) );
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
	const wide = schemaOf( "Widget", scalar("url"), required("name"), scalar("certificateUri"), required("slug"), scalar("description"), scalar("defaultBrowseNs", "Int") );
	const names = ( order?:string[] )=>render( wide, new Widget({name: "w"}), order ? {order} : {} ).componentInstance.fields().map( f=>f.name );

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
		const fixture = render( schemaOf("Widget", required("slug"), scalar("certificateUri"), scalar("url")), new Widget({name: "w"}), {displayNames: {url: "URL"}} );
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
		const fixture = render( schema, new Widget({name: "w", description: "d", url: "opc.tcp://old"}), {readonlyFields: ['url']} );
		expect( fixture.nativeElement.querySelector('input[type=url]').readOnly ).toBe( true );
		expect( fixture.nativeElement.querySelector('textarea').readOnly ).toBe( false );
	} );
	it( 'mark no readonly field required, as the user cannot fill it', ()=>{
		const fixture = render( schema, new Widget({name: "w"}), {readonlyFields: ['name']} );
		expect( fixture.nativeElement.querySelector('.mat-mdc-form-field-required-marker') ).toBeNull();
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

//`picks`, `units` and `hints` inputs (#413):  a page's own select over a scalar the schema has no enum for, null an option like
//any other;  a number input with its unit as the suffix;  and a hint under a field where there is no error to show.
describe( 'Properties picks, units and hints', ()=>{
	const settings = schemaOf( "Widget", required("name"), scalar("securityMode"), scalar("ttl", "UInt"), scalar("locale") );
	const picks = { securityMode: [ {value: null, label: "Default (None)"}, {value: "Sign", label: "Sign"} ] };
	const render = ( record:Widget )=>{
		TestBed.configureTestingModule({ providers: [
			provideRoute(),
			{ provide: Router, useValue: routerStub() },
			pageTitleStub,
			{ provide: SnackbarService, useValue: snackbarStub() },
			{ provide: IGRAPHQL, useValue: {} }
		]});
		const fixture = TestBed.createComponent( Properties );
		for( const [name, value] of Object.entries({ctor: Widget, schema: settings, type: 'widget', record, picks, units: {ttl: "ms"}, hints: {ttl: "Empty:  the gateway's setting", locale: "Comma-separated"}}) )
			fixture.componentRef.setInput( name, value );
		fixture.detectChanges();
		return fixture;
	};
	it( 'renders a pick as a select whose options carry the given values', ()=>{
		const fixture = render( new Widget({name: "w", securityMode: null}) );
		const root = fixture.nativeElement as HTMLElement;
		expect( root.querySelectorAll('mat-select').length ).toBe( 1 );
		const field = fixture.componentInstance.fields().find( f=>f.name=="securityMode" )!;
		expect( field.pick!.map( o=>o.label ) ).toEqual( ["Default (None)", "Sign"] );
		expect( root.querySelector('input[type=text]') ).not.toBeNull();//locale stays a text input
	} );
	//Material treats a null option as a reset unless canSelectNullableOptions is set, and shows nothing for it
	it( 'shows the null option\'s label on an unset field', async ()=>{
		const fixture = render( new Widget({name: "w", securityMode: null}) );
		await stable( fixture );//the select takes its value once its options are in, a microtask on
		expect( (fixture.nativeElement as HTMLElement).querySelector('mat-select')!.textContent!.trim() ).toBe( "Default (None)" );
	} );
	it( 'renders a unit field as a number input with the unit as its suffix', ()=>{
		const root = render( new Widget({name: "w", ttl: 120000}) ).nativeElement as HTMLElement;
		const input = root.querySelector( 'input[type=number]' ) as HTMLInputElement;
		expect( input.value ).toBe( "120000" );
		expect( root.querySelector('[matTextSuffix]')!.textContent!.trim() ).toBe( "ms" );
	} );
	it( 'puts the typed number on the record as the input\'s string', ()=>{
		const fixture = render( new Widget({name: "w"}) );
		paste( fixture.nativeElement.querySelector('input[type=number]'), "30000" );
		expect( fixture.componentInstance.record().ttl ).toBe( "30000" );
	} );
	//`1e` on the way to `1e5` reads "" off a number input:  written to the record, it would come back as "" and wipe the field
	it( 'leaves the record alone while the number is half typed', ()=>{
		const fixture = render( new Widget({name: "w", ttl: "1"}) );
		const input = fixture.nativeElement.querySelector( 'input[type=number]' ) as HTMLInputElement;
		Object.defineProperty( input, "validity", {value: {badInput: true}} );
		paste( input, "" );
		expect( fixture.componentInstance.record().ttl ).toBe( "1" );
	} );
	it( 'shows the hint under a field without an error, and nothing under the rest', ()=>{
		const root = render( new Widget({name: "w"}) ).nativeElement as HTMLElement;
		expect( [...root.querySelectorAll('mat-hint')].map( (h:Element)=>h.textContent!.trim() ) ).toEqual( ["Comma-separated", "Empty:  the gateway's setting"] );//in field order:  locale, ttl
	} );
} );