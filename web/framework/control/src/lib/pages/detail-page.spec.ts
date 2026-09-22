if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { Component } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { ActivatedRoute, Router } from '@angular/router';
import { of } from 'rxjs';
import { vi } from 'vitest';
import { ComponentPageTitle } from 'jde-spa';
import { SnackbarService } from '../shared/snackbar/snackbar-service';
import { IGraphQL } from '../services/graphql';
import { SlugRow } from '../model/ql/slug-row';
import { DetailPage } from './detail-page';

class Widget extends SlugRow<Widget>{
	constructor( obj:any ){ super( "Widget", obj ?? {} ); }
	get properties():Partial<Widget>{ return new Widget( this ); }
}

@Component( {template: '', host: {class: 'detail-page-spec'}} )
class TestPage extends DetailPage<Widget>{
	constructor(){ super( 'testDetail' ); }
	protected override get ctor(){ return Widget; }
	protected override onRow(){ this.loaded.push( this.row.name ); }
	protected override upsert():Widget{ return new Widget( this.properties() ); }
	protected override afterMutate(){ ++this.mutated; }
	loaded:string[] = [];
	mutated = 0;
	override ql:IGraphQL = <IGraphQL><unknown>{ mutate: vi.fn().mockResolvedValue({}) };
}

//review3 C2: this is the skeleton the four detail pages each carried a copy of.  One copy of the behaviour, one set of
//tests for it - L2's clamp was in one page of four precisely because there was nowhere shared to put it.
describe( 'DetailPage', ()=>{
	let navigate:any;
	let exception:any;

	const create = ( row:any, storedTab=0 )=>{
		TestBed.resetTestingModule();//BEFORE seeding localStorage: it destroys the previous fixture, whose ngOnDestroy writes its own tab index back
		localStorage.setItem( 'testDetail', String(storedTab) );
		navigate = vi.fn().mockResolvedValue( true );
		exception = vi.fn();
		TestBed.configureTestingModule({ providers: [
			{ provide: ActivatedRoute, useValue: {data: of({pageData: {row, routing: {}, schema: {}}})} },
			{ provide: Router, useValue: {navigate} },
			{ provide: ComponentPageTitle, useValue: {} },
			{ provide: SnackbarService, useValue: {exception, error: vi.fn()} }
		]});
		const fixture = TestBed.createComponent( TestPage );
		fixture.componentInstance.ngOnInit();
		return fixture;
	};

	afterEach( ()=>localStorage.clear() );

	it( 'loads the row and hands it to the subclass', ()=>{
		const page = create( {id: 3, name: 'w', slug: 'w'} ).componentInstance;
		expect( page.row.id ).toBe( 3 );
		expect( page.loaded ).toEqual( ['w'] );//onRow ran, and after the fields it needs exist
		expect( page.isLoading() ).toBe( false );
	} );

	it( 'clamps a stored tab index for a new record', ()=>{
		expect( create({}, 3).componentInstance.tabIndex() ).toBe( 0 );//review3 L2
		expect( create({id: 3, name: 'w', slug: 'w'}, 3).componentInstance.tabIndex() ).toBe( 3 );
	} );

	it( 'persists the tab index on destroy', ()=>{
		const page = create( {id: 3, name: 'w', slug: 'w'} ).componentInstance;
		page.onTabIndexChanged( 2 );
		page.ngOnDestroy();
		expect( localStorage.getItem('testDetail') ).toBe( '2' );
	} );

	it( 'marks the page dirty only once the edit is saveable', ()=>{
		const fixture = create( {id: 3, name: 'w', slug: 'w'} );
		const page = fixture.componentInstance;
		fixture.detectChanges();
		expect( page.isChanged() ).toBe( false );//loaded, untouched

		page.properties.set( new Widget({id: 3, name: '', slug: ''}) );//canSave false - a half-typed row is not a change to save
		fixture.detectChanges();
		expect( page.isChanged() ).toBe( false );

		page.properties.set( new Widget({id: 3, name: 'renamed', slug: 'w'}) );
		fixture.detectChanges();
		expect( page.isChanged() ).toBe( true );
	} );

	it( 'saves and returns to the list', async ()=>{
		const page = create( {id: 3, name: 'w', slug: 'w'} ).componentInstance;
		page.properties.set( new Widget({id: 3, name: 'renamed', slug: 'w'}) );
		await page.onSubmitClick();
		expect( page.ql.mutate ).toHaveBeenCalled();
		expect( navigate ).toHaveBeenCalledWith( ['..'], expect.anything() );
	} );

	it( 'reports a failed save and stays put', async ()=>{
		const page = create( {id: 3, name: 'w', slug: 'w'} ).componentInstance;
		page.properties.set( new Widget({id: 3, name: 'renamed', slug: 'w'}) );
		(<any>page.ql.mutate).mockRejectedValue( new Error("denied") );
		await page.onSubmitClick();
		expect( exception ).toHaveBeenCalledWith( "Save failed.", expect.any(Error) );
		expect( navigate ).not.toHaveBeenCalled();
	} );

	//reviews/m3-closing.md #5:  a page whose row something else caches (ClientDetail - OpcStore's describe) has to hear that a
	//mutation landed, or the cache outlives the edit.  Once per landed mutation, never for a refused one.
	it( 'tells the subclass a save, delete, restore or purge landed - and not a failed one', async ()=>{
		const page = create( {id: 3, name: 'w', slug: 'w'} ).componentInstance;
		page.properties.set( new Widget({id: 3, name: 'renamed', slug: 'w'}) );
		await page.onSubmitClick();
		await page.onDeleteClick();
		expect( page.mutated ).toBe( 2 );
		(<any>page.ql.mutate).mockRejectedValue( new Error("denied") );
		await page.onSubmitClick();
		await page.onDeleteClick();
		expect( page.mutated ).toBe( 2 );
	} );

	it( 'cancels back to the list', ()=>{
		create( {id: 3, name: 'w', slug: 'w'} ).componentInstance.onCancelClick();
		expect( navigate ).toHaveBeenCalledWith( ['..'], expect.anything() );
	} );
} );
