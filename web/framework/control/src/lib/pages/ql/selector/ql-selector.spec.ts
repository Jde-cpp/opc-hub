if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { signal, WritableSignal } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { ActivatedRoute } from '@angular/router';
import { SelectionModel } from '@angular/cdk/collections';
import { vi } from 'vitest';
import { ProfileStore } from 'jde-spa';
import { SnackbarService } from '../../../shared/snackbar/snackbar-service';
import { QLListData, QLListResolver } from '../../../services/ql-list-resolver';
import { QLRow } from '../../../model/ql/slug-row';
import { QLSelector } from './ql-selector';

//reviews/m3-closing.md #2:  a re-query (a view switch, a filter shown then removed, a sort, a refresh) rebuilds the list's row
//selection from the list's OWN previous selection, so a member the previous rows hid came back shown and unchecked - and the
//rows→ids effect read that as the user unchecking it:  the owner's ids lost it and Save removed the member.
//The list here is a stand-in for QLList:  its data() signal, and requery() doing what QLList.init does to the two signals the
//selector watches - rows checked only by the list's own prior checks, then the new data array, in one turn.
describe( 'QLSelector', ()=>{
	let selector:QLSelector;
	let data:WritableSignal<QLRow[]>;
	const rows = ( ...ids:number[] ):QLRow[]=>ids.map( id=>({id, slug: `u${id}`}) );//new objects every call, as every query builds
	const ownerIds = ()=>[...selector.selections().selected].sort();
	const checkedIds = ()=>selector.rowSelections().selected.map( r=>r.id ).sort();
	const requery = ( next:QLRow[] )=>{
		const priorIds = selector.rowSelections().selected.map( r=>r.id );
		selector.rowSelections.set( new SelectionModel<QLRow>(true, next.filter(r=>priorIds.includes(r.id))) );
		data.set( next );
		TestBed.tick();
	};
	const toggle = ( id:number )=>{//GraphQLTable.toggle:  a new SelectionModel over the same data array
		const current = selector.rowSelections().selected;
		const row = data().find( r=>r.id==id )!;
		selector.rowSelections.set( new SelectionModel<QLRow>(true, current.includes(row) ? current.filter(r=>r!=row) : [...current, row]) );
		TestBed.tick();
	};
	//opens on `shown` with the owner holding `members` - the persisted view index may open the tab on a filtered view.
	const open = ( members:number[], shown:QLRow[] )=>{
		vi.spyOn( QLListResolver, 'data' ).mockReturnValue( new Promise(()=>{}) );//#load parks; the test hands the rows over below
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({ providers: [
			{ provide: ActivatedRoute, useValue: {snapshot: {}} },
			{ provide: ProfileStore, useValue: {} },
			{ provide: SnackbarService, useValue: {exception: vi.fn()} }
		]});
		TestBed.overrideComponent( QLSelector, {set: {template: '', imports: []}} );
		const fixture = TestBed.createComponent( QLSelector );
		selector = fixture.componentInstance;
		fixture.componentRef.setInput( 'type', 'User' );
		fixture.componentRef.setInput( 'ql', {} );
		fixture.componentRef.setInput( 'selections', new SelectionModel<number>(true, members) );
		TestBed.tick();//the load effect runs, and parks
		data = signal<QLRow[]>( shown );
		(selector as any).list = signal( {data} );
		selector.listData.set( {results: {users: shown}} as unknown as QLListData );
		TestBed.tick();
	};
	afterEach( ()=>vi.restoreAllMocks() );

	it( 'keeps a member a narrower view hid when a wider one reveals it', ()=>{
		open( [1, 2], rows(1, 2) );
		expect( checkedIds() ).toEqual( [1, 2] );
		requery( rows(1) );//Users - the certificate identity 2 is filtered out
		expect( ownerIds() ).toEqual( [1, 2] );
		requery( rows(1, 2) );//All
		expect( ownerIds() ).toEqual( [1, 2] );
		expect( checkedIds() ).toEqual( [1, 2] );
	} );

	it( 'keeps every member when the narrower view showed none of them', ()=>{//a Google-only group:  Certs, then All
		open( [1, 2], rows(1, 2) );
		requery( rows(3) );
		requery( rows(1, 2, 3) );
		expect( ownerIds() ).toEqual( [1, 2] );
		expect( checkedIds() ).toEqual( [1, 2] );
	} );

	it( 'keeps members when it opens on a filtered view', ()=>{//the view index is persisted per collection, shared with the list page
		open( [1, 2], rows(1) );
		expect( checkedIds() ).toEqual( [1] );
		requery( rows(1, 2) );
		expect( ownerIds() ).toEqual( [1, 2] );
		expect( checkedIds() ).toEqual( [1, 2] );
	} );

	it( 'keeps members across a filter shown and removed, and across a refresh that clears the rows first', ()=>{
		open( [1, 2], rows(1, 2, 3) );
		requery( [] );//onViewShow/onViewSave clear the rows before the query
		requery( rows(3) );
		requery( [] );
		requery( rows(1, 2, 3) );
		expect( ownerIds() ).toEqual( [1, 2] );
		expect( checkedIds() ).toEqual( [1, 2] );
	} );

	it( 'still takes a toggle as the user\'s word', ()=>{
		open( [1, 2], rows(1, 2, 3) );
		toggle( 2 );
		expect( ownerIds() ).toEqual( [1] );
		toggle( 3 );
		expect( ownerIds() ).toEqual( [1, 3] );
	} );

	it( 'keeps an unsaved tick a narrower view hides, and shows it checked again', ()=>{
		open( [1], rows(1, 2, 3) );
		toggle( 3 );
		requery( rows(1) );
		expect( ownerIds() ).toEqual( [1, 3] );
		requery( rows(1, 2, 3) );
		expect( ownerIds() ).toEqual( [1, 3] );
		expect( checkedIds() ).toEqual( [1, 3] );
	} );
} );
