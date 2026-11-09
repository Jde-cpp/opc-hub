import { TestBed } from '@angular/core/testing';
import { View, ViewType } from '../../../../model/ql/view';
import { QLListSettings } from './ql-list-settings';
import { QLListSettingsDisplay } from './ql-list-settings-display/ql-list-settings-display';
import { id, required, scalar, schemaOf } from 'jde-framework/testing';
import { stable } from 'jde-spa/testing';

const schema = schemaOf( "User", id(), required("name"), scalar("slug"), scalar("deleted", "DateTime") );

//review3 C10: the View Name field was the workspace's last [(ngModel)].  It binds [value]/(input) against the `name`
//signal now - the same signal getView() already reads - so this pins that the round trip still works.
describe( 'QLListSettings view name', ()=>{
	const create = ()=>{
		const fixture = TestBed.createComponent( QLListSettings );
		const view = View.fromTable( {columns: ["name"], sort: "name"}, schema );
		view.name = "Mine";//the settings-shaped ctor arg carries no name; onViewSave assigns it the same way
		fixture.componentRef.setInput( 'view', view );
		fixture.componentRef.setInput( 'schema', schema );
		fixture.componentRef.setInput( 'columns', {name: "Name"} );
		fixture.componentRef.setInput( 'suggestions', {} );
		fixture.componentInstance.ngOnInit();
		fixture.detectChanges();
		return fixture;
	};

	const input = ( fixture:any ):HTMLInputElement=>fixture.nativeElement.querySelector( 'input[matInput]' );

	it( 'renders the current name', ()=>{
		expect( input(create()).value ).toBe( "Mine" );
	} );

	it( 'writes typing back into the signal', ()=>{
		const fixture = create();
		const el = input( fixture );
		el.value = "Renamed";
		el.dispatchEvent( new Event('input') );
		expect( fixture.componentInstance.name() ).toBe( "Renamed" );//what [(ngModel)] used to do
	} );

	it( 'selects the text on focus', ()=>{
		const fixture = create();
		const el = input( fixture );
		el.dispatchEvent( new FocusEvent('focus') );
		expect( el.selectionStart ).toBe( 0 );
		expect( el.selectionEnd ).toBe( "Mine".length );
	} );
} );

//#587:  a view is known by its number and names repeat, so no name is reserved (reviews-issue/317-deleted-rows.md #10,
//reviews-issue/web-refactor-review.md #3).  Save writes a view over its number, and a view under another name is a new one.
describe( 'QLListSettings Save and Delete', ()=>{
	const create = ( name:string|undefined, type:ViewType, number=2 )=>{
		const fixture = TestBed.createComponent( QLListSettings );
		const view = View.fromTable( {columns: ["name"], sort: "name"}, schema );
		view.name = name;
		view.type = type;
		view.number = number;
		fixture.componentRef.setInput( 'view', view );
		fixture.componentRef.setInput( 'schema', schema );
		fixture.componentRef.setInput( 'columns', {name: "Name"} );
		fixture.componentRef.setInput( 'suggestions', {} );
		fixture.componentInstance.ngOnInit();
		fixture.detectChanges();
		return fixture.componentInstance;
	};
	const saved = ( settings:QLListSettings, name?:string )=>{
		if( name!==undefined )
			settings.name.set( name );
		const view = settings.getView( false );
		return [view.number, view.name];
	};

	it( "saves a view the list came with over its number, a built-in's name and all", ()=>{
		expect( saved(create("Deleted", ViewType.System)) ).toEqual( [2, "Deleted"] );
	} );
	it( "saves a twin over its view", ()=>{
		expect( saved(create("Users", ViewType.Adhoc)) ).toEqual( [2, "Users"] );
	} );
	it( 'leaves an unnamed view unnamed, twin or not', ()=>{
		expect( saved(create(undefined, ViewType.System)) ).toEqual( [2, undefined] );
		expect( saved(create(undefined, ViewType.Adhoc)) ).toEqual( [2, undefined] );
	} );
	it( "saves under another name as a new view, another view's name or not", ()=>{
		expect( saved(create(undefined, ViewType.System), "Deleted") ).toEqual( [0, "Deleted"] );
		expect( saved(create("Mine", ViewType.User), "Yours") ).toEqual( [0, "Yours"] );
	} );
	it( 'shows a view under its number, whatever the name box says', ()=>{
		const settings = create( "Mine", ViewType.User );
		settings.name.set( "Yours" );
		const shown = settings.getView( true );
		expect( [shown.number, shown.name] ).toEqual( [2, "Mine"] );
	} );
	it( "deletes any view of the set, and not one under no number", ()=>{
		expect( create("Deleted", ViewType.System).disableDelete() ).toBe( false );
		expect( create("Users", ViewType.Adhoc).disableDelete() ).toBe( false );
		expect( create(undefined, ViewType.System, 0).disableDelete() ).toBe( true );//the default a pick list falls back on
	} );
} );

//reviews-issue/231-paging.md #15:  a node's Children are not read a page at a time, so their Display tab offers no Page Size
describe( 'QLListSettingsDisplay Page Size', ()=>{
	const create = ( paged?:boolean )=>{
		const fixture = TestBed.createComponent( QLListSettingsDisplay );
		const view = View.fromTable( {columns: ["name"], sort: "name"}, schema );
		view.limit = 40;
		fixture.componentRef.setInput( 'view', view );
		fixture.componentRef.setInput( 'schema', schema );
		fixture.componentRef.setInput( 'columns', {name: "Name"} );
		if( paged!==undefined )
			fixture.componentRef.setInput( 'paged', paged );
		fixture.detectChanges();
		return fixture;
	};
	const pageSize = ( fixture:any ):HTMLInputElement|null=>fixture.nativeElement.querySelector( '.page-size input' );

	it( 'is offered where the host pages', ()=>{
		expect( pageSize(create())?.value ).toBe( "40" );
	} );

	it( 'is not offered where it does not, and the view keeps its limit', ()=>{
		const fixture = create( false );
		expect( pageSize(fixture) ).toBeNull();
		expect( fixture.componentInstance.pageSize() ).toBe( 40 );//what QLListSettings.getView writes back
	} );
} );

//A24:  the three tabs and their tables are signal queries - getView reads every tab, and a row added on one re-renders its table.
describe( 'QLListSettings tabs', ()=>{
	it( 'builds the view from all three tabs', async ()=>{
		TestBed.configureTestingModule({});
		const fixture = TestBed.createComponent( QLListSettings );
		fixture.componentRef.setInput( 'view', View.fromTable( {columns: ["name", "slug"], sort: "name"}, schema ) );
		fixture.componentRef.setInput( 'schema', schema );
		fixture.componentRef.setInput( 'columns', {name: "Name", slug: "Slug"} );
		fixture.componentRef.setInput( 'suggestions', {} );
		const settings = fixture.componentInstance;
		settings.ngOnInit();
		await stable( fixture, 1 );
		settings.filter().onAddFilter( "slug" );
		const sort = settings.sort();
		sort.updateSort( sort.dataSource.at(-1)!, "slug" );
		fixture.detectChanges();
		const built = settings.getView( true );
		expect( built.fieldFilters.map( f=>f.field.name ) ).toEqual( ["slug"] );
		expect( built.sort.map( s=>s.active ) ).toEqual( ["name", "slug"] );
		expect( built.fields.map( f=>f.name ) ).toEqual( expect.arrayContaining(["name", "slug"]) );
	} );
} );