if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { TestBed } from '@angular/core/testing';
import { TableSchema } from '../../../../model/ql/schema/table-schema';
import { View, ViewType } from '../../../../model/ql/view';
import { QLListSettings } from './ql-list-settings';
import { QLListSettingsDisplay } from './ql-list-settings-display/ql-list-settings-display';

const schema = new TableSchema( { name: "User", fields: [
	{ name: "id", type: { kind: "NON_NULL", name: null, ofType: { kind: "SCALAR", name: "ID" } } },
	{ name: "name", type: { kind: "NON_NULL", name: null, ofType: { kind: "SCALAR", name: "String" } } },
	{ name: "slug", type: { kind: "SCALAR", name: "String" } },
	{ name: "deleted", type: { kind: "SCALAR", name: "DateTime" } }
] } );

//review3 C10: the View Name field was the workspace's last [(ngModel)].  It binds [value]/(input) against the `name`
//signal now - the same signal disableSave() and getView() already read - so this pins that the round trip still works.
describe( 'QLListSettings view name', ()=>{
	const create = ()=>{
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({});
		const fixture = TestBed.createComponent( QLListSettings );
		const view = new View( {columns: ["name"], sort: "name"}, schema );
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
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({});
		const fixture = TestBed.createComponent( QLListSettings );
		const view = new View( {columns: ["name"], sort: "name"}, schema );
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
	it( 'still saves a user view under the name it already has', ()=>{
		const fixture = rename( create("Deleted", ViewType.User), "Deleted" );
		expect( fixture.componentInstance.reservedName() ).toBe( false );
	} );
} );

//reviews-issue/231-paging.md #15:  a node's Children are not read a page at a time, so their Display tab offers no Page Size
describe( 'QLListSettingsDisplay Page Size', ()=>{
	const create = ( paged?:boolean )=>{
		TestBed.resetTestingModule();
		TestBed.configureTestingModule({});
		const fixture = TestBed.createComponent( QLListSettingsDisplay );
		const view = new View( {columns: ["name"], sort: "name"}, schema );
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