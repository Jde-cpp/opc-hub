import { TestBed } from '@angular/core/testing';
import { describe, it, expect } from 'vitest';
import { Days, Operator, View } from '../../../../../model/ql/view';
import { TableSchema } from '../../../../../model/ql/schema/table-schema';
import { QLListSettingsFilter } from './ql-list-settings-filter';

const schema = new TableSchema( {
	name: "Resource",
	fields: [
		{ name: "id", type: { kind: "NON_NULL", name: null, ofType: { kind: "SCALAR", name: "ID" } } },
		{ name: "name", type: { kind: "NON_NULL", name: null, ofType: { kind: "SCALAR", name: "String" } } },
		{ name: "slug", type: { kind: "NON_NULL", name: null, ofType: { kind: "SCALAR", name: "String" } } },
		{ name: "deleted", type: { kind: "SCALAR", name: "DateTime" } }
	]
} );
const liveToggle = { enable: "Enforce", disable: "Stop enforcing" };

//reviews/m3-closing.md #35:  #16's View.query cells passed on clean one-element arrays, but the dialog's date input and its two
//boxes shared one value array, and a reopened filter showed both boxes clear - so ticking one added a second marker, and the
//query went out as `{gt:"<null>"}` / an unbound $deleted ("Query failed.").  These drive the dialog's handlers into query().
describe( 'QLListSettingsFilter on a DateTime column', ()=>{
	function open( value:any[], settings:{liveToggle?:typeof liveToggle} = {liveToggle} ){
		const view = new View( {columns: ["name", {name: "deleted", displayName: "Enforced", ...settings}], sort: "name"}, schema );
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
		//what QLListSettings.getView does with the dialog's rows
		const query = ( showDeleted=true )=>{
			const v = new View( view );
			v.fieldFilters = dialog.dataSource.filter( c=>c.field ).map( c=>({field: c.field, filter: View.copyFilter(c.filter)}) );
			return v.query( showDeleted, 0 );
		};
		return { dialog, col, query };
	}
	const isNull = ( q:{text:string, vars:any} )=>q.text.includes( "deleted:$deleted" ) && JSON.stringify( q.vars["deleted"] )=="[null]";
	const isNotNull = ( q:{text:string, vars:any} )=>q.text.includes( 'deleted:{"ne":null}' ) && !("deleted" in q.vars);

	it( 'shows a saved filter as ticked, so a toggle replaces it', ()=>{
		const { dialog, col, query } = open( ["<null>"] );
		expect( dialog.nullSignal("deleted")() ).not.toBe( dialog.NullCriteria.None );
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
		expect( dialog.nullSignal("deleted")() ).toBe( dialog.NullCriteria.None );
		expect( query(false).text ).toContain( "deleted:{gt:$deleted}" );//Show deleted off:  on a trash-can column it wins over the filter
	} );

	it( 'labels the boxes by what they mean under a live-toggle column', ()=>{
		const live = open( [] );
		expect( live.dialog.nullLabel(live.col) ).toBe( "Enforced" );
		expect( live.dialog.nonNullLabel(live.col) ).toBe( "Not enforced" );
		const plain = open( [], {} );
		expect( plain.dialog.nullLabel(plain.col) ).toBe( "null" );
		expect( plain.dialog.nonNullLabel(plain.col) ).toBe( "not null" );
	} );
} );

//A saved view from before the fix can still hold what the dialog used to build.
describe( 'View.query on a DateTime filter the old dialog built', ()=>{
	const query = ( value:any[] )=>{
		const view = new View( {columns: ["name", {name: "deleted", liveToggle}], sort: "name"}, schema );
		view.fieldFilters.push( {field: schema.fields.find( f=>f.name=="deleted" )!, filter: {operator: Operator.Greater, value}} );
		return view.query( true, 0 );
	};
	it( 'reads a doubled marker as the marker', ()=>{
		expect( query(["<null>", "<null>"]).vars["deleted"] ).toEqual( [null] );
		expect( query(["<not null>", "<not null>"]).text ).toContain( 'deleted:{"ne":null}' );
	} );
	it( 'never sends a comparison without a value', ()=>{
		expect( query([undefined]).text ).not.toContain( "deleted:" );
		expect( query([null]).text ).not.toContain( "deleted:" );
	} );
} );
