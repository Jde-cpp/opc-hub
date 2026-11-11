import { Component, signal } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { LocalPrefs, ProfileStore } from 'jde-spa';
import { SliceCount } from '../../model/ql/slice';
import { TableSchema } from '../../model/ql/schema/table-schema';
import { View } from '../../model/ql/view';
import { ViewSet } from '../../model/ql/view-set';
import { SlicePanel } from './slice-panel';
import { screenSize, stable } from 'jde-spa/testing';
import { id, required, scalar } from 'jde-framework/testing';

//#595:  the slice panel - the column's name, All and a row per value with its count;  a click narrows, and a second returns to All
const schema = new TableSchema( {name: "Thing", enums: new Map( [["Level", [{id: 1, name: "Debug"}, {id: 2, name: "Warning"}, {id: 3, name: "Critical"}]]] ),
	fields: [id(), required("name"), {name: "level", type: {kind: "ENUM", name: "Level"}}, scalar("size", "UInt")]} );

@Component({
	imports: [SlicePanel],
	template: `<slice-panel [views]="views" [columns]="columns" [counts]="counts()" [failed]="failed()" (viewChange)="changes=changes+1"/>`
})
class Host{
	views:ViewSet<View> = null as any;//before the first render
	columns = {name: "Name", level: "Level", size: "Size"};
	counts = signal<SliceCount[]|undefined>( [{value: "Critical", count: 3}, {value: "Debug", count: 4}, {value: null, count: 1}] );
	failed = signal( false );
	changes = 0;
}

describe( 'SlicePanel', ()=>{
	const open = async ( {sliceBy="level", width='wide'}:{sliceBy?:string; width?:'wide'|'small'|'extra-small'}={} )=>{
		TestBed.configureTestingModule( {providers: [screenSize( width )]} );
		const set = new ViewSet( {type: View, key: "things", profileStore: TestBed.inject( ProfileStore ), prefs: new LocalPrefs()} );
		set.reset( [View.fromTable({columns: ["name", "level"], sort: "name", sliceBy: sliceBy || undefined}, schema)], schema );
		const fixture = TestBed.createComponent( Host );
		fixture.componentInstance.views = set;
		await stable( fixture, 1 );
		const element = fixture.nativeElement as HTMLElement;
		const text = ( e:Element )=>{
			const copy = e.cloneNode( true ) as Element;
			copy.querySelectorAll( 'mat-icon' ).forEach( i=>i.remove() );
			return copy.textContent!.replace( /\s+/g, " " ).trim();
		};
		const rows = ()=>[...element.querySelectorAll<HTMLButtonElement>( 'button.slice-row' )];
		const read = ()=>rows().map( r=>[text( r.querySelector('.label')! ), text( r.querySelector('.count')! ), r.getAttribute( "aria-pressed" )] );
		const click = async ( label:string )=>{
			rows().find( r=>text( r.querySelector('.label')! )==label )!.click();
			await stable( fixture, 1 );
		};
		const menu = async ( trigger:HTMLElement )=>{
			trigger.click();
			await stable( fixture, 1 );
			return [...[...document.querySelectorAll( '.mat-mdc-menu-panel' )].at( -1 )!.querySelectorAll<HTMLButtonElement>( '.mat-mdc-menu-item' )];
		};
		return { fixture, host: fixture.componentInstance, set, element, text, read, click, menu };
	};
	beforeEach( ()=>localStorage.clear() );

	it( "lists All with the list's count, then a row per value in the enum's order with its count, the rows with none last", async ()=>{
		const { read, element } = await open();
		expect( read() ).toEqual( [["All", "8", "true"], ["Debug", "4", "false"], ["Critical", "3", "false"], ["No Level", "1", "false"]] );
		expect( element.querySelector( 'section' )!.getAttribute( "aria-label" ) ).toBe( "Slice by Level" );
	} );

	it( 'narrows the list to the value clicked, and returns to All on the second click or on All', async ()=>{
		const { read, click, set, host } = await open();
		await click( "Critical" );
		expect( [set.slice(), host.changes] ).toEqual( ["Critical", 1] );
		expect( read().map( r=>r[2] ) ).toEqual( ["false", "false", "true", "false"] );
		await click( "Critical" );
		expect( [set.slice(), host.changes] ).toEqual( [undefined, 2] );
		await click( "No Level" );
		expect( set.slice() ).toBeNull();
		await click( "All" );
		expect( [set.slice(), host.changes] ).toEqual( [undefined, 4] );
		await click( "All" );
		expect( host.changes ).toBe( 4 );//as it was:  nothing to re-read
	} );

	it( 'keeps the value held on screen where the counts no longer hold it, and shows no count until they are read', async ()=>{
		const { read, click, host, fixture, element } = await open();
		await click( "Critical" );
		host.counts.set( [{value: "Debug", count: 2}] );//a filter typed
		await stable( fixture, 1 );
		expect( read() ).toEqual( [["All", "2", "false"], ["Debug", "2", "false"], ["Critical", "0", "true"]] );
		host.counts.set( undefined );
		host.failed.set( true );
		await stable( fixture, 1 );
		expect( read() ).toEqual( [["All", "", "false"], ["Critical", "", "true"]] );
		expect( element.querySelector( '.note' )!.textContent ).toContain( "could not be read" );
	} );

	it( "switches the column from its name, a change to the view's draft, and goes with No slicing", async ()=>{
		const { menu, element, text, set, host, fixture } = await open();
		const entries = await menu( element.querySelector<HTMLElement>( 'button.slice-field' )! );
		expect( entries.map( e=>[text( e ), e.getAttribute( "aria-checked" )] ) ).toEqual( [["Level", "true"], ["Size", "false"], ["No slicing", "false"]] );
		entries[1].click();
		await stable( fixture, 1 );
		expect( [set.current().sliceBy, set.current().isAdhoc, host.changes] ).toEqual( ["size", true, 1] );
		( await menu( element.querySelector<HTMLElement>( 'button.slice-field' )! ) ).at( -1 )!.click();
		await stable( fixture, 1 );
		expect( [set.current().sliceBy, element.querySelector( 'section' ), getComputedStyle( element.querySelector('slice-panel')! ).display] ).toEqual( [undefined, null, "none"] );
	} );

	it( 'is nothing where the view is not sliced', async ()=>{
		const { element } = await open( {sliceBy: ""} );
		expect( element.querySelector( 'slice-panel' )!.children.length ).toBe( 0 );
	} );

	it( 'folds to a Slice button on a small screen, its menu the same rows', async ()=>{
		const { element, menu, text, set, fixture } = await open( {width: 'small'} );
		expect( element.querySelector( 'section' ) ).toBeNull();
		const button = element.querySelector<HTMLButtonElement>( 'button.slice-button' )!;
		expect( text( button ) ).toBe( "Slice" );
		const entries = await menu( button );
		expect( entries.map( e=>text( e ) ) ).toEqual( ["Slice by Level", "All8", "Debug4", "Critical3", "No Level1"] );
		entries[2].click();
		await stable( fixture, 1 );
		expect( [set.slice(), text( element.querySelector('button.slice-button')! )] ).toEqual( ["Debug", "Slice: Debug"] );
	} );

	it( 'is absent on a phone', async ()=>{
		const { element } = await open( {width: 'extra-small'} );
		expect( element.querySelector( 'slice-panel' )!.children.length ).toBe( 0 );
	} );
} );