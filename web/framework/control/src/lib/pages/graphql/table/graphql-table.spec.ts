import { signal } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { SelectionModel } from '@angular/cdk/collections';
import { SnackbarService } from '../../../shared/snackbar/snackbar-service';
import { Flex, Style, View } from '../../../model/ql/view';
import { CellText, GraphQLTable } from './graphql-table';
import { object, required, scalar, schemaOf, snackbarStub } from 'jde-framework/testing';
import { stable } from 'jde-spa/testing';

//The gateway's Connections list: a text column, and the grafted count that reads as a number.
const schema = schemaOf( "ServerConnection", required("name"), scalar("deleted", "DateTime"), object("opcSessions", "OpcSessions") );

const build = ( columns:any[], rows:any[] = [] )=>{
	TestBed.resetTestingModule();//a test may build two
	TestBed.configureTestingModule({ providers: [{ provide: SnackbarService, useValue: snackbarStub() }] });
	const fixture = TestBed.createComponent( GraphQLTable );
	fixture.componentRef.setInput( 'dataSource', rows );
	fixture.componentRef.setInput( 'displayedFields', View.fromTable({columns, sort: "name"}, schema).fields );
	fixture.componentRef.setInput( 'selections', new SelectionModel<any>(false, []) );
	return fixture;
};
const create = ( columns:any[] )=>build( columns ).componentInstance;

//A heading has to sit over its values.  Both cells are flex containers and the same columnStyle() output goes on each,
//so an alignment that only set text-align would leave the content wherever justify-content had already put it.
describe( 'GraphQLTable.columnStyle', ()=>{
	it( 'expands a right alignment to both flex and text', ()=>{
		const style = create( ["name", {name:"opcSessions", selection:"count", style: new Style({align:"right"})}] ).columnStyle( "opcSessions" );
		expect( style ).toEqual( {justifyContent: "flex-end", textAlign: "right"} );
	} );

	it( 'centres on center and falls back to the start for left', ()=>{
		const centred = create( [{name:"name", style: new Style({align:"center"})}] ).columnStyle( "name" );
		expect( centred ).toEqual( {justifyContent: "center", textAlign: "center"} );
		const left = create( [{name:"name", style: new Style({align:"left"})}] ).columnStyle( "name" );
		expect( left ).toEqual( {justifyContent: "flex-start", textAlign: "left"} );
	} );

	it( 'keeps the width alongside the alignment', ()=>{
		const style = create( [{name:"name", style: new Style({flex: new Flex(130), align:"right"})}] ).columnStyle( "name" );
		expect( style ).toEqual( {flex: "0 0 130px", justifyContent: "flex-end", textAlign: "right"} );
	} );

	it( 'is empty for a column with no style, so the stylesheet decides', ()=>{
		expect( create(["name"]).columnStyle("name") ).toEqual( {} );
	} );
} );

//The column names the tones and the stylesheet owns the colours, so a value the column did not list has to fall back to the
//theme's default chip rather than pick up someone else's colour.
describe( 'GraphQLTable chip columns', ()=>{
	const status = {name:"opcSessions", selection:"count", chip:{Connected:"ok", Idle:"neutral", Error:"error"} as const};
	it( 'maps each listed value to its tone', ()=>{
		const table = create( ["name", status] );
		expect( table.chipClass("opcSessions", {opcSessions:{name:"Connected"}} as any) ).toBe( "chip-ok" );
		expect( table.chipClass("opcSessions", {opcSessions:{name:"Idle"}} as any) ).toBe( "chip-neutral" );
		expect( table.chipClass("opcSessions", {opcSessions:{name:"Error"}} as any) ).toBe( "chip-error" );
	} );

	it( 'gives an unlisted or missing value no tone', ()=>{
		const table = create( ["name", status] );
		expect( table.chipClass("opcSessions", {opcSessions:{name:"Draining"}} as any) ).toBe( "" );
		expect( table.chipClass("opcSessions", {} as any) ).toBe( "" );
	} );

	it( 'puts the tone class on the rendered chip', async ()=>{
		const fixture = build( ["name", status], [{id: 1, name: "a", opcSessions: {name: "Connected"}}, {id: 2, name: "b", opcSessions: {name: "Draining"}}] );
		await stable( fixture, 1 );
		const chips = [...fixture.nativeElement.querySelectorAll( 'mat-chip' )] as HTMLElement[];
		expect( chips.map( c=>[...c.classList].find( n=>n.startsWith('chip-') ) ) ).toEqual( ["chip-ok", undefined] );
	} );

	it( 'gives a column with no chip setting no tone', ()=>{
		expect( create(["name"]).chipClass("name", {name:"Connected"} as any) ).toBe( "" );
	} );

	//Both buckets feed matColumnDef, so a chip column left in its original one is declared twice and the table throws.
	it( 'takes the column out of the bucket it would otherwise render in', ()=>{
		const table = create( ["name", status] );
		expect( table.columnNames().chip ).toEqual( ["opcSessions"] );
		expect( table.columnNames().object ).toEqual( [] );
		expect( table.columnNames().string ).toContain( "name" );
	} );

	it( 'leaves the buckets alone when nothing asks for a chip', ()=>{
		const table = create( ["name", {name:"opcSessions", selection:"count"}] );
		expect( table.columnNames().chip ).toEqual( [] );
		expect( table.columnNames().object ).toEqual( ["opcSessions"] );
	} );
} );

//The switch a route puts in the row for a table whose soft-delete flag is the feature (resources: a deleted resource is an
//unenforced one).  Rendered off the row and never off the control's own state, so a mutation that fails cannot leave the
//cell showing something the server never agreed to.
describe( 'GraphQLTable live-toggle columns', ()=>{
	const enforced = {name:"deleted", displayName:"Enforced", liveToggle:{enable:"Enforce", disable:"Stop enforcing"}};

	it( 'is on for a live row and off for a deleted one', ()=>{
		const table = create( ["name", enforced] );
		expect( table.isLive("deleted", {deleted:null} as any) ).toBe( true );
		expect( table.isLive("deleted", {} as any) ).toBe( true );
		expect( table.isLive("deleted", {deleted:new Date()} as any) ).toBe( false );
	} );

	it( 'labels the direction a click would take the row, not its current state', ()=>{
		const table = create( ["name", enforced] );
		expect( table.liveToggleLabel("deleted", {deleted:null} as any) ).toBe( "Stop enforcing" );
		expect( table.liveToggleLabel("deleted", {deleted:new Date()} as any) ).toBe( "Enforce" );
	} );

	it( 'has no label for a column that declared no toggle', ()=>{
		expect( create(["name"]).liveToggleLabel("name", {name:"a"} as any) ).toBe( "" );
	} );

	//As the chip bucket: both feed matColumnDef, so a switch column left among the dates is declared twice and mat-table throws.
	it( 'takes the column out of the date bucket', ()=>{
		const table = create( ["name", enforced] );
		expect( table.columnNames().liveToggle ).toEqual( ["deleted"] );
		expect( table.columnNames().date ).toEqual( [] );
	} );

	it( 'leaves deleted a date column when nothing asks for a switch', ()=>{
		const table = create( ["name", "deleted"] );
		expect( table.columnNames().liveToggle ).toEqual( [] );
		expect( table.columnNames().date ).toEqual( ["deleted"] );
	} );
} );


//A22:  the fields are classified once per change of displayedFields, not per change-detection pass and per cell - and each into
//one matColumnDef list at most, since a column declared twice throws.
describe( 'GraphQLTable columns', ()=>{
	it( 'puts a field in one list, a chip or a switch ahead of its type', ()=>{
		const table = create( ["name", {name:"deleted", chip:{x:"ok"}, liveToggle:{enable:"Enforce", disable:"Stop enforcing"}}, "opcSessions"] );
		expect( table.columnNames().chip ).toEqual( ["deleted"] );
		expect( Object.values(table.columnNames()).flat().sort() ).toEqual( ["deleted", "name", "opcSessions"] );
	} );

	it( 'hands a cell the same style each pass', ()=>{
		const table = create( [{name:"name", style: new Style({align:"right"})}, "deleted"] );
		expect( table.columnStyle("name") ).toBe( table.columnStyle("name") );
		expect( table.columnStyle("deleted") ).toBe( table.columnStyle("opcSessions") );//one empty style for the unstyled and the unknown
	} );

	it( 'renders the rows it is given', async ()=>{
		const fixture = build( ["name"], [{id: 1, name: "a"}, {id: 2, name: "b"}] );
		await stable( fixture, 1 );
		const cells = [...fixture.nativeElement.querySelectorAll( 'mat-cell' )].map( (c:Element)=>c.textContent!.trim() );
		expect( cells ).toEqual( ["a", "b"] );
	} );

	//a host's text for a cell, else the value:  one read from a signal redraws as the signal changes - a group's root, once placed
	it( "shows a host's text in place of the value", async ()=>{
		const placed = signal( new Map<string,string>() );
		const text:CellText = ( column, row )=>column=="name" ? placed().get( <string>row.name ) : undefined;
		const fixture = build( ["name"], [{id: 1, name: "a"}, {id: 2, name: "b"}] );
		fixture.componentRef.setInput( 'cellText', text );
		await stable( fixture, 1 );
		const cells = ()=>[...fixture.nativeElement.querySelectorAll( 'mat-cell' )].map( (c:Element)=>c.textContent!.trim() );
		expect( cells() ).toEqual( ["a", "b"] );
		placed.set( new Map( [["b", "Objects/b"]] ) );
		await stable( fixture, 1 );
		expect( cells() ).toEqual( ["a", "Objects/b"] );
	} );
} );