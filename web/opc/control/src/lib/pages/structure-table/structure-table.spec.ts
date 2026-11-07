import { TestBed } from '@angular/core/testing';
import Long from 'long';
import { NodeId } from '../../model/node-id';
import { ExtensionObject, Value } from '../../model/value';
import { StructureTable } from './structure-table';

//clients plan 8b (#422):  a structure in a cell is a table of its members by name, in the type's order, a structure member a table inside
//it, an array of structures one per element, and any other member as valueString spells it;  the cell's own may be an array of them or a
//body no type explains too (reviews-issue/422-structures.md #9, #13)
const render = ( value:Value, label?:string )=>{
	const fixture = TestBed.createComponent( StructureTable );
	fixture.componentRef.setInput( 'value', value );
	if( label!==undefined )
		fixture.componentRef.setInput( 'label', label );
	fixture.detectChanges();
	const table:HTMLTableElement = fixture.nativeElement.querySelector( 'table' );
	const rows = ( t:Element=table )=>[...t.querySelectorAll( ':scope > tbody > tr' )].map( r=>[r.querySelector('th')!.textContent!.trim(), r.querySelector(':scope > td')!.textContent!.trim().replace( /\s+/g, ' ' )] );
	return { fixture, table, rows };
};

describe( 'StructureTable', ()=>{
	it( 'lists the members in order, each as text', ()=>{
		const big = Long.fromString( "9007199254740993" );
		const t = render( {Rpm: 1480.5, Running: true, Label: "sample", Count: big, History: [1, 2, 3], Note: null}, "sample" );
		expect( t.rows() ).toEqual( [['Rpm', '1480.5'], ['Running', 'true'], ['Label', 'sample'], ['Count', '9007199254740993'], ['History', '1, 2, 3'], ['Note', '']] );
		expect( t.table.getAttribute( 'aria-label' ) ).toBe( 'sample members' );
		expect( render({X: 1}).table.getAttribute( 'aria-label' ) ).toBe( 'Structure members' );
	} );
	it( 'nests a structure member as a table of its own, and an array of structures one per element', ()=>{
		const t = render( {Limits: {Low: 0, High: 3000}, Samples: [{X: 1, Name: "a"}, {X: 2, Name: "b"}]} );
		const [limits, samples] = [...t.table.querySelectorAll( ':scope > tbody > tr > td' )];
		expect( t.rows(limits.querySelector('structure-table > table')!) ).toEqual( [['Low', '0'], ['High', '3000']] );
		const tables = [...samples.querySelectorAll( ':scope > structure-table > structure-table > table' )];
		expect( tables.length ).toBe( 2 );
		expect( t.rows(tables[1]) ).toEqual( [['X', '2'], ['Name', 'b']] );
		expect( tables[1].getAttribute( 'aria-label' ) ).toBe( 'Samples 1 members' );
	} );
	it( 'says a member no type explains is undecoded, with the reason as its tooltip', ()=>{
		const t = render( {Inner: new ExtensionObject( new NodeId({ns: 2, i: 3010}), "AAAA" )} );
		expect( t.rows() ).toEqual( [['Inner', 'undecoded ns=2;i=3010']] );
		const span = t.table.querySelector( '.undecoded' )!;
		expect( span ).not.toBeNull();
		expect( span.textContent ).not.toContain( 'AAAA' );
		expect( span.getAttribute( 'tabindex' ) ).toBe( '0' );//a keyboard reaches the reason too
	} );
	it( 'shows the cell\'s own array of structures as a table each, and its own undecoded body as such', ()=>{
		const array = render( [{X: 1}, {X: 2}], "samples" );
		const tables = [...array.fixture.nativeElement.querySelectorAll( ':scope > structure-table > table' )];
		expect( tables.map( (t:Element)=>t.getAttribute('aria-label') ) ).toEqual( ['samples 0 members', 'samples 1 members'] );
		expect( array.rows(tables[1]) ).toEqual( [['X', '2']] );
		expect( array.fixture.nativeElement.classList ).toContain( 'top' );//the cell's own takes its width
		expect( tables[0].parentElement.classList ).not.toContain( 'top' );
		const opaque = render( new ExtensionObject(new NodeId({ns: 2, i: 3010})) );
		expect( opaque.table ).toBeNull();
		expect( opaque.fixture.nativeElement.querySelector( '.undecoded' ).textContent ).toBe( 'undecoded ns=2;i=3010' );
		expect( StructureTable.shows(3) || StructureTable.shows([1]) || StructureTable.shows({ns: 2, i: 5}) ).toBe( false );
	} );
} );