import { Component } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { defer, of } from 'rxjs';
import { IGraphQL } from '../services/graphql';
import { DetailPage } from './detail-page';
import { configureDetailPage, Widget } from 'jde-framework/testing';

@Component( {template: '', host: {class: 'detail-page-spec'}} )
class TestPage extends DetailPage<Widget>{
	constructor(){ super( 'testDetail' ); }
	protected override get ctor(){ return Widget; }
	protected override onRow(){ this.loaded( this.row.name ); }
	protected override upsert():Widget{ return new Widget( this.properties() ); }
	protected override afterMutate(){ this.mutated(); }
	loaded = vi.fn();
	mutated = vi.fn();
	override ql:IGraphQL = <IGraphQL><unknown>{ mutate: vi.fn().mockResolvedValue({}) };
}

//review3 C2: this is the skeleton the four detail pages each carried a copy of.  One copy of the behaviour, one set of
//tests for it - L2's clamp was in one page of four precisely because there was nowhere shared to put it.
describe( 'DetailPage', ()=>{
	const create = ( row:any, storedTab=0 )=>{
		TestBed.resetTestingModule();//a test builds two;  and BEFORE seeding localStorage:  it destroys the previous fixture, and with it the effect that writes that one's tab index
		localStorage.setItem( 'testDetail/tabIndex', String(storedTab) );
		const { router, snackbar } = configureDetailPage( row, {data: defer( ()=>of({pageData: {row, routing: {}, schema: {}}}) )} );//fresh per subscription:  #load clears pageData.row, and a fixture pass calls ngOnInit again
		const fixture = TestBed.createComponent( TestPage );
		fixture.componentInstance.ngOnInit();
		return { fixture, page: fixture.componentInstance, router, snackbar };
	};

	afterEach( ()=>localStorage.clear() );

	it( 'loads the row and hands it to the subclass', ()=>{
		const { page } = create( {id: 3, name: 'w', slug: 'w'} );
		expect( page.row.id ).toBe( 3 );
		expect( page.loaded.mock.calls ).toEqual( [['w']] );//onRow ran, and after the fields it needs exist
		expect( page.isLoading() ).toBe( false );
	} );

	it( 'clamps a stored tab index for a new record', ()=>{
		expect( create({}, 3).page.tabIndex() ).toBe( 0 );//review3 L2
		expect( create({id: 3, name: 'w', slug: 'w'}, 3).page.tabIndex() ).toBe( 3 );
	} );

	it( 'persists the tab index as it changes', ()=>{
		const { fixture, page } = create( {id: 3, name: 'w', slug: 'w'} );
		page.tabIndex.set( 2 );
		fixture.detectChanges();//a component's effects run with the view that hosts it
		expect( localStorage.getItem('testDetail/tabIndex') ).toBe( '2' );
	} );

	it( 'marks the page dirty only once the edit is saveable', ()=>{
		const { fixture, page } = create( {id: 3, name: 'w', slug: 'w'} );
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
		const { page, router } = create( {id: 3, name: 'w', slug: 'w'} );
		page.properties.set( new Widget({id: 3, name: 'renamed', slug: 'w'}) );
		await page.onSubmitClick();
		expect( page.ql.mutate ).toHaveBeenCalled();
		expect( router.navigate ).toHaveBeenCalledWith( ['..'], expect.anything() );
	} );

	it( 'reports a failed save and stays put', async ()=>{
		const { page, router, snackbar } = create( {id: 3, name: 'w', slug: 'w'} );
		page.properties.set( new Widget({id: 3, name: 'renamed', slug: 'w'}) );
		(<any>page.ql.mutate).mockRejectedValue( new Error("denied") );
		await page.onSubmitClick();
		expect( snackbar.exception ).toHaveBeenCalledWith( "Save failed.", expect.any(Error) );
		expect( router.navigate ).not.toHaveBeenCalled();
	} );

	//reviews/m3-closing.md #5:  a page whose row something else caches (ClientDetail - OpcStore's describe) has to hear that a
	//mutation landed, or the cache outlives the edit.  Once per landed mutation, never for a refused one.
	it( 'tells the subclass a save, delete, restore or purge landed - and not a failed one', async ()=>{
		const { page } = create( {id: 3, name: 'w', slug: 'w'} );
		page.properties.set( new Widget({id: 3, name: 'renamed', slug: 'w'}) );
		await page.onSubmitClick();
		await page.onDeleteClick();
		expect( page.mutated ).toHaveBeenCalledTimes( 2 );
		(<any>page.ql.mutate).mockRejectedValue( new Error("denied") );
		await page.onSubmitClick();
		await page.onDeleteClick();
		expect( page.mutated ).toHaveBeenCalledTimes( 2 );
	} );

	it( 'cancels back to the list', ()=>{
		const { page, router } = create( {id: 3, name: 'w', slug: 'w'} );
		page.onCancelClick();
		expect( router.navigate ).toHaveBeenCalledWith( ['..'], expect.anything() );
	} );
} );