import { TestBed } from '@angular/core/testing';
import { View, ViewType } from '../../../../model/ql/view';
import { QLListSettings } from './ql-list-settings';
import { QLListSettingsDisplay } from './ql-list-settings-display/ql-list-settings-display';
import { id, required, scalar, schemaOf } from 'jde-framework/testing';
import { stable } from 'jde-spa/testing';

const schema = schemaOf( "User", id(), required("name"), scalar("slug"), scalar("deleted", "DateTime") );

//review3 C10: the View Name field was the workspace's last [(ngModel)].  It binds [value]/(input) against the `name`
//signal now - the same signal disableSave() and getView() already read - so this pins that the round trip still works.
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

//reviews-issue/317-deleted-rows.md #10:  the Default view saved as "Deleted" stayed a System view, replaced the built-in Deleted
//view for the session and was never persisted.  A host's system view names are reserved now.
describe( 'QLListSettings reserved names', ()=>{
	const create = ( name:string|undefined, type:ViewType )=>{
		const fixture = TestBed.createComponent( QLListSettings );
		const view = View.fromTable( {columns: ["name"], sort: "name"}, schema );
		view.name = name;
		view.type = type;
		fixture.componentRef.setInput( 'view', view );
		fixture.componentRef.setInput( 'schema', schema );
		fixture.componentRef.setInput( 'columns', {name: "Name"} );
		fixture.componentRef.setInput( 'suggestions', {} );
		fixture.componentRef.setInput( 'reservedNames', ["Users", "Deleted"] );
		fixture.componentInstance.ngOnInit();
		fixture.detectChanges();
		return fixture;
	};
	const rename = ( fixture:any, name:string )=>{
		fixture.componentInstance.name.set( name );
		fixture.detectChanges();
		return fixture;
	};
	const hint = ( fixture:any ):string|undefined=>fixture.nativeElement.querySelector( 'mat-hint' )?.textContent.trim();

	it( 'will not save under a system view\'s name, and says why', ()=>{
		const fixture = rename( create(undefined, ViewType.System), "Deleted" );
		expect( fixture.componentInstance.disableSave() ).toBe( true );
		expect( hint(fixture) ).toBe( "Deleted is a built-in view" );
	} );
	it( 'saves under any other name', ()=>{
		const fixture = rename( create(undefined, ViewType.System), "Mine" );
		expect( fixture.componentInstance.disableSave() ).toBe( false );
		expect( hint(fixture) ).toBeUndefined();
	} );
	//reviews-issue/web-refactor-review.md #3:  Save on the twin of the System view Users, its name untouched, was live, and made a User view
	//of the name
	it( "will not save a system view's twin under its name, and says why", ()=>{
		const fixture = create( "Users", ViewType.Adhoc );
		expect( fixture.componentInstance.disableSave() ).toBe( true );
		expect( hint(fixture) ).toBe( "Users is a built-in view" );
		expect( rename( fixture, "Mine" ).componentInstance.disableSave() ).toBe( false );
	} );
	it( 'still saves a user view under the name it already has', ()=>{
		const fixture = rename( create("Deleted", ViewType.User), "Deleted" );
		expect( fixture.componentInstance.reservedName() ).toBe( false );
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