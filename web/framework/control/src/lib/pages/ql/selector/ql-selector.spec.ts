import { signal } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { SelectionModel } from '@angular/cdk/collections';
import { ProfileStore } from 'jde-spa';
import { SnackbarService } from '../../../shared/snackbar/snackbar-service';
import { QLListData, QLListResolver } from '../../../services/ql-list-resolver';
import { QLRow } from '../../../model/ql/slug-row';
import { QLSelector } from './ql-selector';
import { profileStoreStub, provideRoute } from 'jde-spa/testing';
import { scalar, slugSchema, snackbarStub } from 'jde-framework/testing';
import { View } from '../../../model/ql/view';
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

//#591:  a pick list's own terms, anded with whichever view it shows
describe( 'QLSelector.fixedFilter', ()=>{
	const deletable = slugSchema( "User", scalar("deleted", "DateTime") );
	const data = ( columns:(string|object)[], schema=deletable )=>{
		const view = View.fromTable( {columns: columns as string[], sort: "name"}, schema );
		return { data: {schema, profile: {routeDefault: view, current: ()=>view}, routing: {path: "users"}} as unknown as QLListData, view };
	};
	it( 'leaves out the ids it is given and the deleted rows', ()=>{
		expect( QLSelector.fixedFilter( data(["name"]).data, [3, 4] ) ).toBe( "-id:3,4 no:deleted" );
		expect( QLSelector.fixedFilter( data(["name"]).data, [] ) ).toBe( "no:deleted" );
	} );
	it( 'keeps the deleted rows of a live toggle, which are those switched off, and has none to leave out where there is no column', ()=>{
		const enforced = {name: "deleted", displayName: "Enforced", liveToggle: {enable: "Enforce", disable: "Stop enforcing"}};
		expect( QLSelector.fixedFilter( data(["name", enforced]).data, [3] ) ).toBe( "-id:3" );
		expect( QLSelector.fixedFilter( data(["name"], slugSchema("Role")).data, [] ) ).toBe( "" );
	} );
	it( 'lists nothing for has:deleted typed in its box', async ()=>{
		const { data: listData, view } = data( ["name"] );
		view.filter = "has:deleted";
		const query = vi.fn().mockResolvedValue( {users: []} );
		await QLListResolver.load( {query}, {...listData, fixedFilter: QLSelector.fixedFilter(listData, [3])}, null );
		const [text, vars] = query.mock.calls[0];
		expect( text ).toContain( "deleted:{in:$deleted_in,ne:null}" );//deleted, and not:  no row
		expect( text ).toContain( "id:{nin:$id_nin}" );
		expect( vars ).toMatchObject( {deleted_in: [null], id_nin: [3]} );
		expect( view.fixedFilter ).toBe( "" );//on a copy:  the view is the list page's too
	} );
} );