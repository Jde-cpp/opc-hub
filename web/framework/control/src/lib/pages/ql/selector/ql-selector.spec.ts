import { signal } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { SelectionModel } from '@angular/cdk/collections';
import { ProfileStore } from 'jde-spa';
import { SnackbarService } from '../../../shared/snackbar/snackbar-service';
import { QLListData, QLListResolver } from '../../../services/ql-list-resolver';
import { QLRow } from '../../../model/ql/slug-row';
import { QLSelector } from './ql-selector';
import { profileStoreStub, provideRoute } from 'jde-spa/testing';
import { snackbarStub } from 'jde-framework/testing';
import { QLList } from '../list/ql-list';
import { GraphQLTable } from '../../graphql/table/graphql-table';

//reviews/m3-closing.md #2:  a re-query (a view switch, a filter shown then removed, a sort, a refresh) rebuilds the list's row
//selection from the list's OWN previous selection, so a member the previous rows hid came back shown and unchecked - and the
//rows→ids effect read that as the user unchecking it:  the owner's ids lost it and Save removed the member.
//The list here is a stand-in for QLList:  its data() signal, and requery() making QLList.init's two writes to the signals the
//selector watches - the rows its carriedSelection keeps checked, then the new data array, in one turn.
describe( 'QLSelector', ()=>{
	const rows = ( ...ids:number[] ):QLRow[]=>ids.map( id=>({id, slug: `u${id}`}) );//new objects every call, as every query builds
	//opens on `shown` with the owner holding `members` - the persisted view index may open the tab on a filtered view.
	const open = ( members:number[], shown:QLRow[] )=>{
		vi.spyOn( QLListResolver, 'data' ).mockReturnValue( new Promise(()=>{}) );//#load parks; the test hands the rows over below
		TestBed.configureTestingModule({ providers: [
			provideRoute(),
			{ provide: ProfileStore, useValue: profileStoreStub() },
			{ provide: SnackbarService, useValue: snackbarStub() }
		]});
		TestBed.overrideComponent( QLSelector, {set: {template: '', imports: []}} );
		const fixture = TestBed.createComponent( QLSelector );
		const selector = fixture.componentInstance;
		fixture.componentRef.setInput( 'type', 'User' );
		fixture.componentRef.setInput( 'ql', {} );
		fixture.componentRef.setInput( 'selections', new SelectionModel<number>(true, members) );
		fixture.detectChanges();//the load effect runs, and parks
		const data = signal<QLRow[]>( shown );
		(selector as any).list = signal( {data} );
		selector.listData.set( {results: {users: shown}} as unknown as QLListData );
		fixture.detectChanges();
		return {
			ownerIds: ()=>[...selector.selections().selected].sort(),
			checkedIds: ()=>selector.rowSelections().selected.map( r=>r.id ).sort(),
			requery: ( next:QLRow[] )=>{
				selector.rowSelections.set( QLList.carriedSelection(selector.rowSelections(), next, true) );
				data.set( next );
				fixture.detectChanges();
			},
			toggle: ( id:number )=>{//a row's checkbox:  a new SelectionModel over the same data array
				selector.rowSelections.set( GraphQLTable.toggled(selector.rowSelections(), data().find( r=>r.id==id )!) );
				fixture.detectChanges();
			}
		};
	};
	afterEach( ()=>vi.restoreAllMocks() );

	it( 'keeps a member a narrower view hid when a wider one reveals it', ()=>{
		const { ownerIds, checkedIds, requery } = open( [1, 2], rows(1, 2) );
		expect( checkedIds() ).toEqual( [1, 2] );
		requery( rows(1) );//Users - the certificate identity 2 is filtered out
		expect( ownerIds() ).toEqual( [1, 2] );
		requery( rows(1, 2) );//All
		expect( ownerIds() ).toEqual( [1, 2] );
		expect( checkedIds() ).toEqual( [1, 2] );
	} );

	it( 'keeps every member when the narrower view showed none of them', ()=>{//a Google-only group:  Certs, then All
		const { ownerIds, checkedIds, requery } = open( [1, 2], rows(1, 2) );
		requery( rows(3) );
		requery( rows(1, 2, 3) );
		expect( ownerIds() ).toEqual( [1, 2] );
		expect( checkedIds() ).toEqual( [1, 2] );
	} );

	it( 'keeps members when it opens on a filtered view', ()=>{//the view index is persisted per collection, shared with the list page
		const { ownerIds, checkedIds, requery } = open( [1, 2], rows(1) );
		expect( checkedIds() ).toEqual( [1] );
		requery( rows(1, 2) );
		expect( ownerIds() ).toEqual( [1, 2] );
		expect( checkedIds() ).toEqual( [1, 2] );
	} );

	it( 'keeps members across a filter shown and removed, and across a refresh that clears the rows first', ()=>{
		const { ownerIds, checkedIds, requery } = open( [1, 2], rows(1, 2, 3) );
		requery( [] );//onViewShow/onViewSave clear the rows before the query
		requery( rows(3) );
		requery( [] );
		requery( rows(1, 2, 3) );
		expect( ownerIds() ).toEqual( [1, 2] );
		expect( checkedIds() ).toEqual( [1, 2] );
	} );

	it( 'still takes a toggle as the user\'s word', ()=>{
		const { ownerIds, toggle } = open( [1, 2], rows(1, 2, 3) );
		toggle( 2 );
		expect( ownerIds() ).toEqual( [1] );
		toggle( 3 );
		expect( ownerIds() ).toEqual( [1, 3] );
	} );

	it( 'keeps an unsaved tick a narrower view hides, and shows it checked again', ()=>{
		const { ownerIds, checkedIds, requery, toggle } = open( [1], rows(1, 2, 3) );
		toggle( 3 );
		requery( rows(1) );
		expect( ownerIds() ).toEqual( [1, 3] );
		requery( rows(1, 2, 3) );
		expect( ownerIds() ).toEqual( [1, 3] );
		expect( checkedIds() ).toEqual( [1, 3] );
	} );
} );