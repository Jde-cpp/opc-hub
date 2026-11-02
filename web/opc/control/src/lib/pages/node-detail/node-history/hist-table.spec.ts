import { TestBed } from '@angular/core/testing';
import { Paginator } from 'jde-framework';
import { HistValue } from '../../../model/hist';
import { Variable } from '../../../model/node';
import { EAccess } from '../../../model/types';
import { HistTable } from './hist-table';
import { histTick, histValue, variable } from 'jde-opc/testing';

const A = variable( 1, 'A', {historizing: true, userAccessLevel: EAccess.Read|EAccess.HistoryRead|EAccess.HistoryWrite} ), B = variable( 2, 'B', {historizing: true, userAccessLevel: EAccess.Read|EAccess.HistoryRead} );
const at = ( v:Variable, ms:number, value:number, extra:Partial<HistValue>={} )=>histValue( v, ms, value, {sourceTime: histTick(ms), ...extra} );
const render = ( values:HistValue[], inputs:Record<string,unknown>={} )=>{
	const fixture = TestBed.createComponent( HistTable );
	fixture.componentRef.setInput( 'values', values );
	fixture.componentRef.setInput( 'nodes', [A, B] );
	for( const [name, value] of Object.entries( inputs ) )
		fixture.componentRef.setInput( name, value );
	fixture.detectChanges();
	const headers = ()=>[...fixture.nativeElement.querySelectorAll( 'mat-header-cell' )].map( h=>h.textContent.trim() );
	const cells = ( row:number )=>[...fixture.nativeElement.querySelectorAll( 'mat-row' )[row].querySelectorAll( 'mat-cell' )].map( c=>c.textContent.trim() );
	return { fixture, table: fixture.componentInstance, headers, cells };
};

describe( 'HistTable', ()=>{
	it( 'lists the values newest first with the flags column, and no actions without editable nodes', ()=>{
		const t = render( [at(A, 1000, 1), at(B, 2000, 2, {bound: true})] );
		expect( t.headers() ).toEqual( ['Node', 'Source time', 'Server time', 'Status', 'Value', ''] );
		expect( t.cells( 0 )[0] ).toBe( 'B' );
		expect( t.cells( 0 )[5] ).toBe( 'bound' );
		expect( t.fixture.nativeElement.querySelector( '.row-action' ) ).toBeNull();
	} );
	//ModificationInfo in place of the flags:  what the edit did, when, and by whom (plan Phase 4)
	it( 'shows each modification\'s type, time and user in modified mode, with no row actions', ()=>{
		const edited = new Date( 2026, 9, 9, 12, 0, 0, 0 );
		const t = render( [at(A, 1000, 1, {modification: {time: edited, type: 'Replace', user: 'jde'}})], {modified: true, editable: [A]} );
		expect( t.headers() ).toEqual( ['Node', 'Source time', 'Server time', 'Status', 'Value', 'Edit', 'Edited', 'By'] );
		expect( t.cells( 0 ).slice( 5 ) ).toEqual( ['Replace', '2026-10-09 12:00:00.000', 'jde'] );
		expect( t.fixture.nativeElement.querySelector( '.row-action' ) ).toBeNull();
	} );
	//a Replace and a Delete on a value of a node this user may write, and none on another node's, a bound's or a timeless one's,
	//nor on a push's, whose time a Date cut to the millisecond (historian-web-edits #1)
	it( 'offers a Replace and a Delete on the values of the editable nodes alone', ()=>{
		const t = render( [at(A, 500, 5, {sourceTime: undefined}), at(A, 1000, 1), at(B, 2000, 2), at(A, 3000, 3, {bound: true}), {...at(A, 0, 4), source: null, sourceTime: undefined}], {editable: [A]} );
		expect( t.headers().at( -1 ) ).toBe( '' );
		expect( t.headers() ).toHaveLength( 7 );
		const actions = ( row:number )=>[...t.fixture.nativeElement.querySelectorAll( 'mat-row' )[row].querySelectorAll( '.row-action' )].map( b=>b.getAttribute( 'aria-label' ) );
		expect( actions( 0 ) ).toEqual( [] );//the bound at 3000
		expect( actions( 1 ) ).toEqual( [] );//B's
		expect( actions( 2 ) ).toEqual( ['Replace this value', 'Delete this value'] );//A at 1000
		expect( actions( 3 ) ).toEqual( [] );//a push
		expect( actions( 4 ) ).toEqual( [] );//no time
		const replaced:HistValue[] = [], removed:HistValue[] = [];
		t.table.replace.subscribe( v=>replaced.push( v ) );
		t.table.remove.subscribe( v=>removed.push( v ) );
		const [replace, remove] = t.fixture.nativeElement.querySelectorAll( 'mat-row' )[2].querySelectorAll( '.row-action' );
		replace.click();
		remove.click();
		expect( replaced.map( v=>v.source?.getTime() ) ).toEqual( [1000] );
		expect( removed.map( v=>v.source?.getTime() ) ).toEqual( [1000] );
	} );
	//a group's rights come apart:  Update for a Replace, Delete for a Delete (#335)
	it( 'offers each action as the rights allow', ()=>{
		const actions = ( t:ReturnType<typeof render> )=>[...t.fixture.nativeElement.querySelectorAll( '.row-action' )].map( b=>b.getAttribute( 'aria-label' ) );
		expect( actions( render( [at(A, 1000, 1)], {editable: [A], rights: {update: true, delete: false}} ) ) ).toEqual( ['Replace this value'] );
		expect( actions( render( [at(A, 1000, 1)], {editable: [A], rights: {update: false, delete: true}} ) ) ).toEqual( ['Delete this value'] );
	} );
} );

//reviews-issue/329-sticky-table-header.md #11:  the pager is pinned, as the lists' are, so a page turns from part-way down
describe( 'HistTable paging', ()=>{
	afterEach( ()=>vi.restoreAllMocks() );
	const values = Array.from( {length: 250}, (_, i)=>at(A, i*1000, i) );
	const open = ()=>({ ...render( values ), show: vi.spyOn( Paginator, 'showFirstRow' ).mockImplementation( ()=>{} ) });//the rows are real, and jsdom has no scrollIntoView

	it( 'turns the page from the table\'s first row, from the start it showed', ()=>{
		const t = open();
		t.table.onPage( {pageIndex: 1, pageSize: 100, previousPageIndex: 0, length: 250} );
		t.table.onPage( {pageIndex: 0, pageSize: 500, previousPageIndex: 1, length: 250} );
		expect( t.show.mock.calls.map( c=>c.slice(1) ) ).toEqual( [[0, 100], [100, 0]] );
		expect( t.show.mock.calls[0][0] ).toBe( t.fixture.nativeElement );
		expect( [t.table.pageIndex(), t.table.pageSize()] ).toEqual( [0, 500] );
	} );

	it( 'pins the header row', ()=>{
		const t = open();
		expect( t.fixture.nativeElement.querySelector( 'mat-header-row' ).classList ).toContain( 'mat-mdc-table-sticky' );
	} );
} );