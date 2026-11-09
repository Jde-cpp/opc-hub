import { Component } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { defer, of } from 'rxjs';
import { IGraphQL } from '../services/graphql';
import { DetailPage } from './detail-page';
import { MatDialog } from '@angular/material/dialog';
import { configureDetailPage, dialogStub, Widget } from 'jde-framework/testing';

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

	//reviews-issue/web-refactor-review.md #7:  the clamp was stored, and a stored 0 is no entry, so Add then Cancel lost the tab every
	//existing record opened on
	it( 'stores nothing for a new record, and opens the next existing one on the stored tab', ()=>{
		TestBed.resetTestingModule();
		localStorage.setItem( 'testDetail/tabIndex', '3' );
		const { data } = configureDetailPage( {id: 3, name: 'w', slug: 'w'} );
		const fixture = TestBed.createComponent( TestPage );
		const page = fixture.componentInstance;
		const open = ( row:object )=>{
			data.next( {pageData: {row, routing: {}, schema: {}}} );
			fixture.detectChanges();
		};
		fixture.detectChanges();
		expect( page.tabIndex() ).toBe( 3 );
		open( {} );
		expect( page.tabIndex() ).toBe( 0 );
		expect( localStorage.getItem('testDetail/tabIndex') ).toBe( '3' );
		page.tabIndex.set( 0 );//its only tab, clicked
		fixture.detectChanges();
		expect( localStorage.getItem('testDetail/tabIndex') ).toBe( '3' );
		open( {id: 4, name: 'x', slug: 'x'} );
		expect( page.tabIndex() ).toBe( 3 );
		page.tabIndex.set( 1 );
		fixture.detectChanges();
		expect( localStorage.getItem('testDetail/tabIndex') ).toBe( '1' );
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

	//#396:  a page whose rows others plan by (CalendarDetail) says what a Delete costs first, and its refusals on its form
	it( 'asks before a delete where the page gives a warning, never before a restore, and adds its words to the purge', async ()=>{
		@Component( {template: '', host: {class: 'detail-page-warning-spec'}} )
		class WarningPage extends TestPage{
			protected override get deleteWarning(){ return 'Others use it.'; }
			protected override get purgeWarning(){ return 'Its children go too.'; }
			protected override saveFailed( e:unknown ){ this.refused( e ); }
			refused = vi.fn();
		}
		const open = ( row:object, answer:boolean )=>{
			TestBed.resetTestingModule();//a page per answer
			const dialog = dialogStub( answer );
			const { snackbar } = configureDetailPage( row, {providers: [{ provide: MatDialog, useValue: dialog }]} );
			const page = TestBed.createComponent( WarningPage ).componentInstance;
			page.ngOnInit();
			return { page, dialog, snackbar, asked: ()=>dialog.open.mock.calls.map( ([, config])=><{title:string; message:string}>config!.data ) };
		};
		const refused = open( {id: 3, name: 'w', slug: 'w'}, false );
		await refused.page.onDeleteClick();
		expect( refused.asked() ).toEqual( [{title: 'Delete w?', message: 'Others use it.', confirm: 'Delete', destructive: true}] );
		expect( refused.page.ql.mutate ).not.toHaveBeenCalled();

		const confirmed = open( {id: 3, name: 'w', slug: 'w'}, true );
		await confirmed.page.onDeleteClick();
		expect( confirmed.page.ql.mutate ).toHaveBeenCalledWith( 'deleteWidget(id:3)' );
		(<any>confirmed.page.ql.mutate).mockRejectedValue( new Error("denied") );
		confirmed.page.properties.set( new Widget({id: 3, name: 'renamed', slug: 'w'}) );
		await confirmed.page.onSubmitClick();
		expect( confirmed.page.refused ).toHaveBeenCalledWith( expect.any(Error) );//the page's own say, in place of the toast
		expect( confirmed.snackbar.exception ).not.toHaveBeenCalled();

		const deleted = open( {id: 3, name: 'w', slug: 'w', deleted: '2026-01-01T00:00:00Z'}, true );
		await deleted.page.onDeleteClick();
		expect( deleted.asked() ).toEqual( [] );
		expect( deleted.page.ql.mutate ).toHaveBeenCalledWith( 'restoreWidget(id:3)' );
		await deleted.page.onPurgeClick();
		expect( deleted.asked()[0].message ).toBe( 'w will be permanently removed. This cannot be undone - a purged row has no Restore.  Its children go too.' );
	} );
	it( 'deletes at once, and purges with its own words alone, where the page gives none', async ()=>{
		TestBed.resetTestingModule();
		const dialog = dialogStub( true );
		configureDetailPage( {id: 3, name: 'w', slug: 'w', deleted: '2026-01-01T00:00:00Z'}, {providers: [{ provide: MatDialog, useValue: dialog }]} );
		const page = TestBed.createComponent( TestPage ).componentInstance;
		page.ngOnInit();
		await page.onPurgeClick();
		expect( (<{message:string}>dialog.open.mock.calls[0][1]!.data).message ).toBe( 'w will be permanently removed. This cannot be undone - a purged row has no Restore.' );
	} );

	it( 'cancels back to the list', ()=>{
		const { page, router } = create( {id: 3, name: 'w', slug: 'w'} );
		page.onCancelClick();
		expect( router.navigate ).toHaveBeenCalledWith( ['..'], expect.anything() );
	} );
} );