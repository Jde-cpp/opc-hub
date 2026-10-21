import { TestBed } from '@angular/core/testing';
import { Days, Operator, View } from '../../../../../model/ql/view';
import { QLListSettingsFilter } from './ql-list-settings-filter';
import { scalar, slugSchema } from 'jde-framework/testing';
import { stable } from 'jde-spa/testing';

const schema = slugSchema( "Resource", scalar("deleted", "DateTime") );
const liveToggle = { enable: "Enforce", disable: "Stop enforcing" };

//reviews/m3-closing.md #35:  #16's View.query cells passed on clean one-element arrays, but the dialog's date input and its two
//boxes shared one value array, and a reopened filter showed both boxes clear - so ticking one added a second marker, and the
//query went out as `{gt:"<null>"}` / an unbound $deleted ("Query failed.").  These drive the dialog's handlers into query().
describe( 'QLListSettingsFilter on a DateTime column', ()=>{
	function open( value:any[], settings:{liveToggle?:typeof liveToggle} = {liveToggle} ){
		const view = View.fromTable( {columns: ["name", {name: "deleted", displayName: "Enforced", ...settings}], sort: "name"}, schema );
		view.fieldFilters.push( {field: schema.fields.find( f=>f.name=="deleted" )!, filter: {operator: Operator.Greater, value}} );
		const fixture = TestBed.createComponent( QLListSettingsFilter );
		fixture.componentRef.setInput( "view", view );
		fixture.componentRef.setInput( "columns", {name: "Name", deleted: "Enforced"} );
		fixture.componentRef.setInput( "schema", schema );
		fixture.componentRef.setInput( "suggestions", {} );
		fixture.componentRef.setInput( "excludedColumns", [] );
		const dialog = fixture.componentInstance;
		dialog.ngOnInit();
		const col = dialog.dataSource[0];
		//the view QLListSettings.getView builds from the dialog
		const query = ()=>{
			const v = View.copy( view );
			v.fieldFilters = dialog.fieldFilters();
			return v.query( 0 );
		};
		return { dialog, col, query };
	}
	const isNull = ( q:{text:string, vars:any} )=>q.text.includes( "deleted:$deleted" ) && JSON.stringify( q.vars["deleted"] )=="[null]";
	const isNotNull = ( q:{text:string, vars:any} )=>q.text.includes( 'deleted:{"ne":null}' ) && !("deleted" in q.vars);

	it( 'shows a saved filter as ticked, so a toggle replaces it', ()=>{
		const { dialog, col, query } = open( ["<null>"] );
		expect( col.nullCriteria!() ).not.toBe( dialog.NullCriteria.None );
		dialog.onNonNullToggle( true, col );
		expect( isNotNull(query()) ).toBe( true );
	} );

	it( 'sends a well-formed query at every step of toggling between the two boxes', ()=>{
		const { dialog, col, query } = open( [] );
		dialog.onNullToggle( true, col );     expect( isNull(query()) ).toBe( true );
		dialog.onNonNullToggle( true, col );  expect( isNotNull(query()) ).toBe( true );
		dialog.onNullToggle( true, col );     expect( isNull(query()) ).toBe( true );
		dialog.onNullToggle( false, col );    expect( query().text ).not.toContain( "deleted:" );
		dialog.onNonNullToggle( true, col );  expect( isNotNull(query()) ).toBe( true );
	} );

	it( 'keeps a date and a box exclusive', ()=>{
		const { dialog, col, query } = open( [], {} );
		dialog.onChangeDate( {value: new Date()} as any, col );
		dialog.onNullToggle( true, col );
		expect( col.filter.value ).toEqual( ["<null>"] );
		dialog.onChangeDate( {value: new Date()} as any, col );
		expect( col.filter.value.length ).toBe( 1 );
		expect( col.filter.value[0] ).toBeInstanceOf( Days );
		expect( col.nullCriteria!() ).toBe( dialog.NullCriteria.None );
		expect( query().text ).toContain( "deleted:{gt:$deleted}" );//on a trash-can column the filter replaces the implicit not-deleted
	} );

	it( 'labels the boxes by what they mean under a live-toggle column', ()=>{
		const live = open( [] );
		expect( live.dialog.nullLabel(live.col) ).toBe( "Enforced" );
		expect( live.dialog.nonNullLabel(live.col) ).toBe( "Not enforced" );
		const plain = open( [], {} );
		expect( plain.dialog.nullLabel(plain.col) ).toBe( "Not deleted" );//and as a trash can, by the same words
		expect( plain.dialog.nonNullLabel(plain.col) ).toBe( "Deleted" );
	} );
} );

//A15:  each row keeps its own chip input, and Enter in a row's box adds the value and clears that box - the element used to be
//found by counting map keys up to the row's field.
describe( 'QLListSettingsFilter on a text column', ()=>{
	it( 'clears the box a value was added from, and stops offering that value', async ()=>{
		const view = View.fromTable( {columns: ["name", "slug"], sort: "name"}, schema );
		for( const name of ["name", "slug"] )
			view.fieldFilters.push( {field: schema.fields.find( f=>f.name==name )!, filter: {operator: Operator.In, value: []}} );
		const fixture = TestBed.createComponent( QLListSettingsFilter );
		fixture.componentRef.setInput( "view", view );
		fixture.componentRef.setInput( "columns", {name: "Name", slug: "Slug"} );
		fixture.componentRef.setInput( "schema", schema );
		fixture.componentRef.setInput( "suggestions", {name: ["a", "b"], slug: ["x", "y"]} );
		fixture.componentRef.setInput( "excludedColumns", [] );
		await stable( fixture, 1 );
		const dialog = fixture.componentInstance;
		const slug = dialog.dataSource[1];
		const box = fixture.nativeElement.querySelectorAll( 'input.filter-text' )[1] as HTMLInputElement;
		box.value = "x";
		box.dispatchEvent( new Event('input') );
		expect( slug.input.value ).toBe( "x" );
		box.dispatchEvent( new KeyboardEvent('keydown', {keyCode: 13, bubbles: true} as KeyboardEventInit) );//ENTER, a separator key
		await stable( fixture, 1 );
		expect( box.value ).toBe( "" );
		expect( slug.filter.value ).toEqual( ["x"] );
		expect( slug.suggestions.value ).toEqual( ["y"] );
		expect( dialog.dataSource[0].suggestions.value ).toEqual( ["a", "b"] );
	} );
} );